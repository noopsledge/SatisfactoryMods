#pragma once
#include "CoreMinimal.h"
#include "Configuration/ConfigManager.h"
#include "Engine/Engine.h"
#include "BigLiftsConfigurationStruct.generated.h"

/* Struct generated from Mod Configuration Asset '/BigLifts/BigLiftsConfiguration' */
USTRUCT(BlueprintType)
struct FBigLiftsConfigurationStruct {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite)
    int32 MaxHeight{};

    /* Retrieves active configuration value and returns object of this struct containing it */
    static FBigLiftsConfigurationStruct GetActiveConfig(UObject* WorldContext) {
        FBigLiftsConfigurationStruct ConfigStruct{};
        FConfigId ConfigId{"BigLifts", ""};
        if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)) {
            UConfigManager* ConfigManager = World->GetGameInstance()->GetSubsystem<UConfigManager>();
            ConfigManager->FillConfigurationStruct(ConfigId, FDynamicStructInfo{FBigLiftsConfigurationStruct::StaticStruct(), &ConfigStruct});
        }
        return ConfigStruct;
    }
};

