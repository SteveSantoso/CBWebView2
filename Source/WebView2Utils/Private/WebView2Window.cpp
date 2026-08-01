// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#include "WebView2Window.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"

#include "WebView2CompositionHost.h"
#include "WebView2CapturePreviewOutputProvider.h"
#include "WebView2Log.h"
#include "WebView2Manager.h"
#include "WebView2Settings.h"
#include "WebView2Subsystem.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <DispatcherQueue.h>
#include <WinUser.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include "Windows/HideWindowsPlatformTypes.h"

namespace
{
	// Take ownership of COM-allocated strings, convert them to FString, and release the original memory immediately.
	FString TakeCoTaskMemString(LPWSTR RawString)
	{
		const FString Result = RawString ? FString(RawString) : FString();
		if (RawString)
		{
			::CoTaskMemFree(RawString);
		}
		return Result;
	}

	FString GetWebMessagePayload(ICoreWebView2WebMessageReceivedEventArgs* Args)
	{
		LPWSTR RawJsonMessage = nullptr;
		if (SUCCEEDED(Args->get_WebMessageAsJson(&RawJsonMessage)) && RawJsonMessage)
		{
			const FString JsonMessage = TakeCoTaskMemString(RawJsonMessage);

			TSharedPtr<FJsonValue> JsonValue;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonMessage);
			if (FJsonSerializer::Deserialize(Reader, JsonValue) && JsonValue.IsValid() && JsonValue->Type == EJson::String)
			{
				return JsonValue->AsString();
			}

			return JsonMessage;
		}

		LPWSTR RawStringMessage = nullptr;
		if (SUCCEEDED(Args->TryGetWebMessageAsString(&RawStringMessage)) && RawStringMessage)
		{
			return TakeCoTaskMemString(RawStringMessage);
		}

		return FString();
	}

	EWebView2DownloadState ToDownloadState(const COREWEBVIEW2_DOWNLOAD_STATE InState)
	{
		// Convert SDK download state to the plugin's internal enum for reuse across layers.
		switch (InState)
		{
		case COREWEBVIEW2_DOWNLOAD_STATE_INTERRUPTED:
			return EWebView2DownloadState::Interrupted;
		case COREWEBVIEW2_DOWNLOAD_STATE_COMPLETED:
			return EWebView2DownloadState::Completed;
		default:
			return EWebView2DownloadState::InProgress;
		}
	}

	FString DownloadStateToString(const EWebView2DownloadState InState)
	{
		switch (InState)
		{
		case EWebView2DownloadState::Interrupted:
			return TEXT("Interrupted");
		case EWebView2DownloadState::Completed:
			return TEXT("Completed");
		default:
			return TEXT("InProgress");
		}
	}

	FString PermissionKindToString(const COREWEBVIEW2_PERMISSION_KIND InPermissionKind)
	{
		switch (InPermissionKind)
		{
		case COREWEBVIEW2_PERMISSION_KIND_MICROPHONE:
			return TEXT("Microphone");
		case COREWEBVIEW2_PERMISSION_KIND_CAMERA:
			return TEXT("Camera");
		case COREWEBVIEW2_PERMISSION_KIND_GEOLOCATION:
			return TEXT("Geolocation");
		case COREWEBVIEW2_PERMISSION_KIND_NOTIFICATIONS:
			return TEXT("Notifications");
		case COREWEBVIEW2_PERMISSION_KIND_OTHER_SENSORS:
			return TEXT("OtherSensors");
		case COREWEBVIEW2_PERMISSION_KIND_CLIPBOARD_READ:
			return TEXT("ClipboardRead");
		case COREWEBVIEW2_PERMISSION_KIND_MULTIPLE_AUTOMATIC_DOWNLOADS:
			return TEXT("MultipleAutomaticDownloads");
		case COREWEBVIEW2_PERMISSION_KIND_FILE_READ_WRITE:
			return TEXT("FileReadWrite");
		case COREWEBVIEW2_PERMISSION_KIND_AUTOPLAY:
			return TEXT("Autoplay");
		case COREWEBVIEW2_PERMISSION_KIND_LOCAL_FONTS:
			return TEXT("LocalFonts");
		case COREWEBVIEW2_PERMISSION_KIND_MIDI_SYSTEM_EXCLUSIVE_MESSAGES:
			return TEXT("MidiSystemExclusiveMessages");
		case COREWEBVIEW2_PERMISSION_KIND_WINDOW_MANAGEMENT:
			return TEXT("WindowManagement");
		default:
			return TEXT("Unknown");
		}
	}

	FString ProcessFailedKindToString(const COREWEBVIEW2_PROCESS_FAILED_KIND InKind)
	{
		switch (InKind)
		{
		case COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED:
			return TEXT("BrowserProcessExited");
		case COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED:
			return TEXT("RenderProcessExited");
		case COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_UNRESPONSIVE:
			return TEXT("RenderProcessUnresponsive");
		case COREWEBVIEW2_PROCESS_FAILED_KIND_FRAME_RENDER_PROCESS_EXITED:
			return TEXT("FrameRenderProcessExited");
		case COREWEBVIEW2_PROCESS_FAILED_KIND_UTILITY_PROCESS_EXITED:
			return TEXT("UtilityProcessExited");
		case COREWEBVIEW2_PROCESS_FAILED_KIND_SANDBOX_HELPER_PROCESS_EXITED:
			return TEXT("SandboxHelperProcessExited");
		case COREWEBVIEW2_PROCESS_FAILED_KIND_GPU_PROCESS_EXITED:
			return TEXT("GpuProcessExited");
		case COREWEBVIEW2_PROCESS_FAILED_KIND_PPAPI_PLUGIN_PROCESS_EXITED:
			return TEXT("PpapiPluginProcessExited");
		case COREWEBVIEW2_PROCESS_FAILED_KIND_PPAPI_BROKER_PROCESS_EXITED:
			return TEXT("PpapiBrokerProcessExited");
		case COREWEBVIEW2_PROCESS_FAILED_KIND_UNKNOWN_PROCESS_EXITED:
		default:
			return TEXT("UnknownProcessExited");
		}
	}

	FString ProcessFailedReasonToString(const COREWEBVIEW2_PROCESS_FAILED_REASON InReason)
	{
		switch (InReason)
		{
		case COREWEBVIEW2_PROCESS_FAILED_REASON_UNRESPONSIVE:
			return TEXT("Unresponsive");
		case COREWEBVIEW2_PROCESS_FAILED_REASON_TERMINATED:
			return TEXT("Terminated");
		case COREWEBVIEW2_PROCESS_FAILED_REASON_CRASHED:
			return TEXT("Crashed");
		case COREWEBVIEW2_PROCESS_FAILED_REASON_LAUNCH_FAILED:
			return TEXT("LaunchFailed");
		case COREWEBVIEW2_PROCESS_FAILED_REASON_OUT_OF_MEMORY:
			return TEXT("OutOfMemory");
		case COREWEBVIEW2_PROCESS_FAILED_REASON_PROFILE_DELETED:
			return TEXT("ProfileDeleted");
		case COREWEBVIEW2_PROCESS_FAILED_REASON_UNEXPECTED:
		default:
			return TEXT("Unexpected");
		}
	}

	bool IsDangerousBrowserArgument(const FString& Argument)
	{
		const FString LowerArgument = Argument.ToLower();
		return LowerArgument.Contains(TEXT("--disable-web-security")) ||
			LowerArgument.Contains(TEXT("--allow-running-insecure-content")) ||
			LowerArgument.Contains(TEXT("--disable-site-isolation-trials")) ||
			LowerArgument.Contains(TEXT("--disable-features=siteperprocess")) ||
			LowerArgument.Contains(TEXT("--ignore-certificate-errors"));
	}

	FString NormalizeOriginForComparison(FString Origin)
	{
		Origin.TrimStartAndEndInline();
		while (Origin.EndsWith(TEXT("/")))
		{
			Origin.LeftChopInline(1, EAllowShrinking::No);
		}
		return Origin.ToLower();
	}

	FString ExtractOrigin(const FString& Source)
	{
		const FString TrimmedSource = Source.TrimStartAndEnd();
		const int32 SchemeSeparatorIndex = TrimmedSource.Find(TEXT("://"));
		if (SchemeSeparatorIndex == INDEX_NONE)
		{
			return NormalizeOriginForComparison(TrimmedSource);
		}

		const int32 AuthorityStart = SchemeSeparatorIndex + 3;
		int32 AuthorityEnd = TrimmedSource.Len();
		const TCHAR Separators[] = {TEXT('/'), TEXT('?'), TEXT('#')};
		for (const TCHAR Separator : Separators)
		{
			const int32 SeparatorIndex = TrimmedSource.Find(FString::Chr(Separator), ESearchCase::CaseSensitive, ESearchDir::FromStart, AuthorityStart);
			if (SeparatorIndex != INDEX_NONE)
			{
				AuthorityEnd = FMath::Min(AuthorityEnd, SeparatorIndex);
			}
		}

		return NormalizeOriginForComparison(TrimmedSource.Left(AuthorityEnd));
	}

	bool DoesOriginMatchPattern(const FString& Origin, const FString& Pattern)
	{
		const FString NormalizedOrigin = NormalizeOriginForComparison(Origin);
		const FString NormalizedPattern = NormalizeOriginForComparison(Pattern);
		if (NormalizedPattern.IsEmpty())
		{
			return false;
		}

		if (NormalizedPattern == TEXT("*"))
		{
			return true;
		}

		if (!NormalizedPattern.Contains(TEXT("*")))
		{
			return NormalizedOrigin == NormalizedPattern;
		}

		FString Prefix;
		FString Suffix;
		NormalizedPattern.Split(TEXT("*"), &Prefix, &Suffix, ESearchCase::CaseSensitive, ESearchDir::FromStart);
		return NormalizedOrigin.StartsWith(Prefix) && NormalizedOrigin.EndsWith(Suffix);
	}

	bool IsWebMessageOriginAllowed(const FString& Source, const UWebView2Settings* Settings)
	{
		if (!Settings || !Settings->Security.bEnableWebMessageOriginCheck)
		{
			return true;
		}

		const FString Origin = ExtractOrigin(Source);
		for (const FString& AllowedOrigin : Settings->Security.AllowedMessageOrigins)
		{
			if (DoesOriginMatchPattern(Origin, AllowedOrigin))
			{
				return true;
			}
		}

		return false;
	}

	bool IsCBWebView2InternalMessage(const FString& Message)
	{
		return Message.Contains(TEXT("\"__cbwebview2\"")) ||
			Message.Contains(TEXT("__cbwebview2")) ||
			Message.StartsWith(TEXT("IsClickable:")) ||
			Message.Contains(TEXT("IsClickable:")) ||
			Message.StartsWith(TEXT("EditableFocus:")) ||
			Message.Contains(TEXT("EditableFocus:"));
	}

	bool IsCBWebView2InternalOnlyMessage(const FString& Message)
	{
		return Message.Contains(TEXT("__cbwebview2")) && Message.Contains(TEXT("imeCaret"));
	}

	COREWEBVIEW2_PERMISSION_STATE ToCorePermissionState(const ECBWebView2PermissionPolicy Policy)
	{
		switch (Policy)
		{
		case ECBWebView2PermissionPolicy::Allow:
			return COREWEBVIEW2_PERMISSION_STATE_ALLOW;
		case ECBWebView2PermissionPolicy::Deny:
			return COREWEBVIEW2_PERMISSION_STATE_DENY;
		case ECBWebView2PermissionPolicy::Default:
		default:
			return COREWEBVIEW2_PERMISSION_STATE_DEFAULT;
		}
	}

	FString PermissionStateToString(const COREWEBVIEW2_PERMISSION_STATE State)
	{
		switch (State)
		{
		case COREWEBVIEW2_PERMISSION_STATE_ALLOW:
			return TEXT("Allow");
		case COREWEBVIEW2_PERMISSION_STATE_DENY:
			return TEXT("Deny");
		case COREWEBVIEW2_PERMISSION_STATE_DEFAULT:
		default:
			return TEXT("Default");
		}
	}

	winrt::hstring BuildBrowserArguments(const UWebView2Settings* Settings)
	{
		FString Arguments = TEXT("--enable-features=ThirdPartyStoragePartitioning,PartitionedCookies");
		if (Settings)
		{
			for (const FString& Entry : Settings->Environment.AdditionalBrowserArguments)
			{
				const FString TrimmedEntry = Entry.TrimStartAndEnd();
				if (!TrimmedEntry.IsEmpty())
				{
					if (Settings->Security.bWarnOnDangerousBrowserArguments && IsDangerousBrowserArgument(TrimmedEntry))
					{
						UE_LOG(LogWebView2Utils, Warning, TEXT("Detected a browser argument that may reduce WebView2 security: %s"), *TrimmedEntry);
					}

					Arguments += TEXT(" ");
					Arguments += TrimmedEntry;
				}
			}
		}

		Arguments.TrimStartAndEndInline();
		return winrt::hstring(*Arguments);
	}

	COREWEBVIEW2_COLOR ToCoreColor(const FColor& InColor)
	{
		return COREWEBVIEW2_COLOR{InColor.A, InColor.R, InColor.G, InColor.B};
	}

	POINT ToMousePoint(const FVector2D& Point)
	{
		return POINT{
			static_cast<LONG>(FMath::RoundToInt(Point.X)),
			static_cast<LONG>(FMath::RoundToInt(Point.Y))};
	}

	bool TryGetMouseButtonEventKind(const EWebView2MouseButton Button, const bool bIsDown, COREWEBVIEW2_MOUSE_EVENT_KIND& OutKind)
	{
		switch (Button)
		{
		case EWebView2MouseButton::Left:
			OutKind = bIsDown ? COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOWN : COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_UP;
			return true;
		case EWebView2MouseButton::Right:
			OutKind = bIsDown ? COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_DOWN : COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_UP;
			return true;
		case EWebView2MouseButton::Middle:
			OutKind = bIsDown ? COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_DOWN : COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_UP;
			return true;
		case EWebView2MouseButton::XButton1:
		case EWebView2MouseButton::XButton2:
			OutKind = bIsDown ? COREWEBVIEW2_MOUSE_EVENT_KIND_X_BUTTON_DOWN : COREWEBVIEW2_MOUSE_EVENT_KIND_X_BUTTON_UP;
			return true;
		default:
			return false;
		}
	}

	COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS GetMouseButtonVirtualKey(const EWebView2MouseButton Button)
	{
		switch (Button)
		{
		case EWebView2MouseButton::Left:
			return COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_LEFT_BUTTON;
		case EWebView2MouseButton::Right:
			return COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_RIGHT_BUTTON;
		case EWebView2MouseButton::Middle:
			return COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_MIDDLE_BUTTON;
		case EWebView2MouseButton::XButton1:
			return COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_X_BUTTON1;
		case EWebView2MouseButton::XButton2:
			return COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_X_BUTTON2;
		default:
			return COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE;
		}
	}

	UINT32 GetMouseButtonData(const EWebView2MouseButton Button)
	{
		switch (Button)
		{
		case EWebView2MouseButton::XButton1:
			return XBUTTON1;
		case EWebView2MouseButton::XButton2:
			return XBUTTON2;
		default:
			return 0;
		}
	}

	FString LoadPluginResourceScript(const TCHAR* RelativePath)
	{
		struct FCachedResourceScript
		{
			FDateTime Timestamp;
			FString Content;
		};

		static TMap<FString, FCachedResourceScript> ScriptCache;

		const FString CacheKey(RelativePath);

		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("CBWebView2"));
		if (!Plugin.IsValid())
		{
			return FString();
		}

		const FString ScriptPath = FPaths::Combine(Plugin->GetBaseDir(), RelativePath);
		const FDateTime ScriptTimestamp = IFileManager::Get().GetTimeStamp(*ScriptPath);
		if (const FCachedResourceScript* CachedScript = ScriptCache.Find(CacheKey))
		{
			if (CachedScript->Timestamp == ScriptTimestamp)
			{
				return CachedScript->Content;
			}
		}

		FString ScriptContent;
		if (!FFileHelper::LoadFileToString(ScriptContent, *ScriptPath))
		{
			UE_LOG(LogWebView2Utils, Warning, TEXT("Failed to load WebView2 resource script: %s"), *ScriptPath);
			ScriptCache.Remove(CacheKey);
			return FString();
		}

		ScriptCache.Add(CacheKey, FCachedResourceScript{ScriptTimestamp, ScriptContent});
		return ScriptContent;
	}

	FString LoadTransparencyCheckScript(bool bAllowNonInteractiveElementPassthrough)
	{
		const FString ScriptContent = LoadPluginResourceScript(TEXT("Resources/transparency_check.js"));
		if (ScriptContent.IsEmpty())
		{
			return FString();
		}

		return FString::Printf(
			TEXT("window.__cbwebview2TransparencyAllowNonInteractivePassthrough = %s; %s"),
			bAllowNonInteractiveElementPassthrough ? TEXT("true") : TEXT("false"),
			*ScriptContent);
	}

	FString BuildInputEventBridgeScript()
	{
		return LoadPluginResourceScript(TEXT("Resources/input_event_bridge.js"));
	}
}

