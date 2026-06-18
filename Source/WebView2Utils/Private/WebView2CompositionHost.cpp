// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#include "WebView2CompositionHost.h"

#include "WebView2Log.h"
#include "WebView2Window.h"

#include "HAL/PlatformTime.h"

namespace
{
	bool IsMouseMoveMessage(const UINT Message)
	{
		return Message == WM_MOUSEMOVE || Message == WM_NCMOUSEMOVE;
	}

	bool IsMouseButtonDownMessage(const UINT Message)
	{
		return Message == WM_LBUTTONDOWN || Message == WM_RBUTTONDOWN || Message == WM_MBUTTONDOWN || Message == WM_XBUTTONDOWN ||
			Message == WM_NCLBUTTONDOWN || Message == WM_NCRBUTTONDOWN || Message == WM_NCMBUTTONDOWN || Message == WM_NCXBUTTONDOWN;
	}

	bool IsMouseButtonUpMessage(const UINT Message)
	{
		return Message == WM_LBUTTONUP || Message == WM_RBUTTONUP || Message == WM_MBUTTONUP || Message == WM_XBUTTONUP ||
			Message == WM_NCLBUTTONUP || Message == WM_NCRBUTTONUP || Message == WM_NCMBUTTONUP || Message == WM_NCXBUTTONUP;
	}

	bool IsMouseDoubleClickMessage(const UINT Message)
	{
		return Message == WM_LBUTTONDBLCLK || Message == WM_RBUTTONDBLCLK || Message == WM_MBUTTONDBLCLK || Message == WM_XBUTTONDBLCLK ||
			Message == WM_NCLBUTTONDBLCLK || Message == WM_NCRBUTTONDBLCLK || Message == WM_NCMBUTTONDBLCLK || Message == WM_NCXBUTTONDBLCLK;
	}

	bool IsMouseButtonMessage(const UINT Message)
	{
		return IsMouseButtonDownMessage(Message) || IsMouseButtonUpMessage(Message) || IsMouseDoubleClickMessage(Message);
	}

	bool IsScreenSpaceMouseMessage(const UINT Message)
	{
		return Message == WM_MOUSEWHEEL || Message == WM_MOUSEHWHEEL ||
			Message == WM_NCLBUTTONDOWN || Message == WM_NCLBUTTONUP || Message == WM_NCLBUTTONDBLCLK ||
			Message == WM_NCRBUTTONDOWN || Message == WM_NCRBUTTONUP || Message == WM_NCRBUTTONDBLCLK ||
			Message == WM_NCMBUTTONDOWN || Message == WM_NCMBUTTONUP || Message == WM_NCMBUTTONDBLCLK ||
			Message == WM_NCXBUTTONDOWN || Message == WM_NCXBUTTONUP || Message == WM_NCXBUTTONDBLCLK;
	}

	EWebView2MouseButton GetMouseButtonFromMessage(const UINT Message, const WPARAM WParam)
	{
		switch (Message)
		{
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_LBUTTONDBLCLK:
		case WM_NCLBUTTONDOWN:
		case WM_NCLBUTTONUP:
		case WM_NCLBUTTONDBLCLK:
			return EWebView2MouseButton::Left;
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
		case WM_RBUTTONDBLCLK:
		case WM_NCRBUTTONDOWN:
		case WM_NCRBUTTONUP:
		case WM_NCRBUTTONDBLCLK:
			return EWebView2MouseButton::Right;
		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
		case WM_MBUTTONDBLCLK:
		case WM_NCMBUTTONDOWN:
		case WM_NCMBUTTONUP:
		case WM_NCMBUTTONDBLCLK:
			return EWebView2MouseButton::Middle;
		case WM_XBUTTONDOWN:
		case WM_XBUTTONUP:
		case WM_XBUTTONDBLCLK:
		case WM_NCXBUTTONDOWN:
		case WM_NCXBUTTONUP:
		case WM_NCXBUTTONDBLCLK:
			return GET_XBUTTON_WPARAM(WParam) == XBUTTON2
				? EWebView2MouseButton::XButton2
				: EWebView2MouseButton::XButton1;
		default:
			return EWebView2MouseButton::Unknown;
		}
	}

