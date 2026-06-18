// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#include "WebView2Manager.h"

#include "WebView2CompositionHost.h"
#include "WebView2Log.h"
#include "WebView2Settings.h"
#include "WebView2Window.h"

#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <DispatcherQueue.h>
#include "Windows/HideWindowsPlatformTypes.h"

namespace
{
	FString TakeCoTaskMemString(LPWSTR RawString)
	{
		const FString Result = RawString ? FString(RawString) : FString();
		if (RawString)
		{
			::CoTaskMemFree(RawString);
		}
		return Result;
	}
}

FWebView2Manager& FWebView2Manager::Get()
{
	static FWebView2Manager Instance;
	return Instance;
}

void FWebView2Manager::Initialize()
{
	if (bInitialized)
	{
		return;
	}

	// Initialize only once to avoid recreating the DispatcherQueue.
	bInitialized = true;
	DetectRuntimeVersion();

	const UWebView2Settings* Settings = UWebView2Settings::Get();
	if (Settings && Settings->Mode == ECBWebView2Mode::VisualWinComp)
	{
		// WinComp mode requires a DispatcherQueue.
		EnsureDispatcherQueue();
	}
}

void FWebView2Manager::Shutdown()
{
	// Destroy all hosts so editor shutdown or PIE transitions do not leak native resources.
	for (TPair<HWND, TWeakPtr<FWebView2CompositionHost>>& Pair : CompositionHosts)
	{
		if (const TSharedPtr<FWebView2CompositionHost> Host = Pair.Value.Pin())
		{
			Host->Destroy();
		}
	}

	CompositionHosts.Empty();
	DispatcherQueueController = nullptr;
	RuntimeVersion.Reset();
	bRuntimeAvailable = false;
	bInitialized = false;
}

void FWebView2Manager::DetectRuntimeVersion()
{
	LPWSTR RawVersion = nullptr;
	const HRESULT Hr = GetAvailableCoreWebView2BrowserVersionString(nullptr, &RawVersion);
	RuntimeVersion = TakeCoTaskMemString(RawVersion);
	bRuntimeAvailable = SUCCEEDED(Hr) && !RuntimeVersion.IsEmpty();

	if (bRuntimeAvailable)
	{
		UE_LOG(LogWebView2Utils, Log, TEXT("Detected WebView2 Runtime version: %s"), *RuntimeVersion);
	}
	else
	{
		UE_LOG(LogWebView2Utils, Warning, TEXT("No usable WebView2 Runtime was detected. HRESULT=0x%08x. Install the Evergreen WebView2 Runtime or configure a Fixed Version Runtime."), Hr);
	}
}

TSharedPtr<FWebView2Window> FWebView2Manager::CreateWebView(
	HWND ParentWindow,
	const FGuid& InstanceId,
	const FString& InitialUrl,
	const FColor& BackgroundColor,
	bool bEnableTransparencyHitTest,
	bool bAttachToHostVisual,
	HWND VisualHostWindow,
	bool bAllowNonInteractiveElementPassthrough)
{
	Initialize();
	TSharedPtr<FWebView2Window> WebViewWindow = MakeShared<FWebView2Window>(ParentWindow, InstanceId, InitialUrl, BackgroundColor, bEnableTransparencyHitTest, bAttachToHostVisual, VisualHostWindow, bAllowNonInteractiveElementPassthrough);
	WebViewWindow->Initialize();
	return WebViewWindow;
}

bool FWebView2Manager::HasActiveWebViews() const
{
	return FWebView2Window::GetActiveInstanceCount() > 0;
}

TSharedPtr<FWebView2CompositionHost> FWebView2Manager::GetOrCreateCompositionHost(HWND WindowHandle)
{
	if (const TWeakPtr<FWebView2CompositionHost>* Existing = CompositionHosts.Find(WindowHandle))
	{
		if (const TSharedPtr<FWebView2CompositionHost> Host = Existing->Pin())
		{
			return Host;
		}
	}

	TSharedPtr<FWebView2CompositionHost> Host = MakeShared<FWebView2CompositionHost>(
		WindowHandle,
		DispatcherQueueController);
	Host->Initialize();
	CompositionHosts.Add(WindowHandle, Host);
	return Host;
}

bool FWebView2Manager::HandleWindowMessage(HWND WindowHandle, UINT Message, WPARAM WParam, LPARAM LParam)
{
	// Route messages only to the CompositionHost owned by this host HWND.
	bool bHandled = false;
	for (TPair<HWND, TWeakPtr<FWebView2CompositionHost>>& Pair : CompositionHosts)
	{
		if (Pair.Key != WindowHandle)
		{
			continue;
		}

		if (const TSharedPtr<FWebView2CompositionHost> Host = Pair.Value.Pin())
		{
			bHandled |= Host->HandleMouseMessage(Message, WParam, LParam);
		}
	}

	return bHandled;
}

void FWebView2Manager::OnHostWindowClosed(HWND WindowHandle)
{
	if (const TWeakPtr<FWebView2CompositionHost>* Existing = CompositionHosts.Find(WindowHandle))
	{
		if (const TSharedPtr<FWebView2CompositionHost> Host = Existing->Pin())
		{
			Host->Destroy();
		}
	}

	CompositionHosts.Remove(WindowHandle);
}

bool FWebView2Manager::EnsureDispatcherQueue()
{
	if (DispatcherQueueController != nullptr)
	{
		return true;
	}

	// CreateDispatcherQueueController from CoreMessaging.dll is loaded on demand at runtime.
	static decltype(::CreateDispatcherQueueController)* CreateDispatcherQueueControllerFunc = nullptr;
	if (!CreateDispatcherQueueControllerFunc)
	{
		if (HMODULE ModuleHandle = ::LoadLibraryEx(L"CoreMessaging.dll", nullptr, 0))
		{
			CreateDispatcherQueueControllerFunc =
				reinterpret_cast<decltype(::CreateDispatcherQueueController)*>(
					::GetProcAddress(ModuleHandle, "CreateDispatcherQueueController"));
		}
	}

	if (!CreateDispatcherQueueControllerFunc)
	{
		UE_LOG(LogWebView2Utils, Error, TEXT("Failed to create DispatcherQueueController. The system may not support WinComp."));
		return false;
	}

	winrt::Windows::System::DispatcherQueueController NewController{nullptr};
	DispatcherQueueOptions Options{
		sizeof(DispatcherQueueOptions),
		DQTYPE_THREAD_CURRENT,
		DQTAT_COM_STA};

	const HRESULT Hr = CreateDispatcherQueueControllerFunc(
		Options,
		reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(NewController)));

	if (FAILED(Hr))
	{
		UE_LOG(LogWebView2Utils, Error, TEXT("Failed to create DispatcherQueueController. HRESULT=0x%08x"), Hr);
		return false;
	}

	DispatcherQueueController = NewController;
	return true;
}
