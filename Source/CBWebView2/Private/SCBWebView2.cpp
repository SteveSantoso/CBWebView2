// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#include "SCBWebView2.h"

#include "WebView2CompositionHost.h"
#include "WebView2InternalMessage.h"
#include "WebView2Manager.h"
#include "WebView2Settings.h"
#include "WebView2Subsystem.h"

#include "Brushes/SlateColorBrush.h"
#include "Components/SlateWrapperTypes.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <winuser.h>
#include "Windows/HideWindowsPlatformTypes.h"

#define LOCTEXT_NAMESPACE "CBWebView2"

namespace
{
	// Keyboard events can route back into the host window on some paths; use this flag to prevent recursive re-entry.
	bool GCBWebView2KeyReentry = false;

	EWebView2MouseButton GetMouseButtonFromSlateEvent(const FKey& Button)
	{
		if (Button == EKeys::LeftMouseButton)
		{
			return EWebView2MouseButton::Left;
		}
		if (Button == EKeys::RightMouseButton)
		{
			return EWebView2MouseButton::Right;
		}
		if (Button == EKeys::MiddleMouseButton)
		{
			return EWebView2MouseButton::Middle;
		}
		if (Button == EKeys::ThumbMouseButton)
		{
			return EWebView2MouseButton::XButton1;
		}
		if (Button == EKeys::ThumbMouseButton2)
		{
			return EWebView2MouseButton::XButton2;
		}

		return EWebView2MouseButton::Unknown;
	}

	FKey GetSlateMouseButtonFromNative(const EWebView2MouseButton Button)
	{
		switch (Button)
		{
		case EWebView2MouseButton::Left:
			return EKeys::LeftMouseButton;
		case EWebView2MouseButton::Right:
			return EKeys::RightMouseButton;
		case EWebView2MouseButton::Middle:
			return EKeys::MiddleMouseButton;
		case EWebView2MouseButton::XButton1:
			return EKeys::ThumbMouseButton;
		case EWebView2MouseButton::XButton2:
			return EKeys::ThumbMouseButton2;
		default:
			return EKeys::Invalid;
		}
	}

	const TSet<FKey>& GetPressedButtonSet(const FKey& Button)
	{
		static const TSet<FKey> EmptySet;
		static const TSet<FKey> LeftButtonSet = []()
		{
			TSet<FKey> Result;
			Result.Add(EKeys::LeftMouseButton);
			return Result;
		}();
		static const TSet<FKey> RightButtonSet = []()
		{
			TSet<FKey> Result;
			Result.Add(EKeys::RightMouseButton);
			return Result;
		}();
		static const TSet<FKey> MiddleButtonSet = []()
		{
			TSet<FKey> Result;
			Result.Add(EKeys::MiddleMouseButton);
			return Result;
		}();
		static const TSet<FKey> ThumbButtonSet = []()
		{
			TSet<FKey> Result;
			Result.Add(EKeys::ThumbMouseButton);
			return Result;
		}();
		static const TSet<FKey> ThumbButton2Set = []()
		{
			TSet<FKey> Result;
			Result.Add(EKeys::ThumbMouseButton2);
			return Result;
		}();

		if (Button == EKeys::LeftMouseButton)
		{
			return LeftButtonSet;
		}
		if (Button == EKeys::RightMouseButton)
		{
			return RightButtonSet;
		}
		if (Button == EKeys::MiddleMouseButton)
		{
			return MiddleButtonSet;
		}
		if (Button == EKeys::ThumbMouseButton)
		{
			return ThumbButtonSet;
		}
		if (Button == EKeys::ThumbMouseButton2)
		{
			return ThumbButton2Set;
		}

		return EmptySet;
	}

	bool ShouldUseSlateMouseForwarding()
	{
		const UWebView2Settings* Settings = UWebView2Settings::Get();
		return Settings ? Settings->Mode != ECBWebView2Mode::VisualWinComp : false;
	}

	bool ShouldReleaseInputForWebViewVisibility(ESlateVisibility InVisibility)
	{
		return InVisibility == ESlateVisibility::Collapsed ||
			InVisibility == ESlateVisibility::Hidden ||
			InVisibility == ESlateVisibility::HitTestInvisible;
	}

}

