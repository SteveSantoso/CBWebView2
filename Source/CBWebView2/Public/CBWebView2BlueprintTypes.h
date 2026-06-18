// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WebView2EventTypes.h"

#include "CBWebView2BlueprintTypes.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCBWebView2MessageReceived, const FString&, Message);
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnCBWebView2ScriptExecuted, const FString&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCBWebView2LoadCompleted, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCBWebView2LoadStarted, const FString&, Url);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCBWebView2NewWindowRequested, const FString&, Url);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCBWebView2DocumentTitleChanged, const FString&, Title);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCBWebView2SourceChanged, const FString&, Url);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCBWebView2CanGoBackChanged, bool, bCanGoBack);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCBWebView2CanGoForwardChanged, bool, bCanGoForward);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCBWebView2CursorChanged, ECBWebView2CursorType, CursorType);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCBWebView2InputActivationRequested);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCBWebView2PrintToPdfCompleted, bool, bSuccess, const FString&, OutputPath);

UENUM(BlueprintType)
enum class ECBWebView2DownloadState : uint8
{
	InProgress UMETA(DisplayName = "In Progress"),
	Interrupted UMETA(DisplayName = "Interrupted"),
	Completed UMETA(DisplayName = "Completed")
};

USTRUCT(BlueprintType)
struct FCBWebView2DownloadInfo
{
	GENERATED_BODY()

	/** Original download URL. */
	UPROPERTY(BlueprintReadOnly, Category = "CBWebView2|Download")
	FString Uri;

	/** MimeType returned by the server. */
	UPROPERTY(BlueprintReadOnly, Category = "CBWebView2|Download")
	FString MimeType;

	/** Local file path written to disk. */
	UPROPERTY(BlueprintReadOnly, Category = "CBWebView2|Download")
	FString ResultFilePath;

	/** Number of bytes received so far. */
	UPROPERTY(BlueprintReadOnly, Category = "CBWebView2|Download")
	int64 BytesReceived = 0;

	/** Total number of bytes expected; usually -1 when unknown. */
	UPROPERTY(BlueprintReadOnly, Category = "CBWebView2|Download")
	int64 TotalBytesToReceive = -1;

	/** Current download state. */
	UPROPERTY(BlueprintReadOnly, Category = "CBWebView2|Download")
	ECBWebView2DownloadState State = ECBWebView2DownloadState::InProgress;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCBWebView2DownloadEvent, const FCBWebView2DownloadInfo&, DownloadInfo);