FWebView2Window::FWebView2Window(
	HWND InParentWindow,
	const FGuid& InInstanceId,
	const FString& InInitialUrl,
	const FColor& InBackgroundColor,
	bool bInEnableTransparencyHitTest,
	bool bInAttachToHostVisual,
	HWND InVisualHostWindow,
	bool bInAllowNonInteractiveElementPassthrough)
	: ParentWindow(InParentWindow)
	, VisualHostWindow(InVisualHostWindow ? InVisualHostWindow : InParentWindow)
	, InstanceId(InInstanceId)
	, InitialUrl(InInitialUrl)
	, BackgroundColor(InBackgroundColor)
	, bEnableTransparencyHitTest(bInEnableTransparencyHitTest)
	, bAllowNonInteractiveElementPassthrough(bInAllowNonInteractiveElementPassthrough)
	, bAttachToHostVisual(bInAttachToHostVisual)
{
	ActiveInstanceCount.Increment();
}

FWebView2Window::~FWebView2Window()
{
	Close();
	ActiveInstanceCount.Decrement();
}

FThreadSafeCounter FWebView2Window::ActiveInstanceCount;

int32 FWebView2Window::GetActiveInstanceCount()
{
	return ActiveInstanceCount.GetValue();
}

void FWebView2Window::Initialize()
{
	// Reset runtime state on every initialization so recreated instances do not keep stale state.
	bCloseRequested = false;
	bInitialized = false;
	Visible = ESlateVisibility::Visible;
	bHitTestEnabled = true;
	DocumentState = ECBWebView2DocumentState::NoDocument;
	Bounds = {};

	const UWebView2Settings* Settings = UWebView2Settings::Get();

	auto Options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
	// Startup arguments are applied only once when the Environment is created.
	const winrt::hstring BrowserArguments = BuildBrowserArguments(Settings);
	Options->put_AdditionalBrowserArguments(BrowserArguments.c_str());

	if (Settings)
	{
		Options->put_AllowSingleSignOnUsingOSPrimaryAccount(Settings->Environment.bEnableSingleSignOn);
		Options->put_ExclusiveUserDataFolderAccess(Settings->Environment.bExclusiveUserDataFolderAccess);
		Options->put_IsCustomCrashReportingEnabled(Settings->Environment.bCustomCrashReporting);

		if (!Settings->Environment.Language.IsEmpty())
		{
			Options->put_Language(*Settings->Environment.Language);
		}

		Microsoft::WRL::ComPtr<ICoreWebView2EnvironmentOptions5> Options5;
		if (SUCCEEDED(Options.As(&Options5)))
		{
			Options5->put_EnableTrackingPrevention(Settings->Environment.bTrackingPrevention);
		}

		Microsoft::WRL::ComPtr<ICoreWebView2EnvironmentOptions6> Options6;
		if (SUCCEEDED(Options.As(&Options6)))
		{
			Options6->put_AreBrowserExtensionsEnabled(Settings->Environment.bEnableBrowserExtensions);
		}

		Microsoft::WRL::ComPtr<ICoreWebView2EnvironmentOptions8> Options8;
		if (SUCCEEDED(Options.As(&Options8)))
		{
			Options8->put_ScrollBarStyle(COREWEBVIEW2_SCROLLBAR_STYLE_FLUENT_OVERLAY);
		}
	}

	FString UserDataFolder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("CBWebView2/UserData"));
	// Keep the user data directory under Saved to avoid polluting the project root.
	FPaths::NormalizeDirectoryName(UserDataFolder);
	IFileManager::Get().MakeDirectory(*UserDataFolder, true);

	const TWeakPtr<FWebView2Window> WeakWindow = AsShared();
	const HRESULT Hr = CreateCoreWebView2EnvironmentWithOptions(
		nullptr,
		*UserDataFolder,
		Options.Get(),
		Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
			[WeakWindow](HRESULT Result, ICoreWebView2Environment* Environment) -> HRESULT
			{
				if (TSharedPtr<FWebView2Window> Window = WeakWindow.Pin())
				{
					return Window->OnCreateEnvironmentCompleted(Result, Environment);
				}

				return S_OK;
			})
			.Get());

	if (FAILED(Hr))
	{
		UE_LOG(LogWebView2Utils, Error, TEXT("Failed to create the WebView2 Environment. HRESULT=0x%08x"), Hr);
	}
}

