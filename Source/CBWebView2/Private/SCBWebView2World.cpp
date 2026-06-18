// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#include "SCBWebView2World.h"

#include "CBWebView2.h"
#include "WebView2InternalMessage.h"
#include "WebView2Settings.h"
#include "WebView2Subsystem.h"

#include "Framework/Application/SlateApplication.h"
#include "Engine/Texture2D.h"
#include "HAL/PlatformTime.h"
#include "Input/Reply.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"

#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <winuser.h>
#include "Windows/HideWindowsPlatformTypes.h"

namespace
{
	bool IsHiddenVisibility(EVisibility InVisibility)
	{
		return InVisibility == EVisibility::Collapsed || InVisibility == EVisibility::Hidden;
	}

	FVector2D GetCurrentDesktopCursorPosition()
	{
#if PLATFORM_WINDOWS
		POINT CursorPoint;
		if (::GetCursorPos(&CursorPoint))
		{
			return FVector2D(static_cast<double>(CursorPoint.x), static_cast<double>(CursorPoint.y));
		}
#endif

		return FSlateApplication::IsInitialized()
			? FSlateApplication::Get().GetCursorPos()
			: FVector2D::ZeroVector;
	}
}

void SCBWebView2World::Construct(const FArguments& InArgs)
{
	InitialUrl = InArgs._InitialUrl;
	BackgroundColor = InArgs._BackgroundColor;
	bEnableTransparencyHitTest = InArgs._bEnableTransparencyHitTest;
	RefreshRate = FMath::Max(0.0f, InArgs._RefreshRate);
	ParentWindow = InArgs._ParentWindow;
	InstanceId = FGuid::NewGuid();
	OnMessageReceived = InArgs._OnMessageReceived;
	OnNavigationCompleted = InArgs._OnNavigationCompleted;
	OnNavigationStarting = InArgs._OnNavigationStarting;
	OnNewWindowRequested = InArgs._OnNewWindowRequested;
	OnCursorChanged = InArgs._OnCursorChanged;
	OnDownloadStarting = InArgs._OnDownloadStarting;
	OnDownloadUpdated = InArgs._OnDownloadUpdated;
	OnPrintToPdfCompleted = InArgs._OnPrintToPdfCompleted;
	OnDocumentTitleChanged = InArgs._OnDocumentTitleChanged;
	OnSourceChanged = InArgs._OnSourceChanged;
	OnCanGoBackChanged = InArgs._OnCanGoBackChanged;
	OnCanGoForwardChanged = InArgs._OnCanGoForwardChanged;
	OnInputActivationRequested = InArgs._OnInputActivationRequested;
	OnMonitoredEvent = InArgs._OnMonitoredEvent;
	OnFrameUpdated = InArgs._OnFrameUpdated;

	PreviewBrush.DrawAs = ESlateBrushDrawType::Image;
	PreviewBrush.TintColor = FLinearColor::White;
	PreviewBrush.ImageSize = InitialDrawSize;
	PreviewBrush.SetResourceObject(nullptr);

	ChildSlot
	[
		SAssignNew(PreviewImage, SImage)
		.Image(&PreviewBrush)
	];

	BrowserPixelSize = FIntPoint(
		FMath::Max(FMath::RoundToInt(InitialDrawSize.X), 1),
		FMath::Max(FMath::RoundToInt(InitialDrawSize.Y), 1));

	void* FocusParentWindowHandle = nullptr;
	if (TSharedPtr<SWindow> PinnedParentWindow = ParentWindow.Pin())
	{
		if (PinnedParentWindow->GetNativeWindow().IsValid())
		{
			FocusParentWindowHandle = PinnedParentWindow->GetNativeWindow()->GetOSWindowHandle();
		}
	}

	InitializeTextureSource(FocusParentWindowHandle);

	// Slate does not tick unpainted widgets, so a core ticker watches for paint inactivity and suspends
	// the browser process while this widget is hidden, collapsed, or its window is minimized.
	const UWebView2Settings* Settings = UWebView2Settings::Get();
	if (Settings && Settings->Performance.bSuspendWhenHidden)
	{
		TWeakPtr<SCBWebView2World> WeakSelf = SharedThis(this);
		SuspendTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([WeakSelf](float) -> bool
			{
				if (TSharedPtr<SCBWebView2World> Self = WeakSelf.Pin())
				{
					Self->UpdateSuspendOnInactivity();
					return true;
				}
				return false;
			}),
			1.0f);
	}
}

SCBWebView2World::~SCBWebView2World()
{
	BeginDestroy();
}

void SCBWebView2World::BeginDestroy()
{
	if (SuspendTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(SuspendTickerHandle);
		SuspendTickerHandle.Reset();
	}

	if (ContinuousTextureSource.IsValid())
	{
		ContinuousTextureSource->ReleaseInputScreenAnchor();
		ContinuousTextureSource->Shutdown();
		ContinuousTextureSource.Reset();
	}

	WebViewWindow.Reset();
	PresentedTexture = nullptr;
	PreviewBrush.SetResourceObject(nullptr);
}

void SCBWebView2World::ExecuteScript(const FString& Script, FOnCBWebView2WorldScriptCallback Callback) const
{
	if (WebViewWindow.IsValid())
	{
		WebViewWindow->ExecuteScript(Script, [Callback](const FString& Result)
		{
			if (Callback.IsBound())
			{
				Callback.Execute(Result);
			}
		});
	}
}

void SCBWebView2World::LoadURL(const FString& InUrl)
{
	InitialUrl = InUrl;
	if (WebViewWindow.IsValid())
	{
		WebViewWindow->LoadURL(InUrl);
		RequestRefresh();
	}
}

void SCBWebView2World::SetTransparencyHitTestEnabled(bool bInTransparencyHitTestEnabled)
{
	if (bEnableTransparencyHitTest == bInTransparencyHitTestEnabled)
	{
		return;
	}

	bEnableTransparencyHitTest = bInTransparencyHitTestEnabled;
	if (WebViewWindow.IsValid())
	{
		WebViewWindow->SetTransparencyHitTestEnabled(bEnableTransparencyHitTest);
		if (!bEnableTransparencyHitTest)
		{
			WebViewWindow->SetHitTestEnabled(true);
		}
	}

	UpdateTransparentHitTestVisibility();
	RequestRefresh();
}