void SCBWebView2::Construct(const FArguments& InArgs)
{
	// Cache Slate arguments first, then attempt to create the underlying native WebView.
	InitialUrl = InArgs._InitialUrl;
	CurrentUrl = InitialUrl;
	BackgroundColor = InArgs._BackgroundColor;
	ParentWindow = InArgs._ParentWindow;
	bShowAddressBar = InArgs._bShowAddressBar;
	bShowControls = InArgs._bShowControls;
	bShowTouchArea = InArgs._bShowTouchArea;
	bEnableTransparencyHitTest = InArgs._bEnableTransparencyHitTest;
	bAllowNonInteractiveElementPassthrough = InArgs._bAllowNonInteractiveElementPassthrough;
	bShowInitialThrobber = InArgs._bShowInitialThrobber;
	ToolbarHeight = InArgs._ToolbarHeight > 0.0f ? InArgs._ToolbarHeight : 30.0f;
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
	OnMouseButtonDoubleClickEvent = InArgs._OnMouseButtonDoubleClick;
	InstanceId = FGuid::NewGuid();

	ChildSlot
	[
		// Build the address bar, control buttons, and actual WebView overlay in one place.
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			.Visibility((bShowAddressBar || bShowControls) ? EVisibility::Visible : EVisibility::Collapsed)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 5.0f)
			[
				SNew(SHorizontalBox)
				.Visibility(bShowControls ? EVisibility::Visible : EVisibility::Collapsed)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("Back", "Back"))
					.IsEnabled_Lambda([this]() { return bCanGoBack; })
					.OnClicked(this, &SCBWebView2::HandleBackClicked)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("Forward", "Forward"))
					.IsEnabled_Lambda([this]() { return bCanGoForward; })
					.OnClicked(this, &SCBWebView2::HandleForwardClicked)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SButton)
					.Text(this, &SCBWebView2::GetReloadButtonText)
					.OnClicked(this, &SCBWebView2::HandleReloadClicked)
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(5.0f)
			[
				SAssignNew(AddressBarTextBox, SEditableTextBox)
				.Visibility(bShowAddressBar ? EVisibility::Visible : EVisibility::Collapsed)
				.Text(this, &SCBWebView2::GetAddressBarUrlText)
				.OnTextCommitted(this, &SCBWebView2::HandleUrlCommitted)
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SCircularThrobber)
				.Radius(10.0f)
				.Visibility(this, &SCBWebView2::GetLoadingIndicatorVisibility)
			]
			+ SOverlay::Slot()
			[
				SAssignNew(Overlay, SOverlay)
			]
		]
	];

	if (TSharedPtr<SWindow> PinnedWindow = ParentWindow.Pin())
	{
		// WebView2 must bind to a real HWND, so fetch the host window handle during Slate construction.
		if (void* NativeHandle = PinnedWindow->GetNativeWindow()->GetOSWindowHandle())
		{
			WebViewWindow = FWebView2Manager::Get().CreateWebView(
				static_cast<HWND>(NativeHandle),
				InstanceId,
				InitialUrl,
				BackgroundColor,
				bEnableTransparencyHitTest,
				true,
				nullptr,
				bAllowNonInteractiveElementPassthrough);
			BindWebViewEvents();
		}
	}
}

SCBWebView2::~SCBWebView2()
{
	BeginDestroy();
}

void SCBWebView2::BeginDestroy()
{
	// Close the underlying window proactively so destruction order stays predictable.
	if (WebViewWindow.IsValid())
	{
		WebViewWindow->CloseWindow();
		WebViewWindow.Reset();
	}
}

void SCBWebView2::ExecuteScript(const FString& Script, FOnCBWebView2ScriptCallback Callback) const
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

void SCBWebView2::LoadURL(const FString& InUrl)
{
	InitialUrl = InUrl;
	CurrentUrl = InUrl;
	if (WebViewWindow.IsValid())
	{
		WebViewWindow->LoadURL(InUrl);
	}
}

void SCBWebView2::SetTransparencyHitTestEnabled(bool bInTransparencyHitTestEnabled)
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

	if (!bEnableTransparencyHitTest)
	{
		SetVisibility(EVisibility::Visible);
	}
	else if (!bIsHoveringInteractiveContent)
	{
		SetVisibility(EVisibility::HitTestInvisible);
	}
}

void SCBWebView2::SetAllowNonInteractiveElementPassthrough(bool bInAllowNonInteractiveElementPassthrough)
{
	bAllowNonInteractiveElementPassthrough = bInAllowNonInteractiveElementPassthrough;
	if (WebViewWindow.IsValid())
	{
		WebViewWindow->SetAllowNonInteractiveElementPassthrough(bAllowNonInteractiveElementPassthrough);
	}
}

void SCBWebView2::GoForward() const
{
	if (WebViewWindow.IsValid())
	{
		WebViewWindow->GoForward();
	}
}

void SCBWebView2::GoBack() const
{
	if (WebViewWindow.IsValid())
	{
		WebViewWindow->GoBack();
	}
}

void SCBWebView2::Reload() const
{
	if (WebViewWindow.IsValid())
	{
		WebViewWindow->Reload();
	}
}

void SCBWebView2::Stop() const
{
	if (WebViewWindow.IsValid())
	{
		WebViewWindow->Stop();
	}
}

void SCBWebView2::OpenDevToolsWindow() const
{
	if (WebViewWindow.IsValid())
	{
		WebViewWindow->OpenDevToolsWindow();
	}
}