bool FWebView2Window::Close(bool bCleanupUserDataFolder)
{
	if (bCloseRequested)
	{
		return true;
	}

	// Mark the instance as closing first, then unbind events, detach visuals, and release COM objects in reverse order.
	bCloseRequested = true;

	if (CompositionHost.IsValid())
	{
		CompositionHost->DetachWebView(InstanceId, WebViewVisual);
		CompositionHost.Reset();
	}

	if (Controller)
	{
		if (WebView)
		{
			WebView->remove_WebMessageReceived(MessageReceivedToken);
			WebView->remove_NavigationCompleted(NavigationCompletedToken);
			WebView->remove_NavigationStarting(NavigationStartingToken);
			WebView->remove_HistoryChanged(HistoryChangedToken);
			WebView->remove_DocumentTitleChanged(DocumentTitleChangedToken);
			WebView->remove_SourceChanged(SourceChangedToken);
			if (Microsoft::WRL::ComPtr<ICoreWebView2_4> WebView4; SUCCEEDED(WebView.As(&WebView4)) && WebView4)
			{
				WebView4->remove_DownloadStarting(DownloadStartingToken);
			}
			WebView->remove_PermissionRequested(PermissionRequestedToken);
			WebView->remove_ProcessFailed(ProcessFailedToken);
		}

		if (CompositionController)
		{
			CompositionController->put_RootVisualTarget(nullptr);
			CompositionController->remove_CursorChanged(CursorChangedToken);
		}

		Controller->remove_AcceleratorKeyPressed(AcceleratorKeyPressedToken);
		Controller->Close();
	}

	if (WebViewEnvironment)
	{
		WebViewEnvironment->remove_NewBrowserVersionAvailable(NewBrowserVersionAvailableToken);
	}

	WebView = nullptr;
	CompositionController = nullptr;
	Controller = nullptr;
	WebViewEnvironment = nullptr;
	WebViewVisual = nullptr;
	for (const TPair<uint64, FTrackedDownloadRegistration>& Pair : ActiveDownloads)
	{
		if (Pair.Value.Operation)
		{
			Pair.Value.Operation->remove_BytesReceivedChanged(Pair.Value.BytesReceivedChangedToken);
			Pair.Value.Operation->remove_StateChanged(Pair.Value.StateChangedToken);
		}
	}
	ActiveDownloads.Empty();
	if (OutputProvider.IsValid())
	{
		OutputProvider->InvalidateOutput();
		OutputProvider.Reset();
	}
	bInitialized = false;
	return true;
}

void FWebView2Window::CloseWindow()
{
	Close();
}

bool FWebView2Window::IsInitialized() const
{
	return bInitialized;
}

int32 FWebView2Window::GetLayerId() const
{
	return LayerId;
}

void FWebView2Window::SetLayerId(int32 InLayerId)
{
	LayerId = InLayerId;
}

FGuid FWebView2Window::GetInstanceId() const
{
	return InstanceId;
}

void FWebView2Window::ExecuteScript(const FString& Script, TFunction<void(const FString&)> Callback)
{
	if (!WebView)
	{
		return;
	}

	WebView->ExecuteScript(
		*Script,
		Microsoft::WRL::Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
			[Callback](HRESULT ErrorCode, LPCWSTR ResultString) -> HRESULT
			{
				if (SUCCEEDED(ErrorCode) && ResultString && Callback)
				{
					Callback(FString(ResultString));
				}
				return S_OK;
			})
			.Get());
}

void FWebView2Window::OpenDevToolsWindow() const
{
	if (WebView)
	{
		WebView->OpenDevToolsWindow();
	}
}

void FWebView2Window::PrintToPdf(const FString& OutputPath, bool bLandscape)
{
	if (!WebView || OutputPath.IsEmpty())
	{
		OnPrintToPdfCompleted.ExecuteIfBound(false, OutputPath);
		return;
	}

	Microsoft::WRL::ComPtr<ICoreWebView2_7> WebView7;
	if (FAILED(WebView.As(&WebView7)) || !WebView7)
	{
		UE_LOG(LogWebView2Utils, Warning, TEXT("The current WebView2 Runtime does not support the PrintToPdf API."));
		OnPrintToPdfCompleted.ExecuteIfBound(false, OutputPath);
		return;
	}

	const FString AbsoluteOutputPath = FPaths::ConvertRelativePathToFull(OutputPath);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(AbsoluteOutputPath), true);

	Microsoft::WRL::ComPtr<ICoreWebView2PrintSettings> PrintSettings;
	if (bLandscape)
	{
		Microsoft::WRL::ComPtr<ICoreWebView2Environment6> Environment6;
		if (SUCCEEDED(WebViewEnvironment.As(&Environment6)) && Environment6)
		{
			if (SUCCEEDED(Environment6->CreatePrintSettings(&PrintSettings)) && PrintSettings)
			{
				PrintSettings->put_Orientation(COREWEBVIEW2_PRINT_ORIENTATION_LANDSCAPE);
			}
		}
	}

	WebView7->PrintToPdf(
		*AbsoluteOutputPath,
		PrintSettings.Get(),
		Microsoft::WRL::Callback<ICoreWebView2PrintToPdfCompletedHandler>(
			[this, AbsoluteOutputPath](HRESULT ErrorCode, BOOL bSuccess) -> HRESULT
			{
				if (FAILED(ErrorCode))
				{
					UE_LOG(LogWebView2Utils, Error, TEXT("PrintToPdf failed. HRESULT=0x%08x, path=%s"), ErrorCode, *AbsoluteOutputPath);
					OnPrintToPdfCompleted.ExecuteIfBound(false, AbsoluteOutputPath);
					return S_OK;
				}

				OnPrintToPdfCompleted.ExecuteIfBound(!!bSuccess, AbsoluteOutputPath);
				return S_OK;
			})
			.Get());
}

void FWebView2Window::CaptureDiagnosticPreview(FOnWebView2DiagnosticPreviewReady Callback) const
{
	if (!OutputProvider.IsValid())
	{
		if (Callback)
		{
			Callback(false, nullptr, TEXT("No diagnostic output provider has been created for this instance yet."));
		}
		return;
	}

	OutputProvider->RequestDiagnosticPreview(MoveTemp(Callback));
}

void FWebView2Window::SetBackgroundColor(const FColor& InBackgroundColor)
{
	BackgroundColor = InBackgroundColor;
	if (!Controller)
	{
		return;
	}

	Microsoft::WRL::ComPtr<ICoreWebView2Controller2> Controller2;
	if (SUCCEEDED(Controller.As(&Controller2)) && Controller2)
	{
		Controller2->put_DefaultBackgroundColor(ToCoreColor(InBackgroundColor));
	}
}

void FWebView2Window::SetContainerVisual(winrt::Windows::UI::Composition::ContainerVisual InContainerVisual)
{
	WebViewVisual = InContainerVisual;
}

TSharedPtr<FWebView2CompositionHost> FWebView2Window::GetCompositionHost() const
{
	return CompositionHost;
}

TSharedPtr<IWebView2OutputProvider> FWebView2Window::GetOutputProvider() const
{
	return OutputProvider;
}

void FWebView2Window::SetHitTestEnabled(bool bInHitTestEnabled)
{
	bHitTestEnabled = bInHitTestEnabled;
}