	const TCHAR* ToMouseButtonString(const EWebView2MouseButton Button)
	{
		switch (Button)
		{
		case EWebView2MouseButton::Left:
			return TEXT("Left");
		case EWebView2MouseButton::Right:
			return TEXT("Right");
		case EWebView2MouseButton::Middle:
			return TEXT("Middle");
		case EWebView2MouseButton::XButton1:
			return TEXT("XButton1");
		case EWebView2MouseButton::XButton2:
			return TEXT("XButton2");
		default:
			return TEXT("Unknown");
		}
	}

	UINT32 GetMouseMessageData(const UINT Message, const WPARAM WParam)
	{
		switch (Message)
		{
		case WM_MOUSEWHEEL:
		case WM_MOUSEHWHEEL:
			return static_cast<UINT32>(GET_WHEEL_DELTA_WPARAM(WParam));
		case WM_XBUTTONDOWN:
		case WM_XBUTTONUP:
		case WM_XBUTTONDBLCLK:
		case WM_NCXBUTTONDOWN:
		case WM_NCXBUTTONUP:
		case WM_NCXBUTTONDBLCLK:
			return GET_XBUTTON_WPARAM(WParam);
		default:
			return 0;
		}
	}

	int32 GetMouseButtonSequenceIndex(const EWebView2MouseButton Button)
	{
		switch (Button)
		{
		case EWebView2MouseButton::Left:
			return 1;
		case EWebView2MouseButton::Right:
			return 2;
		case EWebView2MouseButton::Middle:
			return 3;
		case EWebView2MouseButton::XButton1:
			return 4;
		case EWebView2MouseButton::XButton2:
			return 5;
		default:
			return 0;
		}
	}

	double GetMouseSequenceWindowSeconds()
	{
		return static_cast<double>(::GetDoubleClickTime()) / 1000.0 + 0.05;
	}

	bool IsWithinMouseSequenceDistance(const POINT& A, const POINT& B)
	{
		const int32 MaxX = FMath::Max(::GetSystemMetrics(SM_CXDOUBLECLK), 4);
		const int32 MaxY = FMath::Max(::GetSystemMetrics(SM_CYDOUBLECLK), 4);
		return FMath::Abs(static_cast<int32>(A.x - B.x)) <= MaxX &&
			FMath::Abs(static_cast<int32>(A.y - B.y)) <= MaxY;
	}

	bool IsWithinMouseSequenceTime(const double EarlierTimeSeconds, const double EventTimeSeconds)
	{
		return EarlierTimeSeconds >= 0.0 && EventTimeSeconds >= EarlierTimeSeconds &&
			EventTimeSeconds - EarlierTimeSeconds <= GetMouseSequenceWindowSeconds();
	}

	bool IsClientPointInsideWebView(const TSharedRef<FWebView2Window>& WebViewWindow, const POINT& ClientPoint)
	{
		if (!WebViewWindow->WebViewVisual)
		{
			return false;
		}

		const auto Offset = WebViewWindow->WebViewVisual.Offset();
		const auto Size = WebViewWindow->WebViewVisual.Size();
		return ClientPoint.x >= Offset.x &&
			ClientPoint.x < Offset.x + Size.x &&
			ClientPoint.y >= Offset.y &&
			ClientPoint.y < Offset.y + Size.y;
	}

	bool IsTrackedMouseTargetUsable(
		const TSharedPtr<FWebView2Window>& WebViewWindow,
		const POINT& TrackedPoint,
		const POINT& ClientPoint,
		const double TrackedTimeSeconds,
		const double EventTimeSeconds,
		const bool bRequireHitTestEnabled)
	{
		if (!WebViewWindow.IsValid() || !WebViewWindow->CompositionController || WebViewWindow->GetVisible() != ESlateVisibility::Visible)
		{
			return false;
		}

		if (bRequireHitTestEnabled && !WebViewWindow->IsHitTestEnabled())
		{
			return false;
		}

		return IsClientPointInsideWebView(WebViewWindow.ToSharedRef(), ClientPoint) &&
			IsWithinMouseSequenceDistance(TrackedPoint, ClientPoint) &&
			IsWithinMouseSequenceTime(TrackedTimeSeconds, EventTimeSeconds);
	}

	COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS GetMouseButtonVirtualKeys(const EWebView2MouseButton Button)
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

