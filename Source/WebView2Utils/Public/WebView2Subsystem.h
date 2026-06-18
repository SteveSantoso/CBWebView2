// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "Tickable.h"
#include "WebView2EventTypes.h"

#include "WebView2Subsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWebView2SubsystemMessage, const FString&, Message);
DECLARE_DELEGATE_OneParam(FOnWebView2SubsystemMessageNative, const FString&);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWebView2SubsystemMonitorEvent, const FCBWebView2MonitoredEvent&, EventInfo);
DECLARE_DELEGATE_OneParam(FOnWebView2SubsystemMonitorEventNative, const FCBWebView2MonitoredEvent&);

/**
 * Engine-level WebView2 subsystem.
 *
 * This layer registers the Windows message handler and tracks whether a WebView currently owns input focus.
 */
UCLASS()
class WEBVIEW2UTILS_API UWebView2Subsystem : public UEngineSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	/** Register the manager, message handler, and editor lifecycle callbacks. */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	/** Unregister the message handler and shut down native hosts. */
	virtual void Deinitialize() override;

	/** Handle host-window focus recovery. */
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

	/** Get the singleton subsystem instance. */
	static UWebView2Subsystem* Get();

	/** Record whether a WebView currently owns real input focus. */
	void SetWebViewFocused(bool bInFocused);
	/** Query the current focus state. */
	bool IsWebViewFocused() const;

	UPROPERTY(BlueprintAssignable, Category = "CBWebView2")
	FOnWebView2SubsystemMessage OnWebMessageReceived;

	UPROPERTY(BlueprintAssignable, Category = "CBWebView2")
	FOnWebView2SubsystemMonitorEvent OnMonitoredEvent;

	FOnWebView2SubsystemMessageNative OnWebMessageReceivedNative;
	FOnWebView2SubsystemMonitorEventNative OnMonitoredEventNative;

private:
	/** Clean up native resources associated with host windows when PIE ends. */
	void OnEndPIE(bool bIsSimulating);

	/** Tracks real input focus only; it does not represent transparency hit-test state. */
	bool bIsWebViewFocused = false;
};