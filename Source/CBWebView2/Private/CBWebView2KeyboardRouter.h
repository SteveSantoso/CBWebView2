// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

#include "WebView2Settings.h"

/**
 * Shared keyboard routing policy for SCBWebView2 and SCBWebView2World.
 *
 * Both widgets used to decide this independently and got it wrong in different ways: the in-window widget
 * claimed every key once it held Slate focus, the world widget claimed every key while the cursor merely
 * hovered the page. Neither left anything for Unreal.
 */
namespace CBWebView2KeyboardRouting
{
	/** Resolve the project-wide default capture mode. */
	inline ECBWebView2KeyboardCaptureMode GetDefaultCaptureMode()
	{
		const UWebView2Settings* Settings = UWebView2Settings::Get();
		return Settings ? Settings->Input.KeyboardCaptureMode : ECBWebView2KeyboardCaptureMode::TextInputOnly;
	}

	/**
	 * Keys the project always wants Unreal to see, regardless of what the page is doing.
	 * Escape is the important one: without it, a page text field could trap the user inside a game UI.
	 */
	inline bool IsKeyReservedForUnreal(const FKey& Key)
	{
		if (!Key.IsValid())
		{
			return false;
		}

		const UWebView2Settings* Settings = UWebView2Settings::Get();
		return Settings && Settings->Input.KeysAlwaysRoutedToUnreal.Contains(Key);
	}

	/**
	 * Whether the page should consume keyboard input right now.
	 *
	 * @param Mode                  Per-widget capture policy.
	 * @param bEditableFocused      A page input / textarea / contenteditable owns DOM focus (reported by the JS bridge).
	 * @param bHoveringInteractive  The cursor is over interactive page content.
	 *
	 * Note on bHoveringInteractive: each widget seeds this differently on purpose. SCBWebView2 starts it true,
	 * because with the transparency hit-test disabled no IsClickable message is ever sent and the whole widget
	 * really is interactive. SCBWebView2World starts it false, because it is only meaningful once a hit-test
	 * (JS or texture alpha) has reported something. Interactive mode inherits those semantics as-is.
	 */
	inline bool ShouldRouteKeysToBrowser(
		ECBWebView2KeyboardCaptureMode Mode,
		bool bEditableFocused,
		bool bHoveringInteractive)
	{
		switch (Mode)
		{
		case ECBWebView2KeyboardCaptureMode::Never:
			return false;

		case ECBWebView2KeyboardCaptureMode::TextInputOnly:
			return bEditableFocused;

		case ECBWebView2KeyboardCaptureMode::Interactive:
			return bEditableFocused || bHoveringInteractive;

		case ECBWebView2KeyboardCaptureMode::Always:
			return true;

		default:
			return bEditableFocused;
		}
	}
}