void SCBWebView2World::GoForward() const
{
	if (WebViewWindow.IsValid())
	{
		WebViewWindow->GoForward();
	}
}

void SCBWebView2World::GoBack() const
{
	if (WebViewWindow.IsValid())
	{
		WebViewWindow->GoBack();
	}
}

void SCBWebView2World::Reload() const
{
	if (WebViewWindow.IsValid())
	{
		WebViewWindow->Reload();
	}
}

void SCBWebView2World::Stop() const
{
	if (WebViewWindow.IsValid())
	{
		WebViewWindow->Stop();
	}
}

void SCBWebView2World::OpenDevToolsWindow() const
{
	if (WebViewWindow.IsValid())
	{
		WebViewWindow->OpenDevToolsWindow();
	}
}

void SCBWebView2World::SetBackgroundColor(const FColor& InBackgroundColor)
{
	BackgroundColor = InBackgroundColor;
	if (WebViewWindow.IsValid())
	{
		WebViewWindow->SetBackgroundColor(InBackgroundColor);
		RequestRefresh();
	}
}

void SCBWebView2World::SetRefreshRate(float InRefreshRate)
{
	RefreshRate = FMath::Max(0.0f, InRefreshRate);
}

void SCBWebView2World::RequestRefresh()
{
	if (ContinuousTextureSource.IsValid())
	{
		ContinuousTextureSource->RequestImmediateUpload();
	}
}

FVector2D SCBWebView2World::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	if (BrowserPixelSize.X > 0 && BrowserPixelSize.Y > 0)
	{
		return FVector2D(BrowserPixelSize);
	}

	return InitialDrawSize;
}

void SCBWebView2World::Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	CachedTickGeometry = AllottedGeometry;
	bHasCachedTickGeometry = true;

	if (!WebViewWindow.IsValid() || !ContinuousTextureSource.IsValid())
	{
		return;
	}

	LastWidgetActiveTime = FPlatformTime::Seconds();
	if (bWebViewSuspendedForInactivity)
	{
		// The widget is being painted again: wake the browser and refresh the texture immediately.
		bWebViewSuspendedForInactivity = false;
		WebViewWindow->SetVisible(ESlateVisibility::Visible);
		ContinuousTextureSource->RequestImmediateUpload();
	}

	PollTransparentHitTestHover(AllottedGeometry);
	UpdateBrowserBounds(AllottedGeometry);
	TickContinuousOutput(InCurrentTime);
}

void SCBWebView2World::UpdateSuspendOnInactivity()
{
	if (bWebViewSuspendedForInactivity || !WebViewWindow.IsValid())
	{
		return;
	}

	const UWebView2Settings* Settings = UWebView2Settings::Get();
	if (!Settings || !Settings->Performance.bSuspendWhenHidden)
	{
		return;
	}

	if (LastWidgetActiveTime <= 0.0 ||
		FPlatformTime::Seconds() - LastWidgetActiveTime < Settings->Performance.SuspendDelaySeconds)
	{
		return;
	}

	bWebViewSuspendedForInactivity = true;
	WebViewWindow->SetVisible(ESlateVisibility::Hidden);
	WebViewWindow->TrySuspend();
}

FReply SCBWebView2World::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!WebViewWindow.IsValid())
	{
		return FReply::Unhandled();
	}

	const FVector2D ScreenSpacePosition = MouseEvent.GetScreenSpacePosition();
	const FVector2D LocalPoint = GetLocalWebViewPoint(MyGeometry, ScreenSpacePosition);
	// TextureAlpha mode: refresh the passthrough state synchronously so the very first click is routed
	// correctly, with no JS round-trip staleness.
	UpdateHoverStateFromTextureAlpha(LocalPoint);
	const bool bBlockedForTransparentHitTest = ShouldBlockInputForTransparentHitTest(false);
	if (bBlockedForTransparentHitTest)
	{
		WebViewWindow->SendMouseMove(LocalPoint);
		ReleaseWebViewInputFocusForTransparentPassthrough();
		return FReply::Unhandled();
	}

	const bool bShouldOwnKeyboardFocus = ShouldForwardKeyboardInput();
	if (bShouldOwnKeyboardFocus)
	{
		AcquireWebViewInputFocus(EFocusCause::Mouse);
	}

	if (bIsEditableElementFocused && ContinuousTextureSource.IsValid())
	{
		UpdateInputScreenAnchorFromPointerEvent(MouseEvent, LocalPoint);
	}
	bIsMouseButtonHeld = true;
	WebViewWindow->SendMouseButton(LocalPoint, MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton, true);

	if (bShouldOwnKeyboardFocus)
	{
		if (UWebView2Subsystem* Subsystem = UWebView2Subsystem::Get())
		{
			Subsystem->SetWebViewFocused(true);
		}
	}

	FReply Reply = FReply::Handled().CaptureMouse(SharedThis(this));
	if (bShouldOwnKeyboardFocus)
	{
		Reply.SetUserFocus(SharedThis(this), EFocusCause::Mouse);
	}
	return Reply;
}

FReply SCBWebView2World::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!WebViewWindow.IsValid())
	{
		return FReply::Unhandled();
	}

	const FVector2D ScreenSpacePosition = MouseEvent.GetScreenSpacePosition();
	const FVector2D LocalPoint = GetLocalWebViewPoint(MyGeometry, ScreenSpacePosition);
	// TextureAlpha mode: refresh the passthrough state synchronously so the very first click is routed
	// correctly, with no JS round-trip staleness.
	UpdateHoverStateFromTextureAlpha(LocalPoint);
	const bool bBlockedForTransparentHitTest = ShouldBlockInputForTransparentHitTest(false);
	if (bBlockedForTransparentHitTest)
	{
		WebViewWindow->SendMouseMove(LocalPoint);
		ReleaseWebViewInputFocusForTransparentPassthrough();
		return FReply::Unhandled();
	}

	const bool bShouldOwnKeyboardFocus = ShouldForwardKeyboardInput();
	if (bShouldOwnKeyboardFocus)
	{
		AcquireWebViewInputFocus(EFocusCause::Mouse);
	}

	if (bIsEditableElementFocused && ContinuousTextureSource.IsValid())
	{
		UpdateInputScreenAnchorFromPointerEvent(MouseEvent, LocalPoint);
	}
	bIsMouseButtonHeld = true;
	WebViewWindow->SendMouseDoubleClick(LocalPoint, MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton);

	if (bShouldOwnKeyboardFocus)
	{
		if (UWebView2Subsystem* Subsystem = UWebView2Subsystem::Get())
		{
			Subsystem->SetWebViewFocused(true);
		}
	}

	FReply Reply = FReply::Handled().CaptureMouse(SharedThis(this));
	if (bShouldOwnKeyboardFocus)
	{
		Reply.SetUserFocus(SharedThis(this), EFocusCause::Mouse);
	}
	return Reply;
}