bool FWebView2Window::IsHitTestEnabled() const
{
	return bHitTestEnabled;
}

void FWebView2Window::SetTransparencyHitTestEnabled(bool bInTransparencyHitTestEnabled)
{
	if (bEnableTransparencyHitTest == bInTransparencyHitTestEnabled)
	{
		return;
	}

	bEnableTransparencyHitTest = bInTransparencyHitTestEnabled;
	if (bEnableTransparencyHitTest)
	{
		RegisterTransparencyHitTestScript(true);
	}
	else
	{
		SetHitTestEnabled(true);
	}
}

bool FWebView2Window::IsTransparencyHitTestEnabled() const
{
	return bEnableTransparencyHitTest;
}

void FWebView2Window::SetAllowNonInteractiveElementPassthrough(bool bInAllowNonInteractiveElementPassthrough)
{
	if (bAllowNonInteractiveElementPassthrough == bInAllowNonInteractiveElementPassthrough)
	{
		return;
	}

	bAllowNonInteractiveElementPassthrough = bInAllowNonInteractiveElementPassthrough;
	if (!WebView)
	{
		return;
	}

	const FString Script = FString::Printf(
		TEXT("window.__cbwebview2TransparencyAllowNonInteractivePassthrough=%s;if(window.__cbwebview2TransparencyCheck&&window.__cbwebview2TransparencyCheck.setAllowNonInteractiveElementPassthrough){window.__cbwebview2TransparencyCheck.setAllowNonInteractiveElementPassthrough(%s);}"),
		bAllowNonInteractiveElementPassthrough ? TEXT("true") : TEXT("false"),
		bAllowNonInteractiveElementPassthrough ? TEXT("true") : TEXT("false"));
	ExecuteScript(Script, nullptr);
}

void FWebView2Window::LoadURL(const FString& InUrl)
{
	if (InUrl.IsEmpty())
	{
		return;
	}

	InitialUrl = InUrl;
	if (WebView)
	{
		WebView->Navigate(*InitialUrl);
	}
}

void FWebView2Window::GoForward() const
{
	if (WebView)
	{
		BOOL bCanGoForward = 0;
		WebView->get_CanGoForward(&bCanGoForward);
		if (bCanGoForward)
		{
			WebView->GoForward();
		}
	}
}

void FWebView2Window::GoBack() const
{
	if (WebView)
	{
		BOOL bCanGoBack = 0;
		WebView->get_CanGoBack(&bCanGoBack);
		if (bCanGoBack)
		{
			WebView->GoBack();
		}
	}
}

void FWebView2Window::Reload() const
{
	if (WebView)
	{
		WebView->Reload();
	}
}

void FWebView2Window::Stop() const
{
	if (WebView)
	{
		WebView->Stop();
	}
}

void FWebView2Window::SetBounds(const RECT& InRect)
{
	// This is called from Slate OnPaint every frame; skip identical rects so the per-frame cost is a four-field
	// compare instead of cross-process put_Bounds / visual updates. Initial application is handled by
	// OnCreateControllerCompleted and AttachWebView, so deduping here cannot lose the first real layout.
	if (bBoundsApplied &&
		Bounds.left == InRect.left && Bounds.top == InRect.top &&
		Bounds.right == InRect.right && Bounds.bottom == InRect.bottom)
	{
		return;
	}

	Bounds = InRect;
	bBoundsApplied = true;
	if (OutputProvider.IsValid())
	{
		OutputProvider->RequestOutputResize(FIntPoint(Bounds.right - Bounds.left, Bounds.bottom - Bounds.top));
	}
	if (Controller)
	{
		// In windowed mode, update controller bounds directly.
		Controller->put_Bounds(Bounds);
	}

	if (CompositionController && WebViewVisual)
	{
		// In WinComp mode, keep the container attached to RootVisualTarget in sync with position and size.
		// WebViewVisual is the same container AttachWebView handed to put_RootVisualTarget, so use it directly
		// instead of re-querying get_RootVisualTarget + QueryInterface.
		WebViewVisual.Offset({static_cast<float>(Bounds.left), static_cast<float>(Bounds.top), 0.0f});
		WebViewVisual.Size({static_cast<float>(Bounds.right - Bounds.left), static_cast<float>(Bounds.bottom - Bounds.top)});
	}
}

void FWebView2Window::SetBounds(const POINT& InOffset, const POINT& InSize)
{
	RECT NewRect;
	NewRect.left = InOffset.x;
	NewRect.top = InOffset.y;
	NewRect.right = InOffset.x + InSize.x;
	NewRect.bottom = InOffset.y + InSize.y;
	SetBounds(NewRect);
}

RECT FWebView2Window::GetBounds() const
{
	return Bounds;
}

void FWebView2Window::SetVisible(ESlateVisibility InVisibility)
{
	Visible = InVisibility;
	const bool bVisible = InVisibility != ESlateVisibility::Collapsed && InVisibility != ESlateVisibility::Hidden;
	if (Controller)
	{
		Controller->put_IsVisible(bVisible);
	}

	const UWebView2Settings* Settings = UWebView2Settings::Get();
	const bool bReduceMemoryWhenHidden = !Settings || Settings->Performance.bReduceMemoryWhenHidden;
	if (bVisible)
	{
		ResumeIfSuspended();
		if (bReduceMemoryWhenHidden)
		{
			SetMemoryUsageTargetLow(false);
		}
	}
	else if (bReduceMemoryWhenHidden)
	{
		SetMemoryUsageTargetLow(true);
	}
}

void FWebView2Window::TrySuspend()
{
	if (!WebView || bCloseRequested)
	{
		return;
	}

	// The runtime rejects TrySuspend while the WebView is visible; the caller hides it first via SetVisible.
	if (Visible != ESlateVisibility::Collapsed && Visible != ESlateVisibility::Hidden)
	{
		return;
	}

	Microsoft::WRL::ComPtr<ICoreWebView2_3> WebView3;
	if (FAILED(WebView.As(&WebView3)) || !WebView3)
	{
		return;
	}

	BOOL bIsSuspended = Windows::FALSE;
	if (SUCCEEDED(WebView3->get_IsSuspended(&bIsSuspended)) && bIsSuspended)
	{
		return;
	}

	WebView3->TrySuspend(
		Microsoft::WRL::Callback<ICoreWebView2TrySuspendCompletedHandler>(
			[](HRESULT ErrorCode, BOOL bIsSuccessful) -> HRESULT
			{
				if (FAILED(ErrorCode) || !bIsSuccessful)
				{
					UE_LOG(LogWebView2Utils, Verbose, TEXT("WebView2 TrySuspend was rejected. HRESULT=0x%08x success=%d"), ErrorCode, bIsSuccessful ? 1 : 0);
				}
				return S_OK;
			})
			.Get());
}

void FWebView2Window::ResumeIfSuspended()
{
	if (!WebView)
	{
		return;
	}

	Microsoft::WRL::ComPtr<ICoreWebView2_3> WebView3;
	if (FAILED(WebView.As(&WebView3)) || !WebView3)
	{
		return;
	}

	BOOL bIsSuspended = Windows::FALSE;
	if (SUCCEEDED(WebView3->get_IsSuspended(&bIsSuspended)) && bIsSuspended)
	{
		WebView3->Resume();
	}
}

void FWebView2Window::SetRasterizationScale(double InScale)
{
	if (InScale <= 0.0 || FMath::IsNearlyEqual(InScale, AppliedRasterizationScale, 0.001))
	{
		return;
	}

	// Remember the scale even before the controller exists; OnCreateControllerCompleted applies it on creation.
	AppliedRasterizationScale = InScale;

	Microsoft::WRL::ComPtr<ICoreWebView2Controller3> Controller3;
	if (Controller && SUCCEEDED(Controller.As(&Controller3)) && Controller3)
	{
		Controller3->put_RasterizationScale(InScale);
	}
}

void FWebView2Window::SetMemoryUsageTargetLow(bool bLowMemory)
{
	if (!WebView)
	{
		return;
	}

	Microsoft::WRL::ComPtr<ICoreWebView2_19> WebView19;
	if (SUCCEEDED(WebView.As(&WebView19)) && WebView19)
	{
		WebView19->put_MemoryUsageTargetLevel(bLowMemory
			? COREWEBVIEW2_MEMORY_USAGE_TARGET_LEVEL_LOW
			: COREWEBVIEW2_MEMORY_USAGE_TARGET_LEVEL_NORMAL);
	}
}

ESlateVisibility FWebView2Window::GetVisible() const
{
	return Visible;
}

ECBWebView2DocumentState FWebView2Window::GetDocumentLoadingState() const
{
	return DocumentState;
}

void FWebView2Window::NotifyParentWindowPositionChanged()
{
	if (Controller)
	{
		// Notify WebView2 that the parent window's screen position changed so its internal IME candidate window can reposition.
		Controller->NotifyParentWindowPositionChanged();
	}
}

void FWebView2Window::MoveFocus(bool bFocus)
{
	if (bFocus && ParentWindow)
	{
		// Ensure the host HWND owns system focus before requesting focus movement inside the WebView.
		if (::GetFocus() != ParentWindow)
		{
			::SetFocus(ParentWindow);
		}
	}

	if (Controller && bFocus)
	{
		Controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
	}
}