void SCBWebView2::PrintToPdf(const FString& OutputPath, bool bLandscape) const
{
	if (WebViewWindow.IsValid())
	{
		WebViewWindow->PrintToPdf(OutputPath, bLandscape);
	}
}

void SCBWebView2::CaptureDiagnosticPreview(FOnCBWebView2DiagnosticPreviewCapturedNative Callback) const
{
	if (!WebViewWindow.IsValid())
	{
		if (Callback.IsBound())
		{
			Callback.Execute(false, nullptr, TEXT("The underlying WebView has not been created yet."));
		}
		return;
	}

	WebViewWindow->CaptureDiagnosticPreview(
		[Callback = MoveTemp(Callback)](bool bSuccess, UTexture2D* Texture, const FString& ErrorMessage) mutable
		{
			if (Callback.IsBound())
			{
				Callback.Execute(bSuccess, Texture, ErrorMessage);
			}
		});
}

void SCBWebView2::SetBackgroundColor(const FColor& InBackgroundColor)
{
	BackgroundColor = InBackgroundColor;
	if (WebViewWindow.IsValid())
	{
		WebViewWindow->SetBackgroundColor(InBackgroundColor);
	}
}

void SCBWebView2::SetWebViewVisibility(ESlateVisibility InVisibility)
{
	if (WebViewWindow.IsValid())
	{
		WebViewWindow->SetVisible(InVisibility);
	}

	EVisibility SlateVisibility = EVisibility::Visible;
	switch (InVisibility)
	{
	case ESlateVisibility::Collapsed:
		SlateVisibility = EVisibility::Collapsed;
		break;
	case ESlateVisibility::Hidden:
		SlateVisibility = EVisibility::Hidden;
		break;
	case ESlateVisibility::HitTestInvisible:
		SlateVisibility = EVisibility::HitTestInvisible;
		break;
	case ESlateVisibility::SelfHitTestInvisible:
		SlateVisibility = EVisibility::SelfHitTestInvisible;
		break;
	default:
		SlateVisibility = EVisibility::Visible;
		break;
	}

	SetVisibility(SlateVisibility);
	if (ShouldReleaseInputForWebViewVisibility(InVisibility))
	{
		ReleaseWebViewInputFocus();
	}
}

ESlateVisibility SCBWebView2::GetWebViewVisibility() const
{
	return WebViewWindow.IsValid() ? WebViewWindow->GetVisible() : ESlateVisibility::Hidden;
}

int32 SCBWebView2::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	CachedPaintGeometry = AllottedGeometry;
	bHasCachedPaintGeometry = true;

	if (WebViewWindow.IsValid())
	{
		CurrentSlateScale = AllottedGeometry.Scale;

		FVector2D Offset = AllottedGeometry.LocalToAbsolute(FVector2D::ZeroVector);
		FVector2D Size = AllottedGeometry.GetDrawSize();
		Size.X = FMath::Clamp(Size.X, 0.0f, 16000.0f);
		Size.Y = FMath::Clamp(Size.Y, 0.0f, 16000.0f);

		POINT WindowOffset{static_cast<LONG>(Offset.X), static_cast<LONG>(Offset.Y)};
		POINT WindowSize{static_cast<LONG>(Size.X), static_cast<LONG>(Size.Y)};
		if (bShowAddressBar || bShowControls)
		{
			// The top toolbar consumes visible space, so subtract its height.
			const LONG ScaledToolbarHeight = static_cast<LONG>(ToolbarHeight * AllottedGeometry.Scale);
			WindowOffset.y += ScaledToolbarHeight;
			WindowSize.y = FMath::Max<LONG>(0, WindowSize.y - ScaledToolbarHeight);
		}

		WebViewWindow->SetBounds(WindowOffset, WindowSize);
		// Keep page rasterization aligned with the actual on-screen pixel density (DPI / UI scale); deduped internally.
		WebViewWindow->SetRasterizationScale(AllottedGeometry.Scale);

		if (TSharedPtr<FWebView2CompositionHost> CompositionHost = WebViewWindow->GetCompositionHost())
		{
			// When multiple WebViews overlap, use the Slate LayerId to drive native Visual ordering.
			if (LayerId != WebViewWindow->GetLayerId())
			{
				WebViewWindow->SetLayerId(LayerId);
				CompositionHost->RefreshVisualOrder();
			}
		}
	}

	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
}

FVector2D SCBWebView2::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	if (GetVisibility() == EVisibility::Collapsed)
	{
		return FVector2D::ZeroVector;
	}

	if (WebViewWindow.IsValid())
	{
		const RECT Bounds = WebViewWindow->GetBounds();
		return FVector2D(Bounds.right - Bounds.left, Bounds.bottom - Bounds.top);
	}

	return FVector2D(640.0f, 360.0f);
}

void SCBWebView2::Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
}

