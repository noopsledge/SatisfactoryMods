#pragma once

#include "CoreMinimal.h"
#include "Module/GameInstanceModule.h"
#include "PPOBGameInstanceModule.generated.h"

class AFGDecorationTemplate;
struct FFGAttachmentPoint;

UCLASS()
class POWERPOLESONBUILDINGS_API UPPOBGameInstanceModule : public UGameInstanceModule
{
	GENERATED_BODY()

public:
	static UPPOBGameInstanceModule* Get(UObject* worldContext);
	static UPPOBGameInstanceModule* Get(UWorld* world);

	FFGAttachmentPoint CreatePowerPoleAttachmentPoint(AActor* owner) const;

	// UGameInstanceModule
	virtual void DispatchLifecycleEvent(ELifecyclePhase phase) override;

private:
	static void AddAttachmentPointComponent(UBlueprintGeneratedClass* blueprintClass, const FVector& offset);

	/// Relative transform for the attachment point added to power pole holograms.
	UPROPERTY(Category="Attachment Points", EditDefaultsOnly)
	FVector PowerPoleAttachmentPoint;

	/// Relative transform for the attachment points added to buildings.
	UPROPERTY(Category = "Attachment Points", EditDefaultsOnly)
	TMap<TSubclassOf<AFGDecorationTemplate>, FVector> BuildingAttachmentPoints;
};
