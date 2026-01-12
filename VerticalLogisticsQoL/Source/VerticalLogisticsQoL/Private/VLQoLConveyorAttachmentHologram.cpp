#include "VLQoLConveyorAttachmentHologram.h"

#include "Buildables/FGBuildableConveyorAttachment.h"
#include "Buildables/FGBuildableConveyorLift.h"
#include "FGConstructDisqualifier.h"
#include "FGFactoryConnectionComponent.h"
#include "FGFactorySettings.h"
#include "FGRecipe.h"
#include "Net/Core/PushModel/PushModel.h"
#include "Net/UnrealNetwork.h"
#include "Resources/FGBuildDescriptor.h"
#include "VLQoLBuildModes.h"
#include "VLQoLConstructDisqualifiers.h"
#include "VLQoLGameInstanceModule.h"

AVLQoLConveyorAttachmentHologram::AVLQoLConveyorAttachmentHologram()
{
}

void AVLQoLConveyorAttachmentHologram::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams params;
	params.bIsPushBased = true;

	DOREPLIFETIME_WITH_PARAMS_FAST(AVLQoLConveyorAttachmentHologram, mConveyorAttachmentMode, params);
}

void AVLQoLConveyorAttachmentHologram::BeginPlay()
{
	auto* gameInstanceModule = UVLQoLGameInstanceModule::Get(this);
	check(gameInstanceModule);

	mRealRecipe = mRecipe;
	mRealBuildClass = mBuildClass;
	mVerticalRecipe = gameInstanceModule->GetVerticalConveyorAttachmentRecipe(mRecipe);
	checkf(mVerticalRecipe, TEXT("Failed to find the vertical version of %s"), *mRecipe->GetName());

	Super::BeginPlay();

	// Checking various assumptions about the hologram internals.
	check(mDefaultBuildMode);
	check(!mUseConveyorConnectionFrameMesh);
	check(mUseConveyorConnectionArrowMesh);
	check(mGeneratedAbstractComponents.Num() == 1 && mGeneratedAbstractComponents[0]->ComponentHasTag(HOLOGRAM_MESH_TAG));
	check(mCachedFactoryConnectionComponents.Num() == CONNECTION_COUNT);

	// Find the arrow mesh for each of the connection components.
	for (UFGFactoryConnectionComponent* connection : mCachedFactoryConnectionComponents)
	{
		switch (const EFactoryConnectionDirection direction = connection->GetDirection())
		{
		case EFactoryConnectionDirection::FCD_INPUT:
		case EFactoryConnectionDirection::FCD_OUTPUT:
		{
			const FName meshTag = direction == EFactoryConnectionDirection::FCD_INPUT
				? mInputConnectionMeshTag
				: mOutputConnectionMeshTag;

			for (USceneComponent* child : connection->GetAttachChildren())
			{
				if (child->ComponentHasTag(meshTag))
				{
					mArrowMeshes.Add(CastChecked<UStaticMeshComponent>(child));
					goto found;
				}
			}

			checkf(false, TEXT("Failed to find the mesh for connection %s (in %s)"),
				*connection->GetName(), *mBuildClass->GetName());

		found:
			break;
		}

		default:
			checkf(false, TEXT("Connection %s (in %s) has an unexpected direction"),
				*connection->GetName(), *mBuildClass->GetName())
		}
	}

	if (mConveyorAttachmentMode != EVLQoLConveyorAttachmentMode::Regular)
	{
		OnRep_ConveyorAttachmentMode();
	}
}

void AVLQoLConveyorAttachmentHologram::GetSupportedBuildModes_Implementation(TArray<TSubclassOf<UFGBuildGunModeDescriptor>>& out_buildmodes) const
{
	Super::GetSupportedBuildModes_Implementation(out_buildmodes);
	out_buildmodes.Add(UVLQoLVerticalUpBuildMode::StaticClass());
	out_buildmodes.Add(UVLQoLVerticalDownBuildMode::StaticClass());
}

void AVLQoLConveyorAttachmentHologram::OnBuildModeChanged(TSubclassOf<UFGHologramBuildModeDescriptor> buildMode)
{
	Super::OnBuildModeChanged(buildMode);

	// Update the mode enum to represent the new build mode.
	EVLQoLConveyorAttachmentMode newMode;
	if (buildMode == UVLQoLVerticalUpBuildMode::StaticClass())
		newMode = EVLQoLConveyorAttachmentMode::VerticalUp;
	else if (buildMode == UVLQoLVerticalDownBuildMode::StaticClass())
		newMode = EVLQoLConveyorAttachmentMode::VerticalDown;
	else
		newMode = CalculateAutoMode();
	SetConveyorAttachmentMode(newMode);
}