FReply SCBWebView2::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bIsHoveringInteractiveContent)
	{
		// If the cursor is over a transparent region, yield hit-testing so lower UMG/WebView content can continue handling it.
		return FReply::Unhandled();
	}

	if (WebViewWindow.IsValid())
	{
		// When interactive web content is hit, forward the native mouse down and claim Slate keyboard focus.
		if (ShouldUseSlateMouseForwarding())
		{
			const FVector2D LocalPoint = GetLocalWebViewPoint(MyGeometry, MouseEvent.GetScreenSpacePosition());
			WebViewWindow->SendMouseButton(LocalPoint, MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton, true);
		}
		if (UWebView2Subsystem* Subsystem = UWebView2Subsystem::Get())
		{
			Subsystem->SetWebViewFocused(true);
		}
		return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::Mouse);
	}

	return FReply::Unhandled();
}

FReply SCBWebView2::OnMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bIsHoveringInteractiveContent)
	{
		// Yield hit-testing on double-clicks that land in transparent regions as well.
		return FReply::Unhandled();
	}

	if (WebViewWindow.IsValid())
	{
		return ExecuteMouseButtonDoubleClickEvent(MyGeometry, MouseEvent);
	}

	return FReply::Unhandled();
}

FReply SCBWebView2::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bIsHoveringInteractiveContent && !HasMouseCapture())
	{
		return FReply::Unhandled();
	}

	if (WebViewWindow.IsValid())
	{
		if (ShouldUseSlateMouseForwarding())
		{
			const FVector2D LocalPoint = GetLocalWebViewPoint(MyGeometry, MouseEvent.GetScreenSpacePosition());
			WebViewWindow->SendMouseButton(LocalPoint, MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton, false);
		}
		return HasMouseCapture() ? FReply::Handled().ReleaseMouseCapture() : FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SCBWebView2::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (WebViewWindow.IsValid())
	{
		// Forward move events even when the region is not ultimately hit so the page can update transparency hit-testing in real time.
		if (ShouldUseSlateMouseForwarding())
		{
			const FVector2D LocalPoint = GetLocalWebViewPoint(MyGeometry, MouseEvent.GetScreenSpacePosition());
			WebViewWindow->SendMouseMove(
				LocalPoint,
				MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton),
				MouseEvent.IsMouseButtonDown(EKeys::RightMouseButton),
				MouseEvent.IsMouseButtonDown(EKeys::MiddleMouseButton));
		}
		return bIsHoveringInteractiveContent ? FReply::Handled() : FReply::Unhandled();
	}

	return FReply::Unhandled();
}

FReply SCBWebView2::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (WebViewWindow.IsValid())
	{
		if (ShouldUseSlateMouseForwarding())
		{
			const FVector2D LocalPoint = GetLocalWebViewPoint(MyGeometry, MouseEvent.GetScreenSpacePosition());
			WebViewWindow->SendMouseWheel(LocalPoint, MouseEvent.GetWheelDelta());
		}
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SCBWebView2::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	if (WebViewWindow.IsValid())
	{
		// Once Slate gains focus, synchronize actual input focus to the underlying WebView.
		SetVisibility(EVisibility::Visible);
		WebViewWindow->MoveFocus(true);
		if (UWebView2Subsystem* Subsystem = UWebView2Subsystem::Get())
		{
			Subsystem->SetWebViewFocused(true);
		}
	}

	return FReply::Handled();
}

