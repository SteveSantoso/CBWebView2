// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "CBWebView2BlueprintTypes.h"

#include "CBWebView2Widget.generated.h"

class SCBWebView2;
class UUserWidget;
class UTexture2D;
struct FWebView2DownloadInfo;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCBWebView2MouseButtonDoubleClick, FGeometry, MyGeometry, const FPointerEvent&, MouseEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnCBWebView2DiagnosticPreviewCaptured, bool, bSuccess, UTexture2D*, Texture, const FString&, ErrorMessage);

/**
 * UMG-layer WebView2 widget.
 *
 * This is the most common project-side entry point, and Blueprints usually interact with this class directly.
 */
UCLASS()
class CBWEBVIEW2_API UCBWebView2Widget : public UWidget
{
	GENERATED_BODY()

public:
	UCBWebView2Widget(const FObjectInitializer& ObjectInitializer);

	/** Release internal Slate resources. */
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	/** Build the Slate widget that actually hosts the WebView. */
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void SetVisibility(ESlateVisibility InVisibility) override;
	virtual void SynchronizeProperties() override;

	UFUNCTION(BlueprintNativeEvent, BlueprintCosmetic, Category = "Mouse")
	FEventReply OnMouseButtonDoubleClick(FGeometry MyGeometry, const FPointerEvent& MouseEvent);
	virtual FEventReply OnMouseButtonDoubleClick_Implementation(FGeometry MyGeometry, const FPointerEvent& MouseEvent);

	/** Initial URL to load. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CBWebView2")
	FString InitialUrl;

	/** Default WebView background color. A value of 0 means transparent. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CBWebView2|Appearance")
	FColor BackgroundColor;

	/** Whether to inject the transparency hit-test script. When disabled, the page will no longer auto-inject transparency_check.js. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CBWebView2|Interaction")
	bool bEnableTransparencyHitTest;

	/** Whether non-interactive painted page areas, such as plain white panels, may pass mouse input through. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CBWebView2|Interaction", meta = (EditCondition = "bEnableTransparencyHitTest"))
	bool bAllowNonInteractiveElementPassthrough;

	/** Broadcast when the page posts a message to the host. */
	UPROPERTY(BlueprintAssignable, Category = "CBWebView2|Events")
	FOnCBWebView2MessageReceived OnMessageReceived;

	/** Broadcast when navigation completes. */
	UPROPERTY(BlueprintAssignable, Category = "CBWebView2|Events")
	FOnCBWebView2LoadCompleted OnLoadCompleted;

	/** Broadcast when a new navigation starts. */
	UPROPERTY(BlueprintAssignable, Category = "CBWebView2|Events")
	FOnCBWebView2LoadStarted OnLoadStarted;

	/** Broadcast when the page requests a new window. */
	UPROPERTY(BlueprintAssignable, Category = "CBWebView2|Events")
	FOnCBWebView2NewWindowRequested OnNewWindowRequested;

	/** Broadcast when the native cursor type changes. */
	UPROPERTY(BlueprintAssignable, Category = "CBWebView2|Events")
	FOnCBWebView2CursorChanged OnCursorChanged;

	/** Broadcast when the WebView requests input focus. */
	UPROPERTY(BlueprintAssignable, Category = "CBWebView2|Events")
	FOnCBWebView2InputActivationRequested OnInputActivationRequested;

	/** Broadcast when the document title changes. */
	UPROPERTY(BlueprintAssignable, Category = "CBWebView2|Events")
	FOnCBWebView2DocumentTitleChanged OnDocumentTitleChanged;

	/** Broadcast when the current page source URL changes. */
	UPROPERTY(BlueprintAssignable, Category = "CBWebView2|Events")
	FOnCBWebView2SourceChanged OnSourceChanged;

	/** Broadcast when backward navigation availability changes. */
	UPROPERTY(BlueprintAssignable, Category = "CBWebView2|Events")
	FOnCBWebView2CanGoBackChanged OnCanGoBackChanged;

	/** Broadcast when forward navigation availability changes. */
	UPROPERTY(BlueprintAssignable, Category = "CBWebView2|Events")
	FOnCBWebView2CanGoForwardChanged OnCanGoForwardChanged;

	/** Fired once when a download is created. */
	UPROPERTY(BlueprintAssignable, Category = "CBWebView2|Events")
	FOnCBWebView2DownloadEvent OnDownloadStarting;

	/** Fired continuously when download progress or state changes. */
	UPROPERTY(BlueprintAssignable, Category = "CBWebView2|Events")
	FOnCBWebView2DownloadEvent OnDownloadUpdated;

	/** Reports the result when PrintToPdf finishes. */
	UPROPERTY(BlueprintAssignable, Category = "CBWebView2|Events")
	FOnCBWebView2PrintToPdfCompleted OnPrintToPdfCompleted;

	/** Capture callback used only for Phase 0 diagnostics. */
	UPROPERTY(BlueprintAssignable, Category = "CBWebView2|Debug")
	FOnCBWebView2DiagnosticPreviewCaptured OnDiagnosticPreviewCaptured;

