// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/AllowWindowsPlatformAtomics.h"

THIRD_PARTY_INCLUDES_START
#include <winrt/Windows.UI.Composition.h>
#include <winnt.h>
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformAtomics.h"
#include "Windows/HideWindowsPlatformTypes.h"

#include "Components/SlateWrapperTypes.h"

#include "Stdafx.h"
#include "IWebView2OutputProvider.h"
#include "IWebView2Window.h"
#include "WebView2EventTypes.h"

class FWebView2CompositionHost;
class FCoreWebView2Settings;

enum class EWebView2MouseButton : uint8
{
	Unknown,
	Left,
	Right,
	Middle,
	XButton1,
	XButton2
};

enum class EWebView2DownloadState : uint8
{
	InProgress,
	Interrupted,
	Completed
};

/** Download-state snapshot shared by native code, Slate, and Blueprints. */
struct FWebView2DownloadInfo
{
	/** Original download URL. */
	FString Uri;
	/** Resource MimeType. */
	FString MimeType;
	/** Local destination path. */
	FString ResultFilePath;
	/** Number of bytes received so far. */
	int64 BytesReceived = 0;
	/** Total number of bytes, or -1 when unknown. */
	int64 TotalBytesToReceive = -1;
	/** Current download state. */
	EWebView2DownloadState State = EWebView2DownloadState::InProgress;
};

/** Native event delegates exposed to higher layers. */
DECLARE_DELEGATE_OneParam(FOnWebView2MessageReceivedNative, const FString&)
DECLARE_DELEGATE_OneParam(FOnWebView2NavigationCompletedNative, bool)
DECLARE_DELEGATE_OneParam(FOnWebView2NavigationStartingNative, const FString&)
DECLARE_DELEGATE_OneParam(FOnWebView2NewWindowRequestedNative, const FString&)
DECLARE_DELEGATE_OneParam(FOnWebView2CursorChangedNative, EMouseCursor::Type)
DECLARE_DELEGATE_OneParam(FOnWebView2DocumentTitleChangedNative, const FString&)
DECLARE_DELEGATE_OneParam(FOnWebView2SourceChangedNative, const FString&)
DECLARE_DELEGATE_OneParam(FOnWebView2CanGoBackChangedNative, bool)
DECLARE_DELEGATE_OneParam(FOnWebView2CanGoForwardChangedNative, bool)
DECLARE_DELEGATE_OneParam(FOnWebView2DownloadEventNative, const FWebView2DownloadInfo&)
DECLARE_DELEGATE_TwoParams(FOnWebView2PrintToPdfCompletedNative, bool, const FString&)
DECLARE_DELEGATE(FOnWebView2InputActivationNative)
DECLARE_DELEGATE_ThreeParams(FOnWebView2NativeMouseButtonDoubleClick, const FVector2D&, const FVector2D&, EWebView2MouseButton)

/**
 * Native WebView2 host object.
 *
 * It wraps the Environment, Controller, CompositionController, and WebView itself,
 * and serves as the low-level core of the plugin.
 */
class WEBVIEW2UTILS_API FWebView2Window : public IWebView2Window, public TSharedFromThis<FWebView2Window>
{
public:
	FWebView2Window(HWND InParentWindow, const FGuid& InInstanceId, const FString& InInitialUrl, const FColor& InBackgroundColor, bool bInEnableTransparencyHitTest, bool bInAttachToHostVisual = true, HWND InVisualHostWindow = nullptr, bool bInAllowNonInteractiveElementPassthrough = false);
	virtual ~FWebView2Window();

	/** Create the Environment / Controller / WebView. */
	void Initialize();
	/** Close the current instance and optionally delete its user data folder. */
	bool Close(bool bCleanupUserDataFolder = false);
	/** Unified external shutdown entry point. */
	void CloseWindow();

	/** Query whether initialization has completed. */
	bool IsInitialized() const;
	/** Number of live FWebView2Window instances across all modes; lets per-frame host code early-out when no WebView exists. */
	static int32 GetActiveInstanceCount();
	/** Current layer ID used for overlap sorting. */
	int32 GetLayerId() const;
	void SetLayerId(int32 InLayerId);
	FGuid GetInstanceId() const;

	/** Common extended capabilities. */
	void ExecuteScript(const FString& Script, TFunction<void(const FString&)> Callback = nullptr);
	void OpenDevToolsWindow() const;
	void PrintToPdf(const FString& OutputPath, bool bLandscape = false);
	/** Diagnostic capture used only for Phase 0; it is not a continuous-output path. */
	void CaptureDiagnosticPreview(FOnWebView2DiagnosticPreviewReady Callback) const;
	void SetBackgroundColor(const FColor& InBackgroundColor);
	void SetContainerVisual(winrt::Windows::UI::Composition::ContainerVisual InContainerVisual);
	TSharedPtr<FWebView2CompositionHost> GetCompositionHost() const;
	TSharedPtr<IWebView2OutputProvider> GetOutputProvider() const;
	/** Set whether this instance currently participates in hit testing. */
	void SetHitTestEnabled(bool bInHitTestEnabled);
	bool IsHitTestEnabled() const;
	void SetTransparencyHitTestEnabled(bool bInTransparencyHitTestEnabled);
	void SetAllowNonInteractiveElementPassthrough(bool bInAllowNonInteractiveElementPassthrough);
	/** Return whether transparency hit-test scripting is enabled on this instance. */
	bool IsTransparencyHitTestEnabled() const;

