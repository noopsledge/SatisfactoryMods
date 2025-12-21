#include "PowerPolesOnBuildings.h"

#include "FGBuildable.h"
#include "FGCircuitConnectionComponent.h"
#include "Hologram/FGPowerPoleHologram.h"
#include "Hologram/FGWireHologram.h"
#include "Patching/NativeHookManager.h"
#include "PPOBGameInstanceModule.h"

namespace
{

bool WireHologramCanSnapPowerPoleToActor(const AFGWireHologram* wire, const AActor* actor)
{
	if (actor == nullptr)
		return false;

	// Actors without power connections don't interact with the wire hologram in any way, so we can
	// always snap to them.
	if (actor->GetComponentByClass<UFGCircuitConnectionComponent>() == nullptr)
		return true;

	// Only allow snapping if the wire was started from the actor, otherwise we probably want to connect
	// the wire to it instead of snapping a power pole onto it.
	const UFGCircuitConnectionComponent* firstConnection = wire->GetConnection(0);
	return firstConnection == nullptr || firstConnection->GetOwner() == actor;
}

} // namespace

void FPowerPolesOnBuildingsModule::StartupModule()
{
#if !WITH_EDITOR
	SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGPowerPoleHologram, BeginPlay,
		[](AFGPowerPoleHologram* hologram)
		{
			// Manually add the attachment point to the power pole.
			// This is usually done by adding a component to the decorator template, and that's what we've done
			// for all of the buildings that we want the power pole to snap to, but power poles don't have
			// decorator templates set up and it isn't worth adding one just for this.
			if (auto* gameInstanceModule = UPPOBGameInstanceModule::Get(hologram))
			{
				const FFGAttachmentPoint attachmentPoint = gameInstanceModule->CreatePowerPoleAttachmentPoint(hologram);
				hologram->mCachedAttachmentPoints.Add(attachmentPoint);
			}

			// The attachment points won't ever be found if the buildings aren't seen as valid.
			hologram->AddValidHitClass(AFGBuildable::StaticClass());
		});

	SUBSCRIBE_UOBJECT_METHOD(AFGWireHologram, TrySnapToActor,
		[](auto& scope, AFGWireHologram* wire, const FHitResult& hitResult)
		{
			// The wire hologram will only try to snap its wall outlet, presumably because the base game doesn't
			// have snap points for power poles. Now that we've added some, we need to have logic for snapping
			// them in the same way.

			if (scope(wire, hitResult))
				return;	// Already handled.

			AFGPowerPoleHologram* powerPole = wire->mPowerPole;
			const int32 currentConnectionIndex = wire->mCurrentConnection;
			UFGCircuitConnectionComponent*& currentConnection = wire->mConnections[currentConnectionIndex];

			if (!IsValid(powerPole) || !wire->mAutomaticPoleAvailable)
				return;	// Don't have a power pole to play with.
			if (currentConnectionIndex < 1)
				return;	// Not placing the power pole yet.
			if (currentConnection != nullptr && currentConnection != wire->mActiveSnapConnection)
				return;	// The wire is currently connected to something other than the power pole.
			if (!WireHologramCanSnapPowerPoleToActor(wire, hitResult.GetActor()))
				return;	// Not allowed to snap to this actor.

			// Preliminary checks passed, now actually try snapping.
			if (powerPole->TrySnapToActor(hitResult))
			{
				wire->SetActiveAutomaticPoleHologram(powerPole);

				UFGCircuitConnectionComponent* snapConnection = powerPole->mSnapConnection;
				currentConnection = snapConnection;
				wire->SetActorTransform(snapConnection->GetComponentTransform());

				scope.Override(true);
			}
		});
#endif
}

void FPowerPolesOnBuildingsModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FPowerPolesOnBuildingsModule, PowerPolesOnBuildings)
