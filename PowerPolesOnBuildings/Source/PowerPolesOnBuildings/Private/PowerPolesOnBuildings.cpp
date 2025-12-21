#include "PowerPolesOnBuildings.h"

#include "FGBuildable.h"
#include "Hologram/FGPowerPoleHologram.h"
#include "Patching/NativeHookManager.h"
#include "PPOBGameInstanceModule.h"

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
#endif
}

void FPowerPolesOnBuildingsModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FPowerPolesOnBuildingsModule, PowerPolesOnBuildings)
