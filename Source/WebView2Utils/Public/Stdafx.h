// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#pragma once

// This header centralizes third-party WebView2 / WinRT / WRL includes
// so individual source files do not need to repeat platform macro handling and warning suppression.
#pragma warning(disable : 4668 4005 4191 4459 4530)

#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/AllowWindowsPlatformAtomics.h"

#include <SDKDDKVer.h>

THIRD_PARTY_INCLUDES_START
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>

#include <wrl.h>

// Official WebView2 SDK headers.
#include "WebView2.h"
#include "WebView2EnvironmentOptions.h"
#include "WebView2Experimental.h"
#include "WebView2ExperimentalEnvironmentOptions.h"
THIRD_PARTY_INCLUDES_END

#include "Windows/HideWindowsPlatformAtomics.h"
#include "Windows/HideWindowsPlatformTypes.h"