FReply SCBWebView2World::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	UpdateHoverStateFromTextureAlpha(GetLocalWebViewPoint(MyGeometry, MouseEvent.GetScreenSpacePosition()));
	if (ShouldBlockInputForTransparentHitTest(true))
	{
		ReleaseWebViewInputFocusForTransparentPassthrough();
		return FReply::Unhandled();
	}

	bIsMouseButtonHeld = false;

	if (!WebViewWindow.IsValid())
	{
		return FReply::Unhandled();
	}

	const bool bShouldOwnKeyboardFocus = ShouldForwardKeyboardInput();

	const FVector2D LocalPoint = GetLocalWebViewPoint(MyGeometry, MouseEvent.GetScreenSpacePosition());
	if (bIsEditableElementFocused && ContinuousTextureSource.IsValid())
	{
		UpdateInputScreenAnchorFromPointerEvent(MouseEvent, LocalPoint);
	}
	WebViewWindow->SendMouseButton(LocalPoint, MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton, false);
	ReleaseWebViewInputFocusForTransparentPassthrough(true);
	if (!bShouldOwnKeyboardFocus && !bEnableTransparencyHitTest)
	{
		if (UWebView2Subsystem* Subsystem = UWebView2Subsystem::Get())
		{
			Subsystem->SetWebViewFocused(false);
		}
	}

	return HasMouseCapture() ? FReply::Handled().ReleaseMouseCapture() : FReply::Handled();
}

FReply SCBWebView2World::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!WebViewWindow.IsValid())
	{
		return FReply::Unhandled();
	}

	const FVector2D LocalPoint = GetLocalWebViewPoint(MyGeometry, MouseEvent.GetScreenSpacePosition());
	WebViewWindow->SendMouseMove(
		LocalPoint,
		MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton),
		MouseEvent.IsMouseButtonDown(EKeys::RightMouseButton),
		MouseEvent.IsMouseButtonDown(EKeys::MiddleMouseButton));
	// Remember the forwarded point so the per-frame hover poll does not send a duplicate synthetic move.
	LastForwardedHoverLocalPoint = LocalPoint;
	UpdateHoverStateFromTextureAlpha(LocalPoint);

	if (HasMouseCapture() || (bEnableTransparencyHitTest && bIsHoveringInteractiveContent))
	{
		return FReply::Handled();
	}

	ReleaseWebViewInputFocusForTransparentPassthrough();
	return FReply::Unhandled();
}

FReply SCBWebView2World::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	UpdateHoverStateFromTextureAlpha(GetLocalWebViewPoint(MyGeometry, MouseEvent.GetScreenSpacePosition()));
	if (ShouldBlockInputForTransparentHitTest(false))
	{
		ReleaseWebViewInputFocusForTransparentPassthrough();
		return FReply::Unhandled();
	}

	if (!WebViewWindow.IsValid())
	{
		return FReply::Unhandled();
	}

	const FVector2D LocalPoint = GetLocalWebViewPoint(MyGeometry, MouseEvent.GetScreenSpacePosition());
	WebViewWindow->SendMouseWheel(LocalPoint, MouseEvent.GetWheelDelta());
	RequestRefresh();
	return FReply::Handled();
}

FReply SCBWebView2World::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	if (!ShouldForwardKeyboardInput())
	{
		return FReply::Unhandled();
	}

	AcquireWebViewInputFocus(EFocusCause::Mouse);

	return FReply::Handled();
}

void SCBWebView2World::OnFocusLost(const FFocusEvent& InFocusEvent)
{
	ReleaseWebViewInputFocus();
	SCompoundWidget::OnFocusLost(InFocusEvent);
}

bool SCBWebView2World::SupportsKeyboardFocus() const
{
	return true;
}

FReply SCBWebView2World::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (!ShouldForwardKeyboardInput())
	{
		return FReply::Unhandled();
	}

	if (!WebViewWindow.IsValid())
	{
		return FReply::Unhandled();
	}

	// Re-entry guard: SendKeyboardMessage uses ::SendMessage which pumps the host HWND's message queue.
	// FWebView2Manager's IWindowsMessageHandler can route the same WM_KEYDOWN back through Slate to this handler,
	// causing unbounded recursion (e.g. pressing the Windows key in fullscreen overflows the stack).
	static thread_local bool bIsForwardingKeyDown = false;
	if (bIsForwardingKeyDown)
	{
		return FReply::Handled();
	}
	TGuardValue<bool> ReentryGuard(bIsForwardingKeyDown, true);

	const uint32 KeyCode = InKeyEvent.GetKeyCode();
	const uint32 ScanCode = MapVirtualKey(KeyCode, 0);
	uint64 LParam = 1 | (static_cast<uint64>(ScanCode) << 16);
	if (InKeyEvent.IsRepeat())
	{
		LParam |= 0x40000000;
	}

	WebViewWindow->SendKeyboardMessage(WM_KEYDOWN, KeyCode, static_cast<int64>(LParam));
	if (ContinuousTextureSource.IsValid())
	{
		ContinuousTextureSource->RefreshImeWindowPosition();
	}
	RequestRefresh();
	return FReply::Handled();
}

