// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogCBWebView2, Log, All);

/**
	* UE-facing module for the plugin.
 *
 * This layer only manages module lifetime and the public UMG/Slate entry points.
 * The native WebView2 integration lives in the WebView2Utils module.
 */
class CBWEBVIEW2_API FCBWebView2Module : public IModuleInterface
{
public:
	/** Reserved startup hook; no instances are created proactively at the moment. */
	virtual void StartupModule() override;
	/** Reserved shutdown hook; actual cleanup is handled by object lifetimes. */
	virtual void ShutdownModule() override;
};