void SCBWebView2::OnFocusLost(const FFocusEvent& InFocusEvent)
{
	const bool bIsMouseDrivenFocusChange = InFocusEvent.GetCause() == EFocusCause::Mouse;
	const bool bShouldPreserveWebViewFocus =
		(bIsEditableElementFocused) ||
		(bIsMouseDrivenFocusChange &&
			FSlateApplication::IsInitialized() &&
			bHasCachedPaintGeometry &&
			CachedPaintGeometry.IsUnderLocation(FSlateApplication::Get().GetCursorPos()));

	if (bShouldPreserveWebViewFocus)
	{
		// While a page input is actively being edited, do not call MoveFocus(false) or reset subsystem focus state.
		if (UWebView2Subsystem* Subsystem = UWebView2Subsystem::Get())
		{
			Subsystem->SetWebViewFocused(true);
		}
		SCompoundWidget::OnFocusLost(InFocusEvent);
		return;
	}

	// If Win32 focus is still inside the host window subtree, including WebView2 or IME child HWNDs,
	// then page input or IME still owns focus. Do not call MoveFocus(false) to steal it back,
	// or the text box can lose focus on the first frame after being clicked.
#if PLATFORM_WINDOWS
	{
		const HWND CurrentNativeFocus = ::GetFocus();
		HWND HostHwnd = nullptr;
		if (TSharedPtr<SWindow> HostWindow = ParentWindow.Pin())
		{
			if (HostWindow->GetNativeWindow().IsValid())
			{
				HostHwnd = static_cast<HWND>(HostWindow->GetNativeWindow()->GetOSWindowHandle());
			}
		}
		if (!HostHwnd && FSlateApplication::IsInitialized())
		{
			if (TSharedPtr<SWindow> ActiveWindow = FSlateApplication::Get().GetActiveTopLevelWindow())
			{
				if (ActiveWindow->GetNativeWindow().IsValid())
				{
					HostHwnd = static_cast<HWND>(ActiveWindow->GetNativeWindow()->GetOSWindowHandle());
				}
			}
		}

		if (HostHwnd && CurrentNativeFocus &&
			(CurrentNativeFocus == HostHwnd || ::IsChild(HostHwnd, CurrentNativeFocus)))
		{
			if (UWebView2Subsystem* Subsystem = UWebView2Subsystem::Get())
			{
				Subsystem->SetWebViewFocused(true);
			}
			SCompoundWidget::OnFocusLost(InFocusEvent);
			return;
		}
	}
#endif

	const bool bCanApplyHoverVisibility = !WebViewWindow.IsValid() || WebViewWindow->GetVisible() == ESlateVisibility::Visible;
	if (WebViewWindow.IsValid())
	{
		WebViewWindow->MoveFocus(false);

		if (TSharedPtr<SWindow> CurrentWindow = FSlateApplication::Get().GetActiveTopLevelWindow())
		{
			if (CurrentWindow->GetNativeWindow().IsValid())
			{
				if (void* NativeHandle = CurrentWindow->GetNativeWindow()->GetOSWindowHandle())
				{
					::SetFocus(static_cast<HWND>(NativeHandle));
				}
			}
		}

		if (UWebView2Subsystem* Subsystem = UWebView2Subsystem::Get())
		{
			Subsystem->SetWebViewFocused(false);
		}
	}

	if (bCanApplyHoverVisibility)
	{
		SetVisibility(bIsHoveringInteractiveContent ? EVisibility::Visible : EVisibility::HitTestInvisible);
	}

	SCompoundWidget::OnFocusLost(InFocusEvent);
}

bool SCBWebView2::SupportsKeyboardFocus() const
{
	return true;
}

FReply SCBWebView2::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (bEnableTransparencyHitTest && !bIsHoveringInteractiveContent)
	{
		return FReply::Unhandled();
	}

	if (GCBWebView2KeyReentry || !WebViewWindow.IsValid())
	{
		return WebViewWindow.IsValid() ? FReply::Handled() : FReply::Unhandled();
	}

	GCBWebView2KeyReentry = true;
	const uint32 KeyCode = InKeyEvent.GetKeyCode();
	const uint32 ScanCode = MapVirtualKey(KeyCode, 0);
	uint64 LParam = 1 | (static_cast<uint64>(ScanCode) << 16);
	if (InKeyEvent.IsRepeat())
	{
		LParam |= 0x40000000;
	}

	WebViewWindow->SendKeyboardMessage(WM_KEYDOWN, KeyCode, static_cast<int64>(LParam));
	GCBWebView2KeyReentry = false;
	return FReply::Handled();
}

FReply SCBWebView2::OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (bEnableTransparencyHitTest && !bIsHoveringInteractiveContent)
	{
		return FReply::Unhandled();
	}

	if (GCBWebView2KeyReentry || !WebViewWindow.IsValid())
	{
		return WebViewWindow.IsValid() ? FReply::Handled() : FReply::Unhandled();
	}

	GCBWebView2KeyReentry = true;
	const uint32 KeyCode = InKeyEvent.GetKeyCode();
	const uint32 ScanCode = MapVirtualKey(KeyCode, 0);
	const uint64 LParam = 1 | (static_cast<uint64>(ScanCode) << 16) | 0xC0000000;
	WebViewWindow->SendKeyboardMessage(WM_KEYUP, KeyCode, static_cast<int64>(LParam));
	GCBWebView2KeyReentry = false;
	return FReply::Handled();
}

FReply SCBWebView2::OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent)
{
	if (bEnableTransparencyHitTest && !bIsHoveringInteractiveContent)
	{
		return FReply::Unhandled();
	}

	if (GCBWebView2KeyReentry || !WebViewWindow.IsValid())
	{
		return WebViewWindow.IsValid() ? FReply::Handled() : FReply::Unhandled();
	}

	GCBWebView2KeyReentry = true;
	WebViewWindow->SendKeyboardMessage(WM_CHAR, InCharacterEvent.GetCharacter(), 1);
	GCBWebView2KeyReentry = false;
	return FReply::Handled();
}

FReply SCBWebView2::HandleBackClicked()
{
	GoBack();
	return FReply::Handled();
}

FReply SCBWebView2::HandleForwardClicked()
{
	GoForward();
	return FReply::Handled();
}

