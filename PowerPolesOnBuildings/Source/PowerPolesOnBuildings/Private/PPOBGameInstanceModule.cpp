#include "PPOBGameInstanceModule.h"

#include "Buildables/FGBuildable.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "FGAttachmentPointComponent.h"
#include "FGDecorationTemplate.h"
#include "Module/GameInstanceModuleManager.h"
#include "PPOBPowerPoleAttachmentPoint.h"

UPPOBGameInstanceModule* UPPOBGameInstanceModule::Get(UObject* worldContext)
{
	if (worldContext != nullptr)
	{
		return Get(worldContext->GetWorld());
	}
	return nullptr;
}

UPPOBGameInstanceModule* UPPOBGameInstanceModule::Get(UWorld* world)
{
	if (world != nullptr)
	{
		if (UGameInstance* gameInstance = world->GetGameInstance())
		{
			if (auto* manager = gameInstance->GetSubsystem<UGameInstanceModuleManager>())
			{
				auto* module = manager->FindModule(UE_MODULE_NAME);
				return Cast<UPPOBGameInstanceModule>(module);
			}
		}
	}
	return nullptr;
}

void UPPOBGameInstanceModule::DispatchLifecycleEvent(ELifecyclePhase phase)
{
	if (!WITH_EDITOR && phase == ELifecyclePhase::INITIALIZATION)
	{
		// Create attachment points for the buildings.
		TSet<UClass*, DefaultKeyFuncs<UClass*>, TInlineSetAllocator<32>> seenDecorators;
		for (auto&& [buildableClass, offset] : BuildingAttachmentPoints)
		{
			UClass* decorationClass = buildableClass.GetDefaultObject()->GetDecorationTemplate();
			checkf(decorationClass, TEXT("%s doesn't have a decoration template!"), *buildableClass->GetName());
			if (bool seen; seenDecorators.Add(decorationClass, &seen), seen)
				continue;	// Skip duplicates.
			auto* blueprintClass = CastChecked<UBlueprintGeneratedClass>(decorationClass);
			AddAttachmentPointComponent(blueprintClass, offset);
		}
	}

	Super::DispatchLifecycleEvent(phase);
}

// Adds a UFGAttachmentPointComponent to the given blueprint.
//
// We'd usually use the actor mixin system for this, but decorator templates are never actually
// instantiated; components are discovered by traversing their construction script (see
// AFGDecorationTemplate::GetComponentsFromSubclass). For that to pick up our new component we need
// it to actually be part of the blueprint and not something that's instantiated dynamically by the
// mixin system.
void UPPOBGameInstanceModule::AddAttachmentPointComponent(UBlueprintGeneratedClass* blueprintClass, const FVector& offset)
{
	const EAttachmentPointUsage attachmentPointUsage = EAttachmentPointUsage::EAPU_BuildableOnly;
	UClass* attachmentPointType = UPPOBPowerPoleAttachmentPoint::StaticClass();

	USimpleConstructionScript* constructionScript = blueprintClass->SimpleConstructionScript;
	check(constructionScript);

	// Make sure that it hasn't already been added - shouldn't happen, but just in case...
	for (USCS_Node* node : constructionScript->GetAllNodes())
	{
		if (node != nullptr)
		{
			if (auto* component = Cast<UFGAttachmentPointComponent>(node->ComponentTemplate))
			{
				if (component->mUsage == attachmentPointUsage && component->mType == attachmentPointType)
				{
					return;
				}
			}
		}
	}

	TStringBuilder<128> componentName;
	componentName << "PPOB_AttachmentPointComponent" << USimpleConstructionScript::ComponentTemplateNameSuffix;

	auto* component = NewObject<UFGAttachmentPointComponent>(blueprintClass, FName(componentName), RF_ArchetypeObject);
	component->SetRelativeLocation_Direct(offset);
	component->mUsage = attachmentPointUsage;
	component->mType = attachmentPointType;

	auto* node = NewObject<USCS_Node>(constructionScript, TEXT("PPOB_AttachmentPointNode"));
	node->ComponentClass = UFGAttachmentPointComponent::StaticClass();
	node->ComponentTemplate = component;

	constructionScript->AddNode(node);
}

FFGAttachmentPoint UPPOBGameInstanceModule::CreatePowerPoleAttachmentPoint(AActor* owner) const
{
	FFGAttachmentPoint result;
	result.RelativeTransform.SetLocation(PowerPoleAttachmentPoint);
	result.Type = UPPOBPowerPoleAttachmentPoint::StaticClass();
	result.Owner = owner;
	return result;
}
