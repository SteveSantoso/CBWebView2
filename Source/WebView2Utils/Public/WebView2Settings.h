// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "InputCoreTypes.h"
#include "WebView2Settings.generated.h"

struct FPropertyChangedEvent;

/** Host modes supported by WebView2. */
UENUM(BlueprintType)
enum class ECBWebView2Mode : uint8
{
	/** Classic child-HWND embedding. Simple, but cannot blend with Slate content. */
	Windowed UMETA(DisplayName = "Windowed"),
	/** Windowless composition through Windows.UI.Composition; supports layering and transparency. */
	VisualWinComp UMETA(DisplayName = "Visual WinComp")
};

/** Default handling policy for WebView2 permission requests. */
UENUM(BlueprintType)
enum class ECBWebView2PermissionPolicy : uint8
{
	Default UMETA(DisplayName = "Use WebView2 Default"),
	Allow UMETA(DisplayName = "Allow by Default"),
	Deny UMETA(DisplayName = "Deny by Default")
};

/** Policy controlling when a WebView is allowed to own keyboard input. */
UENUM(BlueprintType)
enum class ECBWebView2KeyboardCaptureMode : uint8
{
	/** Never route keyboard input to the page. Mouse interaction remains available. */
	Never UMETA(DisplayName = "Never"),

	/** Route keys only while an input, textarea, or contenteditable element owns DOM focus. */
	TextInputOnly UMETA(DisplayName = "Text Input Only"),

	/** Route keys while editable content is focused or the pointer is over interactive page content. */
	Interactive UMETA(DisplayName = "Interactive Content"),

	/** Route keys whenever the WebView owns focus. */
	Always UMETA(DisplayName = "Always")
};

/**
 * World-space texture output path.
 *
 * Selection is fully automatic: Auto resolves to the best path for the active RHI
 * (D3D12 -> shared texture, D3D11 -> GPU copy, otherwise CPU readback) and any GPU-path failure
 * falls back to CpuReadback at runtime. The enum is exposed for diagnostics (GetActiveOutputMode),
 * not as a user setting.
 */
UENUM(BlueprintType)
enum class ECBWebView2WorldOutputMode : uint8
{
	/** Select the best GPU path for the active RHI and fall back to CPU readback if unavailable. */
	Auto UMETA(DisplayName = "Auto"),

	/** Windows Graphics Capture frame readback into a transient UTexture2D. Always available on Win64. */
	CpuReadback UMETA(DisplayName = "CPU Readback"),

	/** Copy the captured D3D11 frame into an Unreal D3D11 texture on the GPU. */
	D3D11GpuCopy UMETA(DisplayName = "D3D11 GPU Copy"),

	/** Share the captured frame with the Unreal D3D12 device through an NT-handle shared texture and a shared fence; full GPU path, no CPU readback. */
	D3D12SharedTexture UMETA(DisplayName = "D3D12 Shared Texture")
};

/** Environment-level WebView2 settings. */
USTRUCT(BlueprintType)
struct WEBVIEW2UTILS_API FCBWebView2EnvironmentOptions
{
	GENERATED_BODY()

	/** Language used by the WebView, for example en-US. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Environment")
	FString Language = TEXT("zh-CN");

	/** Whether to enable AAD / MSA single sign-on. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Environment")
	bool bEnableSingleSignOn = false;

	/** Whether the user data folder requires exclusive access. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Environment")
	bool bExclusiveUserDataFolderAccess = false;

	/** Whether to enable custom crash reporting. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Environment")
	bool bCustomCrashReporting = false;

	/** Whether tracking prevention is enabled. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Environment")
	bool bTrackingPrevention = true;

	/** Whether browser extensions are allowed. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Environment")
	bool bEnableBrowserExtensions = false;

	/**
	 * Additional browser startup arguments.
	 *
	 * Each array element represents a complete argument, for example:
	 * --allow-running-insecure-content
	 * --disable-web-security
	 *
	 * These arguments are applied only once when the WebView2 Environment is created.
	 * After changing them, restart the editor or at least destroy and recreate all WebView instances.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Environment", meta = (ConfigRestartRequired = true, TitleProperty = "{Item}"))
	TArray<FString> AdditionalBrowserArguments;
};

/** WebView2 security and message-filtering settings. */
USTRUCT(BlueprintType)
struct WEBVIEW2UTILS_API FCBWebView2SecurityOptions
{
	GENERATED_BODY()

	/** Whether to validate the source origin for webpage postMessage calls. Disabling this preserves legacy behavior. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Security")
	bool bEnableWebMessageOriginCheck = false;

	/** When origin validation fails, only log a warning instead of blocking the message, so projects can observe behavior before tightening policy. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Security")
	bool bWebMessageOriginWarnOnly = true;

	/** Origins allowed to send WebMessages to the host. Supports *, full origins, or suffix wildcards such as https://example.com and https://*.example.com. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Security", meta = (TitleProperty = "{Item}"))
	TArray<FString> AllowedMessageOrigins;

	/** Whether internal transparency-hit-test and input-focus messages should be filtered out of the public Blueprint message stream. Disabled by default for compatibility. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Security")
	bool bFilterInternalMessagesFromBlueprint = false;

	/** Emit a warning when additional browser arguments weaken security boundaries. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Security")
	bool bWarnOnDangerousBrowserArguments = true;

	/** Default handling policy for permission requests. Finer-grained per-origin or per-permission policies can be added later. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Security")
	ECBWebView2PermissionPolicy DefaultPermissionPolicy = ECBWebView2PermissionPolicy::Default;
};

/** Options used when creating the controller. */
USTRUCT(BlueprintType)
struct WEBVIEW2UTILS_API FCBWebView2ControllerOptions
{
	GENERATED_BODY()

