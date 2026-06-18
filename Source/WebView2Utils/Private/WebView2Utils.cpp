// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#include "WebView2Utils.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"

#include "WebView2Log.h"

DEFINE_LOG_CATEGORY(LogWebView2Utils);

void FWebView2UtilsModule::StartupModule()
{
	// Keep the WebView2Loader.dll path relative to the plugin so the editor and packaged builds use the same binary.
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("CBWebView2"));
	if (!Plugin.IsValid())
	{
		UE_LOG(LogWebView2Utils, Error, TEXT("Failed to locate the CBWebView2 plugin directory."));
		return;
	}

	const FString LoaderPath = FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Source/ThirdParty/WebView2/Dependency/WebView2Loader.dll"));

	// Load the DLL explicitly to avoid relying on the system PATH or external project environment.
	DllHandle = FPlatformProcess::GetDllHandle(*LoaderPath);
	if (!DllHandle)
	{
		const uint32 ErrorCode = FPlatformMisc::GetLastError();
		TCHAR ErrorBuffer[1024] = {};
		FPlatformMisc::GetSystemErrorMessage(ErrorBuffer, UE_ARRAY_COUNT(ErrorBuffer), ErrorCode);
		UE_LOG(
			LogWebView2Utils,
			Error,
			TEXT("Failed to load WebView2Loader.dll: %s, error code: %u, system message: %s"),
			*LoaderPath,
			ErrorCode,
			ErrorBuffer);
	}
	else
	{
		UE_LOG(LogWebView2Utils, Log, TEXT("Loaded WebView2Loader.dll: %s"), *LoaderPath);
	}
}

void FWebView2UtilsModule::ShutdownModule()
{
	// Release the handle on shutdown so hot reload and editor shutdown do not leave stale references behind.
	if (DllHandle)
	{
		FPlatformProcess::FreeDllHandle(DllHandle);
		DllHandle = nullptr;
	}
}

IMPLEMENT_MODULE(FWebView2UtilsModule, WebView2Utils)