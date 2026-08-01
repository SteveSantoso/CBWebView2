// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

#include "WebView2Window.h"

class SEditableTextBox;
class SOverlay;
class UTexture2D;

DECLARE_DELEGATE_OneParam(FOnCBWebView2ScriptCallback, const FString&)
DECLARE_DELEGATE_RetVal_TwoParams(FReply, FOnCBWebView2MouseButtonDoubleClickNative, const FGeometry&, const FPointerEvent&)
DECLARE_DELEGATE_ThreeParams(FOnCBWebView2DiagnosticPreviewCapturedNative, bool, UTexture2D*, const FString&)

/**
 * Slate-layer WebView widget.
 *
 * Responsibilities:
 * 1. Create and manage the native WebView2 host inside the UE UI.
 * 2. Forward Slate input events to the underlying WebView.
 * 3. Handle address bar, control bar, and transparency hit-test UI logic.
 */
class CBWEBVIEW2_API SCBWebView2 : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCBWebView2)
		: _InitialUrl(TEXT("https://www.bing.com"))
		, _BackgroundColor(FColor(255, 255, 255, 255))
		, _bShowAddressBar(false)
		, _bShowControls(false)
		, _bShowTouchArea(false)
		, _bEnableTransparencyHitTest(true)
		, _bAllowNonInteractiveElementPassthrough(false)
		, _bShowInitialThrobber(false)
		, _ToolbarHeight(30.0f)
	{
	}

		// These arguments mirror the most commonly exposed UMG settings.
		SLATE_ARGUMENT(FString, InitialUrl)
		SLATE_ARGUMENT(FColor, BackgroundColor)
		SLATE_ARGUMENT(bool, bShowAddressBar)
		SLATE_ARGUMENT(bool, bShowControls)
		SLATE_ARGUMENT(bool, bShowTouchArea)
		SLATE_ARGUMENT(bool, bEnableTransparencyHitTest)
		SLATE_ARGUMENT(bool, bAllowNonInteractiveElementPassthrough)
		SLATE_ARGUMENT(bool, bShowInitialThrobber)
		SLATE_ARGUMENT(float, ToolbarHeight)
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
		SLATE_EVENT(FOnCBWebView2MouseButtonDoubleClickNative, OnMouseButtonDoubleClick)
	SLATE_END_ARGS()

	/** Build the Slate browser widget and create the underlying native host. */
	void Construct(const FArguments& InArgs);
	virtual ~SCBWebView2() override;

	/** Explicitly close the underlying WebView and release native resources. */
	void BeginDestroy();
	/** Execute JavaScript. */
	void ExecuteScript(const FString& Script, FOnCBWebView2ScriptCallback Callback = FOnCBWebView2ScriptCallback()) const;
	/** Navigate to a new URL. */
	void LoadURL(const FString& InUrl);
	/** Navigate forward. */
	void GoForward() const;
	/** Navigate backward. */
	void GoBack() const;
	/** Reload the current page. */
	void Reload() const;
	/** Stop loading. */
	void Stop() const;
	/** Open DevTools. */
	void OpenDevToolsWindow() const;
	/** Print the page to PDF. */
	void PrintToPdf(const FString& OutputPath, bool bLandscape = false) const;
	/** Capture request used only for Phase 0 diagnostics. */
	void CaptureDiagnosticPreview(FOnCBWebView2DiagnosticPreviewCapturedNative Callback) const;
	/** Change the default background color. */
	void SetBackgroundColor(const FColor& InBackgroundColor);
	/** Keep Slate visibility and native WebView visibility in sync. */
	void SetWebViewVisibility(ESlateVisibility InVisibility);
	void SetTransparencyHitTestEnabled(bool bInTransparencyHitTestEnabled);
	void SetAllowNonInteractiveElementPassthrough(bool bInAllowNonInteractiveElementPassthrough);
	/** Get the underlying native visibility state. */
	ESlateVisibility GetWebViewVisibility() const;

	/** Synchronize the native WebView position, size, and layer during painting. */
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	/** Provide a default desired size for the UMG / Slate layout system. */
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	/** Reserved Tick hook for future extensions. */
	virtual void Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime) override;

	/** These input handlers convert Slate events into native WebView2 input. */
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
	/** Toolbar actions and display helper functions. */
	FReply HandleBackClicked();
	FReply HandleForwardClicked();
	FReply HandleReloadClicked();
	void HandleUrlCommitted(const FText& NewText, ETextCommit::Type CommitType);
	FText GetReloadButtonText() const;
	FText GetTitleText() const;
	FText GetAddressBarUrlText() const;
	EVisibility GetLoadingIndicatorVisibility() const;
	FVector2D GetLocalWebViewPoint(const FGeometry& MyGeometry, const FVector2D& ScreenSpacePosition) const;
	bool ShouldForwardKeyboardInput() const;
	void AcquireWebViewInputFocus(EFocusCause FocusCause);
	void ReleaseWebViewInputFocus();
	FReply ExecuteMouseButtonDoubleClickEvent(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);
	void HandleNativeMouseButtonDoubleClick(const FVector2D& LocalPosition, const FVector2D& ScreenSpacePosition, EWebView2MouseButton Button);
	bool ShouldSuppressDuplicateDoubleClick(const FVector2D& ScreenSpacePosition, EWebView2MouseButton Button, double EventTimeSeconds) const;
	void RecordForwardedDoubleClick(const FVector2D& ScreenSpacePosition, EWebView2MouseButton Button, double EventTimeSeconds);

	/** Bind native WebView2 delegates to Slate and external events. */
	void BindWebViewEvents();
	/** Handle messages from the page, including transparency hit-test status messages. */
	void HandleMessageFromWeb(const FString& Message);

	/** Underlying native WebView host. */
	TSharedPtr<FWebView2Window> WebViewWindow;
	/** Optional address bar. */
	TSharedPtr<SEditableTextBox> AddressBarTextBox;
	/** Overlay container for the WebView. */
	TSharedPtr<SOverlay> Overlay;

	FString InitialUrl;
	FString CurrentUrl;
	FString CurrentTitle;
	FColor BackgroundColor;
	FGuid InstanceId;
	TWeakPtr<SWindow> ParentWindow;
	mutable FGeometry CachedPaintGeometry;
	mutable float CurrentSlateScale = 1.0f;
	FVector2D LastForwardedDoubleClickScreenPosition = FVector2D::ZeroVector;
	double LastForwardedDoubleClickTime = -1.0;

	/** Appearance and input-behavior switches. */
	bool bShowAddressBar = false;
	bool bShowControls = false;
	bool bShowTouchArea = false;
	bool bEnableTransparencyHitTest = true;
	bool bAllowNonInteractiveElementPassthrough = false;
	bool bShowInitialThrobber = false;
	float ToolbarHeight = 30.0f;
	bool bCanGoBack = false;
	bool bCanGoForward = false;
	bool bIsHoveringInteractiveContent = true;
	/** Whether an input / textarea / contenteditable element inside the page currently owns focus, reported by the injected JS bridge via EditableFocus messages. */
	bool bIsEditableElementFocused = false;
	mutable bool bHasCachedPaintGeometry = false;
	EWebView2MouseButton LastForwardedDoubleClickButton = EWebView2MouseButton::Unknown;

	/** Native delegate set forwarded to external consumers. */
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
	FOnCBWebView2MouseButtonDoubleClickNative OnMouseButtonDoubleClickEvent;
};
