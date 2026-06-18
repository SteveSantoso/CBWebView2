// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/AllowWindowsPlatformAtomics.h"

THIRD_PARTY_INCLUDES_START
#include <winrt/Windows.System.h>
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformAtomics.h"
#include "Windows/HideWindowsPlatformTypes.h"

class FWebView2CompositionHost;
class FWebView2Window;

/**
 * Global WebView2 manager.
 *
 * Responsibilities:
 * 1. Share a DispatcherQueue.
 * 2. Create and cache a CompositionHost for each host HWND.
 * 3. Forward window messages to the appropriate WebView host.
 */
class WEBVIEW2UTILS_API FWebView2Manager
{
public:
	static FWebView2Manager& Get();

	/** Initialize the shared DispatcherQueue and internal state. */
	void Initialize();
	/** Shut down all hosts and release WinComp resources. */
	void Shutdown();

	/** Create a new WebView instance for the specified host window. */
	TSharedPtr<FWebView2Window> CreateWebView(
		HWND ParentWindow,
		const FGuid& InstanceId,
		const FString& InitialUrl,
		const FColor& BackgroundColor,
		bool bEnableTransparencyHitTest,
		bool bAttachToHostVisual = true,
		HWND VisualHostWindow = nullptr,
		bool bAllowNonInteractiveElementPassthrough = false);

	/** Get or create the WinComp composition host associated with the host window. */
	TSharedPtr<FWebView2CompositionHost> GetOrCreateCompositionHost(HWND WindowHandle);

	/** Forward a Win32 message to the WebView composition layer under the matching host window. */
	bool HandleWindowMessage(HWND WindowHandle, UINT Message, WPARAM WParam, LPARAM LParam);
	/** Clean up every WebView resource attached to a host window when it closes. */
	void OnHostWindowClosed(HWND WindowHandle);

	/** Return the detected WebView2 Runtime version; empty means no usable runtime was found. */
	FString GetRuntimeVersion() const
	{
		return RuntimeVersion;
	}

	/** Whether WebView2 Runtime was detected on the current machine. */
	bool IsRuntimeAvailable() const
	{
		return bRuntimeAvailable;
	}

	/** Whether any WebView instance is currently alive, regardless of host mode. */
	bool HasActiveWebViews() const;

	/** Return the shared DispatcherQueueController used by WinComp modes. */
	winrt::Windows::System::DispatcherQueueController GetDispatcherQueueController() const
	{
		return DispatcherQueueController;
	}

private:
	FWebView2Manager() = default;

	/** Lazily create a DispatcherQueue for the current thread. */
	bool EnsureDispatcherQueue();
	/** Detect and cache the WebView2 Runtime version. */
	void DetectRuntimeVersion();

	/** One composition host is maintained per host HWND. */
	TMap<HWND, TWeakPtr<FWebView2CompositionHost>> CompositionHosts;
	winrt::Windows::System::DispatcherQueueController DispatcherQueueController{nullptr};
	FString RuntimeVersion;
	bool bRuntimeAvailable = false;
	bool bInitialized = false;
};
