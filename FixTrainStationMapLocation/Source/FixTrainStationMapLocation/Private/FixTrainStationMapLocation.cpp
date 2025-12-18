#include "FixTrainStationMapLocation.h"

#include "FGActorRepresentationManager.h"
#include "FGModTrainStationRepresentation.h"
#include "FGTrainStationIdentifier.h"
#include "Patching/NativeHookManager.h"

void FFixTrainStationMapLocationModule::StartupModule()
{
#if !WITH_EDITOR
	SUBSCRIBE_METHOD(AFGActorRepresentationManager::CreateAndAddNewRepresentation,
		[](auto& scope, AFGActorRepresentationManager* manager, AActor* realActor, bool isLocal, TSubclassOf<UFGActorRepresentation> representationClass)
		{
			// Use our custom representation for train stations.
			if (realActor != nullptr && representationClass == nullptr && realActor->IsA<AFGTrainStationIdentifier>())
			{
				scope(manager, realActor, isLocal, UFGModTrainStationRepresentation::StaticClass());
			}
		});
#endif
}

void FFixTrainStationMapLocationModule::ShutdownModule()
{
}

IMPLEMENT_MODULE(FFixTrainStationMapLocationModule, FixTrainStationMapLocation)
