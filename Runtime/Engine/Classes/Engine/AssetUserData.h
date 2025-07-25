// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif //UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "AssetUserData.generated.h"

/**
 * Object that can be subclassed to store custom data on Unreal asset objects.
 */
UCLASS(DefaultToInstanced, abstract, editinlinenew, MinimalAPI)
class UAssetUserData
	: public UObject
{
	GENERATED_UCLASS_BODY()

	/** used for debugging UAssetUserData data in editor */
	virtual void Draw(class FPrimitiveDrawInterface* PDI, const class FSceneView* View) const {}

	/** used for debugging UAssetUserData data in editor */
	virtual void DrawCanvas(class FCanvas& Canvas, class FSceneView& View) const {}
	
	/** Called when the owner object is modified */

	UE_DEPRECATED(5.6, "PostEditChangeOwner has been deprecated, please override signature with FPropertyChangedEvent parameter instead" )
	virtual void PostEditChangeOwner() {}

	virtual void PostEditChangeOwner(const FPropertyChangedEvent& PropertyChangedEvent) { PRAGMA_DISABLE_DEPRECATION_WARNINGS PostEditChangeOwner(); PRAGMA_ENABLE_DEPRECATION_WARNINGS }
};