FReply SCBWebView2::HandleReloadClicked()
{
	if (WebViewWindow.IsValid() && WebViewWindow->GetDocumentLoadingState() == ECBWebView2DocumentState::Loading)
	{
		Stop();
	}
	else
	{
		Reload();
	}

	return FReply::Handled();
}

void SCBWebView2::HandleUrlCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
	if (CommitType == ETextCommit::OnEnter)
	{
		LoadURL(NewText.ToString());
	}
}

FText SCBWebView2::GetReloadButtonText() const
{
	return (WebViewWindow.IsValid() && WebViewWindow->GetDocumentLoadingState() == ECBWebView2DocumentState::Loading)
		? LOCTEXT("Stop", "Stop")
		: LOCTEXT("Reload", "Reload");
}

FText SCBWebView2::GetTitleText() const
{
	return FText::FromString(CurrentTitle);
}

FText SCBWebView2::GetAddressBarUrlText() const
{
	return FText::FromString(CurrentUrl);
}

EVisibility SCBWebView2::GetLoadingIndicatorVisibility() const
{
	return (bShowInitialThrobber && WebViewWindow.IsValid() && !WebViewWindow->IsInitialized())
		? EVisibility::Visible
		: EVisibility::Hidden;
}
FVector2D SCBWebView2::GetLocalWebViewPoint(const FGeometry& MyGeometry, const FVector2D& ScreenSpacePosition) const
{
	FVector2D LocalPoint = MyGeometry.AbsoluteToLocal(ScreenSpacePosition);
	if (bShowAddressBar || bShowControls)
	{
		LocalPoint.Y -= ToolbarHeight;
	}

	return LocalPoint * MyGeometry.Scale;
}

void SCBWebView2::ReleaseWebViewInputFocus()
{
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

#if PLATFORM_WINDOWS
	TSharedPtr<SWindow> FocusWindow = ParentWindow.Pin();
	if (!FocusWindow.IsValid())
	{
		FocusWindow = SlateApplication.GetActiveTopLevelWindow();
	}

	if (FocusWindow.IsValid() && FocusWindow->GetNativeWindow().IsValid())
	{
		if (void* NativeHandle = FocusWindow->GetNativeWindow()->GetOSWindowHandle())
		{
			const HWND HostHwnd = static_cast<HWND>(NativeHandle);
			const HWND CurrentNativeFocus = ::GetFocus();
			// Pull focus back only when the current system focus is outside the host window subtree,
			// so page input or IME focus is not stolen away.
			if (!CurrentNativeFocus ||
				(CurrentNativeFocus != HostHwnd && !::IsChild(HostHwnd, CurrentNativeFocus)))
			{
				::SetFocus(HostHwnd);
			}
		}
	}
#endif
}
FReply SCBWebView2::ExecuteMouseButtonDoubleClickEvent(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const EWebView2MouseButton Button = GetMouseButtonFromSlateEvent(MouseEvent.GetEffectingButton());
	const FVector2D ScreenSpacePosition = MouseEvent.GetScreenSpacePosition();
	const double EventTimeSeconds = FPlatformTime::Seconds();

	if (ShouldSuppressDuplicateDoubleClick(ScreenSpacePosition, Button, EventTimeSeconds))
	{
		return FReply::Handled();
	}

	RecordForwardedDoubleClick(ScreenSpacePosition, Button, EventTimeSeconds);

	if (ShouldUseSlateMouseForwarding() && WebViewWindow.IsValid())
	{
		const FVector2D LocalPoint = GetLocalWebViewPoint(MyGeometry, ScreenSpacePosition);
		WebViewWindow->SendMouseDoubleClick(LocalPoint, MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton);
	}

	if (UWebView2Subsystem* Subsystem = UWebView2Subsystem::Get())
	{
		Subsystem->SetWebViewFocused(true);
	}

	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::Mouse);
	}

	FReply Reply = FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::Mouse);
	if (OnMouseButtonDoubleClickEvent.IsBound())
	{
		FReply WidgetReply = OnMouseButtonDoubleClickEvent.Execute(MyGeometry, MouseEvent);
		if (WidgetReply.IsEventHandled())
		{
			WidgetReply.CaptureMouse(SharedThis(this));
			WidgetReply.SetUserFocus(SharedThis(this), EFocusCause::Mouse);
			return WidgetReply;
		}
	}

	return Reply;
}

