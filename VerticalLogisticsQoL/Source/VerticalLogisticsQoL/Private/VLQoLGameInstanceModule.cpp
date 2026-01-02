#include "VLQoLGameInstanceModule.h"

#include "Buildables/FGBuildableConveyorAttachment.h"
#include "FGRecipe.h"
#include "Module/GameInstanceModuleManager.h"
#include "Resources/FGBuildDescriptor.h"

#include <concepts>

UVLQoLGameInstanceModule* UVLQoLGameInstanceModule::Get(UObject* worldContext)
{
	if (worldContext != nullptr)
	{
		return Get(worldContext->GetWorld());
	}
	return nullptr;
}

UVLQoLGameInstanceModule* UVLQoLGameInstanceModule::Get(UWorld* world)
{
	if (world != nullptr)
	{
		if (UGameInstance* gameInstance = world->GetGameInstance())
		{
			if (auto* manager = gameInstance->GetSubsystem<UGameInstanceModuleManager>())
			{
				auto* module = manager->FindModule(UE_MODULE_NAME);
				return Cast<UVLQoLGameInstanceModule>(module);
			}
		}
	}
	return nullptr;
}

/// Gets the CDO of the buildable produced by the given recipe.
static AFGBuildableConveyorAttachment* GetBuildableFromConveyorAttachmentRecipe(TSubclassOf<UFGRecipe> recipe)
{
	UClass* descriptor = UFGRecipe::GetDescriptorForRecipe(recipe);
	check(descriptor && descriptor->IsChildOf<UFGBuildDescriptor>());
	UClass* buildClass = UFGBuildDescriptor::GetBuildClass(descriptor);
	check(buildClass && buildClass->IsChildOf<AFGBuildableConveyorAttachment>());
	return CastChecked<AFGBuildableConveyorAttachment>(buildClass->GetDefaultObject());
}

/// Gets the recipe that will be used for a hologram override.
static TSubclassOf<UFGRecipe> GetOverrideRecipe(TArrayView<UFGHologramOverride*> hologramOverrides, UClass* overrideClass)
{
	check(hologramOverrides.Num() == 1);
	UFGHologramOverride* hologramOverride = hologramOverrides[0];
	check(hologramOverride && hologramOverride->IsA(overrideClass));
	return hologramOverride->GetHologramOverrideWithoutChecks();
}

template<std::derived_from<UFGHologramOverride> T>
static TSubclassOf<UFGRecipe> GetOverrideRecipe(TArrayView<UFGHologramOverride*> hologramOverrides)
{
	return GetOverrideRecipe(hologramOverrides, T::StaticClass());
}

void UVLQoLGameInstanceModule::DispatchLifecycleEvent(ELifecyclePhase phase)
{
	Super::DispatchLifecycleEvent(phase);

	if (!WITH_EDITOR && phase == ELifecyclePhase::INITIALIZATION)
	{
		const int32 regularRecipeCount = RegularConveyorAttachmentRecipes.Num();

		CDOEdits.Reserve(regularRecipeCount * 2);
		RegularToVerticalRecipeMap.Reserve(regularRecipeCount);
		VerticalToRegularRecipeMap.Reserve(regularRecipeCount);

		for (TSubclassOf<UFGRecipe> regularRecipe : RegularConveyorAttachmentRecipes)
		{
			check(regularRecipe);
			AFGBuildableConveyorAttachment* regularBuildable = GetBuildableFromConveyorAttachmentRecipe(regularRecipe);
			check(regularBuildable);
			TSubclassOf<UFGRecipe> verticalRecipe = GetOverrideRecipe<UFGHologramOverride_ConveyorAttachment_FloorToLift>(regularBuildable->mHologramOverrides);
			check(verticalRecipe);
			AFGBuildableConveyorAttachment* verticalBuildable = GetBuildableFromConveyorAttachmentRecipe(verticalRecipe);
			check(verticalBuildable);

			// Sanity check to make sure that the vertical version points back to the regular one.
			check(GetOverrideRecipe<UFGHologramOverride_ConveyorAttachment_LiftToFloor>(verticalBuildable->mHologramOverrides) == regularRecipe);

			// Override with our custom hologram class.
			regularBuildable->mHologramClass = HookConveyorAttachmentHologram;
			verticalBuildable->mHologramClass = HookConveyorAttachmentHologram;

			CDOEdits.Add(regularBuildable);
			CDOEdits.Add(verticalBuildable);
			RegularToVerticalRecipeMap.Add(regularRecipe, verticalRecipe);
			VerticalToRegularRecipeMap.Add(verticalRecipe, regularRecipe);
		}
	}
}