	int32 GetDomMouseButton(const EWebView2MouseButton Button)
	{
		switch (Button)
		{
		case EWebView2MouseButton::Middle:
			return 1;
		case EWebView2MouseButton::Right:
			return 2;
		case EWebView2MouseButton::XButton1:
			return 3;
		case EWebView2MouseButton::XButton2:
			return 4;
		case EWebView2MouseButton::Left:
		default:
			return 0;
		}
	}

	const TCHAR* ToJavaScriptBool(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
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

	bool IsSameWebView(const TSharedPtr<FWebView2Window>& A, const TSharedRef<FWebView2Window>& B)
	{
		return A.IsValid() && A->GetInstanceId() == B->GetInstanceId();
	}

	void RequestDomDoubleClickFallback(
		const TSharedRef<FWebView2Window>& WebViewWindow,
		const POINT& LocalPoint,
		const POINT& ScreenPoint,
		const EWebView2MouseButton Button,
		const WPARAM WParam)
	{
		if (!WebViewWindow->WebView)
		{
			return;
		}

		const FString Script = FString::Printf(
			TEXT("(function(){if(window.__cbwebview2DispatchDblClickFallback){window.__cbwebview2DispatchDblClickFallback(%ld,%ld,%d,%ld,%ld,%s,%s,%s,%s);}})();"),
			static_cast<long>(LocalPoint.x),
			static_cast<long>(LocalPoint.y),
			GetDomMouseButton(Button),
			static_cast<long>(ScreenPoint.x),
			static_cast<long>(ScreenPoint.y),
			ToJavaScriptBool((WParam & MK_CONTROL) != 0),
			ToJavaScriptBool((WParam & MK_SHIFT) != 0),
			ToJavaScriptBool((::GetKeyState(VK_MENU) & 0x8000) != 0),
			ToJavaScriptBool((::GetKeyState(VK_LWIN) & 0x8000) != 0 || (::GetKeyState(VK_RWIN) & 0x8000) != 0));

		WebViewWindow->ExecuteScript(Script);
	}
}

FWebView2CompositionHost::FWebView2CompositionHost(
	HWND InWindowHandle,
	winrt::Windows::System::DispatcherQueueController InDispatcherQueueController)
	: WindowHandle(InWindowHandle)
	, DispatcherQueueController(InDispatcherQueueController)
{
	if (DispatcherQueueController)
	{
		Compositor = winrt::Windows::UI::Composition::Compositor();
	}
}

FWebView2CompositionHost::~FWebView2CompositionHost()
{
	Destroy();
}

void FWebView2CompositionHost::Initialize()
{
	if (!Compositor)
	{
		return;
	}

	// Initialize the WinComp root node for the current host window.
	CreateDesktopTarget();
	CreateRootVisual();
}

void FWebView2CompositionHost::AttachWebView(const TSharedRef<FWebView2Window>& WebViewWindow)
{
	if (!WebViewLayer || !WebViewWindow->CompositionController)
	{
		return;
	}

	// Use a separate ContainerVisual for each WebView so positioning and ordering remain independent.
	winrt::Windows::UI::Composition::ContainerVisual Container = Compositor.CreateContainerVisual();
	const RECT Bounds = WebViewWindow->GetBounds();
	Container.Offset({static_cast<float>(Bounds.left), static_cast<float>(Bounds.top), 0.0f});
	Container.Size({
		static_cast<float>(Bounds.right - Bounds.left),
		static_cast<float>(Bounds.bottom - Bounds.top)});

	WebViewLayer.Children().InsertAtTop(Container);
	WebViewWindow->SetContainerVisual(Container);
	WebViewWindow->CompositionController->put_RootVisualTarget(Container.as<IUnknown>().get());

	++NextLayerId;
	WebViewWindow->SetLayerId(NextLayerId);
	WebViews.Add(WebViewWindow->GetInstanceId().ToString(), WebViewWindow);
}

void FWebView2CompositionHost::RefreshVisualOrder()
{
	if (!WebViewLayer)
	{
		return;
	}

	TMap<int32, TSharedRef<FWebView2Window>> Sorted;
	for (const TPair<FString, TWeakPtr<FWebView2Window>>& Pair : WebViews)
	{
		if (const TSharedPtr<FWebView2Window> WebViewWindow = Pair.Value.Pin())
		{
			Sorted.Add(WebViewWindow->GetLayerId(), WebViewWindow.ToSharedRef());
		}
	}

	Sorted.KeySort([](const int32 A, const int32 B)
	{
		return A < B;
	});

	for (const TPair<int32, TSharedRef<FWebView2Window>>& Pair : Sorted)
	{
		WebViewLayer.Children().Remove(Pair.Value->WebViewVisual);
		WebViewLayer.Children().InsertAtTop(Pair.Value->WebViewVisual);
	}
}

void FWebView2CompositionHost::DetachWebView(const FGuid& InstanceId, const winrt::Windows::UI::Composition::ContainerVisual& WebViewContainer)
{
	WebViews.Remove(InstanceId.ToString());
	if (WebViewLayer && WebViewContainer)
	{
		WebViewLayer.Children().Remove(WebViewContainer);
	}
}

void FWebView2CompositionHost::Destroy()
{
	for (const TPair<FString, TWeakPtr<FWebView2Window>>& Pair : WebViews)
	{
		if (const TSharedPtr<FWebView2Window> WebViewWindow = Pair.Value.Pin())
		{
			if (WebViewLayer && WebViewWindow->WebViewVisual)
			{
				WebViewLayer.Children().Remove(WebViewWindow->WebViewVisual);
			}
		}
	}

	WebViews.Empty();

	if (RootVisual)
	{
		RootVisual.Children().RemoveAll();
		RootVisual = nullptr;
	}

	if (WindowTarget)
	{
		WindowTarget.Root(nullptr);
		WindowTarget = nullptr;
	}

	WebViewLayer = nullptr;
}

bool FWebView2CompositionHost::HandleMouseMessage(UINT Message, WPARAM WParam, LPARAM LParam)
{
	if (!RootVisual || Message == WM_CLOSE)
	{
		return false;
	}
				// Do not let explicitly non-hittable transparent regions compete for click targeting.
	const bool bIsMouseButtonDown = IsMouseButtonDownMessage(Message);
	const bool bIsMouseButtonUp = IsMouseButtonUpMessage(Message);
	const bool bIsMouseDoubleClick = IsMouseDoubleClickMessage(Message);
	const bool bIsMouseButton = IsMouseButtonMessage(Message);
	const EWebView2MouseButton MouseButton = bIsMouseButton ? GetMouseButtonFromMessage(Message, WParam) : EWebView2MouseButton::Unknown;
	const double EventTimeSeconds = FPlatformTime::Seconds();

	POINT Point;
	POINTSTOPOINT(Point, LParam);
	if (IsScreenSpaceMouseMessage(Message))
	{
		::ScreenToClient(WindowHandle, &Point);
	}

	const TArray<TSharedRef<FWebView2Window>> VisibleWebViewsUnderCursor = FindWebViewsAtPoint(Point, false);
	if (VisibleWebViewsUnderCursor.IsEmpty())
	{
		UE_LOG(
			LogWebView2Utils,
			VeryVerbose,
			TEXT("CompositionHost passthrough: msg=0x%04x button=%s point=(%ld,%ld) visible=0 interactive=0"),
			Message,
			ToMouseButtonString(MouseButton),
			static_cast<long>(Point.x),
			static_cast<long>(Point.y));
		return false;
	}

	const TArray<TSharedRef<FWebView2Window>> InteractiveWebViewsUnderCursor = FindWebViewsAtPoint(Point, true);
	const POINT OriginalPoint = Point;
	FMouseButtonSequenceState* SequenceState = nullptr;
	if (bIsMouseButton && MouseButton != EWebView2MouseButton::Unknown)
	{
		SequenceState = &MouseButtonSequenceStates[GetMouseButtonSequenceIndex(MouseButton)];
	}

	TSharedPtr<FWebView2Window> TopMost;
	for (const TSharedRef<FWebView2Window>& WebViewWindow : InteractiveWebViewsUnderCursor)
	{
		if (!TopMost.IsValid() || TopMost->GetLayerId() < WebViewWindow->GetLayerId())
		{
			TopMost = WebViewWindow;
		}
	}

	if (IsMouseMoveMessage(Message))
	{
		for (const TSharedRef<FWebView2Window>& WebViewWindow : VisibleWebViewsUnderCursor)
		{
			if (!WebViewWindow->CompositionController || !WebViewWindow->IsTransparencyHitTestEnabled())
			{
				continue;
			}

			if (TopMost.IsValid() && TopMost->GetInstanceId() == WebViewWindow->GetInstanceId())
			{
				continue;
			}

			POINT LocalPoint = Point;
			const RECT Bounds = WebViewWindow->GetBounds();
			LocalPoint.x -= Bounds.left;
			LocalPoint.y -= Bounds.top;

			WebViewWindow->CompositionController->SendMouseInput(
				COREWEBVIEW2_MOUSE_EVENT_KIND_MOVE,
				static_cast<COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS>(GET_KEYSTATE_WPARAM(WParam)),
				0,
				LocalPoint);
		}
	}

		bool bUsedSequenceTarget = false;
		if (SequenceState)
		{
			const TSharedPtr<FWebView2Window> TrackedDownTarget = SequenceState->LastDownTarget.Pin();
			const bool bCanUseTrackedReleaseTarget = bIsMouseButtonUp && SequenceState->bLastDownForwarded &&
				IsTrackedMouseTargetUsable(
					TrackedDownTarget,
					SequenceState->LastDownClientPoint,
					OriginalPoint,
					SequenceState->LastDownTimeSeconds,
					EventTimeSeconds,
					false);
			const bool bCanUseTrackedDoubleClickTarget = bIsMouseDoubleClick && SequenceState->bLastDownForwarded &&
				IsTrackedMouseTargetUsable(
					TrackedDownTarget,
					SequenceState->LastDownClientPoint,
					OriginalPoint,
					SequenceState->LastDownTimeSeconds,
					EventTimeSeconds,
					true);

			if (bCanUseTrackedReleaseTarget || bCanUseTrackedDoubleClickTarget)
			{
				TopMost = TrackedDownTarget;
				bUsedSequenceTarget = true;
			}
		}

		if (!TopMost.IsValid() || !TopMost->CompositionController)
	{
			UE_LOG(
				LogWebView2Utils,
				VeryVerbose,
				TEXT("CompositionHost passthrough: msg=0x%04x button=%s point=(%ld,%ld) visible=%d interactive=%d usedSequence=%d"),
				Message,
				ToMouseButtonString(MouseButton),
				static_cast<long>(OriginalPoint.x),
				static_cast<long>(OriginalPoint.y),
				VisibleWebViewsUnderCursor.Num(),
				InteractiveWebViewsUnderCursor.Num(),
				bUsedSequenceTarget);
		return false;
	}

		if (bIsMouseButtonDown || bIsMouseDoubleClick)
	{
			TopMost->OnInputActivationRequested.ExecuteIfBound();
	}

		const RECT Bounds = TopMost->GetBounds();
		POINT LocalPoint = OriginalPoint;
		LocalPoint.x -= Bounds.left;
		LocalPoint.y -= Bounds.top;

		bool bSynthesizedFirstClick = false;
		bool bRequestedDomDoubleClickFallback = false;
		if (bIsMouseDoubleClick && SequenceState)
		{
			const TSharedRef<FWebView2Window> TopMostRef = TopMost.ToSharedRef();
			const TSharedPtr<FWebView2Window> LastDownTarget = SequenceState->LastDownTarget.Pin();
			const TSharedPtr<FWebView2Window> LastUpTarget = SequenceState->LastUpTarget.Pin();
			const bool bHasCompleteForwardedClick =
				SequenceState->bLastDownForwarded &&
				SequenceState->bLastUpForwarded &&
				IsSameWebView(LastDownTarget, TopMostRef) &&
				IsSameWebView(LastUpTarget, TopMostRef) &&
				SequenceState->LastUpTimeSeconds >= SequenceState->LastDownTimeSeconds &&
				IsWithinMouseSequenceTime(SequenceState->LastUpTimeSeconds, EventTimeSeconds) &&
				IsWithinMouseSequenceDistance(SequenceState->LastDownClientPoint, OriginalPoint) &&
				IsWithinMouseSequenceDistance(SequenceState->LastUpClientPoint, OriginalPoint);

			COREWEBVIEW2_MOUSE_EVENT_KIND SynthDownKind = COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOWN;
			COREWEBVIEW2_MOUSE_EVENT_KIND SynthUpKind = COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_UP;
			if (!bHasCompleteForwardedClick &&
				TryGetMouseButtonEventKind(MouseButton, true, SynthDownKind) &&
				TryGetMouseButtonEventKind(MouseButton, false, SynthUpKind))
			{
				TopMost->CompositionController->SendMouseInput(
					COREWEBVIEW2_MOUSE_EVENT_KIND_MOVE,
					COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE,
					0,
					LocalPoint);
				TopMost->CompositionController->SendMouseInput(
					SynthDownKind,
					GetMouseButtonVirtualKeys(MouseButton),
					GetMouseButtonData(MouseButton),
					LocalPoint);
				TopMost->CompositionController->SendMouseInput(
					SynthUpKind,
					GetMouseButtonVirtualKeys(MouseButton),
					GetMouseButtonData(MouseButton),
					LocalPoint);

				SequenceState->LastDownTarget = TopMost;
				SequenceState->LastDownClientPoint = OriginalPoint;
				SequenceState->LastDownTimeSeconds = EventTimeSeconds;
				SequenceState->bLastDownForwarded = true;
				SequenceState->LastUpTarget = TopMost;
				SequenceState->LastUpClientPoint = OriginalPoint;
				SequenceState->LastUpTimeSeconds = EventTimeSeconds;
				SequenceState->bLastUpForwarded = true;
				bSynthesizedFirstClick = true;
			}
		}

		TopMost->CompositionController->SendMouseInput(
			static_cast<COREWEBVIEW2_MOUSE_EVENT_KIND>(Message),
		static_cast<COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS>(GET_KEYSTATE_WPARAM(WParam)),
			GetMouseMessageData(Message, WParam),
			LocalPoint);

		if (SequenceState)
	{
			if (bIsMouseButtonDown || bIsMouseDoubleClick)
		{
				SequenceState->LastDownTarget = TopMost;
				SequenceState->LastDownClientPoint = OriginalPoint;
				SequenceState->LastDownTimeSeconds = EventTimeSeconds;
				SequenceState->bLastDownForwarded = true;
		}
			else if (bIsMouseButtonUp)
		{
				SequenceState->LastUpTarget = TopMost;
				SequenceState->LastUpClientPoint = OriginalPoint;
				SequenceState->LastUpTimeSeconds = EventTimeSeconds;
				SequenceState->bLastUpForwarded = true;

				const TSharedPtr<FWebView2Window> PendingFallbackTarget = SequenceState->PendingDoubleClickFallbackTarget.Pin();
				if (SequenceState->bHasPendingDoubleClickFallback &&
					IsSameWebView(PendingFallbackTarget, TopMost.ToSharedRef()) &&
					IsWithinMouseSequenceTime(SequenceState->PendingDoubleClickFallbackTimeSeconds, EventTimeSeconds))
				{
					RequestDomDoubleClickFallback(
						TopMost.ToSharedRef(),
						SequenceState->PendingDoubleClickFallbackLocalPoint,
						SequenceState->PendingDoubleClickFallbackScreenPoint,
						MouseButton,
						SequenceState->PendingDoubleClickFallbackWParam);
					bRequestedDomDoubleClickFallback = true;
				}

				SequenceState->bHasPendingDoubleClickFallback = false;
				SequenceState->PendingDoubleClickFallbackTarget.Reset();
		}
	}

		if (bIsMouseDoubleClick)
		{
			POINT ScreenPoint = OriginalPoint;
			::ClientToScreen(WindowHandle, &ScreenPoint);
			if (SequenceState)
			{
				SequenceState->PendingDoubleClickFallbackTarget = TopMost;
				SequenceState->PendingDoubleClickFallbackLocalPoint = LocalPoint;
				SequenceState->PendingDoubleClickFallbackScreenPoint = ScreenPoint;
				SequenceState->PendingDoubleClickFallbackWParam = WParam;
				SequenceState->PendingDoubleClickFallbackTimeSeconds = EventTimeSeconds;
				SequenceState->bHasPendingDoubleClickFallback = true;
			}
			else
			{
				RequestDomDoubleClickFallback(TopMost.ToSharedRef(), LocalPoint, ScreenPoint, MouseButton, WParam);
				bRequestedDomDoubleClickFallback = true;
			}

			const FVector2D LocalPosition(static_cast<double>(LocalPoint.x), static_cast<double>(LocalPoint.y));
			const FVector2D ScreenPosition(static_cast<double>(ScreenPoint.x), static_cast<double>(ScreenPoint.y));
			TopMost->OnNativeMouseButtonDoubleClick.ExecuteIfBound(LocalPosition, ScreenPosition, MouseButton);

			UE_LOG(
				LogWebView2Utils,
				VeryVerbose,
				TEXT("CompositionHost forwarded native dblclick: msg=0x%04x button=%s local=(%ld,%ld) instance=%s usedSequence=%d synthesizedFirstClick=%d pendingDomFallback=%d"),
				Message,
				ToMouseButtonString(MouseButton),
				static_cast<long>(LocalPoint.x),
				static_cast<long>(LocalPoint.y),
				*TopMost->GetInstanceId().ToString(),
				bUsedSequenceTarget,
				bSynthesizedFirstClick,
				SequenceState ? SequenceState->bHasPendingDoubleClickFallback : false);
		}

		UE_LOG(
			LogWebView2Utils,
			VeryVerbose,
			TEXT("CompositionHost handled by WebView: msg=0x%04x button=%s client=(%ld,%ld) local=(%ld,%ld) instance=%s layer=%d visible=%d hitTest=%d visibleCandidates=%d interactiveCandidates=%d usedSequence=%d synthesizedFirstClick=%d domFallbackRequested=%d"),
			Message,
			ToMouseButtonString(MouseButton),
			static_cast<long>(OriginalPoint.x),
			static_cast<long>(OriginalPoint.y),
			static_cast<long>(LocalPoint.x),
			static_cast<long>(LocalPoint.y),
			*TopMost->GetInstanceId().ToString(),
			TopMost->GetLayerId(),
			static_cast<int32>(TopMost->GetVisible()),
			TopMost->IsHitTestEnabled(),
			VisibleWebViewsUnderCursor.Num(),
			InteractiveWebViewsUnderCursor.Num(),
			bUsedSequenceTarget,
			bSynthesizedFirstClick,
			bRequestedDomDoubleClickFallback);

	return true;
}

HWND FWebView2CompositionHost::GetWindowHandle() const
{
	return WindowHandle;
}

void FWebView2CompositionHost::CreateDesktopTarget()
{
	namespace DesktopAbi = ABI::Windows::UI::Composition::Desktop;
	auto Interop = Compositor.as<DesktopAbi::ICompositorDesktopInterop>();
	winrt::check_hresult(Interop->CreateDesktopWindowTarget(
		WindowHandle,
		false,
		reinterpret_cast<DesktopAbi::IDesktopWindowTarget**>(winrt::put_abi(WindowTarget))));
}

void FWebView2CompositionHost::CreateRootVisual()
{
	RootVisual = Compositor.CreateContainerVisual();
	RootVisual.RelativeSizeAdjustment({1.0f, 1.0f});
	WindowTarget.Root(RootVisual);

	WebViewLayer = Compositor.CreateContainerVisual();
	WebViewLayer.RelativeSizeAdjustment({1.0f, 1.0f});
	RootVisual.Children().InsertAtTop(WebViewLayer);
}

TArray<TSharedRef<FWebView2Window>> FWebView2CompositionHost::FindWebViewsAtPoint(const POINT& ClientPoint, bool bRequireHitTestEnabled) const
{
	TArray<TSharedRef<FWebView2Window>> Result;
	for (const TPair<FString, TWeakPtr<FWebView2Window>>& Pair : WebViews)
	{
		const TSharedPtr<FWebView2Window> WebViewWindow = Pair.Value.Pin();
		if (!WebViewWindow.IsValid())
		{
			continue;
		}

		if (!WebViewWindow->WebViewVisual || WebViewWindow->GetVisible() != ESlateVisibility::Visible)
		{
			continue;
		}

		if (bRequireHitTestEnabled && !WebViewWindow->IsHitTestEnabled())
		{
			// Do not let explicitly non-hittable transparent regions compete for click targeting.
			continue;
		}

		auto Offset = WebViewWindow->WebViewVisual.Offset();
		auto Size = WebViewWindow->WebViewVisual.Size();
		if (
			ClientPoint.x >= Offset.x &&
			ClientPoint.x < Offset.x + Size.x &&
			ClientPoint.y >= Offset.y &&
			ClientPoint.y < Offset.y + Size.y)
		{
			Result.Add(WebViewWindow.ToSharedRef());
		}
	}

	return Result;
}