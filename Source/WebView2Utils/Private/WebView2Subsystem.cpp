// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#include "WebView2Subsystem.h"

#include "WebView2Log.h"
#include "WebView2Manager.h"
#include "WebView2NativeFocus.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsApplication.h"
#include "Windows/WindowsHWrapper.h"
#endif

#if WITH_EDITOR
#include "Editor.h"
#endif

namespace
{
	class FCBWebView2WindowsMessageHandler : public IWindowsMessageHandler
	{
	public:
		virtual bool ProcessMessage(HWND Hwnd, uint32 Msg, WPARAM WParam, LPARAM LParam, int32& OutResult) override
		{
			// Do not intercept host-window keyboard messages unless the WebView actually owns keyboard focus.
			UWebView2Subsystem* Subsystem = UWebView2Subsystem::Get();
			if ((Msg == WM_KEYDOWN || Msg == WM_KEYUP || Msg == WM_SYSKEYDOWN || Msg == WM_SYSKEYUP) &&
				(!Subsystem || !Subsystem->IsWebViewFocused()))
			{
				return false;
			}

			return FWebView2Manager::Get().HandleWindowMessage(Hwnd, Msg, WParam, LParam);
		}
	};

	FCBWebView2WindowsMessageHandler GWebView2MessageHandler;
}

void UWebView2Subsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Initialize the global manager first, then register the Windows message handler.
	FWebView2Manager::Get().Initialize();

#if PLATFORM_WINDOWS
	if (FSlateApplication::IsInitialized())
	{
		TSharedPtr<FWindowsApplication> WindowsApplication = StaticCastSharedPtr<FWindowsApplication>(FSlateApplication::Get().GetPlatformApplication());
		if (WindowsApplication.IsValid())
		{
			WindowsApplication->AddMessageHandler(GWebView2MessageHandler);
		}
	}
#endif

#if WITH_EDITOR
	FEditorDelegates::EndPIE.AddUObject(this, &UWebView2Subsystem::OnEndPIE);
#endif
}

void UWebView2Subsystem::Deinitialize()
{
	// Remove editor and window callbacks before shutting down the manager.
#if WITH_EDITOR
	FEditorDelegates::EndPIE.RemoveAll(this);
#endif

#if PLATFORM_WINDOWS
	if (FSlateApplication::IsInitialized())
	{
		TSharedPtr<FWindowsApplication> WindowsApplication = StaticCastSharedPtr<FWindowsApplication>(FSlateApplication::Get().GetPlatformApplication());
		if (WindowsApplication.IsValid())
		{
			WindowsApplication->RemoveMessageHandler(GWebView2MessageHandler);
		}
	}
#endif

	FWebView2Manager::Get().Shutdown();
	Super::Deinitialize();
}

void UWebView2Subsystem::Tick(float DeltaTime)
{
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}

	// Focus restoration only matters while a WebView exists; skip the per-frame Win32 queries otherwise.
	if (!FWebView2Manager::Get().HasActiveWebViews())
	{
		return;
	}

#if PLATFORM_WINDOWS
	if (!bIsWebViewFocused)
	{
		// When the WebView is not focused, return focus to the host HWND so input does not remain on a hidden child path.
		if (TSharedPtr<SWindow> CurrentWindow = FSlateApplication::Get().GetActiveTopLevelWindow())
		{
			if (void* NativeHandle = CurrentWindow->GetNativeWindow()->GetOSWindowHandle())
			{
				HWND NativeWindow = static_cast<HWND>(NativeHandle);
				const HWND CurrentFocus = ::GetFocus();
				const bool bWebViewOwnsFocus =
					CBWebView2NativeFocus::IsWebView2OwnedWindow(CurrentFocus, NativeWindow);
				// Preserve unrelated native children, but reclaim Chromium even though it is also parented
				// under the Unreal HWND. IsChild() alone was the reason keyboard focus could remain trapped.
				if (::GetForegroundWindow() == NativeWindow && CurrentFocus != NativeWindow &&
					(bWebViewOwnsFocus || CurrentFocus == nullptr || !::IsChild(NativeWindow, CurrentFocus)))
				{
					::SetFocus(NativeWindow);
				}
			}
		}
	}
#endif
}

bool UWebView2Subsystem::IsTickable() const
{
	return true;
}

TStatId UWebView2Subsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UWebView2Subsystem, STATGROUP_Tickables);
}

UWebView2Subsystem* UWebView2Subsystem::Get()
{
	return GEngine ? GEngine->GetEngineSubsystem<UWebView2Subsystem>() : nullptr;
}

void UWebView2Subsystem::SetWebViewFocused(bool bInFocused)
{
	// This only tracks actual input focus and does not participate in transparency hit-test decisions.
	bIsWebViewFocused = bInFocused;
}

bool UWebView2Subsystem::IsWebViewFocused() const
{
	return bIsWebViewFocused;
}

void UWebView2Subsystem::OnEndPIE(bool bIsSimulating)
{
#if PLATFORM_WINDOWS
	if (GEngine && GEngine->GameViewport)
	{
		if (TSharedPtr<SWindow> Window = GEngine->GameViewport->GetWindow())
		{
			if (void* NativeHandle = Window->GetNativeWindow()->GetOSWindowHandle())
			{
				FWebView2Manager::Get().OnHostWindowClosed(static_cast<HWND>(NativeHandle));
			}
		}
	}
#endif
}