FReply SCBWebView2World::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (!ShouldForwardKeyboardInput())
	{
		return FReply::Unhandled();
	}

	if (!WebViewWindow.IsValid())
	{
		return FReply::Unhandled();
	}

	static thread_local bool bIsForwardingKeyUp = false;
	if (bIsForwardingKeyUp)
	{
		return FReply::Handled();
	}
	TGuardValue<bool> ReentryGuard(bIsForwardingKeyUp, true);

	const uint32 KeyCode = InKeyEvent.GetKeyCode();
	const uint32 ScanCode = MapVirtualKey(KeyCode, 0);
	const uint64 LParam = 1 | (static_cast<uint64>(ScanCode) << 16) | 0xC0000000;
	WebViewWindow->SendKeyboardMessage(WM_KEYUP, KeyCode, static_cast<int64>(LParam));
	if (ContinuousTextureSource.IsValid())
	{
		ContinuousTextureSource->RefreshImeWindowPosition();
	}
	RequestRefresh();
	return FReply::Handled();
}

FReply SCBWebView2World::OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent)
{
	if (!ShouldForwardKeyboardInput())
	{
		return FReply::Unhandled();
	}

	if (!WebViewWindow.IsValid())
	{
		return FReply::Unhandled();
	}

	static thread_local bool bIsForwardingKeyChar = false;
	if (bIsForwardingKeyChar)
	{
		return FReply::Handled();
	}
	TGuardValue<bool> ReentryGuard(bIsForwardingKeyChar, true);

	WebViewWindow->SendKeyboardMessage(WM_CHAR, InCharacterEvent.GetCharacter(), 1);
	if (ContinuousTextureSource.IsValid())
	{
		ContinuousTextureSource->RefreshImeWindowPosition();
	}
	RequestRefresh();
	return FReply::Handled();
}

void SCBWebView2World::BindWebViewEvents()
{
	if (!WebViewWindow.IsValid())
	{
		return;
	}

	WebViewWindow->OnMessageReceived.BindSP(this, &SCBWebView2World::HandleMessageFromWeb);
	WebViewWindow->OnNavigationCompleted.BindLambda([this](bool bSuccess)
	{
		if (bSuccess)
		{
			RequestRefresh();
		}
		OnNavigationCompleted.ExecuteIfBound(bSuccess);
	});

	WebViewWindow->OnNavigationStarting.BindLambda([this](const FString& Url)
	{
		RequestRefresh();
		OnNavigationStarting.ExecuteIfBound(Url);
	});

	WebViewWindow->OnNewWindowRequested.BindLambda([this](const FString& Url)
	{
		OnNewWindowRequested.ExecuteIfBound(Url);
	});

	WebViewWindow->OnDownloadStarting.BindLambda([this](const FWebView2DownloadInfo& DownloadInfo)
	{
		OnDownloadStarting.ExecuteIfBound(DownloadInfo);
	});

	WebViewWindow->OnDownloadUpdated.BindLambda([this](const FWebView2DownloadInfo& DownloadInfo)
	{
		OnDownloadUpdated.ExecuteIfBound(DownloadInfo);
	});

	WebViewWindow->OnPrintToPdfCompleted.BindLambda([this](bool bSuccess, const FString& OutputPath)
	{
		OnPrintToPdfCompleted.ExecuteIfBound(bSuccess, OutputPath);
	});

	WebViewWindow->OnDocumentTitleChanged.BindLambda([this](const FString& Title)
	{
		OnDocumentTitleChanged.ExecuteIfBound(Title);
	});

	WebViewWindow->OnSourceChanged.BindLambda([this](const FString& Url)
	{
		OnSourceChanged.ExecuteIfBound(Url);
	});

	WebViewWindow->OnCanGoBackChanged.BindLambda([this](bool bValue)
	{
		OnCanGoBackChanged.ExecuteIfBound(bValue);
	});

	WebViewWindow->OnCanGoForwardChanged.BindLambda([this](bool bValue)
	{
		OnCanGoForwardChanged.ExecuteIfBound(bValue);
	});

	WebViewWindow->OnMonitoredEvent.BindLambda([this](const FCBWebView2MonitoredEvent& EventInfo)
	{
		OnMonitoredEvent.ExecuteIfBound(EventInfo);
	});

	WebViewWindow->OnInputActivationRequested.BindLambda([this]()
	{
		OnInputActivationRequested.ExecuteIfBound();

		if (ShouldForwardKeyboardInput())
		{
			AcquireWebViewInputFocus(EFocusCause::Mouse);
		}
	});

	WebViewWindow->OnCursorChanged.BindLambda([this](EMouseCursor::Type CursorType)
	{
		OnCursorChanged.ExecuteIfBound(CursorType);
		if (bEnableTransparencyHitTest)
		{
			SetCursor(CursorType);
		}
	});
	}