void FWebView2Window::SendMouseButton(const FVector2D& Point, bool bIsLeft, bool bIsDown)
{
	if (CompositionController)
	{
		const EWebView2MouseButton Button = bIsLeft ? EWebView2MouseButton::Left : EWebView2MouseButton::Right;
		COREWEBVIEW2_MOUSE_EVENT_KIND Kind = COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOWN;
		if (!TryGetMouseButtonEventKind(Button, bIsDown, Kind))
		{
			return;
		}

		const POINT MousePoint = ToMousePoint(Point);
		CompositionController->SendMouseInput(Kind, GetMouseButtonVirtualKey(Button), GetMouseButtonData(Button), MousePoint);
	}
}

void FWebView2Window::SendMouseDoubleClick(const FVector2D& Point, bool bIsLeft)
{
	if (CompositionController)
	{
		const POINT MousePoint = ToMousePoint(Point);
		const COREWEBVIEW2_MOUSE_EVENT_KIND Kind = bIsLeft
			? COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOUBLE_CLICK
			: COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_DOUBLE_CLICK;
		CompositionController->SendMouseInput(Kind, bIsLeft ? COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_LEFT_BUTTON : COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_RIGHT_BUTTON, 0, MousePoint);
	}
}

void FWebView2Window::SendMouseMove(const FVector2D& Point, bool bIsLeftButtonDown, bool bIsRightButtonDown, bool bIsMiddleButtonDown)
{
	if (CompositionController)
	{
		const POINT MousePoint = ToMousePoint(Point);
		int32 VirtualKeys = static_cast<int32>(COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE);
		if (bIsLeftButtonDown)
		{
			VirtualKeys |= static_cast<int32>(COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_LEFT_BUTTON);
		}
		if (bIsRightButtonDown)
		{
			VirtualKeys |= static_cast<int32>(COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_RIGHT_BUTTON);
		}
		if (bIsMiddleButtonDown)
		{
			VirtualKeys |= static_cast<int32>(COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_MIDDLE_BUTTON);
		}

		CompositionController->SendMouseInput(
			COREWEBVIEW2_MOUSE_EVENT_KIND_MOVE,
			static_cast<COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS>(VirtualKeys),
			0,
			MousePoint);
	}
}

void FWebView2Window::SendMouseWheel(const FVector2D& Point, float Delta)
{
	if (CompositionController)
	{
		const POINT MousePoint = ToMousePoint(Point);
		CompositionController->SendMouseInput(
			COREWEBVIEW2_MOUSE_EVENT_KIND_WHEEL,
			COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE,
			static_cast<UINT32>(Delta * 120.0f),
			MousePoint);
	}
}

void FWebView2Window::SendKeyboardMessage(uint32 Msg, uint64 WParam, int64 LParam)
{
	if (ParentWindow)
	{
		// Route through the host window message chain so input handling remains consistent with the existing path.
		::SendMessageW(ParentWindow, Msg, static_cast<WPARAM>(WParam), static_cast<LPARAM>(LParam));
	}
}

HRESULT FWebView2Window::CreateControllerWithOptions()
{
	// Prefer Environment10 first so ControllerOptions are available.
	Microsoft::WRL::ComPtr<ICoreWebView2Environment10> Environment10;
	WebViewEnvironment.As(&Environment10);
	const TWeakPtr<FWebView2Window> WeakWindow = AsShared();

	const UWebView2Settings* Settings = UWebView2Settings::Get();
	const ECBWebView2Mode Mode = Settings ? Settings->Mode : ECBWebView2Mode::VisualWinComp;

	if (!Environment10)
	{
		// Fall back to the base Controller / CompositionController creation path for older runtimes.
		Microsoft::WRL::ComPtr<ICoreWebView2Environment3> Environment3;
		WebViewEnvironment.As(&Environment3);

		if (Mode == ECBWebView2Mode::Windowed)
		{
			return WebViewEnvironment->CreateCoreWebView2Controller(
				ParentWindow,
				Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
					[WeakWindow](HRESULT Result, ICoreWebView2Controller* InController) -> HRESULT
					{
						if (TSharedPtr<FWebView2Window> Window = WeakWindow.Pin())
						{
							return Window->OnCreateControllerCompleted(Result, InController);
						}

						return S_OK;
					})
					.Get());
		}

		if (!Environment3)
		{
			return E_NOINTERFACE;
		}

		return Environment3->CreateCoreWebView2CompositionController(
			ParentWindow,
			Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>(
				[WeakWindow](HRESULT Result, ICoreWebView2CompositionController* InCompositionController) -> HRESULT
				{
					if (TSharedPtr<FWebView2Window> Window = WeakWindow.Pin())
					{
						Window->CompositionController = InCompositionController;
						Microsoft::WRL::ComPtr<ICoreWebView2Controller> BaseController;
						Window->CompositionController.As(&BaseController);
						return Window->OnCreateControllerCompleted(Result, BaseController.Get());
					}

					return S_OK;
				})
				.Get());
	}

	Microsoft::WRL::ComPtr<ICoreWebView2ControllerOptions> ControllerOptions;
	HRESULT Hr = Environment10->CreateCoreWebView2ControllerOptions(&ControllerOptions);
	if (FAILED(Hr))
	{
		UE_LOG(LogWebView2Utils, Error, TEXT("Failed to create ControllerOptions. HRESULT=0x%08x"), Hr);
		return Hr;
	}

	if (Settings)
	{
		// These are controller-level settings and must be written once before creation.
		ControllerOptions->put_ProfileName(*Settings->Controller.ProfileName);
		ControllerOptions->put_IsInPrivateModeEnabled(Settings->Controller.bInPrivate);

		Microsoft::WRL::ComPtr<ICoreWebView2ControllerOptions2> ControllerOptions2;
		if (SUCCEEDED(ControllerOptions.As(&ControllerOptions2)))
		{
			if (Settings->Controller.bUseOSRegion)
			{
				wchar_t LocaleName[LOCALE_NAME_MAX_LENGTH] = {};
				GetUserDefaultLocaleName(LocaleName, LOCALE_NAME_MAX_LENGTH);
				ControllerOptions2->put_ScriptLocale(LocaleName);
			}
			else if (!Settings->Controller.ScriptLocale.IsEmpty())
			{
				ControllerOptions2->put_ScriptLocale(*Settings->Controller.ScriptLocale);
			}
		}

		Microsoft::WRL::ComPtr<ICoreWebView2ExperimentalControllerOptions2> ExperimentalControllerOptions2;
		if (SUCCEEDED(ControllerOptions.As(&ExperimentalControllerOptions2)))
		{
			ExperimentalControllerOptions2->put_AllowHostInputProcessing(Settings->Controller.bAllowHostInputProcessing);
		}
	}

	if (Mode == ECBWebView2Mode::Windowed)
	{
		return Environment10->CreateCoreWebView2ControllerWithOptions(
			ParentWindow,
			ControllerOptions.Get(),
			Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
				[WeakWindow](HRESULT Result, ICoreWebView2Controller* InController) -> HRESULT
				{
					if (TSharedPtr<FWebView2Window> Window = WeakWindow.Pin())
					{
						return Window->OnCreateControllerCompleted(Result, InController);
					}

					return S_OK;
				})
				.Get());
	}

	return Environment10->CreateCoreWebView2CompositionControllerWithOptions(
		ParentWindow,
		ControllerOptions.Get(),
		Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>(
			[WeakWindow](HRESULT Result, ICoreWebView2CompositionController* InCompositionController) -> HRESULT
			{
				if (TSharedPtr<FWebView2Window> Window = WeakWindow.Pin())
				{
					Window->CompositionController = InCompositionController;
					Microsoft::WRL::ComPtr<ICoreWebView2Controller> BaseController;
					Window->CompositionController.As(&BaseController);
					return Window->OnCreateControllerCompleted(Result, BaseController.Get());
				}

				return S_OK;
			})
			.Get());
}

HRESULT FWebView2Window::OnCreateEnvironmentCompleted(HRESULT Result, ICoreWebView2Environment* Environment)
{
	if (bCloseRequested)
	{
		return S_OK;
	}

	if (FAILED(Result) || !Environment)
	{
		UE_LOG(LogWebView2Utils, Error, TEXT("WebView2 Environment creation failed. HRESULT=0x%08x"), Result);
		return S_OK;
	}

	WebViewEnvironment = Environment;
	WebViewEnvironment->add_NewBrowserVersionAvailable(
		Microsoft::WRL::Callback<ICoreWebView2NewBrowserVersionAvailableEventHandler>(this, &FWebView2Window::OnNewBrowserVersionAvailableInternal).Get(),
		&NewBrowserVersionAvailableToken);

	FCBWebView2MonitoredEvent RuntimeEventInfo;
	RuntimeEventInfo.EventType = ECBWebView2MonitoredEventType::RuntimeVersionDetected;
	RuntimeEventInfo.RuntimeVersion = FWebView2Manager::Get().GetRuntimeVersion();
	RuntimeEventInfo.bSuccess = FWebView2Manager::Get().IsRuntimeAvailable();
	EmitMonitoredEvent(RuntimeEventInfo);

	bInitialized = true;
	return CreateControllerWithOptions();
}