	/** Notify WebView2 that the parent window screen position changed, primarily so IME candidate windows can be repositioned correctly. */
	void NotifyParentWindowPositionChanged();

	/**
	 * Ask the browser process to suspend the page (ICoreWebView2_3::TrySuspend) to save CPU and memory.
	 * The WebView must already be hidden (SetVisible) or the runtime rejects the request. No-op on runtimes without the API.
	 */
	void TrySuspend();
	/** Resume a suspended WebView. Called automatically when the WebView becomes visible again. */
	void ResumeIfSuspended();
	/** Toggle the browser memory usage target between low (hidden) and normal (ICoreWebView2_19). No-op on runtimes without the API. */
	void SetMemoryUsageTargetLow(bool bLowMemory);

	/**
	 * Push the Slate geometry scale into WebView2 (ICoreWebView2Controller3::RasterizationScale) so page content
	 * rasterizes at the exact on-screen pixel density. Deduped internally; call freely from per-frame code.
	 */
	void SetRasterizationScale(double InScale);

	virtual void LoadURL(const FString& InUrl) override;
	virtual void GoForward() const override;
	virtual void GoBack() const override;
	virtual void Reload() const override;
	virtual void Stop() const override;
	virtual void SetBounds(const RECT& InRect) override;
	virtual void SetBounds(const POINT& InOffset, const POINT& InSize) override;
	virtual RECT GetBounds() const override;
	virtual void SetVisible(ESlateVisibility InVisibility) override;
	virtual ESlateVisibility GetVisible() const override;
	virtual ECBWebView2DocumentState GetDocumentLoadingState() const override;
	virtual void MoveFocus(bool bFocus) override;
	virtual void SendMouseButton(const FVector2D& Point, bool bIsLeft, bool bIsDown) override;
	virtual void SendMouseDoubleClick(const FVector2D& Point, bool bIsLeft) override;
	virtual void SendMouseMove(const FVector2D& Point, bool bIsLeftButtonDown = false, bool bIsRightButtonDown = false, bool bIsMiddleButtonDown = false) override;
	virtual void SendMouseWheel(const FVector2D& Point, float Delta) override;
	virtual void SendKeyboardMessage(uint32 Msg, uint64 WParam, int64 LParam) override;

	/** Native events external code can subscribe to. */
	FOnWebView2MessageReceivedNative OnMessageReceived;
	FOnWebView2NavigationCompletedNative OnNavigationCompleted;
	FOnWebView2NavigationStartingNative OnNavigationStarting;
	FOnWebView2NewWindowRequestedNative OnNewWindowRequested;
	FOnWebView2CursorChangedNative OnCursorChanged;
	FOnWebView2DocumentTitleChangedNative OnDocumentTitleChanged;
	FOnWebView2SourceChangedNative OnSourceChanged;
	FOnWebView2CanGoBackChangedNative OnCanGoBackChanged;
	FOnWebView2CanGoForwardChangedNative OnCanGoForwardChanged;
	FOnWebView2DownloadEventNative OnDownloadStarting;
	FOnWebView2DownloadEventNative OnDownloadUpdated;
	FOnWebView2PrintToPdfCompletedNative OnPrintToPdfCompleted;
	FOnWebView2MonitoredEventNative OnMonitoredEvent;
	/** Fired when native routing decides this WebView should take input ownership. */
	FOnWebView2InputActivationNative OnInputActivationRequested;
	/** Double-click event bridged back to Slate / Blueprint after native WinComp routing consumes it. */
	FOnWebView2NativeMouseButtonDoubleClick OnNativeMouseButtonDoubleClick;

	/** Low-level COM objects exposed for composition hosts and advanced features. */
	Microsoft::WRL::ComPtr<ICoreWebView2Environment> WebViewEnvironment;
	Microsoft::WRL::ComPtr<ICoreWebView2Controller> Controller;
	Microsoft::WRL::ComPtr<ICoreWebView2CompositionController> CompositionController;
	Microsoft::WRL::ComPtr<ICoreWebView2> WebView;
	winrt::Windows::UI::Composition::ContainerVisual WebViewVisual{nullptr};

private:
	/** Controller creation helpers. */
	HRESULT CreateControllerWithOptions();

