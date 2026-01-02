#pragma once

#include "CoreMinimal.h"
#include "Module/GameInstanceModule.h"
#include "VLQoLGameInstanceModule.generated.h"

class AFGConveyorAttachmentHologram;
class UFGRecipe;

UCLASS()
class VERTICALLOGISTICSQOL_API UVLQoLGameInstanceModule : public UGameInstanceModule
{
	GENERATED_BODY()

public:
	static UVLQoLGameInstanceModule* Get(UObject* worldContext);
	static UVLQoLGameInstanceModule* Get(UWorld* world);

	/// Gets the recipe for the vertical version of the given recipe.
	/// Returns null if the recipe doesn't represent a regular conveyor attachment.
	TSubclassOf<UFGRecipe> GetVerticalConveyorAttachmentRecipe(TSubclassOf<UFGRecipe> recipe) const
	{
		return RegularToVerticalRecipeMap.FindRef(recipe);
	}

	/// Gets the recipe for the regular (non-vertical) version of the given recipe.
	/// Returns null if the recipe doesn't represent a vertical conveyor attachment.
	TSubclassOf<UFGRecipe> GetRegularConveyorAttachmentRecipe(TSubclassOf<UFGRecipe> recipe) const
	{
		return VerticalToRegularRecipeMap.FindRef(recipe);
	}

	// UGameInstanceModule
	virtual void DispatchLifecycleEvent(ELifecyclePhase phase) override;

protected:
	/// Recipes for the regular (non-vertical) versions of the conveyor attachments.
	/// Don't put the vertical versions here, they'll be discovered automatically.
	UPROPERTY(EditDefaultsOnly)
	TArray<TSubclassOf<UFGRecipe>> RegularConveyorAttachmentRecipes;

	/// Class that will replace the default conveyor attachment hologram.
	UPROPERTY(EditDefaultsOnly)
	TSubclassOf<AFGConveyorAttachmentHologram> HookConveyorAttachmentHologram;

	UPROPERTY()
	TArray<UObject*> CDOEdits;

	UPROPERTY()
	TMap<TSubclassOf<UFGRecipe>, TSubclassOf<UFGRecipe>> RegularToVerticalRecipeMap;

	UPROPERTY()
	TMap<TSubclassOf<UFGRecipe>, TSubclassOf<UFGRecipe>> VerticalToRegularRecipeMap;
};
