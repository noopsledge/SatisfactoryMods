#include "VerticalLogisticsQoL.h"

#include "Buildables/FGBuildableConveyorLift.h"
#include "Buildables/FGBuildablePassthrough.h"
#include "FGFactoryConnectionComponent.h"
#include "Patching/NativeHookManager.h"

void FVerticalLogisticsQoLModule::StartupModule()
{
	if constexpr (!WITH_EDITOR)
	{
		FixLostPassthroughLinks();
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

IMPLEMENT_MODULE(FVerticalLogisticsQoLModule, VerticalLogisticsQoL)