void AVLQoLConveyorAttachmentHologram::CheckValidFloor()
{
	const EVLQoLConveyorAttachmentMode mode = mConveyorAttachmentMode;

	if (mode == EVLQoLConveyorAttachmentMode::Regular || mSnappedConveyor == nullptr)
	{
		// If we're not (or shouldn't be) snapped to a lift, then use the same floor detection as any other
		// buildable. We're intentionally skipping the AFGConveyorAttachmentHologram implementation as that
		// has extra lift logic that we don't want.
		AFGBuildableHologram::CheckValidFloor();
	}
	else if (auto* lift = Cast<AFGBuildableConveyorLift>(mSnappedConveyor))
	{
		// If we have fixed I/O directions, then that should match the flow direction on the lift.
		if (mode != EVLQoLConveyorAttachmentMode::VerticalAuto)
		{
			if ((mode == EVLQoLConveyorAttachmentMode::VerticalUp) != lift->IsFlowUpwards())
			{
				AddConstructDisqualifier(UVLQoLCDWrongLiftDirection::StaticClass());
			}
		}
	}
	else
	{
		// Snapped to a non-lift conveyor.
		AddConstructDisqualifier(UFGCDInvalidPlacement::StaticClass());
	}
}

void AVLQoLConveyorAttachmentHologram::PostHologramPlacement(const FHitResult& hitResult, bool callForChildren)
{
	Super::PostHologramPlacement(hitResult, callForChildren);

	// Keep the attachment type up to date if we're in the auto build mode.
	if (!IsHologramLocked() && IsCurrentBuildMode(mDefaultBuildMode))
	{
		const EVLQoLConveyorAttachmentMode newMode = CalculateAutoMode();
		SetConveyorAttachmentMode(newMode);
	}
}

TOptional<TSubclassOf<UFGRecipe>> AVLQoLConveyorAttachmentHologram::ProcessHologramOverride(const FHitResult& hitResult) const
{
	// The base implementation uses hologram overrides to switch between the regular and vertical
	// versions. We're doing that on our own so this function has been removed.
	return {};
}

TArray<FItemAmount> AVLQoLConveyorAttachmentHologram::GetBaseCost() const
{
	// Use the real recipe when calculating the cost; for some reason some of the attachments have
	// different costs for their vertical versions.
	return UFGRecipe::GetIngredients(mRealRecipe);
}

void AVLQoLConveyorAttachmentHologram::PostConstructMessageDeserialization()
{
	Super::PostConstructMessageDeserialization();

	// The construction message tells us what mode we're in, but not necessarily what the side effects
	// are from switching to that mode. At this point all that we care about is that the recipe is
	// correct, as that's all that we need to construct the item.
	UpdateRecipe();
}

AActor* AVLQoLConveyorAttachmentHologram::Construct(TArray<AActor*>& out_children, FNetConstructionID constructionID)
{
	// Override the recipe and build class with the real versions for construction.
	const auto oldRecipe = mRecipe;
	const auto oldBuildClass = mBuildClass;
	mRecipe = mRealRecipe;
	mBuildClass = mRealBuildClass;
	const auto result = Super::Construct(out_children, constructionID);
	mRecipe = oldRecipe;
	mBuildClass = oldBuildClass;
	return result;
}

void AVLQoLConveyorAttachmentHologram::ConfigureComponents(AFGBuildable* inBuildable) const
{
	// Set up the vertical connection directions if we have a fixed flow direction, the base class will
	// handle the case where the flow is inferred from what the attachment is snapped to.
	switch (const EVLQoLConveyorAttachmentMode mode = mConveyorAttachmentMode)
	{
	case EVLQoLConveyorAttachmentMode::VerticalUp:
	case EVLQoLConveyorAttachmentMode::VerticalDown:
	{
		FName inputName;
		FName outputName;

		if (mode == EVLQoLConveyorAttachmentMode::VerticalUp)
		{
			inputName = mLiftConnection_Top;
			outputName = mLiftConnection_Bottom;
		}
		else
		{
			inputName = mLiftConnection_Bottom;
			outputName = mLiftConnection_Top;
		}

		for (auto* connection : TInlineComponentArray<UFGFactoryConnectionComponent*>(inBuildable))
		{
			const FName name = connection->GetFName();
			if (name == inputName)
				connection->SetDirection(EFactoryConnectionDirection::FCD_INPUT);
			else if (name == outputName)
				connection->SetDirection(EFactoryConnectionDirection::FCD_OUTPUT);
		}

		break;
	}

	default:
		break;
	}

	Super::ConfigureComponents(inBuildable);
}

