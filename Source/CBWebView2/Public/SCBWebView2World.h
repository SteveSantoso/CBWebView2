// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Engine/Texture2D.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SCompoundWidget.h"

#include "WebView2ContinuousTextureSource.h"
#include "WebView2EventTypes.h"
#include "WebView2Settings.h"
#include "WebView2Window.h"

class SImage;
DECLARE_DELEGATE_OneParam(FOnCBWebView2WorldScriptCallback, const FString&)
DECLARE_DELEGATE_OneParam(FOnCBWebView2WorldFrameUpdatedNative, UTexture2D*)

/**
 * World-space WebView Slate proxy.
 *
 * Instead of attaching web content directly to the host window, it periodically pulls image output.
 */
class CBWEBVIEW2_API SCBWebView2World : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCBWebView2World)
		: _InitialUrl(TEXT("https://www.bing.com"))
		, _BackgroundColor(FColor(255, 255, 255, 255))
		, _bEnableTransparencyHitTest(false)
		, _RefreshRate(60.0f)
	{
	}

		SLATE_ARGUMENT(FString, InitialUrl)
		SLATE_ARGUMENT(FColor, BackgroundColor)
		SLATE_ARGUMENT(bool, bEnableTransparencyHitTest)
		SLATE_ARGUMENT(float, RefreshRate)
		SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)

		SLATE_EVENT(FOnWebView2MessageReceivedNative, OnMessageReceived)
		SLATE_EVENT(FOnWebView2NavigationCompletedNative, OnNavigationCompleted)
		SLATE_EVENT(FOnWebView2NavigationStartingNative, OnNavigationStarting)
		SLATE_EVENT(FOnWebView2NewWindowRequestedNative, OnNewWindowRequested)
		SLATE_EVENT(FOnWebView2CursorChangedNative, OnCursorChanged)
		SLATE_EVENT(FOnWebView2DownloadEventNative, OnDownloadStarting)
		SLATE_EVENT(FOnWebView2DownloadEventNative, OnDownloadUpdated)
		SLATE_EVENT(FOnWebView2PrintToPdfCompletedNative, OnPrintToPdfCompleted)
		SLATE_EVENT(FOnWebView2DocumentTitleChangedNative, OnDocumentTitleChanged)
		SLATE_EVENT(FOnWebView2SourceChangedNative, OnSourceChanged)
		SLATE_EVENT(FOnWebView2CanGoBackChangedNative, OnCanGoBackChanged)
		SLATE_EVENT(FOnWebView2CanGoForwardChangedNative, OnCanGoForwardChanged)
		SLATE_EVENT(FOnWebView2InputActivationNative, OnInputActivationRequested)
		SLATE_EVENT(FOnWebView2MonitoredEventNative, OnMonitoredEvent)
		SLATE_EVENT(FOnCBWebView2WorldFrameUpdatedNative, OnFrameUpdated)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SCBWebView2World() override;

	void BeginDestroy();
	void ExecuteScript(const FString& Script, FOnCBWebView2WorldScriptCallback Callback = FOnCBWebView2WorldScriptCallback()) const;
	void LoadURL(const FString& InUrl);
	void GoForward() const;
	void GoBack() const;
	void Reload() const;
	void Stop() const;
	void OpenDevToolsWindow() const;
	void SetBackgroundColor(const FColor& InBackgroundColor);
	void SetTransparencyHitTestEnabled(bool bInTransparencyHitTestEnabled);
	void SetRefreshRate(float InRefreshRate);
	void RequestRefresh();

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	virtual void Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime) override;

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override;
	virtual void OnFocusLost(const FFocusEvent& InFocusEvent) override;
	virtual bool SupportsKeyboardFocus() const override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent) override;