void SCBWebView2World::AcquireWebViewInputFocus(EFocusCause FocusCause)
{
	if (!WebViewWindow.IsValid())
	{
		return;
	}

	// Prevent recursive re-entry: SetKeyboardFocus synchronously dispatches OnFocusLost / OnFocusReceived,
	// and OnFocusReceived calls back into this function. Focus switching between multiple SCBWebView2World instances
	// can otherwise recurse indefinitely and overflow the stack. Asynchronous paths such as OnInputActivationRequested
	// can also re-enter through SetKeyboardFocus.
	static thread_local bool bIsAcquiringFocus = false;
	if (bIsAcquiringFocus)
	{
		return;
	}
	TGuardValue<bool> ReentryGuard(bIsAcquiringFocus, true);

	// Switch Slate keyboard focus first. This synchronously triggers OnFocusLost on the previously focused
	// SCBWebView2World, which calls ReleaseWebViewInputFocus(), clears global subsystem focus state, and
	// calls MoveFocus(false) on the previous WebView2 instance.
	// Only after that should this instance set its own WebView2 focus and subsystem state, otherwise the old
	// instance's release path can overwrite it and prevent inputs in another widget from gaining focus.
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication& SlateApplication = FSlateApplication::Get();
		if (SlateApplication.GetKeyboardFocusedWidget().Get() != this)
		{
			SlateApplication.SetKeyboardFocus(SharedThis(this), FocusCause);
		}
	}

	WebViewWindow->MoveFocus(true);
	if (UWebView2Subsystem* Subsystem = UWebView2Subsystem::Get())
	{
		Subsystem->SetWebViewFocused(true);
	}

	if (ContinuousTextureSource.IsValid())
	{
		ContinuousTextureSource->RefreshImeWindowPosition();
	}
}
void SCBWebView2World::HandleMessageFromWeb(const FString& Message)
{
	static const FString ClickablePrefix(TEXT("IsClickable:"));
	static const FString EditableFocusPrefix(TEXT("EditableFocus:"));

	FString RoutedMessage = Message;
	FCBWebView2InternalMessage InternalMessage;
	const bool bHasInternalJsonMessage = FCBWebView2InternalMessageParser::TryParseJson(Message, InternalMessage);
	const bool bIsInternalJsonMessage = bHasInternalJsonMessage && InternalMessage.ToLegacyMessage(RoutedMessage);
	const bool bLooksLikeInternalMessage = bHasInternalJsonMessage || bIsInternalJsonMessage || Message.Contains(TEXT("__cbwebview2")) || RoutedMessage.StartsWith(ClickablePrefix) || RoutedMessage.Contains(ClickablePrefix) || RoutedMessage.StartsWith(EditableFocusPrefix) || RoutedMessage.Contains(EditableFocusPrefix);

	if (bHasInternalJsonMessage && InternalMessage.IsImeCaret())
	{
		UpdateImeCaretFromBrowserClientPoint(InternalMessage.Point, InternalMessage.ViewportSize, InternalMessage.Height);
	}

	bool bNewEditableFocusState = false;
	if (FCBWebView2InternalMessageParser::TryReadLegacyBoolMessage(RoutedMessage, *EditableFocusPrefix, bNewEditableFocusState))
	{
		if (bIsEditableElementFocused != bNewEditableFocusState)
		{
			bIsEditableElementFocused = bNewEditableFocusState;

			if (bNewEditableFocusState)
			{
				AcquireWebViewInputFocus(EFocusCause::Mouse);
			}
			else
			{
				ReleaseWebViewInputFocus();
			}
		}
	}

	bool bNewHoverState = false;
	if (!bUseTextureAlphaHitTest && FCBWebView2InternalMessageParser::TryReadLegacyBoolMessage(RoutedMessage, *ClickablePrefix, bNewHoverState))
	{
		bIsHoveringInteractiveContent = bNewHoverState;

		if (WebViewWindow.IsValid())
		{
			WebViewWindow->SetHitTestEnabled(bNewHoverState);
		}

		if (bNewHoverState)
		{
			bLastInsideTransparencyHitTestWasInteractive = true;
		}

		UpdateTransparentHitTestVisibility();

		if (!bNewHoverState && HasMouseCapture() && !bIsMouseButtonHeld && FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().ReleaseAllPointerCapture();
		}

		if (!bNewHoverState)
		{
			ReleaseWebViewInputFocusForTransparentPassthrough();
		}
	}

	const bool bIsImeCaretMessage = bHasInternalJsonMessage && InternalMessage.IsImeCaret();
	// The `__cbwebview2` protocol and its legacy prefixes are for internal widget bridging only and should not be forwarded to Blueprint.
	if (!bIsImeCaretMessage && !bLooksLikeInternalMessage)
	{
		OnMessageReceived.ExecuteIfBound(Message);
	}
}

bool SCBWebView2World::ShouldForwardKeyboardInput() const
{
	if (bEnableTransparencyHitTest)
	{
		return bIsHoveringInteractiveContent || bIsEditableElementFocused;
	}

	return bIsEditableElementFocused;
}

bool SCBWebView2World::ShouldBlockInputForTransparentHitTest(bool bAllowCapturedInput) const
{
	if (!bEnableTransparencyHitTest || bIsHoveringInteractiveContent)
	{
		return false;
	}

	if (bAllowCapturedInput && (HasMouseCapture() || bIsMouseButtonHeld))
	{
		return false;
	}

	return true;
}

void SCBWebView2World::ReleaseWebViewInputFocusForTransparentPassthrough(bool bAllowCapturedInput)
{
	if (!bEnableTransparencyHitTest || bIsHoveringInteractiveContent)
	{
		return;
	}

	if (!bAllowCapturedInput && (HasMouseCapture() || bIsMouseButtonHeld))
	{
		return;
	}

	bIsEditableElementFocused = false;
	ReleaseWebViewInputFocus();
}

void SCBWebView2World::UpdateTransparentHitTestVisibility()
{
	if (!bEnableTransparencyHitTest)
	{
		SetVisibility(EVisibility::Visible);
		return;
	}

	if (HasMouseCapture() || bIsMouseButtonHeld || bIsHoveringInteractiveContent || ShouldKeepVisibleForTransparencyProbe())
	{
		SetVisibility(EVisibility::Visible);
		return;
	}

	SetVisibility(EVisibility::HitTestInvisible);
}

bool SCBWebView2World::ShouldKeepVisibleForTransparencyProbe() const
{
	return bWasCursorInsideTransparencyHitTest &&
		bLastInsideTransparencyHitTestWasInteractive &&
		FPlatformTime::Seconds() < TransparencyProbeVisibleUntilTime;
}

