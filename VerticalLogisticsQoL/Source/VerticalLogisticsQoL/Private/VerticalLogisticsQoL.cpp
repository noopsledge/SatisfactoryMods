#include "VerticalLogisticsQoL.h"

#include "Buildables/FGBuildableConveyorAttachment.h"
#include "Buildables/FGBuildableConveyorLift.h"
#include "Buildables/FGBuildablePassthrough.h"
#include "Equipment/FGBuildGunBuild.h"
#include "FGFactoryConnectionComponent.h"
#include "Hologram/FGConveyorAttachmentHologram.h"
#include "Hologram/FGConveyorLiftHologram.h"
#include "Net/UnrealNetwork.h"
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
		FixMassDismantleVerticalAttachmentAndLifts();
		AllowConnectionToExistingAttachment();
		PrepareCustomAttachmentHologram();
		NetworkVerticalAttachmentFlowDirection();
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

void FVerticalLogisticsQoLModule::FixMassDismantleVerticalAttachmentAndLifts()
{
	// When dismantling a vertical attachment, it will join the top and bottom lifts together and leave
	// that behind. This is the desired behavior when dismantling just the attachment, but if you're
	// trying to mass-dismantle everything together then (if the attachment gets dismantled first)
	// you'll end up with a lift at the end that you don't want. This has been fixed by adding a
	// dismantle dependency from the attachment to its lifts, so if they're getting dismantled together
	// the lifts will go first and there'll therefore be nothing left for the attachment to merge.

	SUBSCRIBE_METHOD_VIRTUAL_AFTER(IFGDismantleInterface::GetDismantleDependencies_Implementation,
		static_cast<const IFGDismantleInterface*>(GetDefault<AFGBuildable>()),
		[](const IFGDismantleInterface* buildable, TArray<AActor*>& out_dismantleDependencies)
		{
			auto* attachment = Cast<AFGBuildableConveyorAttachment>(static_cast<const AFGBuildable*>(buildable));
			if (attachment == nullptr)
				return;

			const FName verticalName1 = AFGConveyorAttachmentHologram::mLiftConnection_Bottom;
			const FName verticalName2 = AFGConveyorAttachmentHologram::mLiftConnection_Top;

			for (auto* connection : TInlineComponentArray<UFGFactoryConnectionComponent*>(attachment))
			{
				const FName name = connection->GetFName();
				if (name != verticalName1 && name != verticalName2)
					continue;
				UFGFactoryConnectionComponent* connectedComponent = connection->GetConnection();
				if (connectedComponent == nullptr)
					continue;
				auto* lift = Cast<AFGBuildableConveyorLift>(connectedComponent->GetOuterBuildable());
				if (lift == nullptr)
					continue;
				out_dismantleDependencies.Add(lift);
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

void FVerticalLogisticsQoLModule::NetworkVerticalAttachmentFlowDirection()
{
	// Each attachment saves the direction of its connection components after it's constructed by the
	// hologram, which is needed for vertical attachments because the direction of their top and bottom
	// connections can change depending on how the attachment was built. This data (mSavedDirections)
	// isn't available on non-authoritative clients, so they won't know the direction of those
	// connections. We don't need all of the saved directions to be replicated, the side connections are
	// static and the top and bottom connections are in opposing directions, so everything can be
	// inferred from just a single flag.
	//
	// Adding this flag is a whole other problem, we can't just extend the attachment class because that
	// would throw off everything in the derived classes. Fortunately there's eome padding at the end of
	// mCachedInventorySize that we can use for some extra storage, all we need to do is make a new
	// FProperty and point it to that padding. This will obviously blow up if the layout changes in
	// future updates, but that's the same for any of the game structures that we access normally too so
	// I don't think that's any more of a problem. The only issue would be if another mod is equally
	// mischievous and has another use for that padding...
	//
	// If the padding disappears in the future, plan B is to remove the mHologramOverrides property and
	// use that space instead; our new hologram makes that property obselete. However that's more likely
	// to be noticed by other mods so that's being left as a last resort.

	static constexpr size_t isUpwardsFlowOffset =
		STRUCT_OFFSET(AFGBuildableConveyorAttachment, mCachedInventorySize)
		+ sizeof(AFGBuildableConveyorAttachment::mCachedInventorySize);

	static_assert(isUpwardsFlowOffset + sizeof(bool) <= STRUCT_OFFSET(AFGBuildableConveyorAttachment, mHologramOverrides));

	// Creates the property and registers it with the networking system.
	class LifetimeRepHook
	{
	public:
		explicit LifetimeRepHook(UClass* buildableClass)
		{
			using namespace UECodeGen_Private;

			FProperty* prevProperty = FindPreviousProperty(
				buildableClass,
				GET_MEMBER_NAME_CHECKED(AFGBuildableConveyorAttachment, mHologramOverrides));

			check(prevProperty);
			check(prevProperty->GetOffset_ForDebug() + prevProperty->GetSize() <= isUpwardsFlowOffset);

			IsUpwardsFlowProperty = new FBoolProperty(buildableClass, FBoolPropertyParams
			{
				.NameUTF8 = "mIsUpwardsFlow",
				.PropertyFlags = CPF_Net | CPF_Transient | CPF_NativeAccessSpecifierPrivate,
				.Flags = EPropertyGenFlags::Bool | EPropertyGenFlags::NativeBool,
				.ArrayDim = 1,
				.ElementSize = sizeof(bool),
				.SizeOfOuter = sizeof(AFGBuildableConveyorAttachment),
				.SetBitFunc = [](void* o) { *(bool*)((unsigned char*)o + isUpwardsFlowOffset) = true; },
			});

			// By default the property gets added to the front of the property list, but we're re-linking it to
			// be where it was if it was declared naturally as part of the class.
			check(buildableClass->ChildProperties == IsUpwardsFlowProperty);
			check(buildableClass->PropertyLink != IsUpwardsFlowProperty);
			buildableClass->ChildProperties = IsUpwardsFlowProperty->Next;
			IsUpwardsFlowProperty->Next = prevProperty->Next;
			IsUpwardsFlowProperty->PropertyLinkNext = prevProperty->PropertyLinkNext;
			prevProperty->Next = IsUpwardsFlowProperty;
			prevProperty->PropertyLinkNext = IsUpwardsFlowProperty;

			buildableClass->ClassFlags &= ~CLASS_ReplicationDataIsSetUp;
		}

		void operator()(const AFGBuildable* buildable, TArray<FLifetimeProperty>& OutLifetimeProps)
		{
			auto* attachment = Cast<AFGBuildableConveyorAttachment>(buildable);
			if (attachment == nullptr)
				return;

			RegisterReplicatedLifetimeProperty(IsUpwardsFlowProperty, OutLifetimeProps,
			{
				// Don't need networking for non-vertical attachments as their directions are all static.
				.Condition = IsVerticalAttachment(attachment) ? COND_InitialOnly : COND_Never,
			});
		}

	private:
		static FProperty* FindPreviousProperty(UClass* cls, FName name)
		{
			FProperty* prev = nullptr;
			FProperty* curr = cls->PropertyLink;

			while (curr != nullptr)
			{
				if (curr->GetFName() == name)
					return prev;
				prev = curr;
				curr = curr->PropertyLinkNext;
			}

			return nullptr;
		}

		static bool IsVerticalAttachment(const AFGBuildableConveyorAttachment* attachment)
		{
			for (const UFGHologramOverride* override : attachment->mHologramOverrides)
			{
				if (override && override->IsA<UFGHologramOverride_ConveyorAttachment_LiftToFloor>())
					return true;
			}
			return false;
		}

		FBoolProperty* IsUpwardsFlowProperty;
	};

	// Sets the new networked flag on the server, which the client uses to fix up the lift connection
	// directions.
	class BeginPlayHook
	{
	public:
		void operator()(AFGBuildableConveyorAttachment* attachment) const
		{
			bool& isUpwardsFlow = *(bool*)((unsigned char*)attachment + isUpwardsFlowOffset);
			TInlineComponentArray<UFGFactoryConnectionComponent*> connections(attachment);

			if (attachment->HasAuthority())
			{
				isUpwardsFlow = GetIsUpwardsFlow(connections);
			}
			else
			{
				SetIsUpwardsFlow(connections, isUpwardsFlow);
			}
		}

	private:
		static bool GetIsUpwardsFlow(const TInlineComponentArray<UFGFactoryConnectionComponent*>& connections)
		{
			const FName bottomName = AFGConveyorAttachmentHologram::mLiftConnection_Top;

			for (const UFGFactoryConnectionComponent* connection : connections)
			{
				if (connection->GetFName() == bottomName)
				{
					return connection->GetDirection() == EFactoryConnectionDirection::FCD_INPUT;
				}
			}

			return false;
		}

		static void SetIsUpwardsFlow(const TInlineComponentArray<UFGFactoryConnectionComponent*>& connections, bool isUpwardsFlow)
		{
			const FName bottomName = AFGConveyorAttachmentHologram::mLiftConnection_Top;
			const FName topName = AFGConveyorAttachmentHologram::mLiftConnection_Bottom;

			for (UFGFactoryConnectionComponent* connection : connections)
			{
				if (connection->GetDirection() != EFactoryConnectionDirection::FCD_ANY)
					continue;

				const FName name = connection->GetFName();

				if (name == bottomName)
				{
					connection->SetDirection(
						isUpwardsFlow
						? EFactoryConnectionDirection::FCD_INPUT
						: EFactoryConnectionDirection::FCD_OUTPUT);
				}
				else if (name == topName)
				{
					connection->SetDirection(
						isUpwardsFlow
						? EFactoryConnectionDirection::FCD_OUTPUT
						: EFactoryConnectionDirection::FCD_INPUT);
				}
			}
		}
	};

	UClass* buildableClass = AFGBuildableConveyorAttachment::StaticClass();
	auto* defaultBuildable = CastChecked<AFGBuildableConveyorAttachment>(buildableClass->GetDefaultObject());
	SUBSCRIBE_METHOD_VIRTUAL_AFTER(AFGBuildable::GetLifetimeReplicatedProps, defaultBuildable, LifetimeRepHook(buildableClass));
	SUBSCRIBE_METHOD_VIRTUAL_AFTER(AFGBuildableConveyorAttachment::BeginPlay, defaultBuildable, BeginPlayHook());
}

IMPLEMENT_MODULE(FVerticalLogisticsQoLModule, VerticalLogisticsQoL)
