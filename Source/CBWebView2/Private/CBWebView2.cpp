// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#include "CBWebView2.h"

DEFINE_LOG_CATEGORY(LogCBWebView2);

void FCBWebView2Module::StartupModule()
{
	// This module does not own any long-lived state.
	// No objects are created proactively during startup.
	// The actual WebView lifetime is started on demand by widgets or the subsystem.
}

void FCBWebView2Module::ShutdownModule()
{
	// Keep this empty to avoid coupling with submodule destruction order.
	// Each module and object is responsible for releasing its own resources.
}

IMPLEMENT_MODULE(FCBWebView2Module, CBWebView2)