void SCBWebView2::HandleNativeMouseButtonDoubleClick(const FVector2D& LocalPosition, const FVector2D& ScreenSpacePosition, EWebView2MouseButton Button)
{
	(void)LocalPosition;

	const FKey SlateButton = GetSlateMouseButtonFromNative(Button);
	if (SlateButton == EKeys::Invalid)
	{
		return;
	}

	const FModifierKeysState ModifierKeys = FSlateApplication::IsInitialized()
		? FSlateApplication::Get().GetModifierKeys()
		: FModifierKeysState();
	const FPointerEvent MouseEvent(
		0,
		ScreenSpacePosition,
		ScreenSpacePosition,
		GetPressedButtonSet(SlateButton),
		SlateButton,
		0.0f,
		ModifierKeys);

	const FGeometry EventGeometry = bHasCachedPaintGeometry
		? CachedPaintGeometry
		: GetTickSpaceGeometry();

	ExecuteMouseButtonDoubleClickEvent(EventGeometry, MouseEvent);
}

bool SCBWebView2::ShouldSuppressDuplicateDoubleClick(const FVector2D& ScreenSpacePosition, EWebView2MouseButton Button, double EventTimeSeconds) const
{
	static constexpr double DuplicateTimeThresholdSeconds = 0.05;
	static constexpr double DuplicateDistanceThresholdSquared = 4.0;

	if (LastForwardedDoubleClickTime < 0.0 || LastForwardedDoubleClickButton != Button)
	{
		return false;
	}

	if (EventTimeSeconds - LastForwardedDoubleClickTime > DuplicateTimeThresholdSeconds)
	{
		return false;
	}

	return FVector2D::DistSquared(ScreenSpacePosition, LastForwardedDoubleClickScreenPosition) <= DuplicateDistanceThresholdSquared;
}

void SCBWebView2::RecordForwardedDoubleClick(const FVector2D& ScreenSpacePosition, EWebView2MouseButton Button, double EventTimeSeconds)
{
	LastForwardedDoubleClickScreenPosition = ScreenSpacePosition;
	LastForwardedDoubleClickButton = Button;
	LastForwardedDoubleClickTime = EventTimeSeconds;
}