	/** Double-click event bridged back from the WinComp native path. */
	UPROPERTY(BlueprintAssignable, Category = "CBWebView2|Events", meta = (DisplayName = "On Mouse Button Double Click"))
	FOnCBWebView2MouseButtonDoubleClick OnMouseButtonDoubleClickEvent;

	/** Temporary texture generated by the most recent diagnostic capture. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "CBWebView2|Debug")
	TObjectPtr<UTexture2D> LastDiagnosticPreviewTexture = nullptr;

	/** Unified monitored-event output for logging, telemetry, and debugging. */
	UPROPERTY(BlueprintAssignable, Category = "CBWebView2|Monitoring")
	FOnCBWebView2MonitoredEvent OnMonitoredEvent;

	/** Navigate to the specified URL. */
	UFUNCTION(BlueprintCallable, Category = "CBWebView2")
	void LoadURL(const FString& InUrl);

	/** Return the most recent URL reported by WebView2 navigation/source events. */
	UFUNCTION(BlueprintPure, Category = "CBWebView2")
	FString GetCurrentURL() const;

	/** Return the most recent document title reported by WebView2. */
	UFUNCTION(BlueprintPure, Category = "CBWebView2")
	FString GetCurrentTitle() const;

	/** Execute JavaScript and return the result string through a callback. */
	UFUNCTION(BlueprintCallable, Category = "CBWebView2")
	void ExecuteScript(const FString& Script, FOnCBWebView2ScriptExecuted Callback);

	/** Navigate forward in browser history. */
	UFUNCTION(BlueprintCallable, Category = "CBWebView2")
	void GoForward();

	/** Navigate backward in browser history. */
	UFUNCTION(BlueprintCallable, Category = "CBWebView2")
	void GoBack();

	/** Reload the current page. */
	UFUNCTION(BlueprintCallable, Category = "CBWebView2")
	void Reload();

	/** Stop the current navigation. */
	UFUNCTION(BlueprintCallable, Category = "CBWebView2")
	void StopLoading();

	/** Open the browser DevTools window. */
	UFUNCTION(BlueprintCallable, Category = "CBWebView2|Debug")
	void OpenDevToolsWindow();

	/** Export the current page as PDF. */
	UFUNCTION(BlueprintCallable, Category = "CBWebView2|Printing")
	void PrintToPdf(const FString& OutputPath, bool bLandscape = false);

	/** Diagnostic capture request used only for Phase 0 validation. */
	UFUNCTION(BlueprintCallable, Category = "CBWebView2|Debug")
	void CaptureDiagnosticPreview();

	/** Change the default background color at runtime. */
	UFUNCTION(BlueprintCallable, Category = "CBWebView2|Appearance")
	void SetBackgroundColorEx(FColor InBackgroundColor);

	/** Keep UMG and the native WebView visibility in sync. */
	UFUNCTION(BlueprintCallable, Category = "CBWebView2|Appearance")
	void SetWebViewVisibility(ESlateVisibility InVisibility);

private:
	void BindOwnerVisibilityChanged();
	void UnbindOwnerVisibilityChanged();
	void ApplyWebViewVisibility();
	ESlateVisibility GetEffectiveWebViewVisibility() const;

	UFUNCTION()
	void HandleOwnerVisibilityChanged(ESlateVisibility InVisibility);

	FReply HandleSlateMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);
	/** These handlers bridge native / Slate events into Blueprint dynamic delegates. */
	void HandleMessageReceived(const FString& Message);
	void HandleNavigationCompleted(bool bSuccess);
	void HandleNavigationStarted(const FString& Url);
	void HandleNewWindowRequested(const FString& Url);
	void HandleCursorChangedNative(EMouseCursor::Type CursorType);
	void HandleInputActivationRequestedNative();
	void HandleDocumentTitleChangedNative(const FString& Title);
	void HandleSourceChangedNative(const FString& Url);
	void HandleCanGoBackChangedNative(bool bCanGoBackValue);
	void HandleCanGoForwardChangedNative(bool bCanGoForwardValue);
	void HandleDownloadStartingNative(const FWebView2DownloadInfo& DownloadInfo);
	void HandleDownloadUpdatedNative(const FWebView2DownloadInfo& DownloadInfo);
	void HandlePrintToPdfCompletedNative(bool bSuccess, const FString& OutputPath);
	void HandleDiagnosticPreviewCaptured(bool bSuccess, UTexture2D* Texture, const FString& ErrorMessage);
	void HandleMonitoredEventNative(const FCBWebView2MonitoredEvent& EventInfo);

	TWeakObjectPtr<UUserWidget> BoundVisibilityOwner;
	ESlateVisibility OwnWidgetVisibility = ESlateVisibility::Visible;
	FString CurrentUrl;
	FString CurrentTitle;
	/** Actual Slate widget instance wrapped by the UMG control. */
	TSharedPtr<SCBWebView2> SlateWebView;
};
