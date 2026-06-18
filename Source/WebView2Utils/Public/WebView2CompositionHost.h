// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/AllowWindowsPlatformAtomics.h"

THIRD_PARTY_INCLUDES_START
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <winrt/Windows.UI.Composition.h>
#include <windows.ui.composition.interop.h>
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformAtomics.h"
#include "Windows/HideWindowsPlatformTypes.h"

class FWebView2Window;

/**
 * WinComp composition host.
 *
 * When the WebView runs in windowless mode, its Visual is attached to the
 * visual tree managed here.
 */
class WEBVIEW2UTILS_API FWebView2CompositionHost
{
public:
	FWebView2CompositionHost(
		HWND InWindowHandle,
		winrt::Windows::System::DispatcherQueueController InDispatcherQueueController);
	~FWebView2CompositionHost();

	/** Initialize the desktop target and root visual tree. */
	void Initialize();
	/** Attach a WebView to the current host visual tree. */
	void AttachWebView(const TSharedRef<FWebView2Window>& WebViewWindow);
	/** Reorder all WebView visuals by LayerId. */
	void RefreshVisualOrder();
	/** Remove the specified WebView from the visual tree. */
	void DetachWebView(const FGuid& InstanceId, const winrt::Windows::UI::Composition::ContainerVisual& WebViewContainer);
	/** Destroy all visual objects. */
	void Destroy();

	/** Handle mouse messages received by the host window. */
	bool HandleMouseMessage(UINT Message, WPARAM WParam, LPARAM LParam);
	HWND GetWindowHandle() const;

private:
	struct FMouseButtonSequenceState
	{
		TWeakPtr<FWebView2Window> LastDownTarget;
		TWeakPtr<FWebView2Window> LastUpTarget;
		TWeakPtr<FWebView2Window> PendingDoubleClickFallbackTarget;
		POINT LastDownClientPoint = {};
		POINT LastUpClientPoint = {};
		POINT PendingDoubleClickFallbackLocalPoint = {};
		POINT PendingDoubleClickFallbackScreenPoint = {};
		double LastDownTimeSeconds = -1.0;
		double LastUpTimeSeconds = -1.0;
		double PendingDoubleClickFallbackTimeSeconds = -1.0;
		WPARAM PendingDoubleClickFallbackWParam = 0;
		bool bLastDownForwarded = false;
		bool bLastUpForwarded = false;
		bool bHasPendingDoubleClickFallback = false;
	};

	/** Create the DesktopWindowTarget. */
	void CreateDesktopTarget();
	/** Create the root visual tree and the WebView layer. */
	void CreateRootVisual();
	/** Find WebViews hit by the given point, optionally requiring them to be currently interactive. */
	TArray<TSharedRef<FWebView2Window>> FindWebViewsAtPoint(const POINT& ClientPoint, bool bRequireHitTestEnabled) const;

	HWND WindowHandle = nullptr;
	int32 NextLayerId = 0;
	static constexpr int32 MouseButtonSequenceStateCount = 6;

	/** Cache all WebViews under this host, keyed by instance ID. */
	TMap<FString, TWeakPtr<FWebView2Window>> WebViews;

	winrt::Windows::System::DispatcherQueueController DispatcherQueueController{nullptr};
	winrt::Windows::UI::Composition::Compositor Compositor{nullptr};
	winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget WindowTarget{nullptr};
	winrt::Windows::UI::Composition::ContainerVisual RootVisual{nullptr};
	winrt::Windows::UI::Composition::ContainerVisual WebViewLayer{nullptr};
	FMouseButtonSequenceState MouseButtonSequenceStates[MouseButtonSequenceStateCount];
};