	/** WebView2 lifecycle callbacks. */
	HRESULT OnCreateEnvironmentCompleted(HRESULT Result, ICoreWebView2Environment* Environment);
	HRESULT OnCreateControllerCompleted(HRESULT Result, ICoreWebView2Controller* InController);
	HRESULT OnMessageReceivedInternal(ICoreWebView2* Sender, ICoreWebView2WebMessageReceivedEventArgs* Args);
	HRESULT OnNewWindowRequestedInternal(ICoreWebView2* Sender, ICoreWebView2NewWindowRequestedEventArgs* Args);
	HRESULT OnNavigationCompletedInternal(ICoreWebView2* Sender, ICoreWebView2NavigationCompletedEventArgs* Args);
	HRESULT OnNavigationStartingInternal(ICoreWebView2* Sender, ICoreWebView2NavigationStartingEventArgs* Args);
	HRESULT OnCursorChangedInternal(ICoreWebView2CompositionController* Sender, IUnknown* Args);
	HRESULT OnAcceleratorKeyPressedInternal(ICoreWebView2Controller* Sender, ICoreWebView2AcceleratorKeyPressedEventArgs* Args);
	HRESULT OnHistoryChangedInternal(ICoreWebView2* Sender, IUnknown* Args);
	HRESULT OnDocumentTitleChangedInternal(ICoreWebView2* Sender, IUnknown* Args);
	HRESULT OnSourceChangedInternal(ICoreWebView2* Sender, ICoreWebView2SourceChangedEventArgs* Args);
	HRESULT OnDownloadStartingInternal(ICoreWebView2* Sender, ICoreWebView2DownloadStartingEventArgs* Args);
	HRESULT OnPermissionRequestedInternal(ICoreWebView2* Sender, ICoreWebView2PermissionRequestedEventArgs* Args);
	HRESULT OnProcessFailedInternal(ICoreWebView2* Sender, ICoreWebView2ProcessFailedEventArgs* Args);
	HRESULT OnNewBrowserVersionAvailableInternal(ICoreWebView2Environment* Sender, IUnknown* Args);
	FWebView2DownloadInfo BuildDownloadInfo(ICoreWebView2DownloadOperation* DownloadOperation) const;
	FCBWebView2MonitoredEvent BuildDownloadMonitoredEvent(ECBWebView2MonitoredEventType EventType, ICoreWebView2DownloadOperation* DownloadOperation) const;
	void EmitMonitoredEvent(const FCBWebView2MonitoredEvent& EventInfo) const;
	void RegisterDownloadTracking(ICoreWebView2DownloadOperation* DownloadOperation);
	void UnregisterDownloadTracking(uint64 DownloadKey);

	void ApplyRuntimeSettings() const;
	void RegisterCoreEvents();
	void RegisterTransparencyHitTestScript(bool bExecuteImmediately);

	/** Download event registration info used later for callback removal. */
	struct FTrackedDownloadRegistration
	{
		Microsoft::WRL::ComPtr<ICoreWebView2DownloadOperation> Operation;
		EventRegistrationToken BytesReceivedChangedToken = {};
		EventRegistrationToken StateChangedToken = {};
	};

	HWND ParentWindow = nullptr;
	HWND VisualHostWindow = nullptr;
	RECT Bounds = {};
	/** True once SetBounds has applied a rect, enabling the per-frame dedup early-out. */
	bool bBoundsApplied = false;
	/** Last rasterization scale pushed to the controller; 0 means never set. */
	double AppliedRasterizationScale = 0.0;
	ESlateVisibility Visible = ESlateVisibility::Visible;
	ECBWebView2DocumentState DocumentState = ECBWebView2DocumentState::NoDocument;
	bool bInitialized = false;
	bool bCloseRequested = false;
	FGuid InstanceId;
	FString InitialUrl;
	FColor BackgroundColor;
	bool bEnableTransparencyHitTest = false;
	bool bAllowNonInteractiveElementPassthrough = false;
	bool bTransparencyHitTestScriptRegistered = false;
	bool bAttachToHostVisual = true;
	bool bHitTestEnabled = true;
	int32 LayerId = 0;
	UINT32 BrowserProcessId = 0;
	TSharedPtr<FWebView2CompositionHost> CompositionHost;
	TSharedPtr<IWebView2OutputProvider> OutputProvider;

	/** Core event tokens used for safe unbinding during Close. */
	EventRegistrationToken MessageReceivedToken = {};
	EventRegistrationToken NavigationCompletedToken = {};
	EventRegistrationToken NavigationStartingToken = {};
	EventRegistrationToken NewWindowRequestedToken = {};
	EventRegistrationToken CursorChangedToken = {};
	EventRegistrationToken AcceleratorKeyPressedToken = {};
	EventRegistrationToken HistoryChangedToken = {};
	EventRegistrationToken DocumentTitleChangedToken = {};
	EventRegistrationToken SourceChangedToken = {};
	EventRegistrationToken DownloadStartingToken = {};
	EventRegistrationToken PermissionRequestedToken = {};
	EventRegistrationToken ProcessFailedToken = {};
	EventRegistrationToken NewBrowserVersionAvailableToken = {};
	EventRegistrationToken ZoomFactorChangedToken = {};
	TMap<uint64, FTrackedDownloadRegistration> ActiveDownloads;

	/** Live instance count maintained by the constructor/destructor. */
	static FThreadSafeCounter ActiveInstanceCount;
};