void SCBWebView2World::PollTransparentHitTestHover(const FGeometry& AllottedGeometry)
{
	if (!bEnableTransparencyHitTest || !WebViewWindow.IsValid() || !FSlateApplication::IsInitialized())
	{
		return;
	}

	if (HasMouseCapture() || bIsMouseButtonHeld)
	{
		return;
	}

	const FVector2D CursorPosition = GetCurrentDesktopCursorPosition();
	const bool bIsCursorInside = AllottedGeometry.IsUnderLocation(CursorPosition);
	if (!bIsCursorInside)
	{
		bWasCursorInsideTransparencyHitTest = false;
		TransparencyProbeVisibleUntilTime = 0.0;
		LastForwardedHoverLocalPoint.Reset();
		// Cursor is not over the world widget at all. Whatever stale "hovering interactive content" state we had
		// must be cleared, otherwise keyboard input will keep being forwarded to the WebView even when the user
		// is interacting with the rest of the game UI.
		if (bIsHoveringInteractiveContent)
		{
			bIsHoveringInteractiveContent = false;
			if (WebViewWindow.IsValid())
			{
				WebViewWindow->SetHitTestEnabled(false);
			}
			UpdateTransparentHitTestVisibility();
			ReleaseWebViewInputFocusForTransparentPassthrough();
		}
		return;
	}

	if (!bWasCursorInsideTransparencyHitTest && bLastInsideTransparencyHitTestWasInteractive && !bUseTextureAlphaHitTest)
	{
		// If the application lost focus while the cursor was outside, Slate may not deliver a move event before
		// the next click. Keep the widget visible for a few frames so the page can refresh its JS hit-test state.
		// TextureAlpha mode needs no probe window: the sample below gives ground truth synchronously.
		TransparencyProbeVisibleUntilTime = FPlatformTime::Seconds() + 0.25;
		bIsHoveringInteractiveContent = true;
		if (WebViewWindow.IsValid())
		{
			WebViewWindow->SetHitTestEnabled(true);
			WebViewWindow->ExecuteScript(
				TEXT("if(window.__cbwebview2TransparencyCheck&&window.__cbwebview2TransparencyCheck.resetLastState){window.__cbwebview2TransparencyCheck.resetLastState();}"),
				nullptr);
		}
		// The JS hit-test state was just reset, so force the next move through even at the same position.
		LastForwardedHoverLocalPoint.Reset();
	}
	bWasCursorInsideTransparencyHitTest = true;

	const FVector2D LocalPoint = GetLocalWebViewPoint(AllottedGeometry, CursorPosition);
	// Skip the cross-process synthetic move (and the JS hit-test it triggers) while the cursor is effectively
	// stationary, including when a real OnMouseMove already forwarded this exact point this frame. The local
	// point also changes when the widget moves under a stationary cursor, so world motion still re-evaluates.
	if (!LastForwardedHoverLocalPoint.IsSet() || !LastForwardedHoverLocalPoint.GetValue().Equals(LocalPoint, 0.25))
	{
		LastForwardedHoverLocalPoint = LocalPoint;
		WebViewWindow->SendMouseMove(LocalPoint);
	}
	// Sample even when the cursor is stationary: page animation can change what is under it.
	// The sampler throttles itself (50 ms / 2 px), so this is nearly free per frame.
	UpdateHoverStateFromTextureAlpha(LocalPoint);
	UpdateTransparentHitTestVisibility();
}

void SCBWebView2World::UpdateHoverStateFromTextureAlpha(const FVector2D& LocalPoint)
{
	if (!bUseTextureAlphaHitTest || !ContinuousTextureSource.IsValid())
	{
		return;
	}

	uint8 Alpha = 0;
	if (!ContinuousTextureSource->TrySampleAlphaAtPoint(LocalPoint, Alpha))
	{
		// No frame to sample yet (startup) — keep the current state until one arrives.
		return;
	}

	const bool bNewHoverState = Alpha > static_cast<uint8>(FMath::Clamp(TextureAlphaThreshold, 0, 255));
	if (bNewHoverState == bIsHoveringInteractiveContent)
	{
		return;
	}

	// Mirror the state transitions of the JS hitTest message handler in HandleMessageFromWeb.
	bIsHoveringInteractiveContent = bNewHoverState;

	if (WebViewWindow.IsValid())
	{
		WebViewWindow->SetHitTestEnabled(bNewHoverState);
	}

	if (bNewHoverState)
	{
		bLastInsideTransparencyHitTestWasInteractive = true;
	}

	UpdateTransparentHitTestVisibility();

	if (!bNewHoverState && HasMouseCapture() && !bIsMouseButtonHeld && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().ReleaseAllPointerCapture();
	}

	if (!bNewHoverState)
	{
		ReleaseWebViewInputFocusForTransparentPassthrough();
	}
}

void SCBWebView2World::UpdateBrowserBounds(const FGeometry& AllottedGeometry)
{
	if (!WebViewWindow.IsValid())
	{
		return;
	}

	FVector2D DesiredPixelSize = AllottedGeometry.GetLocalSize() * AllottedGeometry.Scale;
	if (DesiredPixelSize.X <= 0.0f || DesiredPixelSize.Y <= 0.0f)
	{
		DesiredPixelSize = InitialDrawSize;
	}

	const FIntPoint NewPixelSize(
		FMath::Clamp(FMath::RoundToInt(DesiredPixelSize.X), 1, 16000),
		FMath::Clamp(FMath::RoundToInt(DesiredPixelSize.Y), 1, 16000));

	if (NewPixelSize == BrowserPixelSize)
	{
		return;
	}

	BrowserPixelSize = NewPixelSize;
	if (ContinuousTextureSource.IsValid())
	{
		ContinuousTextureSource->Resize(BrowserPixelSize);
	}
	RequestRefresh();
}

