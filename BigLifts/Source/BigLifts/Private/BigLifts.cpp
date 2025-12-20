#include "BigLifts.h"

#include "FGConveyorLiftHologram.h"
#include "Patching/NativeHookManager.h"

static constexpr float MaxLiftHeightMultiplier = 10.0f;

void FBigLiftsModule::StartupModule()
{
#if !WITH_EDITOR
	SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGConveyorLiftHologram, BeginPlay,
		[](AFGConveyorLiftHologram* hologram)
		{
			hologram->mMaximumHeight *= MaxLiftHeightMultiplier;
		});
#endif
}

void FBigLiftsModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FBigLiftsModule, BigLifts)
