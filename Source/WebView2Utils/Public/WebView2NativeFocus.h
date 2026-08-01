// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"

/**
 * Win32 focus / caret ownership helpers shared by the Slate widgets and the subsystem.
 *
 * Keyboard input in visual-hosting mode is decided entirely by native focus: while a WebView2-owned HWND
 * holds it, the OS routes keys straight into the browser and Slate never sees them. Deciding whether the
 * current focus belongs to WebView2 is therefore the same question in three different places, and it used
 * to be answered by three subtly different copies of the same parent-chain walk.
 */
namespace CBWebView2NativeFocus
{
	/**
	 * Whether Candidate belongs to a WebView2 host: either it sits under OwnedRoot, or its parent chain
	 * passes through a Chromium host window class before reaching StopAt.
	 *
	 * @param Candidate  The window to classify, typically ::GetFocus().
	 * @param StopAt     Where to stop walking, typically the Unreal host HWND. May be null to walk to the desktop.
	 * @param OwnedRoot  An HWND that is WebView2's by construction and is not necessarily a child of StopAt.
	 *                   The world-space path parents its controller to a hidden top-level window, which is
	 *                   invisible to a walk that terminates at the Unreal window - pass it here.
	 */
	WEBVIEW2UTILS_API bool IsWebView2OwnedWindow(HWND Candidate, HWND StopAt, HWND OwnedRoot = nullptr);

	/** Whether the calling thread's caret currently belongs to Window. Guards ::DestroyCaret from stealing another widget's caret. */
	WEBVIEW2UTILS_API bool ThreadOwnsCaretFor(HWND Window);
}

#endif
