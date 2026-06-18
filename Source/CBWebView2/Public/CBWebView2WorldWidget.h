// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "CBWebView2BlueprintTypes.h"
#include "WebView2Settings.h"

#include "CBWebView2WorldWidget.generated.h"

class SCBWebView2World;
class UTexture2D;
struct FWebView2DownloadInfo;

/**
 * World-space WebView proxy widget for WidgetComponent-based workflows.
 *
 * Put it into a UserWidget and let a WidgetComponent present it in the scene.
 */
UCLASS()
class CBWEBVIEW2_API UCBWebView2WorldWidget : public UWidget
{
	GENERATED_BODY()

public:
	UCBWebView2WorldWidget(const FObjectInitializer& ObjectInitializer);

	/** Release internal Slate resources. */
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;
	/** Build the Slate widget that hosts the world-space WebView. */
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void SynchronizeProperties() override;

	/** Initial URL to load. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CBWebView2")
	FString InitialUrl;

	/** Default WebView background color. A value of 0 means transparent. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CBWebView2|Appearance")
	FColor BackgroundColor;

	/** Whether to inject the transparency hit-test script. When disabled, the page will no longer auto-inject transparency_check.js. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CBWebView2|Interaction")
	bool bEnableTransparencyHitTest;

	/** Refresh rate for world-space texture output. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "CBWebView2|World", meta = (ClampMin = "0.0"))
	float RefreshRate;

	/** Most recent texture pushed to the world-space material. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "CBWebView2|World")
	TObjectPtr<UTexture2D> LastRenderedTexture = nullptr;

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

	/** Change the default background color at runtime. */
	UFUNCTION(BlueprintCallable, Category = "CBWebView2|Appearance")
	void SetBackgroundColorEx(FColor InBackgroundColor);

	/** Change the world-space texture refresh rate at runtime. */
	UFUNCTION(BlueprintCallable, Category = "CBWebView2|World")
	void SetRefreshRateEx(float InRefreshRate);

	/** Explicitly request a world-space texture refresh. */
	UFUNCTION(BlueprintCallable, Category = "CBWebView2|World")
	void RequestRefresh();

private:
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
	void HandleFrameUpdated(UTexture2D* Texture);
	void HandleMonitoredEventNative(const FCBWebView2MonitoredEvent& EventInfo);

	TSharedPtr<SCBWebView2World> SlateWorldWebView;
	FString CurrentUrl;
	FString CurrentTitle;
};