EVLQoLConveyorAttachmentMode AVLQoLConveyorAttachmentHologram::CalculateAutoMode() const
{
	// Build a vertical attachment when snapped to a lift.
	const bool snappedToLift = mSnappedConveyor && mSnappedConveyor->IsA<AFGBuildableConveyorLift>();
	return snappedToLift ? EVLQoLConveyorAttachmentMode::VerticalAuto : EVLQoLConveyorAttachmentMode::Regular;
}

void AVLQoLConveyorAttachmentHologram::SetConveyorAttachmentMode(EVLQoLConveyorAttachmentMode newMode)
{
	if (newMode == mConveyorAttachmentMode)
		return;
	mConveyorAttachmentMode = newMode;
	OnRep_ConveyorAttachmentMode();
	MARK_PROPERTY_DIRTY_FROM_NAME(AVLQoLConveyorAttachmentHologram, mConveyorAttachmentMode, this);
}

void AVLQoLConveyorAttachmentHologram::OnRep_ConveyorAttachmentMode()
{
	if (!HasActorBegunPlay())
		return;
	UpdateRecipe();
	UpdateClearance();
	UpdateHologramComponents();
}

void AVLQoLConveyorAttachmentHologram::UpdateRecipe()
{
	TSubclassOf<UFGRecipe> newRecipe;
	TSubclassOf<AFGBuildableConveyorAttachment> newBuildClass;

	if (mConveyorAttachmentMode == EVLQoLConveyorAttachmentMode::Regular)
		newRecipe = mRecipe;
	else
		newRecipe = mVerticalRecipe;

	// It's possible that the recipe doesn't change even if the attachment mode does, e.g. both up and
	// down use the same vertical attachment recipe.
	if (newRecipe == mRealRecipe)
		return;

	// Find the buildable from the recipe.
	{
		UClass* descriptor = UFGRecipe::GetDescriptorForRecipe(newRecipe);
		UClass* buildClass = UFGBuildDescriptor::GetBuildClass(descriptor);
		check(buildClass && buildClass->IsChildOf<AFGBuildableConveyorAttachment>());
		newBuildClass = buildClass;
	}

	mRealRecipe = newRecipe;
	mRealBuildClass = newBuildClass;

	// Refresh the buildable property names; we don't use these, but the base class does.
	mBuildablePropertyNames.Reset();
	for (TFieldIterator<FProperty> it(newBuildClass); it; ++it)
	{
		mBuildablePropertyNames.Add(it->GetFName());
	}
}

void AVLQoLConveyorAttachmentHologram::UpdateClearance()
{
	// Vertical attachments don't have any clearance data usually, which means that they never get
	// warnings about clipping. Our hologram is initialized with the clerance from the regular
	// attachment, but that's enough to get warnings when putting an attachment on a lift that's backed
	// up to a wall. I don't want to remove the cleraance entirely in vertical mode, so I'm just
	// shrinking the box slightly to fix the wall case and hope that's enough.

	constexpr float inset = 25.0f;

	mClearanceData.Reset();
	IFGClearanceInterface::Execute_GetClearanceData(mBuildClass.GetDefaultObject(), mClearanceData);

	if (mConveyorAttachmentMode != EVLQoLConveyorAttachmentMode::Regular && !mClearanceData.IsEmpty())
	{
		FBox& box = mClearanceData[0].ClearanceBox;
		const FVector insetVec(inset, inset, 0);
		box.Min += insetVec;
		box.Max -= insetVec;
	}
}

