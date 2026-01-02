#include "VerticalLogisticsQoL.h"

#include "Buildables/FGBuildableConveyorLift.h"
#include "Buildables/FGBuildablePassthrough.h"
#include "Equipment/FGBuildGunBuild.h"
#include "FGFactoryConnectionComponent.h"
#include "Hologram/FGConveyorAttachmentHologram.h"
#include "Hologram/FGConveyorLiftHologram.h"
#include "Patching/NativeHookManager.h"
#include "VLQoLGameInstanceModule.h"

namespace
{

bool IsVerticalConnector(float normalUp)
{
	// We aren't expecting any diagonal connectors, so really this could be closer to 1, but 0.5 is
	// being used to match the checks that the game does in similar places.
	return FMath::Abs(normalUp) > 0.5f;
}

} // namespace

void FVerticalLogisticsQoLModule::StartupModule()
{
	if constexpr (!WITH_EDITOR)
	{
		FixLostPassthroughLinks();
		FixHologramLocking();
		FixLiftOnAttachmentOffByHalf();
		FixAttachmentOnLiftOffByHalf();
		FixClearanceWarnings();
		AllowConnectionToExistingAttachment();
		PrepareCustomAttachmentHologram();
	}
}

void FVerticalLogisticsQoLModule::ShutdownModule()
{
}

void FVerticalLogisticsQoLModule::FixLostPassthroughLinks()
{
	// When the game merges and splits lifts, which can happen when adding and removing conveyor
	// attachments, it will correctly transfer snapped passthroughs from the old lifts to the new lifts.
	// However it forgets to update the connections on the passthroughs, which means that they don't
	// think that they have anything snapped to them.

	const class
	{
	public:
		// Duplicate
		void operator()(AFGBuildableConveyorLift* result, AFGBuildableConveyorLift* originalLift, bool dismantleOriginalLift) const
		{
			if (dismantleOriginalLift)
			{
				Repair(result);
			}
		}

		// Merge
		void operator()(AFGBuildableConveyorLift* result, const TArray<AFGBuildableConveyorLift*>& lifts) const
		{
			Repair(result);
		}

		// Split
		void operator()(const TArray<AFGBuildableConveyorLift*>& result, AFGBuildableConveyorLift* lift, float offset) const
		{
			for (AFGBuildableConveyorLift* newLift : result)
			{
				Repair(newLift);
			}
		}

	private:
		static void Repair(AFGBuildableConveyorLift* lift)
		{
			if (lift == nullptr)
				return;

			const TArray<AFGBuildablePassthrough*>& snappedPassthroughs = lift->mSnappedPassthroughs;

			if (snappedPassthroughs.Num() != 2)
				return;	// Shouldn't ever happen?
			if (snappedPassthroughs[0] == nullptr && snappedPassthroughs[1] == nullptr)
				return;	// Not snapped to any passthroughs.

			// Don't do anything if the passthroughs do in fact link back to the lift, as that probably means
			// that we're running on a game version where the original bug has been fixed.
			for (AFGBuildablePassthrough* passthrough : snappedPassthroughs)
			{
				if (passthrough != nullptr)
				{
					if (UFGConnectionComponent* top = passthrough->mTopSnappedConnection)
					{
						if (top->GetOwner() == lift)
							return;
					}
					if (UFGConnectionComponent* bottom = passthrough->mBottomSnappedConnection)
					{
						if (bottom->GetOwner() == lift)
							return;
					}
				}
			}

			UFGFactoryConnectionComponent* connectionComponents[] =
			{
				lift->GetConnection0(),
				lift->GetConnection1(),
			};

			for (int i = 0; i != 2; ++i)
			{
				AFGBuildablePassthrough* passthrough = snappedPassthroughs[i];

				if (passthrough == nullptr)
					continue;

				UFGFactoryConnectionComponent* connection = connectionComponents[i];
				UFGFactoryConnectionComponent* oppositeConnection = connectionComponents[!i];

				// Use the vertical position to infer which side of the passthrough it must be snapped to.
				if (passthrough->GetActorLocation().Z < oppositeConnection->GetComponentLocation().Z)
				{
					passthrough->SetTopSnappedConnection(connection);
				}
				else
				{
					passthrough->SetBottomSnappedConnection(connection);
				}
			}
		}
	} hook;

	SUBSCRIBE_METHOD_AFTER(AFGBuildableConveyorLift::DuplicateLift, hook);
	SUBSCRIBE_METHOD_AFTER(AFGBuildableConveyorLift::Merge, hook);
	SUBSCRIBE_METHOD_AFTER(AFGBuildableConveyorLift::Split, hook);
}

