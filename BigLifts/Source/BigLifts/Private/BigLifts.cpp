#include "BigLifts.h"

#include "BigLiftsConfigurationStruct.h"
#include "FGConveyorLiftHologram.h"
#include "Patching/NativeHookManager.h"

void FBigLiftsModule::StartupModule()
{
#if !WITH_EDITOR
	SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGConveyorLiftHologram, BeginPlay,
		[](AFGConveyorLiftHologram* hologram)
		{
			const auto& config = FBigLiftsConfigurationStruct::GetActiveConfig(hologram);
			float maxHeight = config.MaxHeight * 100.0f;
			maxHeight = FMath::RoundUpToClosestMultiple(maxHeight, hologram->mStepHeight);
			maxHeight = FMath::Max(maxHeight, hologram->mMinimumHeight);
			hologram->mMaximumHeight = maxHeight;
		});
#endif
}

void FBigLiftsModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FBigLiftsModule, BigLifts)