void AVLQoLConveyorAttachmentHologram::UpdateHologramComponents()
{
	const AFGBuildableConveyorAttachment* buildable = mRealBuildClass.GetDefaultObject();
	check(buildable);

	// Update the main mesh to match the new buildbale.
	{
		UStaticMeshComponent* component = mGeneratedAbstractComponents[0];

		UAbstractInstanceDataObject* abstractData = buildable->GetLightweightInstanceData();
		check(abstractData && abstractData->HasValidInstanceData());
		// Not using the public GetInstanceData() because that copies the entire array :(
		const FInstanceData& instance = abstractData->Instances[0];

		component->SetStaticMesh(instance.StaticMesh);
		component->SetRelativeTransform(instance.RelativeTransform);
	}

	// Collect all of the connections on the new buildable.
	TArray<const USCS_Node*, TInlineAllocator<CONNECTION_COUNT>> buildableConnectionNodes;
	{
		auto* buildableClass = CastChecked<UBlueprintGeneratedClass>(buildable->GetClass());
		for (const USCS_Node* node : buildableClass->SimpleConstructionScript->GetRootNodes())
		{
			if (node->ComponentClass->IsChildOf<UFGFactoryConnectionComponent>())
			{
				buildableConnectionNodes.Add(node);
			}
		}
		check(buildableConnectionNodes.Num() == CONNECTION_COUNT);
	}

	UFGFactorySettings* settings = UFGFactorySettings::Get();
	check(settings);

	// Update the hologram connections to match the buildable ones.
	for (int32 i = 0; i != CONNECTION_COUNT; ++i)
	{
		UFGFactoryConnectionComponent* hologramConnection = mCachedFactoryConnectionComponents[i];
		const USCS_Node* buildableConnectionNode = buildableConnectionNodes[i];

		const UFGFactoryConnectionComponent* buildableConnection =
			CastChecked<UFGFactoryConnectionComponent>(buildableConnectionNode->ComponentTemplate);

		// This only works because we know that the connection is a root component, so we don't have a
		// parent transform to update as well.
		hologramConnection->SetRelativeTransform(buildableConnection->GetRelativeTransform());

		const EFactoryConnectionDirection connectionDirection =
			CalculateConnectionDirection(buildableConnection, buildableConnectionNode->GetVariableName());

		if (hologramConnection->GetDirection() != connectionDirection)
		{
			hologramConnection->SetDirection(connectionDirection);
			UpdateArrowMesh(mArrowMeshes[i], connectionDirection, settings);
		}
	}
}

EFactoryConnectionDirection AVLQoLConveyorAttachmentHologram::CalculateConnectionDirection(const UFGFactoryConnectionComponent* connection, FName connectionName) const
{
	const EFactoryConnectionDirection staticDirection = connection->GetDirection();

	if (staticDirection != EFactoryConnectionDirection::FCD_ANY)
		return staticDirection;	// The direction is known.
	if (connectionName != mLiftConnection_Top && connectionName != mLiftConnection_Bottom)
		return staticDirection;	// Not a lift connection, not sure what to do with this.

	// Use the vertical flow direction to determine what should be an input or an output.

	bool flowUpwards;

	switch (mConveyorAttachmentMode)
	{
	case EVLQoLConveyorAttachmentMode::VerticalUp:
		flowUpwards = true;
		break;

	case EVLQoLConveyorAttachmentMode::VerticalDown:
		flowUpwards = false;
		break;

	default:
		// In auto mode, the direction is defined by the snapped lift.
		if (auto* lift = Cast<AFGBuildableConveyorLift>(mSnappedConveyor))
		{
			flowUpwards = lift->IsFlowUpwards();
		}
		else
		{
			return staticDirection;
		}
	}

	return flowUpwards == (connectionName == mLiftConnection_Top)
		? EFactoryConnectionDirection::FCD_INPUT
		: EFactoryConnectionDirection::FCD_OUTPUT;
}

void AVLQoLConveyorAttachmentHologram::UpdateArrowMesh(UStaticMeshComponent* arrowMesh, EFactoryConnectionDirection direction, const UFGFactorySettings* settings)
{
	// This could've been an input or an output, clear the tags in preparation for its new identity.
	arrowMesh->ComponentTags.Remove(mInputConnectionMeshTag);
	arrowMesh->ComponentTags.Remove(mOutputConnectionMeshTag);

	if (direction == EFactoryConnectionDirection::FCD_ANY)
	{
		// These connections aren't rendered.
		arrowMesh->SetVisibility(false);
	}
	else
	{
		FName tag;
		UMaterialInstance* material;
		FQuat rotation;

		// Inputs are flipped 180 degrees to point back in to the connection.
		if (direction == EFactoryConnectionDirection::FCD_INPUT)
		{
			tag = mInputConnectionMeshTag;
			material = settings->mDefaultInputConnectionMaterial;
			rotation = FRotator(0, 180, 0).Quaternion();
		}
		else
		{
			tag = mOutputConnectionMeshTag;
			material = settings->mDefaultOutputConnectionMaterial;
			rotation = FQuat::Identity;
		}

		arrowMesh->ComponentTags.Add(tag);
		arrowMesh->SetMaterial(0, material);
		arrowMesh->SetRelativeRotation(rotation);
		arrowMesh->SetVisibility(true);
	}
}