void FVerticalLogisticsQoLModule::FixHologramLocking()
{
	// The hologram throws away its snap state in PreHologramPlacement on the assumption that it'll be
	// followed by a call to TrySnapToActor to re-calculate the state, however that won't happen when
	// the hologram is locked so it just ends up forgetting what it's snapped to.

	SUBSCRIBE_UOBJECT_METHOD(AFGConveyorAttachmentHologram, PreHologramPlacement,
		[](auto& scope, AFGConveyorAttachmentHologram* hologram, const FHitResult& hitResult, bool callForChildren)
		{
			if (!hologram->IsHologramLocked())
				return;

			// The hologram is locked, make sure that the snap state is restored.

			const auto upgradedConveyorAttachment = hologram->mUpgradedConveyorAttachment;
			const auto snappedConveyor = hologram->mSnappedConveyor;
			const auto snappedConveyorOffset = hologram->mSnappedConveyorOffset;

			scope(hologram, hitResult, callForChildren);

			if (hologram->mUpgradedConveyorAttachment == nullptr)
			{
				hologram->mUpgradedConveyorAttachment = upgradedConveyorAttachment;
			}

			if (hologram->mSnappedConveyor == nullptr)
			{
				hologram->mSnappedConveyor = snappedConveyor;
				hologram->mSnappedConveyorOffset = snappedConveyorOffset;
			}
		});
}

void FVerticalLogisticsQoLModule::FixLiftOnAttachmentOffByHalf()
{
	// If the base of the lift isn't aligned to 1m, then the extra offset should be applied after
	// rounding to the step height so that you don't just round away the offset. The game does this
	// properly for passthroughs, but for vertical connections it adds the extra 0.5m before calculating
	// the steps. We can't easily patch the dodgy bit of code that deals with vertical connections in
	// this function, so we'll have to hackily hide the connection from it before calling so that it
	// skips that code and then we can manually fix it up afterwards.

	SUBSCRIBE_METHOD(AFGConveyorLiftHologram::UpdateTopTransform,
		[](auto& scope, AFGConveyorLiftHologram* hologram, const FHitResult& hitResult, const FRotator& rotation)
		{
			if (hologram->mSnappedPassthroughs[0] != nullptr)
				return;	// Snapped to a passthrough.
			UFGFactoryConnectionComponent* connection = hologram->mSnappedConnectionComponents[0];
			if (connection == nullptr)
				return;	// Not snapped to a connection.
			const float normalUp = connection->GetConnectorNormal().Z;
			if (!IsVerticalConnector(normalUp))
				return;	// The snapped connection isn't vertical.

			// The original function uses 2.5m and 3.5m for vertical connections, but we've taken off the extra
			// 0.5m and will apply it afterwards.
			const float minimumHeight = normalUp >= 0.0f ? 200.0f : 300.0f;
			const float origMinimumHeight = hologram->mMinimumHeight;

			// Call the original function as if there's no connection.
			{
				hologram->mSnappedConnectionComponents[0] = nullptr;
				hologram->mMinimumHeight = minimumHeight;

				scope(hologram, hitResult, rotation);

				hologram->mSnappedConnectionComponents[0] = connection;
				hologram->mMinimumHeight = origMinimumHeight;
			}

			// Add the 0.5m back on.
			FVector top = hologram->mTopTransform.GetLocation();
			top.Z += top.Z >= 0.0f ? 50.0f : -50.0f;
			hologram->mTopTransform.SetLocation(top);
		});
}

void FVerticalLogisticsQoLModule::FixAttachmentOnLiftOffByHalf()
{
	// The offset used for placing attachment on lifts doesn't account for the extra length added by
	// vertical connections, which can push it off grid.

	SUBSCRIBE_UOBJECT_METHOD(AFGBuildableConveyorLift, FindOffsetClosestToLocation,
		[](auto& scope, const AFGBuildableConveyorLift* lift, const FVector& location)
		{
			const float offset = scope(lift, location);
			if (FMath::RoundToInt(offset) % 100 != 0)
				return;	// Looks like the bug has been fixed?

			float extraOffset;

			if (const AFGBuildablePassthrough* passthrough = lift->mSnappedPassthroughs[0])
			{
				// The lift starts from the center of the passthrough, which means that there's half of the
				// passthrough's thickness on either side of the lift before we get to the usable bits. When using
				// 1m foundations, half is 0.5m and that throws off the alignment.
				const int halfThickness = FMath::RoundToInt(0.5f * passthrough->mSnappedBuildingThickness);
				const int remainder = halfThickness % 100;
				if (remainder == 0)
					return;
				extraOffset = static_cast<float>(remainder);
			}
			else
			{
				// Vertical connections on a splitter/merger are inset by 0.5m.
				const UFGFactoryConnectionComponent* connection = lift->mConnection0->GetConnection();
				if (connection == nullptr)
					return;	// Not connected to anything.
				if (!IsVerticalConnector(connection->GetConnectorNormal().Z))
					return;	// Not a vertical connection.
				extraOffset = 50.0f;
			}

			scope.Override(offset + FMath::Sign(offset) * extraOffset);
		});
}