HRESULT FWebView2Window::OnCreateControllerCompleted(HRESULT Result, ICoreWebView2Controller* InController)
{
	if (bCloseRequested)
	{
		return S_OK;
	}

	if (FAILED(Result) || !InController)
	{
		UE_LOG(LogWebView2Utils, Error, TEXT("WebView2 Controller creation failed. HRESULT=0x%08x"), Result);
		return S_OK;
	}

	Controller = InController;
	Microsoft::WRL::ComPtr<ICoreWebView2> CoreWebView;
	Controller->get_CoreWebView2(&CoreWebView);
	CoreWebView.As(&WebView);

	if (WebView)
	{
		WebView->get_BrowserProcessId(&BrowserProcessId);
	}

	if (!OutputProvider.IsValid())
	{
		OutputProvider = MakeShared<FWebView2CapturePreviewOutputProvider>(AsShared());
		OutputProvider->RequestOutputResize(FIntPoint(Bounds.right - Bounds.left, Bounds.bottom - Bounds.top));
	}

	const UWebView2Settings* Settings = UWebView2Settings::Get();
	if (Settings)
	{
		Microsoft::WRL::ComPtr<ICoreWebView2_13> WebView13;
		if (SUCCEEDED(WebView.As(&WebView13)) && WebView13 && !Settings->Controller.DownloadPath.IsEmpty())
		{
			Microsoft::WRL::ComPtr<ICoreWebView2Profile> Profile;
			if (SUCCEEDED(WebView13->get_Profile(&Profile)) && Profile)
			{
				Profile->put_DefaultDownloadFolderPath(*Settings->Controller.DownloadPath);
			}
		}
	}

	Controller->put_Bounds(Bounds);
	Controller->put_IsVisible(Visible != ESlateVisibility::Collapsed && Visible != ESlateVisibility::Hidden);

	{
		// The host hands WebView2 raw-pixel bounds and pushes the Slate scale explicitly through
		// SetRasterizationScale, so disable monitor-scale auto-detection. This also keeps the world-space
		// hidden host window from re-rasterizing when IME anchoring moves it across monitors.
		Microsoft::WRL::ComPtr<ICoreWebView2Controller3> Controller3;
		if (SUCCEEDED(Controller.As(&Controller3)) && Controller3)
		{
			Controller3->put_BoundsMode(COREWEBVIEW2_BOUNDS_MODE_USE_RAW_PIXELS);
			Controller3->put_ShouldDetectMonitorScaleChanges(Windows::FALSE);
			if (AppliedRasterizationScale > 0.0)
			{
				Controller3->put_RasterizationScale(AppliedRasterizationScale);
			}
		}
	}

	if (CompositionController && bAttachToHostVisual)
	{
		// In windowless mode, attach this WebView to the composition host owned by the matching host HWND.
		CompositionHost = FWebView2Manager::Get().GetOrCreateCompositionHost(VisualHostWindow);
		if (CompositionHost.IsValid())
		{
			CompositionHost->AttachWebView(AsShared());
		}
	}

	ApplyRuntimeSettings();
	RegisterCoreEvents();
	SetBackgroundColor(BackgroundColor);

	RegisterTransparencyHitTestScript(false);

	if (WebView)
	{
		const FString InputEventBridgeScript = BuildInputEventBridgeScript();
		WebView->AddScriptToExecuteOnDocumentCreated(
			*InputEventBridgeScript,
			Microsoft::WRL::Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
				[](HRESULT ErrorCode, PCWSTR ScriptId) -> HRESULT
				{
					if (FAILED(ErrorCode))
					{
						UE_LOG(LogWebView2Utils, Warning, TEXT("Failed to inject the CBWebView2 input event bridge. HRESULT=0x%08x"), ErrorCode);
					}
					return S_OK;
				})
				.Get());
	}

	if (!InitialUrl.IsEmpty())
	{
		WebView->Navigate(*InitialUrl);
	}

	return S_OK;
}

HRESULT FWebView2Window::OnMessageReceivedInternal(ICoreWebView2* Sender, ICoreWebView2WebMessageReceivedEventArgs* Args)
{
	const FString Message = GetWebMessagePayload(Args);

	LPWSTR RawSource = nullptr;
	Args->get_Source(&RawSource);
	const FString Source = TakeCoTaskMemString(RawSource);

	const UWebView2Settings* Settings = UWebView2Settings::Get();
	const bool bOriginAllowed = IsWebMessageOriginAllowed(Source, Settings);
	const bool bIsInternalMessage = IsCBWebView2InternalMessage(Message);
	UE_LOG(LogWebView2Utils, Verbose, TEXT("Received web message: Source=%s Message=%s"), *Source, *Message);

	FCBWebView2MonitoredEvent EventInfo;
	EventInfo.EventType = ECBWebView2MonitoredEventType::WebMessageReceived;
	EventInfo.Message = Message;
	EventInfo.Source = Source;
	EventInfo.Url = Source;
	EventInfo.bBlocked = !bIsInternalMessage && !bOriginAllowed && Settings && Settings->Security.bEnableWebMessageOriginCheck && !Settings->Security.bWebMessageOriginWarnOnly;
	EmitMonitoredEvent(EventInfo);

	if (!bOriginAllowed && Settings && Settings->Security.bEnableWebMessageOriginCheck)
	{
		if (!bIsInternalMessage)
		{
			UE_LOG(LogWebView2Utils, Warning, TEXT("WebMessage source failed allowlist validation: Source=%s Origin=%s"), *Source, *ExtractOrigin(Source));
			if (!Settings->Security.bWebMessageOriginWarnOnly)
			{
				return S_OK;
			}
		}
	}

	OnMessageReceived.ExecuteIfBound(Message);
	const bool bFilterInternalMessage = IsCBWebView2InternalOnlyMessage(Message) || (Settings && Settings->Security.bFilterInternalMessagesFromBlueprint && bIsInternalMessage);
	if (!bFilterInternalMessage)
	{
		if (UWebView2Subsystem* Subsystem = UWebView2Subsystem::Get())
		{
			// Broadcast through the subsystem as well so global listeners can observe it.
			Subsystem->OnWebMessageReceivedNative.ExecuteIfBound(Message);
			Subsystem->OnWebMessageReceived.Broadcast(Message);
		}
	}

	return S_OK;
}

void FWebView2Window::RegisterTransparencyHitTestScript(bool bExecuteImmediately)
{
	if (!WebView || !bEnableTransparencyHitTest)
	{
		return;
	}

	const FString InjectedScript = LoadTransparencyCheckScript(bAllowNonInteractiveElementPassthrough);
	if (InjectedScript.IsEmpty())
	{
		UE_LOG(LogWebView2Utils, Error, TEXT("CBWebView2 transparency hit-test script (Resources/transparency_check.js) is missing or empty; transparent-area passthrough will not work for this WebView."));
		return;
	}

	if (!bTransparencyHitTestScriptRegistered)
	{
		bTransparencyHitTestScriptRegistered = true;
		WebView->AddScriptToExecuteOnDocumentCreated(
			*InjectedScript,
			Microsoft::WRL::Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
				[](HRESULT ErrorCode, PCWSTR ScriptId) -> HRESULT
				{
					if (FAILED(ErrorCode))
					{
						UE_LOG(LogWebView2Utils, Warning, TEXT("Failed to inject the transparency hit-test script. HRESULT=0x%08x"), ErrorCode);
					}
					return S_OK;
				})
				.Get());
	}

	if (bExecuteImmediately)
	{
		ExecuteScript(InjectedScript, nullptr);
	}
}

HRESULT FWebView2Window::OnNewWindowRequestedInternal(ICoreWebView2* Sender, ICoreWebView2NewWindowRequestedEventArgs* Args)
{
	LPWSTR Uri = nullptr;
	Args->get_Uri(&Uri);
	const FString TargetUrl = TakeCoTaskMemString(Uri);

	FCBWebView2MonitoredEvent EventInfo;
	EventInfo.EventType = ECBWebView2MonitoredEventType::NewWindowRequested;
	EventInfo.Url = TargetUrl;
	EmitMonitoredEvent(EventInfo);

	OnNewWindowRequested.ExecuteIfBound(TargetUrl);
	Args->put_Handled(true);
	return S_OK;
}

HRESULT FWebView2Window::OnNavigationCompletedInternal(ICoreWebView2* Sender, ICoreWebView2NavigationCompletedEventArgs* Args)
{
	BOOL bSuccess = 0;
	Args->get_IsSuccess(&bSuccess);
	DocumentState = bSuccess ? ECBWebView2DocumentState::Completed : ECBWebView2DocumentState::Error;
	if (bSuccess && WebView)
	{
		ExecuteScript(BuildInputEventBridgeScript());
	}

	FCBWebView2MonitoredEvent EventInfo;
	EventInfo.EventType = ECBWebView2MonitoredEventType::NavigationCompleted;
	EventInfo.bSuccess = !!bSuccess;
	EmitMonitoredEvent(EventInfo);

	OnNavigationCompleted.ExecuteIfBound(!!bSuccess);
	return S_OK;
}

HRESULT FWebView2Window::OnNavigationStartingInternal(ICoreWebView2* Sender, ICoreWebView2NavigationStartingEventArgs* Args)
{
	LPWSTR Uri = nullptr;
	Args->get_Uri(&Uri);
	DocumentState = ECBWebView2DocumentState::Loading;
	const FString TargetUrl = TakeCoTaskMemString(Uri);

	FCBWebView2MonitoredEvent EventInfo;
	EventInfo.EventType = ECBWebView2MonitoredEventType::NavigationStarting;
	EventInfo.Url = TargetUrl;
	EmitMonitoredEvent(EventInfo);

	OnNavigationStarting.ExecuteIfBound(TargetUrl);
	return S_OK;
}

HRESULT FWebView2Window::OnCursorChangedInternal(ICoreWebView2CompositionController* Sender, IUnknown* Args)
{
	HCURSOR CursorHandle = nullptr;
	const HRESULT Hr = Sender->get_Cursor(&CursorHandle);
	if (FAILED(Hr))
	{
		return Hr;
	}

	EMouseCursor::Type CursorType = EMouseCursor::Default;
	if (CursorHandle == LoadCursor(nullptr, IDC_HAND))
	{
		CursorType = EMouseCursor::Hand;
	}
	else if (CursorHandle == LoadCursor(nullptr, IDC_IBEAM))
	{
		CursorType = EMouseCursor::TextEditBeam;
	}

	OnCursorChanged.ExecuteIfBound(CursorType);
	return S_OK;
}

