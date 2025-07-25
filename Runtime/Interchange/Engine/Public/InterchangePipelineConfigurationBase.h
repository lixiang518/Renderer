// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "InterchangeSourceData.h"
#include "InterchangeTranslatorBase.h"
#include "Nodes/InterchangeBaseNodeContainer.h"

#include "InterchangePipelineConfigurationBase.generated.h"

UENUM(BlueprintType)
enum class EInterchangePipelineConfigurationDialogResult : uint8
{
	Cancel		UMETA(DisplayName = "Cancel"),
	Import		UMETA(DisplayName = "Import"),
	ImportAll	UMETA(DisplayName = "Import All"),
	SaveConfig	UMETA(DisplayName = "Save Config"),
};

USTRUCT(BlueprintType)
struct FInterchangeStackInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interchange | Translator")
	FName StackName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interchange | Translator")
	TArray<TObjectPtr<UInterchangePipelineBase>> Pipelines;
};

UCLASS(BlueprintType, Blueprintable, MinimalAPI)
class UInterchangePipelineConfigurationBase : public UObject
{
	GENERATED_BODY()

public:

	/**
	 * Non-virtual helper that allows Blueprint to implement an event-based function to implement ShowPipelineConfigurationDialog().
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interchange | Translator")
	INTERCHANGEENGINE_API EInterchangePipelineConfigurationDialogResult ScriptedShowPipelineConfigurationDialog(TArray<FInterchangeStackInfo>& PipelineStacks
		, TArray<UInterchangePipelineBase*>& OutPipelines
		, UInterchangeSourceData* SourceData
		, UInterchangeTranslatorBase* Translator
		, UInterchangeBaseNodeContainer* BaseNodeContainer);

	/** The default implementation, which is called if the Blueprint does not have any implementation, calls the virtual ShowPipelineConfigurationDialog(). */
	EInterchangePipelineConfigurationDialogResult ScriptedShowPipelineConfigurationDialog_Implementation(TArray<FInterchangeStackInfo>& PipelineStacks
		, TArray<UInterchangePipelineBase*>& OutPipelines
		, UInterchangeSourceData* SourceData
		, UInterchangeTranslatorBase* Translator
		, UInterchangeBaseNodeContainer* BaseNodeContainer)
	{
		//By default we call the virtual import pipeline execution
		return ShowPipelineConfigurationDialog(PipelineStacks, OutPipelines, SourceData, Translator, BaseNodeContainer);
	}

	/**
	 * Non-virtual helper that allows Blueprint to implement an event-based function to implement ShowScenePipelineConfigurationDialog().
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interchange | Translator")
	INTERCHANGEENGINE_API EInterchangePipelineConfigurationDialogResult ScriptedShowScenePipelineConfigurationDialog(TArray<FInterchangeStackInfo>& PipelineStacks
		, TArray<UInterchangePipelineBase*>& OutPipelines
		, UInterchangeSourceData* SourceData
		, UInterchangeTranslatorBase* Translator
		, UInterchangeBaseNodeContainer* BaseNodeContainer);

	/** The default implementation, which is called if the Blueprint does not have any implementation, calls the virtual ShowScenePipelineConfigurationDialog(). */
	EInterchangePipelineConfigurationDialogResult ScriptedShowScenePipelineConfigurationDialog_Implementation(TArray<FInterchangeStackInfo>& PipelineStacks
		, TArray<UInterchangePipelineBase*>& OutPipelines
		, UInterchangeSourceData* SourceData
		, UInterchangeTranslatorBase* Translator
		, UInterchangeBaseNodeContainer* BaseNodeContainer)
	{
		//By default we call the virtual import pipeline execution
		return ShowScenePipelineConfigurationDialog(PipelineStacks, OutPipelines, SourceData, Translator, BaseNodeContainer);
	}

	/**
	 * Non-virtual helper that allows Blueprint to implement an event-based function to implement ShowReimportPipelineConfigurationDialog().
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interchange | Translator")
	INTERCHANGEENGINE_API EInterchangePipelineConfigurationDialogResult ScriptedShowReimportPipelineConfigurationDialog(TArray<FInterchangeStackInfo>& PipelineStacks
		, TArray<UInterchangePipelineBase*>& OutPipelines
		, UInterchangeSourceData* SourceData
		, UInterchangeTranslatorBase* Translator
		, UInterchangeBaseNodeContainer* BaseNodeContainer
		, UObject* ReimportAsset
		, bool bSceneImport = false);

	/** The default implementation, which is called if the Blueprint does not have any implementation, calls the virtual ShowReimportPipelineConfigurationDialog(). */
	EInterchangePipelineConfigurationDialogResult ScriptedShowReimportPipelineConfigurationDialog_Implementation(TArray<FInterchangeStackInfo>& PipelineStacks
		, TArray<UInterchangePipelineBase*>& OutPipelines
		, UInterchangeSourceData* SourceData
		, UInterchangeTranslatorBase* Translator
		, UInterchangeBaseNodeContainer* BaseNodeContainer
		, UObject* ReimportAsset
		, bool bSceneImport = false)
	{
		//By default we call the virtual import pipeline execution
		return ShowReimportPipelineConfigurationDialog(PipelineStacks, OutPipelines, SourceData, Translator, BaseNodeContainer, ReimportAsset, bSceneImport);
	}