void FVerticalLogisticsQoLModule::FixClearanceWarnings()
{
	// The opposing vertical connections on a conveyor attachment have the same location, it's just the
	// rotation that differs. This means that a lift on the bottom connection has the same location as a
	// lift on the top connection, which triggers the overlap warning. We need to find what's connected
	// to the opposing connection and ignore it when doing clearance checks.

	SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGConveyorLiftHologram, GetIgnoredClearanceActors,
		[](const AFGConveyorLiftHologram* hologram, TSet<AActor*>& ignoredActors)
		{
			for (const UFGFactoryConnectionComponent* connection : hologram->mSnappedConnectionComponents)
			{
				if (connection == nullptr)
					continue;

				const FVector connectorNormal = connection->GetConnectorNormal();
				if (!IsVerticalConnector(connectorNormal.Z))
					continue;
				const FVector connectorLocation = connection->GetConnectorLocation();

				for (auto* otherConnection : TInlineComponentArray<UFGFactoryConnectionComponent*>(connection->GetOuterBuildable()))
				{
					if (otherConnection == connection)
						continue;	// We only care about other connections.
					UFGFactoryConnectionComponent* connectedTo = otherConnection->GetConnection();
					if (connectedTo == nullptr)
						continue;	// Not connected to anything, so there's nothing to clip with.
					if (!otherConnection->GetConnectorLocation().Equals(connectorLocation))
						continue;	// Doesn't have the same location, so it won't clip.
					if (!FVector::Coincident(otherConnection->GetConnectorNormal(), -connectorNormal))
						continue;	// Not going in the opposite direction.
					ignoredActors.Add(connectedTo->GetOuterBuildable());
				}
			}
		});
}

void FVerticalLogisticsQoLModule::AllowConnectionToExistingAttachment()
{
	// The base implementation filters out vertical connections because the normal for a lift connection
	// points out horizontally, which it doesn't see as aligned with vertical connections so it doesn't
	// allow them.

	SUBSCRIBE_METHOD(AFGConveyorLiftHologram::CanConnectToConnection,
		[](auto& scope, const AFGConveyorLiftHologram* hologram, UFGFactoryConnectionComponent* from, UFGFactoryConnectionComponent* to)
		{
			if (scope(hologram, from, to))
				return;	// Already handled.
			if (from == nullptr || to == nullptr)
				return;	// Nothing to connect.
			const FVector toNormal = to->GetConnectorNormal();
			if (!IsVerticalConnector(toNormal.Z))
				return;	// Not a vertical connection, not our problem.
			const FVector toLocation = to->GetConnectorLocation();
			const FVector connectionVector = toLocation - from->GetConnectorLocation();
			if (!FMath::IsNearlyZero(connectionVector.X) || !FMath::IsNearlyZero(connectionVector.Y))
				return;	// Not aligned horizontally with the lift.
			if (FMath::Abs(connectionVector.Z) > hologram->mStepHeight + to->GetConnectorClearance())
				return;	// Too far away vertically.
			if ((toNormal.Z >= 0.0f) == (toLocation.Z >= hologram->GetActorLocation().Z))
				return;	// Wrong side.
			scope.Override(true);
		});
}

void FVerticalLogisticsQoLModule::PrepareCustomAttachmentHologram()
{
	// Our hologram expects to be given the regular recipe, and it will switch to the vertical one based
	// on the build mode, so we need to make sure that the vertical recipe is never directly used. This
	// can only happen when sampling an existing vertical attachment, as there's no other way to select
	// them in the build menu.

	SUBSCRIBE_METHOD(UFGBuildGunStateBuild::SetActiveRecipe,
		[](auto& scope, UFGBuildGunStateBuild* state, TSubclassOf<UFGRecipe> recipe)
		{
			if (auto* gameInstanceModule = UVLQoLGameInstanceModule::Get(state))
			{
				if (TSubclassOf<UFGRecipe> overrideRecipe = gameInstanceModule->GetRegularConveyorAttachmentRecipe(recipe))
				{
					scope(state, overrideRecipe);
				}
			}
		});
}

IMPLEMENT_MODULE(FVerticalLogisticsQoLModule, VerticalLogisticsQoL)