HRESULT FWebView2Window::OnAcceleratorKeyPressedInternal(ICoreWebView2Controller* Sender, ICoreWebView2AcceleratorKeyPressedEventArgs* Args)
{
	COREWEBVIEW2_KEY_EVENT_KIND EventKind = COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN;
	Args->get_KeyEventKind(&EventKind);

	UINT VirtualKey = 0;
	Args->get_VirtualKey(&VirtualKey);

	UINT HostMessage = 0;
	if (EventKind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN)
	{
		HostMessage = WM_KEYDOWN;
	}
	else if (EventKind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_UP)
	{
		HostMessage = WM_KEYUP;
	}
	else if (EventKind == COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN)
	{
		HostMessage = WM_SYSKEYDOWN;
	}
	else if (EventKind == COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_UP)
	{
		HostMessage = WM_SYSKEYUP;
	}

	const UWebView2Settings* Settings = UWebView2Settings::Get();
	const FKey SlateKey = FInputKeyManager::Get().GetKeyFromCodes(VirtualKey, 0);
	const bool bReservedForUnreal =
		Settings &&
		SlateKey.IsValid() &&
		Settings->Input.KeysAlwaysRoutedToUnreal.Contains(SlateKey);
	if (bReservedForUnreal)
	{
		// Stop the browser from acting on the key, then repost it to the Unreal host where Slate/gameplay
		// input can process it. This also covers keys that arrive directly through Chromium's HWND path.
		Args->put_Handled(true);
	}

	if (HostMessage != 0 && ParentWindow)
	{
		::PostMessageW(ParentWindow, HostMessage, VirtualKey, 0);
	}

	return S_OK;
}

HRESULT FWebView2Window::OnHistoryChangedInternal(ICoreWebView2* Sender, IUnknown* Args)
{
	BOOL bCanGoBack = 0;
	BOOL bCanGoForward = 0;
	Sender->get_CanGoBack(&bCanGoBack);
	Sender->get_CanGoForward(&bCanGoForward);

	FCBWebView2MonitoredEvent EventInfo;
	EventInfo.EventType = ECBWebView2MonitoredEventType::HistoryChanged;
	EventInfo.bCanGoBack = !!bCanGoBack;
	EventInfo.bCanGoForward = !!bCanGoForward;
	EmitMonitoredEvent(EventInfo);

	OnCanGoBackChanged.ExecuteIfBound(!!bCanGoBack);
	OnCanGoForwardChanged.ExecuteIfBound(!!bCanGoForward);
	return S_OK;
}

HRESULT FWebView2Window::OnDocumentTitleChangedInternal(ICoreWebView2* Sender, IUnknown* Args)
{
	LPWSTR RawTitle = nullptr;
	Sender->get_DocumentTitle(&RawTitle);
	const FString Title = TakeCoTaskMemString(RawTitle);

	FCBWebView2MonitoredEvent EventInfo;
	EventInfo.EventType = ECBWebView2MonitoredEventType::DocumentTitleChanged;
	EventInfo.Title = Title;
	EmitMonitoredEvent(EventInfo);

	OnDocumentTitleChanged.ExecuteIfBound(Title);
	return S_OK;
}

HRESULT FWebView2Window::OnSourceChangedInternal(ICoreWebView2* Sender, ICoreWebView2SourceChangedEventArgs* Args)
{
	LPWSTR RawSource = nullptr;
	Sender->get_Source(&RawSource);
	const FString Source = RawSource ? TakeCoTaskMemString(RawSource) : TEXT("about:blank");

	FCBWebView2MonitoredEvent EventInfo;
	EventInfo.EventType = ECBWebView2MonitoredEventType::SourceChanged;
	EventInfo.Url = Source;
	EmitMonitoredEvent(EventInfo);

	OnSourceChanged.ExecuteIfBound(Source);
	return S_OK;
}

HRESULT FWebView2Window::OnDownloadStartingInternal(ICoreWebView2* Sender, ICoreWebView2DownloadStartingEventArgs* Args)
{
	Microsoft::WRL::ComPtr<ICoreWebView2DownloadOperation> DownloadOperation;
	if (FAILED(Args->get_DownloadOperation(&DownloadOperation)) || !DownloadOperation)
	{
		return S_OK;
	}

	EmitMonitoredEvent(BuildDownloadMonitoredEvent(ECBWebView2MonitoredEventType::DownloadStarting, DownloadOperation.Get()));
	OnDownloadStarting.ExecuteIfBound(BuildDownloadInfo(DownloadOperation.Get()));
	// After download start, register two ongoing events: byte-count changes and state changes.
	RegisterDownloadTracking(DownloadOperation.Get());
	return S_OK;
}

HRESULT FWebView2Window::OnPermissionRequestedInternal(ICoreWebView2* Sender, ICoreWebView2PermissionRequestedEventArgs* Args)
{
	COREWEBVIEW2_PERMISSION_KIND PermissionKind = COREWEBVIEW2_PERMISSION_KIND_UNKNOWN_PERMISSION;
	Args->get_PermissionKind(&PermissionKind);

	LPWSTR RawUri = nullptr;
	Args->get_Uri(&RawUri);

	BOOL bUserInitiated = 0;
	Args->get_IsUserInitiated(&bUserInitiated);

	FCBWebView2MonitoredEvent EventInfo;
	EventInfo.EventType = ECBWebView2MonitoredEventType::PermissionRequested;
	EventInfo.Url = TakeCoTaskMemString(RawUri);
	EventInfo.PermissionKind = PermissionKindToString(PermissionKind);
	EventInfo.bUserInitiated = !!bUserInitiated;

	const UWebView2Settings* Settings = UWebView2Settings::Get();
	const COREWEBVIEW2_PERMISSION_STATE PermissionState = Settings
		? ToCorePermissionState(Settings->Security.DefaultPermissionPolicy)
		: COREWEBVIEW2_PERMISSION_STATE_DEFAULT;
	Args->put_State(PermissionState);
	EventInfo.State = PermissionStateToString(PermissionState);

	EmitMonitoredEvent(EventInfo);
	return S_OK;
}

HRESULT FWebView2Window::OnProcessFailedInternal(ICoreWebView2* Sender, ICoreWebView2ProcessFailedEventArgs* Args)
{
	COREWEBVIEW2_PROCESS_FAILED_KIND FailedKind = COREWEBVIEW2_PROCESS_FAILED_KIND_UNKNOWN_PROCESS_EXITED;
	Args->get_ProcessFailedKind(&FailedKind);

	FCBWebView2MonitoredEvent EventInfo;
	EventInfo.EventType = ECBWebView2MonitoredEventType::ProcessFailed;
	EventInfo.ProcessFailedKind = ProcessFailedKindToString(FailedKind);
	EventInfo.BrowserProcessId = static_cast<int32>(BrowserProcessId);
	EventInfo.bSuccess = false;

	Microsoft::WRL::ComPtr<ICoreWebView2ProcessFailedEventArgs2> Args2;
	if (SUCCEEDED(Args->QueryInterface(IID_PPV_ARGS(&Args2))) && Args2)
	{
		COREWEBVIEW2_PROCESS_FAILED_REASON Reason = COREWEBVIEW2_PROCESS_FAILED_REASON_UNEXPECTED;
		Args2->get_Reason(&Reason);
		EventInfo.ProcessFailedReason = ProcessFailedReasonToString(Reason);

		INT32 ExitCode = 0;
		Args2->get_ExitCode(&ExitCode);
		EventInfo.ExitCode = ExitCode;

		LPWSTR RawProcessDescription = nullptr;
		if (SUCCEEDED(Args2->get_ProcessDescription(&RawProcessDescription)))
		{
			EventInfo.ProcessDescription = TakeCoTaskMemString(RawProcessDescription);
		}
	}

	UE_LOG(
		LogWebView2Utils,
		Warning,
		TEXT("WebView2 process failed: Kind=%s Reason=%s ExitCode=%d BrowserProcessId=%u Description=%s"),
		*EventInfo.ProcessFailedKind,
		*EventInfo.ProcessFailedReason,
		EventInfo.ExitCode,
		BrowserProcessId,
		*EventInfo.ProcessDescription);
	EmitMonitoredEvent(EventInfo);
	return S_OK;
}

HRESULT FWebView2Window::OnNewBrowserVersionAvailableInternal(ICoreWebView2Environment* Sender, IUnknown* Args)
{
	FCBWebView2MonitoredEvent EventInfo;
	EventInfo.EventType = ECBWebView2MonitoredEventType::BrowserVersionAvailable;
	EventInfo.RuntimeVersion = FWebView2Manager::Get().GetRuntimeVersion();
	EventInfo.bSuccess = true;
	EmitMonitoredEvent(EventInfo);

	UE_LOG(LogWebView2Utils, Log, TEXT("A new WebView2 Runtime version is available. Restart the WebView or editor to use it. Current version: %s"), *EventInfo.RuntimeVersion);
	return S_OK;
}