	/**
	 * Non-virtual helper that allows Blueprint to implement an event-based function to implement ShowTestPlanPipelineConfigurationDialog().
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interchange | Translator")
	INTERCHANGEENGINE_API EInterchangePipelineConfigurationDialogResult ScriptedShowTestPlanConfigurationDialog(TArray<FInterchangeStackInfo>& PipelineStacks
		, TArray<UInterchangePipelineBase*>& OutPipelines
		, UInterchangeSourceData* SourceData
		, UInterchangeTranslatorBase* Translator
		, UInterchangeBaseNodeContainer* BaseNodeContainer
		, UObject* ReimportAsset
		, bool bSceneImport = false
		, bool bReimport = false);

	/** The default implementation, which is called if the Blueprint does not have any implementation, calls the virtual ShowTestPlanAssetPipelineConfigurationDialog(). */
	EInterchangePipelineConfigurationDialogResult ScriptedShowTestPlanConfigurationDialog_Implementation(TArray<FInterchangeStackInfo>& PipelineStacks
		, TArray<UInterchangePipelineBase*>& OutPipelines
		, UInterchangeSourceData* SourceData
		, UInterchangeTranslatorBase* Translator
		, UInterchangeBaseNodeContainer* BaseNodeContainer
		, UObject* ReimportAsset
		, bool bSceneImport = false
		, bool bReimport = false)
	{
		//By default we call the virtual import pipeline execution
		return ShowTestPlanConfigurationDialog(PipelineStacks, OutPipelines, SourceData, Translator, BaseNodeContainer, ReimportAsset, bSceneImport, bReimport);
	}

protected:

	/**
	 * This function shows a dialog used to configure pipeline stacks and returns a stack name that tells the caller the user's choice.
	 */
	virtual EInterchangePipelineConfigurationDialogResult ShowPipelineConfigurationDialog(TArray<FInterchangeStackInfo>& PipelineStacks
		, TArray<UInterchangePipelineBase*>& OutPipelines
		, TWeakObjectPtr<UInterchangeSourceData> SourceData
		, TWeakObjectPtr <UInterchangeTranslatorBase> Translator
		, TWeakObjectPtr <UInterchangeBaseNodeContainer> BaseNodeContainer)
	{ 
		//Not implemented
		return EInterchangePipelineConfigurationDialogResult::Cancel;
	}

	/**
	 * This function shows a dialog used to configure pipeline stacks and returns a stack name that tells the caller the user's choice.
	 */
	virtual EInterchangePipelineConfigurationDialogResult ShowScenePipelineConfigurationDialog(TArray<FInterchangeStackInfo>& PipelineStacks
		, TArray<UInterchangePipelineBase*>& OutPipelines
		, TWeakObjectPtr<UInterchangeSourceData> SourceData
		, TWeakObjectPtr <UInterchangeTranslatorBase> Translator
		, TWeakObjectPtr <UInterchangeBaseNodeContainer> BaseNodeContainer)
	{ 
		//Not implemented
		return EInterchangePipelineConfigurationDialogResult::Cancel;
	}

	/**
	 * This function shows a dialog used to configure pipeline stacks and returns a stack name that tells the caller the user's choice.
	 */
	virtual EInterchangePipelineConfigurationDialogResult ShowReimportPipelineConfigurationDialog(TArray<FInterchangeStackInfo>& PipelineStacks
		, TArray<UInterchangePipelineBase*>& OutPipelines
		, TWeakObjectPtr<UInterchangeSourceData> SourceData
		, TWeakObjectPtr <UInterchangeTranslatorBase> Translator
		, TWeakObjectPtr <UInterchangeBaseNodeContainer> BaseNodeContainer
		, TWeakObjectPtr <UObject> ReimportAsset
		, bool bSceneImport)
	{
		//Not implemented
		return EInterchangePipelineConfigurationDialogResult::Cancel;
	}

	/**
	 * This function shows a dialog used to configure pipeline stacks in interchange test plan asset.
	 */
	virtual EInterchangePipelineConfigurationDialogResult ShowTestPlanConfigurationDialog(TArray<FInterchangeStackInfo>& PipelineStacks
		, TArray<UInterchangePipelineBase*>& OutPipelines
		, TWeakObjectPtr<UInterchangeSourceData> SourceData
		, TWeakObjectPtr <UInterchangeTranslatorBase> Translator
		, TWeakObjectPtr <UInterchangeBaseNodeContainer> BaseNodeContainer
		, TWeakObjectPtr <UObject> ReimportAsset
		, bool bSceneImport
		, bool bReimport)
	{
		//Not implemented
		return EInterchangePipelineConfigurationDialogResult::Cancel;
	}
};
