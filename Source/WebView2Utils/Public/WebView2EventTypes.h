// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WebView2EventTypes.generated.h"

/** Event types used by the unified monitoring pipeline. */
UENUM(BlueprintType)
enum class ECBWebView2MonitoredEventType : uint8
{
	NavigationStarting UMETA(DisplayName = "Navigation Starting"),
	NavigationCompleted UMETA(DisplayName = "Navigation Completed"),
	SourceChanged UMETA(DisplayName = "Source Changed"),
	DocumentTitleChanged UMETA(DisplayName = "Document Title Changed"),
	HistoryChanged UMETA(DisplayName = "History Changed"),
	WebMessageReceived UMETA(DisplayName = "Web Message Received"),
	NewWindowRequested UMETA(DisplayName = "New Window Requested"),
	DownloadStarting UMETA(DisplayName = "Download Starting"),
	DownloadUpdated UMETA(DisplayName = "Download Updated"),
	PermissionRequested UMETA(DisplayName = "Permission Requested"),
	RuntimeVersionDetected UMETA(DisplayName = "Runtime Version Detected"),
	ProcessFailed UMETA(DisplayName = "Process Failed"),
	BrowserVersionAvailable UMETA(DisplayName = "Browser Version Available")
};

UENUM(BlueprintType)
enum class ECBWebView2CursorType : uint8
{
	Default UMETA(DisplayName = "Default"),
	TextEditBeam UMETA(DisplayName = "Text Input"),
	Hand UMETA(DisplayName = "Hand"),
	ResizeLeftRight UMETA(DisplayName = "Resize Left Right"),
	ResizeUpDown UMETA(DisplayName = "Resize Up Down"),
	ResizeSouthEast UMETA(DisplayName = "Resize South East"),
	ResizeSouthWest UMETA(DisplayName = "Resize South West"),
	CardinalCross UMETA(DisplayName = "Cardinal Cross"),
	Crosshairs UMETA(DisplayName = "Crosshairs"),
	GrabHand UMETA(DisplayName = "Grab"),
	GrabHandClosed UMETA(DisplayName = "Grabbing"),
	SlashedCircle UMETA(DisplayName = "Disabled"),
	EyeDropper UMETA(DisplayName = "Eye Dropper"),
	Custom UMETA(DisplayName = "Custom")
};

/** Common payload for unified monitored events. */
USTRUCT(BlueprintType)
struct WEBVIEW2UTILS_API FCBWebView2MonitoredEvent
{
	GENERATED_BODY()

	/** Event type. */
	UPROPERTY(BlueprintReadOnly, Category = "CBWebView2|Monitoring")
	ECBWebView2MonitoredEventType EventType = ECBWebView2MonitoredEventType::WebMessageReceived;

	/** Event-related URL. */
	UPROPERTY(BlueprintReadOnly, Category = "CBWebView2|Monitoring")
	FString Url;

	/** Document title. */
	UPROPERTY(BlueprintReadOnly, Category = "CBWebView2|Monitoring")
	FString Title;

	/** Text message payload. */
	UPROPERTY(BlueprintReadOnly, Category = "CBWebView2|Monitoring")
	FString Message;

	/** Source origin for a WebMessage or event. */
	UPROPERTY(BlueprintReadOnly, Category = "CBWebView2|Monitoring")
	FString Source;

	/** WebView2 Runtime version string. */
	UPROPERTY(BlueprintReadOnly, Category = "CBWebView2|Monitoring")
	FString RuntimeVersion;

	/** Process failure type. */
	UPROPERTY(BlueprintReadOnly, Category = "CBWebView2|Monitoring")
	FString ProcessFailedKind;

	/** Process failure reason. */
	UPROPERTY(BlueprintReadOnly, Category = "CBWebView2|Monitoring")
	FString ProcessFailedReason;

	/** Failed process description. */
	UPROPERTY(BlueprintReadOnly, Category = "CBWebView2|Monitoring")
	FString ProcessDescription;

	/** Permission kind name. */
	UPROPERTY(BlueprintReadOnly, Category = "CBWebView2|Monitoring")
	FString PermissionKind;

	/** Download output path. */
	UPROPERTY(BlueprintReadOnly, Category = "CBWebView2|Monitoring")
	FString ResultFilePath;

	/** Download state string. */
	UPROPERTY(BlueprintReadOnly, Category = "CBWebView2|Monitoring")
	FString State;

	/** Number of download bytes received. */
	UPROPERTY(BlueprintReadOnly, Category = "CBWebView2|Monitoring")
	int64 BytesReceived = 0;

	/** Total number of download bytes. */
	UPROPERTY(BlueprintReadOnly, Category = "CBWebView2|Monitoring")
	int64 TotalBytesToReceive = -1;

	/** Generic success flag, commonly used for completed navigation or successful downloads. */
	UPROPERTY(BlueprintReadOnly, Category = "CBWebView2|Monitoring")
	bool bSuccess = false;

	/** Whether the browser can navigate backward. */
	UPROPERTY(BlueprintReadOnly, Category = "CBWebView2|Monitoring")
	bool bCanGoBack = false;

	/** Whether the browser can navigate forward. */
	UPROPERTY(BlueprintReadOnly, Category = "CBWebView2|Monitoring")
	bool bCanGoForward = false;

	/** Whether the permission request was initiated by a user gesture. */
	UPROPERTY(BlueprintReadOnly, Category = "CBWebView2|Monitoring")
	bool bUserInitiated = false;

	/** Whether the WebMessage was blocked by security policy. */
	UPROPERTY(BlueprintReadOnly, Category = "CBWebView2|Monitoring")
	bool bBlocked = false;

	/** WebView2 browser process ID. */
	UPROPERTY(BlueprintReadOnly, Category = "CBWebView2|Monitoring")
	int32 BrowserProcessId = 0;

	/** Process exit code. */
	UPROPERTY(BlueprintReadOnly, Category = "CBWebView2|Monitoring")
	int32 ExitCode = 0;
};

// Both native code and Blueprints reuse this monitored-event structure.
DECLARE_DELEGATE_OneParam(FOnWebView2MonitoredEventNative, const FCBWebView2MonitoredEvent&)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCBWebView2MonitoredEvent, const FCBWebView2MonitoredEvent&, EventInfo);