private:
	void AcquireWebViewInputFocus(EFocusCause FocusCause);
	void BindWebViewEvents();
	void HandleMessageFromWeb(const FString& Message);
	bool ShouldForwardKeyboardInput() const;
	bool ShouldBlockInputForTransparentHitTest(bool bAllowCapturedInput) const;
	void ReleaseWebViewInputFocusForTransparentPassthrough(bool bAllowCapturedInput = false);
	void UpdateTransparentHitTestVisibility();
	bool ShouldKeepVisibleForTransparencyProbe() const;
	void PollTransparentHitTestHover(const FGeometry& AllottedGeometry);
	void UpdateBrowserBounds(const FGeometry& AllottedGeometry);
	void UpdateImeCaretFromBrowserClientPoint(const FVector2D& BrowserClientPoint, const FVector2D& BrowserViewportSize, float CaretHeight);
	FVector2D GetDesktopScreenPointForPointerEvent(const FPointerEvent& MouseEvent, const FVector2D& LocalPoint) const;
	void UpdateInputScreenAnchorFromPointerEvent(const FPointerEvent& MouseEvent, const FVector2D& LocalPoint);
	void TickContinuousOutput(double InCurrentTime);
	/** Suspend the browser process once this widget has gone unpainted for the configured delay (runs on a core ticker). */
	void UpdateSuspendOnInactivity();
	/** TextureAlpha hit-test mode: drive the hover/passthrough state from the captured texture's alpha at the cursor. */
	void UpdateHoverStateFromTextureAlpha(const FVector2D& LocalPoint);
	FVector2D GetLocalWebViewPoint(const FGeometry& MyGeometry, const FVector2D& ScreenSpacePosition) const;
	void ReleaseWebViewInputFocus();
	bool InitializeTextureSource(void* FocusParentWindowHandle);

	TSharedPtr<FWebView2ContinuousTextureSource> ContinuousTextureSource;
	TSharedPtr<FWebView2Window> WebViewWindow;
	TSharedPtr<SImage> PreviewImage;
	FSlateBrush PreviewBrush;
	UTexture2D* PresentedTexture = nullptr;
	TWeakPtr<SWindow> ParentWindow;
	FGuid InstanceId;
	FIntPoint BrowserPixelSize = FIntPoint::ZeroValue;
	FGeometry CachedTickGeometry;
	FVector2D LastBrowserDesktopTopLeft = FVector2D::ZeroVector;
	/**
	 * Pre-layout browser size. The browser is resized to the real layout size on the first Tick
	 * (UpdateBrowserBounds), so this only covers the brief window before geometry exists and the
	 * Draw-at-Desired-Size fallback.
	 */
	FVector2D InitialDrawSize = FVector2D(1920.0f, 1080.0f);
	FColor BackgroundColor;
	FString InitialUrl;
	float RefreshRate = 60.0f;
	bool bEnableTransparencyHitTest = false;
	/** True when passthrough decisions come from UE-side texture alpha sampling instead of the injected JS hit-test. */
	bool bUseTextureAlphaHitTest = false;
	/** Alpha (0-255) above which a sampled pixel counts as solid; copied from project settings at construction. */
	int32 TextureAlphaThreshold = 8;
	// Default to false: assume the cursor is over a transparent (non-interactive) area until the JS bridge proves otherwise.
	// A default of true would cause keyboard input to be forwarded to the WebView before any hit-test message has arrived.
	bool bIsHoveringInteractiveContent = false;
	bool bLastInsideTransparencyHitTestWasInteractive = false;
	bool bIsEditableElementFocused = false;
	bool bIsMouseButtonHeld = false;
	bool bHasCachedTickGeometry = false;
	bool bHasBrowserDesktopAnchor = false;
	bool bWasCursorInsideTransparencyHitTest = false;
	double TransparencyProbeVisibleUntilTime = 0.0;
	/**
	 * Last local-space point forwarded to the browser as a hover mouse move (real or synthetic).
	 * Lets the per-frame hover poll skip cross-process SendMouseMove + JS hit-tests while the
	 * cursor is stationary, and dedupes the poll against real OnMouseMove sends in the same frame.
	 */
	TOptional<FVector2D> LastForwardedHoverLocalPoint;
	/** Last time Slate ticked (painted) this widget; drives hidden-suspend detection. */
	double LastWidgetActiveTime = 0.0;
	bool bWebViewSuspendedForInactivity = false;
	FTSTicker::FDelegateHandle SuspendTickerHandle;

	FOnWebView2MessageReceivedNative OnMessageReceived;
	FOnWebView2NavigationCompletedNative OnNavigationCompleted;
	FOnWebView2NavigationStartingNative OnNavigationStarting;
	FOnWebView2NewWindowRequestedNative OnNewWindowRequested;
	FOnWebView2CursorChangedNative OnCursorChanged;
	FOnWebView2DownloadEventNative OnDownloadStarting;
	FOnWebView2DownloadEventNative OnDownloadUpdated;
	FOnWebView2PrintToPdfCompletedNative OnPrintToPdfCompleted;
	FOnWebView2DocumentTitleChangedNative OnDocumentTitleChanged;
	FOnWebView2SourceChangedNative OnSourceChanged;
	FOnWebView2CanGoBackChangedNative OnCanGoBackChanged;
	FOnWebView2CanGoForwardChangedNative OnCanGoForwardChanged;
	FOnWebView2InputActivationNative OnInputActivationRequested;
	FOnWebView2MonitoredEventNative OnMonitoredEvent;
	FOnCBWebView2WorldFrameUpdatedNative OnFrameUpdated;
};
