// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * WebView2Utils is the plugin's native integration module.
 *
 * Responsibilities:
 * 1. Load WebView2Loader.dll.
 * 2. Own the shared log category.
 * 3. Provide native WebView2 host functionality.
 */
class WEBVIEW2UTILS_API FWebView2UtilsModule : public IModuleInterface
{
public:
	/** Try to load WebView2Loader.dll during startup. */
	virtual void StartupModule() override;
	/** Release the loaded DLL handle during shutdown. */
	virtual void ShutdownModule() override;

private:
	/** Hold the loader DLL manually to avoid missing runtime symbols. */
	void* DllHandle = nullptr;
};