void SCBWebView2World::UpdateImeCaretFromBrowserClientPoint(const FVector2D& BrowserClientPoint, const FVector2D& BrowserViewportSize, float CaretHeight)
{
	if (!ContinuousTextureSource.IsValid())
	{
		return;
	}

	FVector2D LocalPoint = BrowserClientPoint;
	float LocalCaretHeight = CaretHeight;
	if (BrowserViewportSize.X > 0.0f && BrowserViewportSize.Y > 0.0f && BrowserPixelSize.X > 0 && BrowserPixelSize.Y > 0)
	{
		const FVector2D CssToBrowserScale(
			static_cast<float>(BrowserPixelSize.X) / BrowserViewportSize.X,
			static_cast<float>(BrowserPixelSize.Y) / BrowserViewportSize.Y);
		LocalPoint.X *= CssToBrowserScale.X;
		LocalPoint.Y *= CssToBrowserScale.Y;
		LocalCaretHeight *= CssToBrowserScale.Y;
	}

	if (BrowserPixelSize.X > 0)
	{
		LocalPoint.X = FMath::Clamp(LocalPoint.X, 0.0f, static_cast<float>(BrowserPixelSize.X - 1));
	}
	if (BrowserPixelSize.Y > 0)
	{
		LocalPoint.Y = FMath::Clamp(LocalPoint.Y, 0.0f, static_cast<float>(BrowserPixelSize.Y - 1));
	}

	FVector2D ScreenSpacePoint = FVector2D::ZeroVector;
	if (bHasBrowserDesktopAnchor)
	{
		ScreenSpacePoint = LastBrowserDesktopTopLeft + LocalPoint;
	}
	else if (FSlateApplication::IsInitialized())
	{
		ScreenSpacePoint = FSlateApplication::Get().GetCursorPos();
		LastBrowserDesktopTopLeft = ScreenSpacePoint - LocalPoint;
		bHasBrowserDesktopAnchor = true;
	}
	else if (bHasCachedTickGeometry)
	{
		const float GeometryScale = FMath::Max(CachedTickGeometry.Scale, UE_SMALL_NUMBER);
		ScreenSpacePoint = CachedTickGeometry.LocalToAbsolute(LocalPoint / GeometryScale);
		LastBrowserDesktopTopLeft = ScreenSpacePoint - LocalPoint;
		bHasBrowserDesktopAnchor = true;
	}
	else
	{
		return;
	}

	ContinuousTextureSource->UpdateImeCaretAnchor(ScreenSpacePoint, LocalPoint, LocalCaretHeight);
}

FVector2D SCBWebView2World::GetDesktopScreenPointForPointerEvent(const FPointerEvent& MouseEvent, const FVector2D& LocalPoint) const
{
	const FVector2D EventScreenPoint = MouseEvent.GetScreenSpacePosition();
	const bool bEventMatchesLocalPoint = FVector2D::DistSquared(EventScreenPoint, LocalPoint) <= 4.0f;
	FVector2D CursorPoint = FVector2D::ZeroVector;
	bool bEventMatchesCursor = false;
	if (FSlateApplication::IsInitialized())
	{
		CursorPoint = FSlateApplication::Get().GetCursorPos();
		bEventMatchesCursor = FVector2D::DistSquared(CursorPoint, EventScreenPoint) <= 16.0f;
		if (bEventMatchesCursor)
		{
			return CursorPoint;
		}
	}

	HWND HostHwnd = nullptr;
	if (TSharedPtr<SWindow> HostWindow = ParentWindow.Pin())
	{
		if (HostWindow->GetNativeWindow().IsValid())
		{
			HostHwnd = static_cast<HWND>(HostWindow->GetNativeWindow()->GetOSWindowHandle());
		}
	}

	if (HostHwnd)
	{
		RECT ClientRect = {};
		POINT ClientTopLeft = {0, 0};
		if (::GetClientRect(HostHwnd, &ClientRect) && ::ClientToScreen(HostHwnd, &ClientTopLeft))
		{
			const float ClientWidth = static_cast<float>(ClientRect.right - ClientRect.left);
			const float ClientHeight = static_cast<float>(ClientRect.bottom - ClientRect.top);
			const bool bEventInsideClientLocalSpace =
				EventScreenPoint.X >= 0.0f &&
				EventScreenPoint.Y >= 0.0f &&
				EventScreenPoint.X <= ClientWidth &&
				EventScreenPoint.Y <= ClientHeight;
			const bool bEventInsideClientDesktopSpace =
				EventScreenPoint.X >= static_cast<float>(ClientTopLeft.x) &&
				EventScreenPoint.Y >= static_cast<float>(ClientTopLeft.y) &&
				EventScreenPoint.X <= static_cast<float>(ClientTopLeft.x) + ClientWidth &&
				EventScreenPoint.Y <= static_cast<float>(ClientTopLeft.y) + ClientHeight;

			if (bEventMatchesLocalPoint || (!bEventMatchesCursor && bEventInsideClientLocalSpace && !bEventInsideClientDesktopSpace))
			{
				return FVector2D(ClientTopLeft.x + EventScreenPoint.X, ClientTopLeft.y + EventScreenPoint.Y);
			}
		}
	}

	return EventScreenPoint;
}

void SCBWebView2World::UpdateInputScreenAnchorFromPointerEvent(const FPointerEvent& MouseEvent, const FVector2D& LocalPoint)
{
	if (!ContinuousTextureSource.IsValid())
	{
		return;
	}

	const FVector2D DesktopScreenPoint = GetDesktopScreenPointForPointerEvent(MouseEvent, LocalPoint);
	LastBrowserDesktopTopLeft = DesktopScreenPoint - LocalPoint;
	bHasBrowserDesktopAnchor = true;
	ContinuousTextureSource->UpdateInputScreenAnchor(DesktopScreenPoint, LocalPoint);
}

void SCBWebView2World::TickContinuousOutput(double InCurrentTime)
{
	if (!ContinuousTextureSource.IsValid() || IsHiddenVisibility(GetVisibility()))
	{
		return;
	}

	UTexture2D* UpdatedTexture = nullptr;
	if (!ContinuousTextureSource->TickOutput(InCurrentTime, RefreshRate, UpdatedTexture) || !UpdatedTexture)
	{
		return;
	}

	PresentedTexture = UpdatedTexture;
	PreviewBrush.SetResourceObject(UpdatedTexture);
	PreviewBrush.ImageSize = FVector2D(UpdatedTexture->GetSizeX(), UpdatedTexture->GetSizeY());
	OnFrameUpdated.ExecuteIfBound(UpdatedTexture);
	Invalidate(EInvalidateWidgetReason::Paint);
	if (PreviewImage.IsValid())
	{
		PreviewImage->Invalidate(EInvalidateWidgetReason::Paint);
	}
}

FVector2D SCBWebView2World::GetLocalWebViewPoint(const FGeometry& MyGeometry, const FVector2D& ScreenSpacePosition) const
{
	FVector2D LocalPoint = MyGeometry.AbsoluteToLocal(ScreenSpacePosition) * MyGeometry.Scale;
	if (BrowserPixelSize.X > 0)
	{
		LocalPoint.X = FMath::Clamp(LocalPoint.X, 0.0f, static_cast<float>(BrowserPixelSize.X - 1));
	}
	if (BrowserPixelSize.Y > 0)
	{
		LocalPoint.Y = FMath::Clamp(LocalPoint.Y, 0.0f, static_cast<float>(BrowserPixelSize.Y - 1));
	}
	return LocalPoint;
}

