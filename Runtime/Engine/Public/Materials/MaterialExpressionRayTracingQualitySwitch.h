// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "MaterialExpressionIO.h"
#include "MaterialValueType.h"
#include "Materials/MaterialExpression.h"
#include "MaterialExpressionRayTracingQualitySwitch.generated.h"

UCLASS()
class UMaterialExpressionRayTracingQualitySwitch : public UMaterialExpression
{
	GENERATED_BODY()

public:

	/** Used for standard rasterization */
	UPROPERTY()
	FExpressionInput Normal;

	/** Used for simplified ray trace eval */
	UPROPERTY()
	FExpressionInput RayTraced;

	//~ Begin UMaterialExpression Interface
#if WITH_EDITOR
	virtual int32 Compile(class FMaterialCompiler* Compiler, int32 OutputIndex) override;
	virtual void GetCaption(TArray<FString>& OutCaptions) const override;
	virtual bool IsResultMaterialAttributes(int32 OutputIndex) override;
	virtual EMaterialValueType GetInputValueType(int32 InputIndex) override;
	virtual EMaterialValueType GetOutputValueType(int32 OutputIndex) override { return MCT_Unknown; }

#endif
	//~ End UMaterialExpression Interface
};