	/** Optional profile name. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Controller")
	FString ProfileName;

	/** Whether to enable InPrivate mode. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Controller")
	bool bInPrivate = false;

	/** Optional download folder. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Controller")
	FString DownloadPath;

	/** Optional script locale. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Controller")
	FString ScriptLocale;

	/** Whether to use the operating system region directly. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Controller")
	bool bUseOSRegion = false;

	/** Whether the host can process input before forwarding it to the WebView. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Controller")
	bool bAllowHostInputProcessing = true;
};

/** Common WebView feature toggles. */
USTRUCT(BlueprintType)
struct WEBVIEW2UTILS_API FCBWebView2FeatureSettings
{
	GENERATED_BODY()

	/** Whether the page's default context menu is enabled. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Features")
	bool bEnableContextMenus = false;

	/** Whether web pages may display alert / confirm / prompt script dialogs. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Features")
	bool bEnableScriptDialogs = true;

	/** Whether opening developer tools is allowed. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Features")
	bool bEnableDevTools = true;

	/** Whether the host may inject Host Objects into the page. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Features")
	bool bAllowHostObjects = false;

	/** Whether WebView2 built-in error pages are enabled. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Features")
	bool bEnableBuiltInErrorPage = true;

	/** Whether the page may execute JavaScript. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Features")
	bool bEnableScript = true;

	/** Whether the browser status bar is visible. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Features")
	bool bEnableStatusBar = true;

	/** Whether WebMessage communication between the page and the host is enabled. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Features")
	bool bEnableWebMessage = true;

	/** Whether users may zoom page content. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Features")
	bool bEnableZoomControl = true;

	/** Whether the current WebView starts muted by default. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Features")
	bool bMuted = false;
};

/** Performance and power-saving settings. */
USTRUCT(BlueprintType)
struct WEBVIEW2UTILS_API FCBWebView2PerformanceSettings
{
	GENERATED_BODY()

	/** Suspend the browser process (ICoreWebView2_3::TrySuspend) once a world-space WebView has been hidden for SuspendDelaySeconds. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Performance")
	bool bSuspendWhenHidden = true;

	/** Seconds a world-space WebView must stay hidden / unpainted before it is suspended. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Performance", meta = (ClampMin = "0.5"))
	float SuspendDelaySeconds = 5.0f;

	/** Ask the browser to shrink its caches (ICoreWebView2_19 MemoryUsageTargetLevel) while a WebView is hidden. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Performance")
	bool bReduceMemoryWhenHidden = true;
};

/** Keyboard ownership settings shared by screen-space and world-space WebViews. */
USTRUCT(BlueprintType)
struct WEBVIEW2UTILS_API FCBWebView2InputSettings
{
	GENERATED_BODY()

	/**
	 * When the browser may consume keyboard input.
	 * Text Input Only keeps gameplay and editor shortcuts in Unreal until a page text field is actually focused.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Input")
	ECBWebView2KeyboardCaptureMode KeyboardCaptureMode = ECBWebView2KeyboardCaptureMode::TextInputOnly;

	/** Keys that are always returned to Unreal even while the browser is accepting text input. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Input")
	TArray<FKey> KeysAlwaysRoutedToUnreal = {EKeys::Escape};
};

/**
 * World-space output defaults.
 *
 * The output path and the transparency hit-test strategy are chosen automatically:
 * the best GPU path for the active RHI (with CPU-readback fallback), and texture-alpha
 * passthrough whenever the output path supports sampling and the page does not rely on
 * JS-side interactive-element semantics (bAllowNonInteractiveElementPassthrough).
 */
USTRUCT(BlueprintType)
struct WEBVIEW2UTILS_API FCBWebView2WorldOutputSettings
{
	GENERATED_BODY()

	/** Alpha value (0-255) above which a sampled pixel counts as solid when texture-alpha passthrough is active. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "World Output", meta = (ClampMin = "0", ClampMax = "255"))
	int32 TextureAlphaThreshold = 8;
};

/**
 * Plugin project settings entry point.
 *
 * All public configuration is centralized here so projects can manage it from the editor settings panel.
 */
UCLASS(Config = CBWebView2, DefaultConfig, meta = (DisplayName = "CB WebView2"))
class WEBVIEW2UTILS_API UWebView2Settings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UWebView2Settings();

	virtual FName GetCategoryName() const override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	UFUNCTION(BlueprintPure, Category = "CBWebView2")
	static const UWebView2Settings* Get();

	/** Default runtime mode used by WebView. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "General", meta = (ConfigRestartRequired = true))
	ECBWebView2Mode Mode = ECBWebView2Mode::VisualWinComp;

	/** Environment-level options. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "General", meta = (ConfigRestartRequired = true))
	FCBWebView2EnvironmentOptions Environment;

	/** Controller-level options. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "General", meta = (ConfigRestartRequired = true))
	FCBWebView2ControllerOptions Controller;

	/** Feature toggles. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "General")
	FCBWebView2FeatureSettings Features;

	/** Security and message filtering settings. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "General")
	FCBWebView2SecurityOptions Security;

	/** Performance and power-saving settings. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "General")
	FCBWebView2PerformanceSettings Performance;

	/** Keyboard focus and routing policy. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Input")
	FCBWebView2InputSettings Input;

	/** World-space output defaults. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "World")
	FCBWebView2WorldOutputSettings WorldOutput;

	/** Default background color. A value of 0 means transparent. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	FColor DefaultBackgroundColor = FColor(255, 255, 255, 255);

private:
#if WITH_EDITOR
	bool DoesChangeRequireEditorRestart(const FPropertyChangedEvent& PropertyChangedEvent) const;
#endif
};
