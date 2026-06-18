// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WebView2Settings.h"

class FWebView2Window;
class UTexture2D;

/**
 * Continuous texture output source for world-space presentation.
 *
 * Internally it creates an off-screen host window, attaches the WebView2
 * instance to that window's WinComp visual tree, and then updates a UE
 * texture through continuous frame capture for higher-level consumers such as
 * WidgetComponent.
 */
class WEBVIEW2UTILS_API FWebView2ContinuousTextureSource
{
public:
	FWebView2ContinuousTextureSource(
		const FString& InInitialUrl,
		const FColor& InBackgroundColor,
		bool bInEnableTransparencyHitTest,
		ECBWebView2WorldOutputMode InOutputMode = ECBWebView2WorldOutputMode::Auto,
		bool bInAllowCpuFallback = true,
		bool bInInjectTransparencyHitTestScript = true);
	~FWebView2ContinuousTextureSource();

	/**
	 * Predict whether the resolved output mode for InRequestedMode keeps a persistent capture-side frame
	 * that supports TrySampleAlphaAtPoint. Lets callers choose TextureAlpha hit-testing (and skip the JS
	 * script injection) before the source is initialized.
	 */
	static bool PredictsAlphaSamplingSupport(ECBWebView2WorldOutputMode InRequestedMode);

	bool Initialize(const FIntPoint& InInitialSize, void* InFocusParentWindow = nullptr);
	void Shutdown();
	void Resize(const FIntPoint& InSize);
	void RequestImmediateUpload();
	void UpdateInputScreenAnchor(const FVector2D& InScreenSpacePosition, const FVector2D& InLocalWebViewPoint);
	void UpdateImeCaretAnchor(const FVector2D& InScreenSpacePosition, const FVector2D& InLocalWebViewPoint, float InCaretHeight);
	void RefreshImeWindowPosition();
	void ReleaseInputScreenAnchor();
	bool TickOutput(double InCurrentTime, float InMaxFramesPerSecond, UTexture2D*& OutUpdatedTexture);

	/**
	 * Sample the latest captured frame's alpha at a browser-client-space pixel (game thread).
	 * Internally throttled and serialized against the capture thread; a 1x1 readback on the private
	 * capture device, so it never touches the Unreal render thread.
	 * Returns false when the active output mode has no persistent capture-side frame to sample.
	 */
	bool TrySampleAlphaAtPoint(const FVector2D& InBrowserPoint, uint8& OutAlpha);
	/** Whether the active output mode supports TrySampleAlphaAtPoint. Valid after Initialize. */
	bool SupportsAlphaSampling() const;

	UTexture2D* GetTexture() const;
	TSharedPtr<FWebView2Window> GetWebViewWindow() const;
	ECBWebView2WorldOutputMode GetRequestedOutputMode() const;
	ECBWebView2WorldOutputMode GetActiveOutputMode() const;

private:
	struct FImpl;
	TUniquePtr<FImpl> Impl;
	TSharedPtr<FWebView2Window> WebViewWindow;
	FString InitialUrl;
	FColor BackgroundColor;
	ECBWebView2WorldOutputMode RequestedOutputMode = ECBWebView2WorldOutputMode::Auto;
	bool bAllowCpuFallback = true;
	bool bEnableTransparencyHitTest = false;
	bool bInjectTransparencyHitTestScript = true;
};