void SCBWebView2World::ReleaseWebViewInputFocus()
{
	// Prevent recursive re-entry: ::SetFocus triggers Win32 WM_KILLFOCUS / WM_SETFOCUS,
	// and Slate can dispatch another OnFocusLost back here in PIE, multi-window, or nested focus-change scenarios.
	// Without this guard the path can recurse indefinitely, overflow the stack, and crash.
	static thread_local bool bIsReleasingFocus = false;
	if (bIsReleasingFocus)
	{
		return;
	}
	TGuardValue<bool> ReentryGuard(bIsReleasingFocus, true);

	bIsMouseButtonHeld = false;
	UpdateTransparentHitTestVisibility();
	if (ContinuousTextureSource.IsValid())
	{
		ContinuousTextureSource->ReleaseInputScreenAnchor();
	}
	bHasBrowserDesktopAnchor = false;

	if (WebViewWindow.IsValid())
	{
		WebViewWindow->MoveFocus(false);
	}

	if (UWebView2Subsystem* Subsystem = UWebView2Subsystem::Get())
	{
		Subsystem->SetWebViewFocused(false);
	}

	if (!FSlateApplication::IsInitialized())
	{
		return;
	}

	FSlateApplication& SlateApplication = FSlateApplication::Get();
	if (HasMouseCapture())
	{
		SlateApplication.ReleaseAllPointerCapture();
	}

	const TSharedPtr<SWidget> FocusedWidget = SlateApplication.GetKeyboardFocusedWidget();
	if (FocusedWidget.Get() == this)
	{
		SlateApplication.ClearKeyboardFocus(EFocusCause::Cleared);
	}

	TSharedPtr<SWindow> FocusWindow = ParentWindow.Pin();
	if (!FocusWindow.IsValid())
	{
		FocusWindow = SlateApplication.GetActiveTopLevelWindow();
	}

	if (!FocusWindow.IsValid() || !FocusWindow->GetNativeWindow().IsValid())
	{
		return;
	}

	HWND HostHwnd = static_cast<HWND>(FocusWindow->GetNativeWindow()->GetOSWindowHandle());
	if (!HostHwnd)
	{
		return;
	}

	// Call SetFocus only when Win32 focus is actually inside the WebView2 subtree and must be pulled back to the UE main window.
	// Normal Slate focus changes within the same window do not need another ::SetFocus, which avoids retriggering the WM_KILLFOCUS path.
	HWND CurrentFocus = ::GetFocus();
	if (!CurrentFocus || CurrentFocus == HostHwnd)
	{
		return;
	}

	// With the dual-HWND split, the WebView2 controller is parented to the UE main HWND, so IsChild() alone cannot distinguish
	// between WebView2's own child HWNDs (Chrome_*) and other legitimate Slate-owned children. Walk up the parent chain
	// looking for any window whose class name indicates it belongs to WebView2's Chromium host.
	auto IsWebView2OwnedFocus = [](HWND InFocus, HWND InHost) -> bool
	{
		HWND Walk = InFocus;
		WIDECHAR ClassName[64];
		while (Walk && Walk != InHost)
		{
			const int32 Length = ::GetClassNameW(Walk, ClassName, UE_ARRAY_COUNT(ClassName));
			if (Length > 0)
			{
				const FString ClassStr(Length, ClassName);
				// WebView2 child HWND classes all start with "Chrome_" (e.g. Chrome_WidgetWin_1, Chrome_RenderWidgetHostHWND).
				if (ClassStr.StartsWith(TEXT("Chrome_")) || ClassStr.StartsWith(TEXT("Intermediate D3D Window")))
				{
					return true;
				}
			}
			Walk = ::GetParent(Walk);
		}
		return false;
	};

	if (!IsWebView2OwnedFocus(CurrentFocus, HostHwnd))
	{
		// Focus is on a non-WebView2 part of the host subtree (e.g. the Slate viewport itself). Leave it alone.
		return;
	}

	::SetFocus(HostHwnd);
}

bool SCBWebView2World::InitializeTextureSource(void* FocusParentWindowHandle)
{
	const UWebView2Settings* Settings = UWebView2Settings::Get();

	// The passthrough strategy is chosen automatically: UE-side texture-alpha sampling whenever the output
	// path supports it, otherwise the injected JavaScript hit-test (D3D11 RHI).
	TextureAlphaThreshold = Settings ? Settings->WorldOutput.TextureAlphaThreshold : 8;
	bUseTextureAlphaHitTest = bEnableTransparencyHitTest &&
		FWebView2ContinuousTextureSource::PredictsAlphaSamplingSupport(ECBWebView2WorldOutputMode::Auto);
	if (bEnableTransparencyHitTest)
	{
		UE_LOG(LogCBWebView2, Log, TEXT("CBWebView2 world transparency hit-test mode: %s"),
			bUseTextureAlphaHitTest ? TEXT("TextureAlpha") : TEXT("JavaScript"));
	}

	ContinuousTextureSource = MakeShared<FWebView2ContinuousTextureSource>(
		InitialUrl,
		BackgroundColor,
		bEnableTransparencyHitTest,
		ECBWebView2WorldOutputMode::Auto,
		/*bInAllowCpuFallback=*/ true,
		/*bInInjectTransparencyHitTestScript=*/ !bUseTextureAlphaHitTest);
	if (!ContinuousTextureSource.IsValid() || !ContinuousTextureSource->Initialize(BrowserPixelSize, FocusParentWindowHandle))
	{
		ContinuousTextureSource.Reset();
		return false;
	}

	WebViewWindow = ContinuousTextureSource->GetWebViewWindow();
	BindWebViewEvents();

	if (UTexture2D* InitialTexture = ContinuousTextureSource->GetTexture())
	{
		PresentedTexture = InitialTexture;
		PreviewBrush.SetResourceObject(InitialTexture);
		PreviewBrush.ImageSize = FVector2D(InitialTexture->GetSizeX(), InitialTexture->GetSizeY());
	}

	return WebViewWindow.IsValid();
}