FWebView2DownloadInfo FWebView2Window::BuildDownloadInfo(ICoreWebView2DownloadOperation* DownloadOperation) const
{
	FWebView2DownloadInfo Info;
	if (!DownloadOperation)
	{
		return Info;
	}

	LPWSTR RawString = nullptr;
	if (SUCCEEDED(DownloadOperation->get_Uri(&RawString)))
	{
		Info.Uri = TakeCoTaskMemString(RawString);
	}

	RawString = nullptr;
	if (SUCCEEDED(DownloadOperation->get_MimeType(&RawString)))
	{
		Info.MimeType = TakeCoTaskMemString(RawString);
	}

	RawString = nullptr;
	if (SUCCEEDED(DownloadOperation->get_ResultFilePath(&RawString)))
	{
		Info.ResultFilePath = TakeCoTaskMemString(RawString);
	}

	DownloadOperation->get_BytesReceived(&Info.BytesReceived);
	DownloadOperation->get_TotalBytesToReceive(&Info.TotalBytesToReceive);

	COREWEBVIEW2_DOWNLOAD_STATE DownloadState = COREWEBVIEW2_DOWNLOAD_STATE_IN_PROGRESS;
	if (SUCCEEDED(DownloadOperation->get_State(&DownloadState)))
	{
		Info.State = ToDownloadState(DownloadState);
	}

	return Info;
}

FCBWebView2MonitoredEvent FWebView2Window::BuildDownloadMonitoredEvent(ECBWebView2MonitoredEventType EventType, ICoreWebView2DownloadOperation* DownloadOperation) const
{
	const FWebView2DownloadInfo DownloadInfo = BuildDownloadInfo(DownloadOperation);

	FCBWebView2MonitoredEvent EventInfo;
	EventInfo.EventType = EventType;
	EventInfo.Url = DownloadInfo.Uri;
	EventInfo.ResultFilePath = DownloadInfo.ResultFilePath;
	EventInfo.BytesReceived = DownloadInfo.BytesReceived;
	EventInfo.TotalBytesToReceive = DownloadInfo.TotalBytesToReceive;
	EventInfo.State = DownloadStateToString(DownloadInfo.State);
	EventInfo.bSuccess = DownloadInfo.State == EWebView2DownloadState::Completed;
	return EventInfo;
}

void FWebView2Window::EmitMonitoredEvent(const FCBWebView2MonitoredEvent& EventInfo) const
{
	// Notify listeners on this instance first, then broadcast globally through the subsystem.
	OnMonitoredEvent.ExecuteIfBound(EventInfo);
	if (UWebView2Subsystem* Subsystem = UWebView2Subsystem::Get())
	{
		Subsystem->OnMonitoredEventNative.ExecuteIfBound(EventInfo);
		Subsystem->OnMonitoredEvent.Broadcast(EventInfo);
	}
}

void FWebView2Window::RegisterDownloadTracking(ICoreWebView2DownloadOperation* DownloadOperation)
{
	if (!DownloadOperation)
	{
		return;
	}

	// Use the COM pointer address as the download instance key so duplicate registration and unbinding are easy to manage.
	const uint64 DownloadKey = reinterpret_cast<uint64>(DownloadOperation);
	if (ActiveDownloads.Contains(DownloadKey))
	{
		return;
	}

	FTrackedDownloadRegistration Registration;
	Registration.Operation = DownloadOperation;

	DownloadOperation->add_BytesReceivedChanged(
		Microsoft::WRL::Callback<ICoreWebView2BytesReceivedChangedEventHandler>(
			[this](ICoreWebView2DownloadOperation* InDownloadOperation, IUnknown* Args) -> HRESULT
			{
				EmitMonitoredEvent(BuildDownloadMonitoredEvent(ECBWebView2MonitoredEventType::DownloadUpdated, InDownloadOperation));
				OnDownloadUpdated.ExecuteIfBound(BuildDownloadInfo(InDownloadOperation));
				return S_OK;
			})
			.Get(),
		&Registration.BytesReceivedChangedToken);

	DownloadOperation->add_StateChanged(
		Microsoft::WRL::Callback<ICoreWebView2StateChangedEventHandler>(
			[this](ICoreWebView2DownloadOperation* InDownloadOperation, IUnknown* Args) -> HRESULT
			{
				const FWebView2DownloadInfo DownloadInfo = BuildDownloadInfo(InDownloadOperation);
				EmitMonitoredEvent(BuildDownloadMonitoredEvent(ECBWebView2MonitoredEventType::DownloadUpdated, InDownloadOperation));
				OnDownloadUpdated.ExecuteIfBound(DownloadInfo);

				if (DownloadInfo.State != EWebView2DownloadState::InProgress)
				{
					UnregisterDownloadTracking(reinterpret_cast<uint64>(InDownloadOperation));
				}
				return S_OK;
			})
			.Get(),
		&Registration.StateChangedToken);

	ActiveDownloads.Add(DownloadKey, MoveTemp(Registration));
}

void FWebView2Window::UnregisterDownloadTracking(uint64 DownloadKey)
{
	if (FTrackedDownloadRegistration* Registration = ActiveDownloads.Find(DownloadKey))
	{
		if (Registration->Operation)
		{
			Registration->Operation->remove_BytesReceivedChanged(Registration->BytesReceivedChangedToken);
			Registration->Operation->remove_StateChanged(Registration->StateChangedToken);
		}
		ActiveDownloads.Remove(DownloadKey);
	}
}

void FWebView2Window::ApplyRuntimeSettings() const
{
	if (!WebView)
	{
		return;
	}

	// Runtime switches differ from Environment / Controller configuration and can be written after instance creation.
	Microsoft::WRL::ComPtr<ICoreWebView2Settings> RuntimeSettings;
	WebView->get_Settings(&RuntimeSettings);

	const UWebView2Settings* Settings = UWebView2Settings::Get();
	if (!RuntimeSettings || !Settings)
	{
		return;
	}

	RuntimeSettings->put_AreDefaultContextMenusEnabled(Settings->Features.bEnableContextMenus);
	RuntimeSettings->put_AreDefaultScriptDialogsEnabled(Settings->Features.bEnableScriptDialogs);
	RuntimeSettings->put_AreDevToolsEnabled(Settings->Features.bEnableDevTools);
	RuntimeSettings->put_AreHostObjectsAllowed(Settings->Features.bAllowHostObjects);
	RuntimeSettings->put_IsBuiltInErrorPageEnabled(Settings->Features.bEnableBuiltInErrorPage);
	RuntimeSettings->put_IsScriptEnabled(Settings->Features.bEnableScript);
	RuntimeSettings->put_IsStatusBarEnabled(Settings->Features.bEnableStatusBar);
	RuntimeSettings->put_IsWebMessageEnabled(Settings->Features.bEnableWebMessage);
	RuntimeSettings->put_IsZoomControlEnabled(Settings->Features.bEnableZoomControl);

	Microsoft::WRL::ComPtr<ICoreWebView2_8> WebView8;
	if (SUCCEEDED(WebView.As(&WebView8)) && WebView8)
	{
		WebView8->put_IsMuted(Settings->Features.bMuted);
	}
}

void FWebView2Window::RegisterCoreEvents()
{
	if (!WebView || !Controller)
	{
		return;
	}

	// Register all core events in one place so Close can safely unbind them with the matching tokens.
	WebView->add_WebMessageReceived(
		Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(this, &FWebView2Window::OnMessageReceivedInternal).Get(),
		&MessageReceivedToken);

	WebView->add_NavigationCompleted(
		Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(this, &FWebView2Window::OnNavigationCompletedInternal).Get(),
		&NavigationCompletedToken);

	WebView->add_NavigationStarting(
		Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>(this, &FWebView2Window::OnNavigationStartingInternal).Get(),
		&NavigationStartingToken);

	WebView->add_NewWindowRequested(
		Microsoft::WRL::Callback<ICoreWebView2NewWindowRequestedEventHandler>(this, &FWebView2Window::OnNewWindowRequestedInternal).Get(),
		&NewWindowRequestedToken);

	WebView->add_HistoryChanged(
		Microsoft::WRL::Callback<ICoreWebView2HistoryChangedEventHandler>(this, &FWebView2Window::OnHistoryChangedInternal).Get(),
		&HistoryChangedToken);

	WebView->add_DocumentTitleChanged(
		Microsoft::WRL::Callback<ICoreWebView2DocumentTitleChangedEventHandler>(this, &FWebView2Window::OnDocumentTitleChangedInternal).Get(),
		&DocumentTitleChangedToken);

	WebView->add_SourceChanged(
		Microsoft::WRL::Callback<ICoreWebView2SourceChangedEventHandler>(this, &FWebView2Window::OnSourceChangedInternal).Get(),
		&SourceChangedToken);

	if (Microsoft::WRL::ComPtr<ICoreWebView2_4> WebView4; SUCCEEDED(WebView.As(&WebView4)) && WebView4)
	{
		WebView4->add_DownloadStarting(
			Microsoft::WRL::Callback<ICoreWebView2DownloadStartingEventHandler>(this, &FWebView2Window::OnDownloadStartingInternal).Get(),
			&DownloadStartingToken);
	}

	WebView->add_PermissionRequested(
		Microsoft::WRL::Callback<ICoreWebView2PermissionRequestedEventHandler>(this, &FWebView2Window::OnPermissionRequestedInternal).Get(),
		&PermissionRequestedToken);

	WebView->add_ProcessFailed(
		Microsoft::WRL::Callback<ICoreWebView2ProcessFailedEventHandler>(this, &FWebView2Window::OnProcessFailedInternal).Get(),
		&ProcessFailedToken);

	Controller->add_AcceleratorKeyPressed(
		Microsoft::WRL::Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(this, &FWebView2Window::OnAcceleratorKeyPressedInternal).Get(),
		&AcceleratorKeyPressedToken);

	if (CompositionController)
	{
		CompositionController->add_CursorChanged(
			Microsoft::WRL::Callback<ICoreWebView2CursorChangedEventHandler>(this, &FWebView2Window::OnCursorChangedInternal).Get(),
			&CursorChangedToken);
	}
}
