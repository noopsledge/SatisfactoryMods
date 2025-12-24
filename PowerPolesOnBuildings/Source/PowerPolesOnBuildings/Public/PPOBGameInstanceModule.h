#pragma once

#include "CoreMinimal.h"
#include "Module/GameInstanceModule.h"
#include "PPOBGameInstanceModule.generated.h"

class AFGBuildable;
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
	/// This should really be referencing AFGDecorationTemplate subclasses, as that's what the
	/// attachment points will be added to, but it turns out that there're some mods that replace the
	/// entire decorator template with their own instead of being kind and adding to the existing one.
	/// In an attempt to be compatible with them, we're referencing buildables instead and will fetch
	/// their assigned decorator at runtime. If multiple buildables reference the same decorator, then
	/// only one of them needs to be listed here.
	UPROPERTY(Category = "Attachment Points", EditDefaultsOnly)
	TMap<TSubclassOf<AFGBuildable>, FVector> BuildingAttachmentPoints;
};
