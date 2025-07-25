// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "GenericPlatform/GenericPlatformSplash.h"

/**
 * Windows splash implementation.
 */
struct FWindowsPlatformSplash
	: public FGenericPlatformSplash
{
	/** Show the splash screen. */
	static APPLICATIONCORE_API void Show();

	/** Hide the splash screen. */
	static APPLICATIONCORE_API void Hide();

	/**
	 * Sets the progress displayed on the application icon (for startup/loading progress).
	 *
	 * @param InType Progress value in percent.
	 */
	static APPLICATIONCORE_API void SetProgress(int ProgressPercent);

	/**
	 * Sets the text displayed on the splash screen (for startup/loading progress).
	 *
	 * @param InType Type of text to change.
	 * @param InText Text to display.
	 */
	using FGenericPlatformSplash::SetSplashText;
	static APPLICATIONCORE_API void SetSplashText( const SplashTextType::Type InType, const FText& InText );

	/**
	 * Return whether the splash screen is being shown or not
	 */
	static APPLICATIONCORE_API bool IsShown();
};


typedef FWindowsPlatformSplash FPlatformSplash;