void SCBWebView2::BindWebViewEvents()
{
	if (!WebViewWindow.IsValid())
	{
		return;
	}

	// Bridge native-layer events into Slate state and external delegates here.
	WebViewWindow->OnMessageReceived.BindSP(this, &SCBWebView2::HandleMessageFromWeb);
	WebViewWindow->OnNavigationCompleted.BindLambda([this](bool bSuccess)
	{
		OnNavigationCompleted.ExecuteIfBound(bSuccess);
	});

	WebViewWindow->OnNavigationStarting.BindLambda([this](const FString& Url)
	{
		CurrentUrl = Url;
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

	WebViewWindow->OnMonitoredEvent.BindLambda([this](const FCBWebView2MonitoredEvent& EventInfo)
	{
		OnMonitoredEvent.ExecuteIfBound(EventInfo);
	});

	WebViewWindow->OnInputActivationRequested.BindLambda([this]()
	{
		OnInputActivationRequested.ExecuteIfBound();

		// When native WinComp routing confirms this WebView should own input, proactively acquire Slate keyboard focus.
		if (!FSlateApplication::IsInitialized())
		{
			return;
		}

		FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::Mouse);
		if (UWebView2Subsystem* Subsystem = UWebView2Subsystem::Get())
		{
			Subsystem->SetWebViewFocused(true);
		}
	});

	WebViewWindow->OnNativeMouseButtonDoubleClick.BindSP(this, &SCBWebView2::HandleNativeMouseButtonDoubleClick);

	WebViewWindow->OnCanGoBackChanged.BindLambda([this](bool bValue)
	{
		bCanGoBack = bValue;
		OnCanGoBackChanged.ExecuteIfBound(bValue);
	});

	WebViewWindow->OnCanGoForwardChanged.BindLambda([this](bool bValue)
	{
		bCanGoForward = bValue;
		OnCanGoForwardChanged.ExecuteIfBound(bValue);
	});

	WebViewWindow->OnDocumentTitleChanged.BindLambda([this](const FString& Title)
	{
		CurrentTitle = Title;
		OnDocumentTitleChanged.ExecuteIfBound(Title);
	});

	WebViewWindow->OnSourceChanged.BindLambda([this](const FString& Url)
	{
		CurrentUrl = Url;
		OnSourceChanged.ExecuteIfBound(Url);
	});

	WebViewWindow->OnCursorChanged.BindLambda([this](EMouseCursor::Type CursorType)
	{
		OnCursorChanged.ExecuteIfBound(CursorType);
		SetCursor(CursorType);
	});
}

void SCBWebView2::HandleMessageFromWeb(const FString& Message)
{
	static const FString ClickablePrefix(TEXT("IsClickable:"));
	static const FString EditableFocusPrefix(TEXT("EditableFocus:"));

	FString RoutedMessage = Message;
	FCBWebView2InternalMessage InternalMessage;
	const bool bHasInternalJsonMessage = FCBWebView2InternalMessageParser::TryParseJson(Message, InternalMessage);
	const bool bIsInternalJsonMessage = bHasInternalJsonMessage && InternalMessage.ToLegacyMessage(RoutedMessage);
	const bool bLooksLikeInternalMessage = bHasInternalJsonMessage || bIsInternalJsonMessage || Message.Contains(TEXT("__cbwebview2")) || RoutedMessage.StartsWith(ClickablePrefix) || RoutedMessage.Contains(ClickablePrefix) || RoutedMessage.StartsWith(EditableFocusPrefix) || RoutedMessage.Contains(EditableFocusPrefix);
	const bool bIsImeCaretMessage = bHasInternalJsonMessage && InternalMessage.IsImeCaret();

	// EditableFocus:1/0 is reported by the injected script and indicates whether a page-editable element owns focus.
	// Once tracked, focus-protection logic can distinguish external focus loss from temporary IME/input transitions.
	bool bNewEditableFocusState = false;
	if (FCBWebView2InternalMessageParser::TryReadLegacyBoolMessage(RoutedMessage, *EditableFocusPrefix, bNewEditableFocusState))
	{
		if (bIsEditableElementFocused != bNewEditableFocusState)
		{
			bIsEditableElementFocused = bNewEditableFocusState;

			if (bNewEditableFocusState && FSlateApplication::IsInitialized())
			{
				// A page input just gained focus: proactively claim Slate keyboard focus so subsequent keys forward to the WebView.
				FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::Mouse);
				if (UWebView2Subsystem* Subsystem = UWebView2Subsystem::Get())
				{
					Subsystem->SetWebViewFocused(true);
				}
			}
		}
	}

	bool bNewHoverState = false;
	if (FCBWebView2InternalMessageParser::TryReadLegacyBoolMessage(RoutedMessage, *ClickablePrefix, bNewHoverState))
	{
		bIsHoveringInteractiveContent = bNewHoverState;

		if (WebViewWindow.IsValid())
		{
			WebViewWindow->SetHitTestEnabled(bNewHoverState);
		}

		// Only switch to HitTestInvisible from JS reports when transparency hit-testing is enabled;
		// otherwise IsClickable:0 would also remove any keyboard focus already acquired by the widget.
		if (bEnableTransparencyHitTest)
		{
			// Do not set HitTestInvisible while a page input is active, or IME composition and selection can be interrupted.
			if (bNewHoverState || bIsEditableElementFocused)
			{
				SetVisibility(EVisibility::Visible);
			}
			else
			{
				if (HasMouseCapture() && FSlateApplication::IsInitialized())
				{
					FSlateApplication::Get().ReleaseAllPointerCapture();
				}
				SetVisibility(EVisibility::HitTestInvisible);
			}

			// Critical: when the cursor moves onto a transparent region, return keyboard focus to UE or keys like WASD keep going to the page.
			// Regardless of whether a page input is focused, entering a transparent region must:
			// 1. call JS blur so the activeElement releases page-side key handling
			// 2. call WebViewWindow->MoveFocus(false) + Subsystem::SetWebViewFocused(false)
			// 3. clear Slate keyboard focus and return it to the upper viewport
			if (!bNewHoverState)
			{
				if (WebViewWindow.IsValid())
				{
					WebViewWindow->ExecuteScript(
						TEXT("if(document.activeElement && document.activeElement.blur){document.activeElement.blur();}"),
						nullptr);
					WebViewWindow->MoveFocus(false);
				}

				if (UWebView2Subsystem* Subsystem = UWebView2Subsystem::Get())
				{
					Subsystem->SetWebViewFocused(false);
				}

				if (FSlateApplication::IsInitialized())
				{
					FSlateApplication& SlateApplication = FSlateApplication::Get();
					if (SlateApplication.GetKeyboardFocusedWidget().Get() == this)
					{
						SlateApplication.ClearKeyboardFocus(EFocusCause::Cleared);
					}

					// Explicitly pull Win32 focus back to the main host window. MoveFocus(false) alone does not change Win32 focus,
					// so a WebView2 IME child HWND can still own it. UWebView2Subsystem::Tick will not reclaim it because of IsChild,
					// and keyboard input would continue to be intercepted by WebView2 instead of reaching UE.
					if (TSharedPtr<SWindow> CurrentWindow = SlateApplication.GetActiveTopLevelWindow())
					{
						if (CurrentWindow->GetNativeWindow().IsValid())
						{
							if (void* NativeHandle = CurrentWindow->GetNativeWindow()->GetOSWindowHandle())
							{
								::SetFocus(static_cast<HWND>(NativeHandle));
							}
						}
					}
				}
			}
		}
	}

	// The `__cbwebview2` protocol and its legacy prefixes are for internal widget bridging only and should not be forwarded to Blueprint.
	if (!bIsImeCaretMessage && !bLooksLikeInternalMessage)
	{
		OnMessageReceived.ExecuteIfBound(Message);
	}
}

#undef LOCTEXT_NAMESPACE
