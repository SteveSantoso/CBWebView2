// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#include "WebView2ContinuousTextureSource.h"

#include "WebView2Log.h"
#include "WebView2Manager.h"
#include "WebView2Window.h"

#include "DynamicRHI.h"
#include "Engine/Texture2D.h"
#include "ID3D11DynamicRHI.h"
#include "WebView2D3D12RHIBridge.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "RHITypes.h"
#include "TextureResource.h"

#include <atomic>

#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/AllowWindowsPlatformAtomics.h"
THIRD_PARTY_INCLUDES_START
#include <d3d11.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_2.h>
#include <dwmapi.h>
#include <imm.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformAtomics.h"
#include "Windows/HideWindowsPlatformTypes.h"

namespace
{
	// MinUpdateInterval only exists in Windows SDK 10.0.26100+ projections; detect the member at compile
	// time so the plugin still builds against older SDKs, and probe availability at runtime before calling.
	template <typename SessionType>
	auto SetCaptureMinUpdateInterval(SessionType& Session, winrt::Windows::Foundation::TimeSpan Interval, int)
		-> decltype(Session.MinUpdateInterval(Interval), bool())
	{
		Session.MinUpdateInterval(Interval);
		return true;
	}

	template <typename SessionType>
	bool SetCaptureMinUpdateInterval(SessionType&, winrt::Windows::Foundation::TimeSpan, long)
	{
		return false;
	}

	constexpr const TCHAR* HiddenHostWindowClassName = TEXT("CBWebView2ContinuousTextureHostWindow");
	constexpr int32 HiddenHostWindowFallbackOffset = -12000;
	constexpr int32 HiddenHostWindowMinimumSafetyPadding = 12000;
	constexpr UINT HiddenHostWindowRefreshImeMessage = WM_APP + 0x201;

	// If Windows Graphics Capture never delivers a frame within this window after StartCapture, treat the
	// capture as effectively dead (unsupported Windows build, RDP/VM session, GPU driver bug, or a fully
	// off-screen window with no live DWM composition surface) and clear the placeholder texture so the user
	// sees a transparent widget plus an actionable log line instead of an uninitialized white quad.
	constexpr double NoFrameWatchdogTimeoutSeconds = 5.0;

	// How often the game thread retries starting the WGC capture pipeline when the previous attempt failed
	// (e.g. CreateForWindow rejects the offscreen window, or the composition visual is not created yet). The
	// retry prefers Visual capture as soon as the visual exists, so recovery is usually well under a second.
	constexpr double CapturePipelineRetryIntervalSeconds = 0.5;

	const TCHAR* LexToString(const ECBWebView2WorldOutputMode InMode)
	{
		switch (InMode)
		{
		case ECBWebView2WorldOutputMode::CpuReadback:
			return TEXT("CpuReadback");
		case ECBWebView2WorldOutputMode::D3D11GpuCopy:
			return TEXT("D3D11GpuCopy");
		case ECBWebView2WorldOutputMode::D3D12SharedTexture:
			return TEXT("D3D12SharedTexture");
		case ECBWebView2WorldOutputMode::Auto:
		default:
			return TEXT("Auto");
		}
	}

	ECBWebView2WorldOutputMode ResolveAutoOutputMode()
	{
		if (GDynamicRHI)
		{
			const ERHIInterfaceType InterfaceType = GDynamicRHI->GetInterfaceType();
			if (InterfaceType == ERHIInterfaceType::D3D11)
			{
				return ECBWebView2WorldOutputMode::D3D11GpuCopy;
			}
			if (InterfaceType == ERHIInterfaceType::D3D12)
			{
				// Full GPU path: shared NT-handle texture + shared fence between the capture D3D11 device
				// and the Unreal D3D12 device. Falls back to CpuReadback at runtime if interop fails.
				return ECBWebView2WorldOutputMode::D3D12SharedTexture;
			}
		}

		return ECBWebView2WorldOutputMode::CpuReadback;
	}

	bool IsD3D11RHIActive()
	{
		return GDynamicRHI != nullptr && GDynamicRHI->GetInterfaceType() == ERHIInterfaceType::D3D11;
	}

	bool IsD3D12RHIActive()
	{
		return GDynamicRHI != nullptr && GDynamicRHI->GetInterfaceType() == ERHIInterfaceType::D3D12;
	}

	POINT GetFarOffscreenWindowOrigin(const FIntPoint& InWindowSize)
	{
		const FIntPoint SafeWindowSize = InWindowSize.ComponentMax(FIntPoint(1, 1));
		const int32 VirtualLeft = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
		const int32 VirtualTop = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
		const int32 VirtualWidth = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
		const int32 VirtualHeight = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);

		if (VirtualWidth <= 0 || VirtualHeight <= 0)
		{
			return POINT
			{
				HiddenHostWindowFallbackOffset - SafeWindowSize.X,
				HiddenHostWindowFallbackOffset - SafeWindowSize.Y
			};
		}

		const int32 SafetyPadding = FMath::Max3(VirtualWidth, VirtualHeight, HiddenHostWindowMinimumSafetyPadding);
		return POINT
		{
			VirtualLeft - SafeWindowSize.X - SafetyPadding,
			VirtualTop - SafeWindowSize.Y - SafetyPadding
		};
	}

	class IHiddenHostWindowOwner
	{
	public:
		virtual void HandleHiddenHostWindowMessage(UINT Message) = 0;
		virtual ~IHiddenHostWindowOwner() = default;
	};

	bool IsImeWindowMessage(UINT Message)
	{
		switch (Message)
		{
		case WM_SETFOCUS:
		case WM_IME_SETCONTEXT:
		case WM_IME_STARTCOMPOSITION:
		case WM_IME_COMPOSITION:
		case WM_IME_NOTIFY:
		case WM_INPUTLANGCHANGE:
			return true;
		default:
			return false;
		}
	}

	LRESULT CALLBACK HiddenHostWindowProc(HWND WindowHandle, UINT Message, WPARAM WParam, LPARAM LParam)
	{
		if (Message == WM_NCCREATE)
		{
			if (const CREATESTRUCT* CreateStruct = reinterpret_cast<const CREATESTRUCT*>(LParam))
			{
				::SetWindowLongPtr(WindowHandle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(CreateStruct->lpCreateParams));
			}
		}

		if (Message == WM_NCHITTEST)
		{
			return HTTRANSPARENT;
		}

		if (Message == WM_SETCURSOR)
		{
			::SetCursor(nullptr);
			return Windows::TRUE;
		}

		if (Message == HiddenHostWindowRefreshImeMessage)
		{
			if (IHiddenHostWindowOwner* Owner = reinterpret_cast<IHiddenHostWindowOwner*>(::GetWindowLongPtr(WindowHandle, GWLP_USERDATA)))
			{
				Owner->HandleHiddenHostWindowMessage(Message);
			}

			return 0;
		}

		// Mouse input for the world-space path is forwarded manually by SCBWebView2World at the Slate layer.
		// If the hidden host HWND also routes its own messages back into the CompositionHost, input is duplicated,
		// and some synchronous WebView2 SendMouseInput callbacks can recurse back here and overflow the stack.

		const LRESULT Result = DefWindowProc(WindowHandle, Message, WParam, LParam);

		if (IsImeWindowMessage(Message))
		{
			if (::GetWindowLongPtr(WindowHandle, GWLP_USERDATA) != 0)
			{
				::PostMessageW(WindowHandle, HiddenHostWindowRefreshImeMessage, 0, 0);
			}
		}

		if (Message == WM_NCDESTROY)
		{
			::SetWindowLongPtr(WindowHandle, GWLP_USERDATA, 0);
			FWebView2Manager::Get().OnHostWindowClosed(WindowHandle);
		}

		return Result;
	}

	bool EnsureHiddenHostWindowClassRegistered()
	{
		static ATOM WindowClassAtom = 0;
		if (WindowClassAtom != 0)
		{
			return true;
		}

		WNDCLASSEX WindowClass = {};
		WindowClass.cbSize = sizeof(WindowClass);
		WindowClass.lpfnWndProc = HiddenHostWindowProc;
		WindowClass.hInstance = GetModuleHandle(nullptr);
		WindowClass.hCursor = nullptr;
		WindowClass.lpszClassName = HiddenHostWindowClassName;

		WindowClassAtom = RegisterClassEx(&WindowClass);
		return WindowClassAtom != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
	}

	bool EnsureTextureMatches(UTexture2D*& InOutTexture, const FIntPoint& InSize)
	{
		if (InOutTexture && InOutTexture->GetSizeX() == InSize.X && InOutTexture->GetSizeY() == InSize.Y)
		{
			return true;
		}

		InOutTexture = UTexture2D::CreateTransient(InSize.X, InSize.Y, PF_B8G8R8A8);
		if (!InOutTexture)
		{
			return false;
		}

		InOutTexture->SetFlags(RF_Transient);
		InOutTexture->SRGB = true;
		InOutTexture->NeverStream = true;
		InOutTexture->UpdateResource();
		FlushRenderingCommands();
		return true;
	}

	void UploadTextureData(UTexture2D* Texture, TArray<uint8>&& PixelBytes, const FIntPoint& Size)
	{
		if (!Texture || PixelBytes.Num() <= 0)
		{
			return;
		}

		// Take ownership of the frame bytes instead of copying them; the render thread frees both
		// the array and the region descriptor once the RHI upload has consumed the data.
		FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, Size.X, Size.Y);
		TArray<uint8>* UploadBytes = new TArray<uint8>(MoveTemp(PixelBytes));

		Texture->UpdateTextureRegions(
			0,
			1,
			Region,
			static_cast<uint32>(Size.X * 4),
			4,
			UploadBytes->GetData(),
			[UploadBytes](uint8* /*SrcData*/, const FUpdateTextureRegion2D* Regions)
			{
				delete UploadBytes;
				delete Regions;
			});
	}
}

/**
 * Unreal-side resources for the D3D12SharedTexture output mode: the shared capture texture and fence
 * opened on the Unreal D3D12 device, plus the RHI wrapper used as a copy source.
 * Owned jointly by FImpl and in-flight render commands; all fields except bOpenFailed are
 * only touched on the render thread.
 */
struct FWebView2D3D12SharedOutputResources
{
	winrt::com_ptr<ID3D12Resource> SharedTexture;
	winrt::com_ptr<ID3D12Fence> SharedFence;
	FTextureRHIRef WrappedRHITexture;
	uint64 Generation = 0;
	/** Set on the render thread when interop setup fails; the game thread reacts by falling back to CPU readback. */
	std::atomic<bool> bOpenFailed{false};
};

struct FWebView2ContinuousTextureSource::FImpl : IHiddenHostWindowOwner
{
	HWND HiddenWindow = nullptr;
	TWeakPtr<FWebView2Window> WebViewWindowWeak;
	FIntPoint WindowSize = FIntPoint::ZeroValue;
	winrt::com_ptr<ID3D11Device> CaptureDevice;
	winrt::com_ptr<ID3D11DeviceContext> CaptureContext;
	winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice WinRTDevice{nullptr};
	winrt::Windows::Graphics::Capture::GraphicsCaptureItem CaptureItem{nullptr};
	winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool FramePool{nullptr};
	winrt::Windows::Graphics::Capture::GraphicsCaptureSession CaptureSession{nullptr};
	winrt::event_token FrameArrivedToken{};
	FCriticalSection CaptureResourceCriticalSection;
	// Held for the whole CPU readback in OnFrameArrived (including the GPU-blocking Map) and by shutdown.
	// Lock order: CaptureReadbackCriticalSection before CaptureResourceCriticalSection.
	FCriticalSection CaptureReadbackCriticalSection;
	winrt::com_ptr<ID3D11Texture2D> StagingTexture;
	FIntPoint StagingTextureSize = FIntPoint::ZeroValue;
	FCriticalSection PendingFrameCriticalSection;
	TArray<uint8> PendingFrameBytes;
	TArray<uint8> ReadbackScratchBytes;
	winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame PendingGpuFrame{nullptr};
	winrt::com_ptr<ID3D11Texture2D> PendingGpuFrameTexture;
	FIntPoint PendingFrameSize = FIntPoint::ZeroValue;
	bool bHasPendingFrame = false;
	bool bHasPendingGpuFrame = false;

	// D3D12SharedTexture mode: capture-side shared resources (guarded by CaptureResourceCriticalSection).
	winrt::com_ptr<ID3D11DeviceContext4> CaptureContext4;
	winrt::com_ptr<ID3D11Fence> SharedFenceD3D11;
	winrt::com_ptr<ID3D11Texture2D> SharedIntermediateTexture;
	FIntPoint SharedIntermediateSize = FIntPoint::ZeroValue;
	uint64 SharedTextureGeneration = 0;
	uint64 SharedFenceNextValue = 0;
	/** NT handle of the shared fence; consumed once by the render side. Game-thread access only. */
	HANDLE SharedFenceHandle = nullptr;
	// D3D12SharedTexture pending state (guarded by PendingFrameCriticalSection).
	HANDLE PendingSharedTextureHandle = nullptr;
	uint64 PendingSharedTextureGeneration = 0;
	uint64 PendingSharedFenceValue = 0;
	bool bHasPendingSharedFrame = false;
	TSharedPtr<FWebView2D3D12SharedOutputResources, ESPMode::ThreadSafe> D3D12SharedOutput;
	TStrongObjectPtr<UTexture2D> PresentedTexture;
	double LastUploadTime = -1.0;
	bool bForceUpload = true;
	float AppliedMinUpdateIntervalFps = -1.0f;

	// Cursor alpha sampling for TextureAlpha hit-testing (game-thread access only except the D3D copy/lock).
	winrt::com_ptr<ID3D11Texture2D> AlphaSampleStagingTexture;
	FVector2D LastAlphaSamplePoint = FVector2D(-1.0e9, -1.0e9);
	double LastAlphaSampleTime = -1.0;
	uint8 LastSampledAlpha = 0;
	bool bLastAlphaSampleValid = false;
	ECBWebView2WorldOutputMode RequestedOutputMode = ECBWebView2WorldOutputMode::Auto;
	ECBWebView2WorldOutputMode ActiveOutputMode = ECBWebView2WorldOutputMode::CpuReadback;
	bool bAllowCpuFallback = true;
	bool bLoggedGpuFallback = false;
	bool bLoggedOutputMode = false;
	bool bLoggedGpuFrameReceived = false;
	// First-frame watchdog state. bHasEverReceivedFrame is set from the WGC callback thread, so it is atomic;
	// the rest are touched only on the game thread (InitializeCaptureResources / TickOutput / shutdown).
	std::atomic<bool> bHasEverReceivedFrame{false};
	double CaptureStartTime = -1.0;
	bool bLoggedNoFrameTimeout = false;
	bool bClearedPlaceholderOnTimeout = false;

	// Where WGC pulls pixels from. Window capture (CreateForWindow) is the default because the host window
	// exists synchronously; Visual capture (GraphicsCaptureItem::CreateFromVisual) is the more robust target
	// on machines where DWM keeps no composition surface for a fully-offscreen window. The watchdog auto-switches
	// from Window to Visual if no frame arrives, so no developer-facing setting is needed.
	enum class ECaptureSource : uint8 { Window, Visual };
	ECaptureSource ActiveCaptureSource = ECaptureSource::Window;
	bool bAttemptedVisualSource = false;
	// Deferred capture-pipeline start. The capture device can be ready while no pipeline is running yet, because
	// Window capture can be rejected on some machines and the composition Visual is created asynchronously. The
	// game thread retries (preferring Visual) until one source starts; only then does the first-frame watchdog arm.
	bool bCapturePipelineActive = false;
	double CaptureDeviceReadyTime = -1.0;
	double LastPipelineStartAttemptTime = -1.0;
	bool bLoggedCaptureStartFailure = false;
	FVector2D LastInputScreenSpacePosition = FVector2D::ZeroVector;
	FVector2D LastInputLocalPoint = FVector2D::ZeroVector;
	FVector2D LastImeCaretLocalPoint = FVector2D::ZeroVector;
	float LastImeCaretHeight = 18.0f;
	bool bHasInputScreenAnchor = false;
	bool bHasExplicitImeCaretAnchor = false;
	bool bIsRefreshingImeWindowPosition = false;
	bool bImeRefreshPending = false;
	// Current screen position of HiddenWindow (top-left). Used to decide whether SetWindowPos is necessary,
	// which avoids retriggering the window-position update path on every IME callback and helps prevent
	// synchronous re-entrant deadlocks with the internal WebView2 message pump, especially with multiple instances.
	FIntPoint LastAnchoredScreenPos = FIntPoint(MIN_int32, MIN_int32);

	void QueueImeWindowRefresh()
	{
		if (!HiddenWindow)
		{
			return;
		}

		bImeRefreshPending = true;
		::PostMessageW(HiddenWindow, HiddenHostWindowRefreshImeMessage, 0, 0);
	}

	void MoveWindowOffscreen()
	{
		if (!HiddenWindow)
		{
			return;
		}

		const POINT OffscreenOrigin = GetFarOffscreenWindowOrigin(WindowSize);

		if (LastAnchoredScreenPos.X == OffscreenOrigin.x && LastAnchoredScreenPos.Y == OffscreenOrigin.y)
		{
			return;
		}

		LastAnchoredScreenPos = FIntPoint(OffscreenOrigin.x, OffscreenOrigin.y);

		::SetWindowPos(
			HiddenWindow,
			HWND_BOTTOM,
			OffscreenOrigin.x,
			OffscreenOrigin.y,
			WindowSize.X,
			WindowSize.Y,
			SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOREDRAW | SWP_NOSENDCHANGING);
	}

	void RepositionWindowToInputAnchor(const FVector2D& InScreenTopLeft)
	{
		// HiddenWindow is already hidden from the user via DWM cloak, so it can safely be moved into the visible desktop area.
		// Windows positions IME candidate windows from the owner HWND screen position, so SetWindowPos is enough for the OS to see the new location.
		// There is no need to call ICoreWebView2Controller::NotifyParentWindowPositionChanged here,
		// because that call can synchronously pump messages and block between CompositionController instances sharing the same DispatcherQueue.
		if (!HiddenWindow)
		{
			return;
		}

		const int32 AnchorX = FMath::RoundToInt(InScreenTopLeft.X);
		const int32 AnchorY = FMath::RoundToInt(InScreenTopLeft.Y);

		// Return immediately when the position is unchanged so frequent IME callbacks do not trigger a WM_WINDOWPOSCHANGED storm.
		if (LastAnchoredScreenPos.X == AnchorX && LastAnchoredScreenPos.Y == AnchorY)
		{
			return;
		}

		LastAnchoredScreenPos = FIntPoint(AnchorX, AnchorY);

		::SetWindowPos(
			HiddenWindow,
			HWND_BOTTOM,
			AnchorX,
			AnchorY,
			WindowSize.X,
			WindowSize.Y,
			SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOREDRAW | SWP_NOSENDCHANGING);

		// The WebView2 controller caches the parent window's screen position and uses it to position its internal IME candidate window.
		// SetWindowPos alone does not force WebView2 to refresh that cached value, so notify it explicitly.
		if (TSharedPtr<FWebView2Window> PinnedWindow = WebViewWindowWeak.Pin())
		{
			PinnedWindow->NotifyParentWindowPositionChanged();
		}
	}

	bool CreateHiddenWindow(const FIntPoint& InSize, bool bInEnableTransparencyHitTest)
	{
		if (HiddenWindow)
		{
			ResizeHiddenWindow(InSize);
			return true;
		}

		if (!EnsureHiddenHostWindowClassRegistered())
		{
			return false;
		}

		WindowSize = InSize.ComponentMax(FIntPoint(1, 1));
		const POINT OffscreenOrigin = GetFarOffscreenWindowOrigin(WindowSize);
		DWORD WindowExStyle = WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT;
		if (bInEnableTransparencyHitTest)
		{
			WindowExStyle |= WS_EX_NOACTIVATE;
		}
		HiddenWindow = CreateWindowEx(
			WindowExStyle,
			HiddenHostWindowClassName,
			TEXT("CBWebView2ContinuousTextureHost"),
			WS_POPUP,
			OffscreenOrigin.x,
			OffscreenOrigin.y,
			WindowSize.X,
			WindowSize.Y,
			nullptr,
			nullptr,
			GetModuleHandle(nullptr),
			this);

		if (!HiddenWindow)
		{
			return false;
		}

		// Mark HiddenWindow with DWM cloak so it is invisible to the user but still composed by DWM.
		// That allows the window to live at a real desktop screen position for IME candidate positioning,
		// while keeping WebView2 invisible on the desktop and still capturable through the Graphics Capture API.
		BOOL bCloak = Windows::TRUE;
		::DwmSetWindowAttribute(HiddenWindow, DWMWA_CLOAK, &bCloak, sizeof(bCloak));

		ShowWindow(HiddenWindow, SW_SHOWNOACTIVATE);
		UpdateWindow(HiddenWindow);
		return true;
	}

	void DestroyHiddenWindow()
	{
		if (HiddenWindow)
		{
			DestroyWindow(HiddenWindow);
			HiddenWindow = nullptr;
		}

		WindowSize = FIntPoint::ZeroValue;
		bHasInputScreenAnchor = false;
		::DestroyCaret();
	}

	bool TryGetCaretClientPoint(POINT& OutCaretClientPoint, LONG& OutCaretHeight) const
	{
		if (!HiddenWindow)
		{
			return false;
		}

		const DWORD WindowThreadId = ::GetWindowThreadProcessId(HiddenWindow, nullptr);
		GUITHREADINFO GuiThreadInfo = {};
		GuiThreadInfo.cbSize = sizeof(GuiThreadInfo);
		if (WindowThreadId != 0 && ::GetGUIThreadInfo(WindowThreadId, &GuiThreadInfo))
		{
			HWND CaretWindow = GuiThreadInfo.hwndCaret ? GuiThreadInfo.hwndCaret : GuiThreadInfo.hwndFocus;
			if (CaretWindow)
			{
				POINT CaretPoints[2] =
				{
					{GuiThreadInfo.rcCaret.left, GuiThreadInfo.rcCaret.top},
					{GuiThreadInfo.rcCaret.right, GuiThreadInfo.rcCaret.bottom}
				};

				if (CaretWindow != HiddenWindow)
				{
					::MapWindowPoints(CaretWindow, HiddenWindow, CaretPoints, 2);
				}

				OutCaretClientPoint = CaretPoints[0];
				OutCaretHeight = FMath::Max<LONG>(CaretPoints[1].y - CaretPoints[0].y, 18);
				return true;
			}
		}

		POINT CaretPoint = {};
		if (::GetCaretPos(&CaretPoint))
		{
			OutCaretClientPoint = CaretPoint;
			OutCaretHeight = 18;
			return true;
		}

		return false;
	}

	bool IsCurrentProcessWindow(HWND WindowHandle) const
	{
		if (!WindowHandle)
		{
			return false;
		}

		DWORD WindowProcessId = 0;
		::GetWindowThreadProcessId(WindowHandle, &WindowProcessId);
		return WindowProcessId == ::GetCurrentProcessId();
	}

	void ApplyImeWindowPosition(HWND TargetWindow, const POINT& CaretScreenPoint, LONG CaretHeight, bool bUpdateCaret)
	{
		if (!TargetWindow || !IsCurrentProcessWindow(TargetWindow))
		{
			return;
		}

		HIMC InputContext = ::ImmGetContext(TargetWindow);
		if (!InputContext)
		{
			return;
		}

		POINT ClientPoint = CaretScreenPoint;
		::ScreenToClient(TargetWindow, &ClientPoint);
		CaretHeight = FMath::Max<LONG>(CaretHeight, 18);

		if (bUpdateCaret)
		{
			::CreateCaret(TargetWindow, nullptr, 1, CaretHeight);
			::SetCaretPos(ClientPoint.x, ClientPoint.y);
			::ShowCaret(TargetWindow);
		}

		COMPOSITIONFORM CompositionForm = {};
		CompositionForm.dwStyle = CFS_FORCE_POSITION;
		CompositionForm.ptCurrentPos = ClientPoint;
		::ImmSetCompositionWindow(InputContext, &CompositionForm);

		for (DWORD CandidateIndex = 0; CandidateIndex < 4; ++CandidateIndex)
		{
			CANDIDATEFORM CandidateForm = {};
			CandidateForm.dwIndex = CandidateIndex;
			CandidateForm.dwStyle = CFS_EXCLUDE;
			CandidateForm.ptCurrentPos = ClientPoint;
			CandidateForm.rcArea.left = ClientPoint.x;
			CandidateForm.rcArea.top = ClientPoint.y;
			CandidateForm.rcArea.right = ClientPoint.x + 1;
			CandidateForm.rcArea.bottom = ClientPoint.y + CaretHeight;
			::ImmSetCandidateWindow(InputContext, &CandidateForm);
		}

		::ImmReleaseContext(TargetWindow, InputContext);
	}

	void ApplyImeWindowPositionToActiveContexts(const POINT& CaretScreenPoint, LONG CaretHeight)
	{
		HWND AppliedWindows[4] = {};
		int32 AppliedWindowCount = 0;

		auto ApplyOnce = [&](HWND TargetWindow, bool bUpdateCaret)
		{
			if (!TargetWindow)
			{
				return;
			}

			for (int32 Index = 0; Index < AppliedWindowCount; ++Index)
			{
				if (AppliedWindows[Index] == TargetWindow)
				{
					return;
				}
			}

			if (AppliedWindowCount < UE_ARRAY_COUNT(AppliedWindows))
			{
				AppliedWindows[AppliedWindowCount++] = TargetWindow;
			}

			ApplyImeWindowPosition(TargetWindow, CaretScreenPoint, CaretHeight, bUpdateCaret);
		};

		ApplyOnce(HiddenWindow, true);
		ApplyOnce(::GetFocus(), false);
		ApplyOnce(::GetActiveWindow(), false);
		ApplyOnce(::GetForegroundWindow(), false);
	}

	void RefreshImeWindowPosition()
	{
		if (!HiddenWindow || !bHasInputScreenAnchor)
		{
			return;
		}

		if (bIsRefreshingImeWindowPosition)
		{
			bImeRefreshPending = true;
			return;
		}

		TGuardValue<bool> RefreshGuard(bIsRefreshingImeWindowPosition, true);
		bImeRefreshPending = false;

		FVector2D DesiredBrowserScreenTopLeft = LastInputScreenSpacePosition - LastInputLocalPoint;
		if (bHasExplicitImeCaretAnchor)
		{
			DesiredBrowserScreenTopLeft.Y += LastImeCaretHeight;
		}
		RepositionWindowToInputAnchor(DesiredBrowserScreenTopLeft);

		POINT CaretClientPoint =
		{
			FMath::RoundToInt(bHasExplicitImeCaretAnchor ? LastImeCaretLocalPoint.X : LastInputLocalPoint.X),
			FMath::RoundToInt(bHasExplicitImeCaretAnchor ? LastImeCaretLocalPoint.Y : LastInputLocalPoint.Y)
		};
		LONG CaretHeight = FMath::Max<LONG>(FMath::RoundToInt(LastImeCaretHeight), 18);
		if (!bHasExplicitImeCaretAnchor)
		{
			TryGetCaretClientPoint(CaretClientPoint, CaretHeight);
		}

		const POINT CaretScreenPoint =
		{
			FMath::RoundToInt(DesiredBrowserScreenTopLeft.X) + CaretClientPoint.x,
			FMath::RoundToInt(DesiredBrowserScreenTopLeft.Y) + CaretClientPoint.y
		};
		ApplyImeWindowPositionToActiveContexts(CaretScreenPoint, CaretHeight);

		if (bImeRefreshPending)
		{
			QueueImeWindowRefresh();
		}
	}

	void UpdateInputScreenAnchor(const FVector2D& InScreenSpacePosition, const FVector2D& InLocalWebViewPoint)
	{
		LastInputScreenSpacePosition = InScreenSpacePosition;
		LastInputLocalPoint = InLocalWebViewPoint;
		bHasInputScreenAnchor = true;
		bHasExplicitImeCaretAnchor = false;
		QueueImeWindowRefresh();
	}

	void UpdateImeCaretAnchor(const FVector2D& InScreenSpacePosition, const FVector2D& InLocalWebViewPoint, float InCaretHeight)
	{
		LastInputScreenSpacePosition = InScreenSpacePosition;
		LastInputLocalPoint = InLocalWebViewPoint;
		LastImeCaretLocalPoint = InLocalWebViewPoint;
		LastImeCaretHeight = FMath::Max(InCaretHeight, 1.0f);
		bHasInputScreenAnchor = true;
		bHasExplicitImeCaretAnchor = true;
		QueueImeWindowRefresh();
	}

	void ReleaseInputScreenAnchor()
	{
		bHasInputScreenAnchor = false;
		bHasExplicitImeCaretAnchor = false;
		bImeRefreshPending = false;
		::DestroyCaret();
		MoveWindowOffscreen();
	}

	void ResizeHiddenWindow(const FIntPoint& InSize)
	{
		WindowSize = InSize.ComponentMax(FIntPoint(1, 1));
		if (!HiddenWindow)
		{
			return;
		}

		if (bHasInputScreenAnchor)
		{
			RepositionWindowToInputAnchor(LastInputScreenSpacePosition - LastInputLocalPoint);
		}
		else
		{
			MoveWindowOffscreen();
		}

		FScopeLock ScopeLock(&CaptureResourceCriticalSection);
		if (FramePool && WinRTDevice)
		{
			FramePool.Recreate(
				WinRTDevice,
				winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
				2,
				{WindowSize.X, WindowSize.Y});
		}
	}

	bool FallBackToCpuReadback(const TCHAR* Reason)
	{
		if (!bAllowCpuFallback)
		{
			UE_LOG(LogWebView2Utils, Error, TEXT("CBWebView2 world output %s is unavailable: %s"), LexToString(ActiveOutputMode), Reason);
			return false;
		}

		if (!bLoggedGpuFallback)
		{
			UE_LOG(LogWebView2Utils, Warning, TEXT("CBWebView2 world output %s is unavailable: %s. Falling back to CpuReadback."), LexToString(ActiveOutputMode), Reason);
			bLoggedGpuFallback = true;
		}

		ActiveOutputMode = ECBWebView2WorldOutputMode::CpuReadback;
		return true;
	}

	bool PrepareOutputMode()
	{
		ActiveOutputMode = RequestedOutputMode == ECBWebView2WorldOutputMode::Auto
			? ResolveAutoOutputMode()
			: RequestedOutputMode;

		if (!bLoggedOutputMode)
		{
			const TCHAR* RHIName = GDynamicRHI ? GDynamicRHI->GetName() : TEXT("None");
			UE_LOG(LogWebView2Utils, Log, TEXT("CBWebView2 world output requested=%s active=%s rhi=%s cpuFallback=%s"),
				LexToString(RequestedOutputMode),
				LexToString(ActiveOutputMode),
				RHIName,
				bAllowCpuFallback ? TEXT("true") : TEXT("false"));
			bLoggedOutputMode = true;
		}

		if (ActiveOutputMode == ECBWebView2WorldOutputMode::D3D11GpuCopy && !IsD3D11RHIActive())
		{
			return FallBackToCpuReadback(TEXT("the active Unreal RHI is not D3D11"));
		}

		if (ActiveOutputMode == ECBWebView2WorldOutputMode::D3D12SharedTexture && !IsD3D12RHIActive())
		{
			return FallBackToCpuReadback(TEXT("the active Unreal RHI is not D3D12"));
		}

		return true;
	}

	/**
	 * Create the capture D3D11 device on the same adapter as the Unreal D3D12 device (shared-handle interop
	 * requires it) and create the shared fence. On failure, falls back and leaves CaptureDevice/Context valid
	 * when they can still serve the CPU readback path.
	 */
	bool InitializeD3D12SharedCaptureDevice()
	{
		if (!IsD3D12RHIActive())
		{
			FallBackToCpuReadback(TEXT("the active Unreal RHI changed before capture initialization"));
			return false;
		}

		ID3D12Device* D3D12Device = GDynamicRHI ? static_cast<ID3D12Device*>(GDynamicRHI->RHIGetNativeDevice()) : nullptr;
		if (!D3D12Device)
		{
			FallBackToCpuReadback(TEXT("the Unreal D3D12 device is not available"));
			return false;
		}

		const LUID AdapterLuid = D3D12Device->GetAdapterLuid();
		winrt::com_ptr<IDXGIFactory1> DxgiFactory;
		if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), DxgiFactory.put_void())) || DxgiFactory.get() == nullptr)
		{
			FallBackToCpuReadback(TEXT("CreateDXGIFactory1 failed"));
			return false;
		}

		winrt::com_ptr<IDXGIAdapter1> MatchingAdapter;
		for (UINT AdapterIndex = 0;; ++AdapterIndex)
		{
			winrt::com_ptr<IDXGIAdapter1> Adapter;
			if (DxgiFactory->EnumAdapters1(AdapterIndex, Adapter.put()) == DXGI_ERROR_NOT_FOUND)
			{
				break;
			}

			DXGI_ADAPTER_DESC1 AdapterDesc = {};
			if (Adapter && SUCCEEDED(Adapter->GetDesc1(&AdapterDesc)) &&
				AdapterDesc.AdapterLuid.LowPart == AdapterLuid.LowPart &&
				AdapterDesc.AdapterLuid.HighPart == AdapterLuid.HighPart)
			{
				MatchingAdapter = Adapter;
				break;
			}
		}

		if (MatchingAdapter.get() == nullptr)
		{
			FallBackToCpuReadback(TEXT("no DXGI adapter matches the Unreal D3D12 adapter LUID"));
			return false;
		}

		D3D_FEATURE_LEVEL FeatureLevels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
		D3D_FEATURE_LEVEL CreatedFeatureLevel = D3D_FEATURE_LEVEL_11_0;
		const HRESULT CreateResult = D3D11CreateDevice(
			MatchingAdapter.get(),
			D3D_DRIVER_TYPE_UNKNOWN,
			nullptr,
			D3D11_CREATE_DEVICE_BGRA_SUPPORT,
			FeatureLevels,
			UE_ARRAY_COUNT(FeatureLevels),
			D3D11_SDK_VERSION,
			CaptureDevice.put(),
			&CreatedFeatureLevel,
			CaptureContext.put());
		if (FAILED(CreateResult) || CaptureDevice.get() == nullptr || CaptureContext.get() == nullptr)
		{
			CaptureDevice = nullptr;
			CaptureContext = nullptr;
			FallBackToCpuReadback(TEXT("D3D11CreateDevice on the Unreal adapter failed"));
			return false;
		}

		// From here on the device stays usable for CPU readback even when fence interop is unavailable.
		CaptureContext4 = CaptureContext.try_as<ID3D11DeviceContext4>();
		winrt::com_ptr<ID3D11Device5> Device5 = CaptureDevice.try_as<ID3D11Device5>();
		if (CaptureContext4.get() == nullptr || Device5.get() == nullptr)
		{
			FallBackToCpuReadback(TEXT("ID3D11Device5 / ID3D11DeviceContext4 (shared fences) are not available"));
			return false;
		}

		if (FAILED(Device5->CreateFence(0, D3D11_FENCE_FLAG_SHARED, __uuidof(ID3D11Fence), SharedFenceD3D11.put_void())) || SharedFenceD3D11.get() == nullptr)
		{
			SharedFenceD3D11 = nullptr;
			FallBackToCpuReadback(TEXT("creating the shared fence failed"));
			return false;
		}

		HANDLE FenceHandle = nullptr;
		if (FAILED(SharedFenceD3D11->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &FenceHandle)) || !FenceHandle)
		{
			SharedFenceD3D11 = nullptr;
			FallBackToCpuReadback(TEXT("sharing the fence handle failed"));
			return false;
		}

		SharedFenceHandle = FenceHandle;
		return true;
	}

	bool InitializeD3D11CaptureDevice()
	{
		if (ActiveOutputMode == ECBWebView2WorldOutputMode::D3D11GpuCopy)
		{
			if (!IsD3D11RHIActive())
			{
				if (!FallBackToCpuReadback(TEXT("the active Unreal RHI changed before capture initialization")))
				{
					return false;
				}
			}
			else
			{
				ID3D11DynamicRHI* D3D11RHI = GetID3D11DynamicRHI();
				if (!D3D11RHI || !D3D11RHI->RHIGetDevice() || !D3D11RHI->RHIGetDeviceContext())
				{
					if (!FallBackToCpuReadback(TEXT("the Unreal D3D11 device/context is not available")))
					{
						return false;
					}
				}
				else
				{
					CaptureDevice.copy_from(D3D11RHI->RHIGetDevice());
					CaptureContext.copy_from(D3D11RHI->RHIGetDeviceContext());
					return CaptureDevice.get() != nullptr && CaptureContext.get() != nullptr;
				}

			}
		}

		if (ActiveOutputMode == ECBWebView2WorldOutputMode::D3D12SharedTexture)
		{
			if (InitializeD3D12SharedCaptureDevice())
			{
				return true;
			}

			if (ActiveOutputMode == ECBWebView2WorldOutputMode::D3D12SharedTexture)
			{
				return false;
			}

			if (CaptureDevice.get() != nullptr && CaptureContext.get() != nullptr)
			{
				// The adapter-matched device was created before interop failed; reuse it for CPU readback.
				return true;
			}
		}

		UINT DeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
		D3D_FEATURE_LEVEL FeatureLevels[] =
		{
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_10_1,
			D3D_FEATURE_LEVEL_10_0
		};

		D3D_FEATURE_LEVEL CreatedFeatureLevel = D3D_FEATURE_LEVEL_11_0;
		HRESULT Result = D3D11CreateDevice(
			nullptr,
			D3D_DRIVER_TYPE_HARDWARE,
			nullptr,
			DeviceFlags,
			FeatureLevels,
			UE_ARRAY_COUNT(FeatureLevels),
			D3D11_SDK_VERSION,
			CaptureDevice.put(),
			&CreatedFeatureLevel,
			CaptureContext.put());

		if (FAILED(Result))
		{
			Result = D3D11CreateDevice(
				nullptr,
				D3D_DRIVER_TYPE_WARP,
				nullptr,
				DeviceFlags,
				FeatureLevels,
				UE_ARRAY_COUNT(FeatureLevels),
				D3D11_SDK_VERSION,
				CaptureDevice.put(),
				&CreatedFeatureLevel,
				CaptureContext.put());
		}

		return SUCCEEDED(Result) && CaptureDevice.get() != nullptr && CaptureContext.get() != nullptr;
	}

	/** Whether Windows Graphics Capture is available on this OS at all. Older Windows 10 builds, some Server SKUs, and sessions without DWM (certain RDP/VM configurations) report false. */
	static bool IsGraphicsCaptureSupported()
	{
		// IsSupported() can throw on systems where the WinRT type is absent; treat any failure as unsupported.
		try
		{
			return winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported();
		}
		catch (...)
		{
			return false;
		}
	}

	bool InitializeCaptureResources()
	{
		if (!HiddenWindow)
		{
			UE_LOG(LogWebView2Utils, Error, TEXT("CBWebView2 world capture init failed: the offscreen host window is missing."));
			return false;
		}

		if (!IsGraphicsCaptureSupported())
		{
			UE_LOG(LogWebView2Utils, Error,
				TEXT("CBWebView2 world capture is unavailable: Windows Graphics Capture is not supported on this machine. ")
				TEXT("This typically means an old Windows 10 build (needs 1903+), a session without DWM (some Remote Desktop / VM configs), or a disabled/blocked screen-capture policy. The world widget will show a transparent (blank) texture."));
			return false;
		}

		if (!PrepareOutputMode() || !InitializeD3D11CaptureDevice())
		{
			UE_LOG(LogWebView2Utils, Error, TEXT("CBWebView2 world capture init failed: could not prepare the output mode or create the capture D3D11 device (check the GPU driver / adapter)."));
			return false;
		}

		winrt::com_ptr<IDXGIDevice> DxgiDevice;
		CaptureDevice.as(DxgiDevice);
		if (DxgiDevice.get() == nullptr)
		{
			UE_LOG(LogWebView2Utils, Error, TEXT("CBWebView2 world capture init failed: the capture D3D11 device does not expose IDXGIDevice."));
			return false;
		}

		winrt::com_ptr<IInspectable> InspectableDevice;
		HRESULT Result = CreateDirect3D11DeviceFromDXGIDevice(DxgiDevice.get(), InspectableDevice.put());
		if (FAILED(Result) || InspectableDevice.get() == nullptr)
		{
			UE_LOG(LogWebView2Utils, Error, TEXT("CBWebView2 world capture init failed: CreateDirect3D11DeviceFromDXGIDevice returned 0x%08X."), static_cast<uint32>(Result));
			return false;
		}

		WinRTDevice = InspectableDevice.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
		if (!WinRTDevice)
		{
			UE_LOG(LogWebView2Utils, Error, TEXT("CBWebView2 world capture init failed: could not obtain the IDirect3DDevice WinRT projection."));
			return false;
		}

		bLoggedNoFrameTimeout = false;
		bClearedPlaceholderOnTimeout = false;
		bAttemptedVisualSource = false;
		bLoggedCaptureStartFailure = false;
		CaptureDeviceReadyTime = FPlatformTime::Seconds();
		LastPipelineStartAttemptTime = -1.0;

		// Try Window capture first: its host window exists synchronously, so machines where it already works are
		// unaffected. But do NOT fail initialization if it cannot start — on some machines CreateForWindow rejects
		// the offscreen window outright (observed: "could not create the capture item for source=Window"). The
		// capture device is ready regardless; TickEnsureCapturePipeline() retries on the game thread and switches to
		// Visual capture (CreateFromVisual) as soon as the composition visual exists, which is created asynchronously.
		StartCapturePipeline(ECaptureSource::Window);
		return true;
	}

	/** Build the WGC capture item from the requested source. CaptureItem is left null on failure. */
	bool CreateCaptureItemForSource(ECaptureSource Source)
	{
		CaptureItem = nullptr;

		if (Source == ECaptureSource::Visual)
		{
			const TSharedPtr<FWebView2Window> Window = WebViewWindowWeak.Pin();
			if (!Window.IsValid() || !Window->WebViewVisual)
			{
				// The WebView2 composition visual is created asynchronously; it may simply not exist yet.
				return false;
			}

			// GraphicsCaptureItem::CreateFromVisual captures the live WinRT composition subtree directly, so it does
			// not depend on the offscreen host window having a DWM composition surface (the usual cause of blank frames).
			try
			{
				CaptureItem = winrt::Windows::Graphics::Capture::GraphicsCaptureItem::CreateFromVisual(Window->WebViewVisual);
			}
			catch (...)
			{
				CaptureItem = nullptr;
			}
			return CaptureItem != nullptr;
		}

		auto CaptureItemInterop = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
		if (!CaptureItemInterop)
		{
			return false;
		}

		const HRESULT Result = CaptureItemInterop->CreateForWindow(
			HiddenWindow,
			winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
			reinterpret_cast<void**>(winrt::put_abi(CaptureItem)));
		return SUCCEEDED(Result) && CaptureItem != nullptr;
	}

	/**
	 * (Re)build the frame pool + session against the requested capture source and start capture, reusing the
	 * already-created capture device. Tears down any previous pool/session first. Re-arms the first-frame watchdog.
	 */
	bool StartCapturePipeline(ECaptureSource Source)
	{
		if (!WinRTDevice)
		{
			UE_LOG(LogWebView2Utils, Error, TEXT("CBWebView2 world capture init failed: the WinRT capture device is not available."));
			return false;
		}

		// Drop any existing pipeline (keep the capture device) before rebuilding. Unsubscribe first so no new
		// OnFrameArrived starts, then take both locks (readback before resource, matching ShutdownCaptureResources)
		// to wait out any in-flight CPU readback before releasing the pool/session.
		if (FramePool)
		{
			FramePool.FrameArrived(FrameArrivedToken);
		}
		{
			FScopeLock ReadbackScopeLock(&CaptureReadbackCriticalSection);
			FScopeLock ScopeLock(&CaptureResourceCriticalSection);
			CaptureSession = nullptr;
			FramePool = nullptr;
			CaptureItem = nullptr;
		}
		bCapturePipelineActive = false;

		// Log a start failure at most once until the next successful start, so the game-thread retry loop
		// (TickEnsureCapturePipeline, every CapturePipelineRetryIntervalSeconds) does not spam the log.
		const auto LogStartFailureOnce = [this](const TCHAR* Reason)
		{
			if (!bLoggedCaptureStartFailure)
			{
				UE_LOG(LogWebView2Utils, Warning, TEXT("CBWebView2 world capture pipeline start failed (will retry): %s"), Reason);
				bLoggedCaptureStartFailure = true;
			}
		};

		if (!CreateCaptureItemForSource(Source))
		{
			LogStartFailureOnce(Source == ECaptureSource::Visual
				? TEXT("could not create the capture item for source=Visual (the composition visual may not be ready yet)")
				: TEXT("could not create the capture item for source=Window"));
			return false;
		}

		FramePool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
			WinRTDevice,
			winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
			2,
			{WindowSize.X, WindowSize.Y});
		if (!FramePool)
		{
			LogStartFailureOnce(TEXT("Direct3D11CaptureFramePool::CreateFreeThreaded returned null"));
			return false;
		}

		FrameArrivedToken = FramePool.FrameArrived({this, &FImpl::OnFrameArrived});
		CaptureSession = FramePool.CreateCaptureSession(CaptureItem);
		if (!CaptureSession)
		{
			LogStartFailureOnce(TEXT("CreateCaptureSession returned null"));
			FramePool.FrameArrived(FrameArrivedToken);
			FramePool = nullptr;
			return false;
		}

		ApplyCaptureSessionQualityOptions();
		// Force MinUpdateInterval to be re-pushed onto the new session on the next tick.
		AppliedMinUpdateIntervalFps = -1.0f;
		CaptureSession.StartCapture();

		ActiveCaptureSource = Source;
		bCapturePipelineActive = true;
		bLoggedCaptureStartFailure = false;
		UE_LOG(LogWebView2Utils, Log, TEXT("CBWebView2 world capture started: source=%s size=%dx%d."),
			Source == ECaptureSource::Visual ? TEXT("Visual") : TEXT("Window"), WindowSize.X, WindowSize.Y);

		// Arm the first-frame watchdog (see NoFrameWatchdogTimeoutSeconds) for this source.
		bHasEverReceivedFrame = false;
		bLoggedGpuFrameReceived = false;
		CaptureStartTime = FPlatformTime::Seconds();
		return true;
	}

	/**
	 * Game-thread retry for the capture pipeline. Runs while no pipeline is active (Window capture was rejected,
	 * or the composition visual was not ready at init). Throttled by CapturePipelineRetryIntervalSeconds; prefers
	 * Visual capture (CreateFromVisual) the moment the visual exists, since it does not need the offscreen window
	 * to be capturable. If nothing can start for NoFrameWatchdogTimeoutSeconds, clears the placeholder to
	 * transparent so the widget never sits on an uninitialized white texture. Returns true on the clearing tick.
	 */
	bool TickEnsureCapturePipeline(UTexture2D*& OutUpdatedTexture)
	{
		if (bCapturePipelineActive)
		{
			return false;
		}

		const double Now = FPlatformTime::Seconds();
		if (LastPipelineStartAttemptTime < 0.0 || Now - LastPipelineStartAttemptTime >= CapturePipelineRetryIntervalSeconds)
		{
			LastPipelineStartAttemptTime = Now;

			const TSharedPtr<FWebView2Window> Window = WebViewWindowWeak.Pin();
			const bool bVisualReady = Window.IsValid() && Window->WebViewVisual != nullptr;
			if (bVisualReady && StartCapturePipeline(ECaptureSource::Visual))
			{
				bAttemptedVisualSource = true;
				return false;
			}
			if (StartCapturePipeline(ECaptureSource::Window))
			{
				return false;
			}
		}

		// Could not start any pipeline yet. Once the grace period elapses, fall back to a transparent clear (once).
		if (!bClearedPlaceholderOnTimeout && CaptureDeviceReadyTime >= 0.0 &&
			Now - CaptureDeviceReadyTime >= NoFrameWatchdogTimeoutSeconds)
		{
			if (!bLoggedNoFrameTimeout)
			{
				UE_LOG(LogWebView2Utils, Warning,
					TEXT("CBWebView2 world could not start any capture pipeline within %.0fs (window capture rejected and the composition visual never became capturable). ")
					TEXT("Check the Windows build, GPU driver, and that this is not a Remote Desktop / headless session. Clearing the widget to transparent."),
					NoFrameWatchdogTimeoutSeconds);
				bLoggedNoFrameTimeout = true;
			}
			return ClearPlaceholderToTransparent(OutUpdatedTexture);
		}

		return false;
	}

	/** Upload a fully transparent frame so a failed capture reads as blank instead of an uninitialized white quad. Returns true if a texture was produced. */
	bool ClearPlaceholderToTransparent(UTexture2D*& OutUpdatedTexture)
	{
		FIntPoint ClearSize = FIntPoint::ZeroValue;
		if (UTexture2D* Existing = PresentedTexture.Get())
		{
			ClearSize = FIntPoint(Existing->GetSizeX(), Existing->GetSizeY());
		}
		if (ClearSize.X <= 0 || ClearSize.Y <= 0)
		{
			ClearSize = WindowSize;
		}
		if (ClearSize.X <= 0 || ClearSize.Y <= 0)
		{
			return false;
		}

		UTexture2D* Texture = PresentedTexture.Get();
		if (!EnsureTextureMatches(Texture, ClearSize))
		{
			return false;
		}
		PresentedTexture.Reset(Texture);

		TArray<uint8> TransparentBytes;
		TransparentBytes.SetNumZeroed(ClearSize.X * ClearSize.Y * 4);
		UploadTextureData(Texture, MoveTemp(TransparentBytes), ClearSize);
		bClearedPlaceholderOnTimeout = true;
		OutUpdatedTexture = Texture;
		return true;
	}

	/** Disable capture extras that only cost performance for an offscreen, cloaked host window. Each option is probed because availability depends on the OS build. */
	void ApplyCaptureSessionQualityOptions()
	{
		using winrt::Windows::Foundation::Metadata::ApiInformation;
		static const winrt::hstring SessionTypeName = L"Windows.Graphics.Capture.GraphicsCaptureSession";

		try
		{
			if (ApiInformation::IsPropertyPresent(SessionTypeName, L"IsCursorCaptureEnabled"))
			{
				CaptureSession.IsCursorCaptureEnabled(false);
			}
		}
		catch (...)
		{
		}

		try
		{
			if (ApiInformation::IsPropertyPresent(SessionTypeName, L"IsBorderRequired"))
			{
				CaptureSession.IsBorderRequired(false);
			}
		}
		catch (...)
		{
			// Removing the capture border can require programmatic-capture consent on some builds; keep the border if denied.
		}
	}

	/** Push the widget refresh rate down into the OS capture layer (Win11 24H2+) so throttled frames are never produced at all. */
	void ApplyMinUpdateInterval(float InMaxFramesPerSecond)
	{
		if (InMaxFramesPerSecond == AppliedMinUpdateIntervalFps)
		{
			return;
		}

		AppliedMinUpdateIntervalFps = InMaxFramesPerSecond;

		FScopeLock ScopeLock(&CaptureResourceCriticalSection);
		if (!CaptureSession)
		{
			return;
		}

		using winrt::Windows::Foundation::Metadata::ApiInformation;
		try
		{
			if (ApiInformation::IsPropertyPresent(L"Windows.Graphics.Capture.GraphicsCaptureSession", L"MinUpdateInterval"))
			{
				const int64 IntervalHundredNanoseconds = InMaxFramesPerSecond > 0.0f
					? static_cast<int64>(10000000.0 / InMaxFramesPerSecond)
					: 0;
				SetCaptureMinUpdateInterval(CaptureSession, winrt::Windows::Foundation::TimeSpan{IntervalHundredNanoseconds}, 0);
			}
		}
		catch (...)
		{
		}
	}

	void ShutdownCaptureResources()
	{
		if (FramePool)
		{
			FramePool.FrameArrived(FrameArrivedToken);
		}

		{
			// Wait for any in-flight CPU readback before tearing the capture resources down.
			FScopeLock ReadbackScopeLock(&CaptureReadbackCriticalSection);
			FScopeLock ScopeLock(&CaptureResourceCriticalSection);
			CaptureSession = nullptr;
			FramePool = nullptr;
			CaptureItem = nullptr;
			WinRTDevice = nullptr;
			CaptureContext4 = nullptr;
			SharedFenceD3D11 = nullptr;
			SharedIntermediateTexture = nullptr;
			SharedIntermediateSize = FIntPoint::ZeroValue;
			CaptureContext = nullptr;
			CaptureDevice = nullptr;
			StagingTexture = nullptr;
			StagingTextureSize = FIntPoint::ZeroValue;
			AlphaSampleStagingTexture = nullptr;
			bLastAlphaSampleValid = false;
		}

		if (SharedFenceHandle)
		{
			::CloseHandle(SharedFenceHandle);
			SharedFenceHandle = nullptr;
		}
		// In-flight render commands keep the opened D3D12 resources alive through their own shared reference.
		D3D12SharedOutput.Reset();

		FScopeLock ScopeLock(&PendingFrameCriticalSection);
		PendingFrameBytes.Reset();
		ReadbackScratchBytes.Reset();
		PendingGpuFrame = nullptr;
		PendingGpuFrameTexture = nullptr;
		PendingFrameSize = FIntPoint::ZeroValue;
		bHasPendingFrame = false;
		bHasPendingGpuFrame = false;
		bHasPendingSharedFrame = false;
		if (PendingSharedTextureHandle)
		{
			::CloseHandle(PendingSharedTextureHandle);
			PendingSharedTextureHandle = nullptr;
		}

		// Disarm the first-frame watchdog so it cannot fire against a torn-down or not-yet-restarted capture.
		CaptureStartTime = -1.0;
		bHasEverReceivedFrame = false;
		ActiveCaptureSource = ECaptureSource::Window;
		bAttemptedVisualSource = false;
		bCapturePipelineActive = false;
		CaptureDeviceReadyTime = -1.0;
		LastPipelineStartAttemptTime = -1.0;
		bLoggedCaptureStartFailure = false;
	}

	/**
	 * (Re)create the NT-handle shared intermediate texture for D3D12SharedTexture mode whenever the captured
	 * frame size changes. Runs on the capture thread under CaptureResourceCriticalSection; the exported handle
	 * is parked in the pending state for the game thread to forward to the render thread.
	 */
	bool EnsureSharedIntermediateTexture(const D3D11_TEXTURE2D_DESC& InSourceDesc)
	{
		const FIntPoint RequiredSize(static_cast<int32>(InSourceDesc.Width), static_cast<int32>(InSourceDesc.Height));
		if (SharedIntermediateTexture.get() != nullptr && RequiredSize == SharedIntermediateSize)
		{
			return true;
		}

		SharedIntermediateTexture = nullptr;
		SharedIntermediateSize = FIntPoint::ZeroValue;

		D3D11_TEXTURE2D_DESC SharedDesc = {};
		SharedDesc.Width = InSourceDesc.Width;
		SharedDesc.Height = InSourceDesc.Height;
		SharedDesc.MipLevels = 1;
		SharedDesc.ArraySize = 1;
		SharedDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		SharedDesc.SampleDesc.Count = 1;
		SharedDesc.Usage = D3D11_USAGE_DEFAULT;
		SharedDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		SharedDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

		if (FAILED(CaptureDevice->CreateTexture2D(&SharedDesc, nullptr, SharedIntermediateTexture.put())) || SharedIntermediateTexture.get() == nullptr)
		{
			SharedIntermediateTexture = nullptr;
			return false;
		}

		winrt::com_ptr<IDXGIResource1> DxgiResource = SharedIntermediateTexture.try_as<IDXGIResource1>();
		HANDLE TextureHandle = nullptr;
		if (DxgiResource.get() == nullptr ||
			FAILED(DxgiResource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &TextureHandle)) ||
			!TextureHandle)
		{
			SharedIntermediateTexture = nullptr;
			return false;
		}

		SharedIntermediateSize = RequiredSize;
		++SharedTextureGeneration;

		FScopeLock PendingFrameScopeLock(&PendingFrameCriticalSection);
		if (PendingSharedTextureHandle)
		{
			// A previous generation was never consumed (rapid resizes); drop its handle.
			::CloseHandle(PendingSharedTextureHandle);
		}
		PendingSharedTextureHandle = TextureHandle;
		PendingSharedTextureGeneration = SharedTextureGeneration;
		return true;
	}

	void EnsureStagingTexture(const D3D11_TEXTURE2D_DESC& InSourceDesc)
	{
		const FIntPoint RequiredSize(static_cast<int32>(InSourceDesc.Width), static_cast<int32>(InSourceDesc.Height));
		if (StagingTexture.get() != nullptr && RequiredSize == StagingTextureSize)
		{
			return;
		}

		D3D11_TEXTURE2D_DESC StagingDesc = InSourceDesc;
		StagingDesc.BindFlags = 0;
		StagingDesc.MiscFlags = 0;
		StagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		StagingDesc.Usage = D3D11_USAGE_STAGING;

		StagingTexture = nullptr;
		if (SUCCEEDED(CaptureDevice->CreateTexture2D(&StagingDesc, nullptr, StagingTexture.put())) && StagingTexture.get() != nullptr)
		{
			StagingTextureSize = RequiredSize;
		}
		else
		{
			StagingTextureSize = FIntPoint::ZeroValue;
		}
	}

	void OnFrameArrived(winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& Sender, winrt::Windows::Foundation::IInspectable const&)
	{
		auto Frame = Sender.TryGetNextFrame();
		if (!Frame)
		{
			return;
		}

		const auto ContentSize = Frame.ContentSize();
		if (ContentSize.Width <= 0 || ContentSize.Height <= 0)
		{
			return;
		}

		auto* SurfaceInspectable = reinterpret_cast<IInspectable*>(winrt::get_abi(Frame.Surface()));
		if (SurfaceInspectable == nullptr)
		{
			return;
		}

		winrt::com_ptr<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> DxgiInterfaceAccess;
		if (FAILED(SurfaceInspectable->QueryInterface(__uuidof(::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess), DxgiInterfaceAccess.put_void())) || DxgiInterfaceAccess.get() == nullptr)
		{
			return;
		}

		winrt::com_ptr<ID3D11Texture2D> FrameTexture;
		if (FAILED(DxgiInterfaceAccess.get()->GetInterface(__uuidof(ID3D11Texture2D), FrameTexture.put_void())) || FrameTexture.get() == nullptr)
		{
			return;
		}

		D3D11_TEXTURE2D_DESC FrameDesc = {};
		FrameTexture->GetDesc(&FrameDesc);
		const int32 Width = static_cast<int32>(FrameDesc.Width);
		const int32 Height = static_cast<int32>(FrameDesc.Height);

		// Disarm the first-frame watchdog: WGC is delivering frames regardless of which output mode consumes them.
		bHasEverReceivedFrame = true;

		if (ActiveOutputMode == ECBWebView2WorldOutputMode::D3D11GpuCopy)
		{
			if (!bLoggedGpuFrameReceived)
			{
				UE_LOG(LogWebView2Utils, Log, TEXT("CBWebView2 world GPU frame received mode=%s size=%dx%d format=%u"),
					LexToString(ActiveOutputMode),
					Width,
					Height,
					static_cast<uint32>(FrameDesc.Format));
				bLoggedGpuFrameReceived = true;
			}

			FScopeLock PendingFrameScopeLock(&PendingFrameCriticalSection);
			PendingGpuFrame = Frame;
			PendingGpuFrameTexture = FrameTexture;
			PendingFrameSize = FIntPoint(Width, Height);
			bHasPendingGpuFrame = true;
			return;
		}

		if (ActiveOutputMode == ECBWebView2WorldOutputMode::D3D12SharedTexture)
		{
			// Serialize against shutdown the same way the CPU readback does.
			FScopeLock SharedCopyScopeLock(&CaptureReadbackCriticalSection);

			winrt::com_ptr<ID3D11DeviceContext> CopyContext;
			winrt::com_ptr<ID3D11DeviceContext4> CopyContext4;
			winrt::com_ptr<ID3D11Texture2D> CopyTarget;
			winrt::com_ptr<ID3D11Fence> CopyFence;
			uint64 SignalValue = 0;
			{
				FScopeLock ScopeLock(&CaptureResourceCriticalSection);
				if (CaptureContext.get() == nullptr || CaptureDevice.get() == nullptr ||
					CaptureContext4.get() == nullptr || SharedFenceD3D11.get() == nullptr)
				{
					return;
				}

				if (!EnsureSharedIntermediateTexture(FrameDesc))
				{
					return;
				}

				CopyContext = CaptureContext;
				CopyContext4 = CaptureContext4;
				CopyTarget = SharedIntermediateTexture;
				CopyFence = SharedFenceD3D11;
				SignalValue = ++SharedFenceNextValue;
			}

			if (!bLoggedGpuFrameReceived)
			{
				UE_LOG(LogWebView2Utils, Log, TEXT("CBWebView2 world GPU frame received mode=%s size=%dx%d format=%u"),
					LexToString(ActiveOutputMode),
					Width,
					Height,
					static_cast<uint32>(FrameDesc.Format));
				bLoggedGpuFrameReceived = true;
			}

			// Last-writer-wins: there is intentionally no back-pressure wait on the consumer, so a hidden or
			// throttled widget can never stall the capture queue. If the producer overwrites mid-copy the
			// worst case is a single torn frame.
			CopyContext->CopyResource(CopyTarget.get(), FrameTexture.get());
			CopyContext4->Signal(CopyFence.get(), SignalValue);
			// Submit immediately so the D3D12 queue wait observes the signal without waiting for an implicit flush.
			CopyContext->Flush();

			{
				FScopeLock PendingFrameScopeLock(&PendingFrameCriticalSection);
				PendingSharedFenceValue = SignalValue;
				PendingFrameSize = FIntPoint(Width, Height);
				bHasPendingSharedFrame = true;
			}
			return;
		}

		// Serializes the readback (and ShutdownCaptureResources) without holding CaptureResourceCriticalSection
		// across the GPU sync below, so the game thread can resize/recreate the frame pool while a readback drains.
		FScopeLock ReadbackScopeLock(&CaptureReadbackCriticalSection);

		winrt::com_ptr<ID3D11DeviceContext> ReadbackContext;
		winrt::com_ptr<ID3D11Texture2D> ReadbackStaging;
		{
			FScopeLock ScopeLock(&CaptureResourceCriticalSection);
			if (CaptureContext.get() == nullptr || CaptureDevice.get() == nullptr)
			{
				return;
			}

			EnsureStagingTexture(FrameDesc);
			if (StagingTexture.get() == nullptr)
			{
				return;
			}

			ReadbackContext = CaptureContext;
			ReadbackStaging = StagingTexture;
		}

		ReadbackContext->CopyResource(ReadbackStaging.get(), FrameTexture.get());

		D3D11_MAPPED_SUBRESOURCE MappedResource = {};
		if (FAILED(ReadbackContext->Map(ReadbackStaging.get(), 0, D3D11_MAP_READ, 0, &MappedResource)))
		{
			return;
		}

		ReadbackScratchBytes.SetNumUninitialized(Width * Height * 4);

		for (int32 RowIndex = 0; RowIndex < Height; ++RowIndex)
		{
			const uint8* SourceRow = static_cast<const uint8*>(MappedResource.pData) + (RowIndex * MappedResource.RowPitch);
			uint8* DestinationRow = ReadbackScratchBytes.GetData() + (RowIndex * Width * 4);
			FMemory::Memcpy(DestinationRow, SourceRow, Width * 4);
		}

		ReadbackContext->Unmap(ReadbackStaging.get(), 0);

		{
			FScopeLock PendingFrameScopeLock(&PendingFrameCriticalSection);
			Swap(PendingFrameBytes, ReadbackScratchBytes);
			PendingFrameSize = FIntPoint(Width, Height);
			bHasPendingFrame = true;
		}
	}

	virtual void HandleHiddenHostWindowMessage(UINT Message) override
	{
		if (IsImeWindowMessage(Message) || Message == HiddenHostWindowRefreshImeMessage)
		{
			RefreshImeWindowPosition();
		}
	}

	bool ShouldThrottle(double InCurrentTime, float InMaxFramesPerSecond) const
	{
		const bool bUseThrottle = InMaxFramesPerSecond > 0.0f;
		if (!bForceUpload && bUseThrottle && LastUploadTime >= 0.0)
		{
			const double UploadInterval = 1.0 / InMaxFramesPerSecond;
			return (InCurrentTime - LastUploadTime) < UploadInterval;
		}

		return false;
	}

	bool TickD3D11GpuOutput(double InCurrentTime, float InMaxFramesPerSecond, UTexture2D*& OutUpdatedTexture)
	{
		if (ShouldThrottle(InCurrentTime, InMaxFramesPerSecond))
		{
			return false;
		}

		winrt::com_ptr<ID3D11Texture2D> FrameTexture;
		winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame FrameLifetime{nullptr};
		FIntPoint FrameSize = FIntPoint::ZeroValue;
		{
			FScopeLock ScopeLock(&PendingFrameCriticalSection);
			if (!bHasPendingGpuFrame || PendingGpuFrameTexture.get() == nullptr)
			{
				return false;
			}

			FrameLifetime = PendingGpuFrame;
			PendingGpuFrame = nullptr;
			FrameTexture = PendingGpuFrameTexture;
			PendingGpuFrameTexture = nullptr;
			FrameSize = PendingFrameSize;
			PendingFrameSize = FIntPoint::ZeroValue;
			bHasPendingGpuFrame = false;
		}

		if (FrameSize.X <= 0 || FrameSize.Y <= 0)
		{
			return false;
		}

		UTexture2D* Texture = PresentedTexture.Get();
		if (!EnsureTextureMatches(Texture, FrameSize))
		{
			return false;
		}

		PresentedTexture.Reset(Texture);
		FTextureResource* TextureResource = Texture->GetResource();
		if (!TextureResource)
		{
			return false;
		}

		ENQUEUE_RENDER_COMMAND(CBWebView2D3D11GpuCopy)(
			[FrameLifetime, FrameTexture, TextureResource](FRHICommandListImmediate&)
			{
				(void)FrameLifetime;
				if (!FrameTexture.get() || !TextureResource || !TextureResource->TextureRHI.IsValid() || !IsD3D11RHIActive())
				{
					return;
				}

				ID3D11DynamicRHI* D3D11RHI = GetID3D11DynamicRHI();
				ID3D11DeviceContext* DeviceContext = D3D11RHI ? D3D11RHI->RHIGetDeviceContext() : nullptr;
				ID3D11Resource* DestinationResource = D3D11RHI ? D3D11RHI->RHIGetResource(TextureResource->TextureRHI.GetReference()) : nullptr;
				if (DeviceContext && DestinationResource)
				{
					DeviceContext->CopyResource(DestinationResource, FrameTexture.get());
				}
			});

		LastUploadTime = InCurrentTime;
		bForceUpload = false;
		OutUpdatedTexture = Texture;
		return true;
	}

	bool TickD3D12SharedOutput(double InCurrentTime, float InMaxFramesPerSecond, UTexture2D*& OutUpdatedTexture)
	{
		if (D3D12SharedOutput.IsValid() && D3D12SharedOutput->bOpenFailed.load(std::memory_order_relaxed))
		{
			D3D12SharedOutput.Reset();
			FallBackToCpuReadback(TEXT("opening the shared texture/fence on the Unreal D3D12 device failed"));
			return false;
		}

		if (ShouldThrottle(InCurrentTime, InMaxFramesPerSecond))
		{
			return false;
		}

		uint64 FenceWaitValue = 0;
		FIntPoint FrameSize = FIntPoint::ZeroValue;
		HANDLE NewTextureHandle = nullptr;
		uint64 NewTextureGeneration = 0;
		{
			FScopeLock ScopeLock(&PendingFrameCriticalSection);
			if (!bHasPendingSharedFrame)
			{
				return false;
			}

			FenceWaitValue = PendingSharedFenceValue;
			FrameSize = PendingFrameSize;
			bHasPendingSharedFrame = false;
			// Hand the (re)created texture handle to the render thread exactly once per generation.
			NewTextureHandle = PendingSharedTextureHandle;
			NewTextureGeneration = PendingSharedTextureGeneration;
			PendingSharedTextureHandle = nullptr;
		}

		auto CloseUnconsumedHandle = [&NewTextureHandle]()
		{
			if (NewTextureHandle)
			{
				::CloseHandle(NewTextureHandle);
				NewTextureHandle = nullptr;
			}
		};

		if (FrameSize.X <= 0 || FrameSize.Y <= 0)
		{
			CloseUnconsumedHandle();
			return false;
		}

		UTexture2D* Texture = PresentedTexture.Get();
		if (!EnsureTextureMatches(Texture, FrameSize))
		{
			CloseUnconsumedHandle();
			return false;
		}

		PresentedTexture.Reset(Texture);
		FTextureResource* TextureResource = Texture->GetResource();
		if (!TextureResource)
		{
			CloseUnconsumedHandle();
			return false;
		}

		if (!D3D12SharedOutput.IsValid())
		{
			D3D12SharedOutput = MakeShared<FWebView2D3D12SharedOutputResources, ESPMode::ThreadSafe>();
		}

		// The fence handle is created once at device initialization and consumed by the first render command.
		HANDLE FenceHandleToOpen = SharedFenceHandle;
		SharedFenceHandle = nullptr;

		TSharedPtr<FWebView2D3D12SharedOutputResources, ESPMode::ThreadSafe> Output = D3D12SharedOutput;

		ENQUEUE_RENDER_COMMAND(CBWebView2D3D12SharedCopy)(
			[Output, FenceWaitValue, NewTextureHandle, NewTextureGeneration, FenceHandleToOpen, TextureResource](FRHICommandListImmediate& RHICmdList)
			{
				auto CloseHandles = [&]()
				{
					if (NewTextureHandle)
					{
						::CloseHandle(NewTextureHandle);
					}
					if (FenceHandleToOpen)
					{
						::CloseHandle(FenceHandleToOpen);
					}
				};

				if (!IsD3D12RHIActive())
				{
					CloseHandles();
					Output->bOpenFailed.store(true, std::memory_order_relaxed);
					return;
				}

				ID3D12Device* Device = CBWebView2D3D12Bridge::GetDevice();
				if (!Device)
				{
					CloseHandles();
					Output->bOpenFailed.store(true, std::memory_order_relaxed);
					return;
				}

				if (FenceHandleToOpen)
				{
					winrt::com_ptr<ID3D12Fence> OpenedFence;
					const HRESULT OpenFenceResult = Device->OpenSharedHandle(FenceHandleToOpen, __uuidof(ID3D12Fence), OpenedFence.put_void());
					::CloseHandle(FenceHandleToOpen);
					if (FAILED(OpenFenceResult) || OpenedFence.get() == nullptr)
					{
						if (NewTextureHandle)
						{
							::CloseHandle(NewTextureHandle);
						}
						UE_LOG(LogWebView2Utils, Warning, TEXT("CBWebView2 D3D12SharedTexture failed to open the shared fence. HRESULT=0x%08x"), static_cast<uint32>(OpenFenceResult));
						Output->bOpenFailed.store(true, std::memory_order_relaxed);
						return;
					}
					Output->SharedFence = OpenedFence;
				}

				if (NewTextureHandle)
				{
					winrt::com_ptr<ID3D12Resource> OpenedTexture;
					const HRESULT OpenTextureResult = Device->OpenSharedHandle(NewTextureHandle, __uuidof(ID3D12Resource), OpenedTexture.put_void());
					::CloseHandle(NewTextureHandle);
					if (FAILED(OpenTextureResult) || OpenedTexture.get() == nullptr)
					{
						UE_LOG(LogWebView2Utils, Warning, TEXT("CBWebView2 D3D12SharedTexture failed to open the shared texture. HRESULT=0x%08x"), static_cast<uint32>(OpenTextureResult));
						Output->bOpenFailed.store(true, std::memory_order_relaxed);
						return;
					}

					Output->SharedTexture = OpenedTexture;
					Output->WrappedRHITexture = CBWebView2D3D12Bridge::CreateTexture2DFromResource(PF_B8G8R8A8, OpenedTexture.get());
					Output->Generation = NewTextureGeneration;
					if (!Output->WrappedRHITexture.IsValid())
					{
						UE_LOG(LogWebView2Utils, Warning, TEXT("CBWebView2 D3D12SharedTexture failed to wrap the shared texture as an RHI texture."));
						Output->bOpenFailed.store(true, std::memory_order_relaxed);
						return;
					}

					static bool bLoggedSharedTextureOpened = false;
					if (!bLoggedSharedTextureOpened)
					{
						UE_LOG(LogWebView2Utils, Log, TEXT("CBWebView2 D3D12SharedTexture opened the shared capture texture on the Unreal device."));
						bLoggedSharedTextureOpened = true;
					}
				}

				if (Output->SharedFence.get() == nullptr || !Output->WrappedRHITexture.IsValid() ||
					!TextureResource || !TextureResource->TextureRHI.IsValid())
				{
					return;
				}

				FRHITexture* SourceTexture = Output->WrappedRHITexture.GetReference();
				FRHITexture* DestTexture = TextureResource->TextureRHI.GetReference();
				if (SourceTexture->GetSizeXY() != DestTexture->GetSizeXY())
				{
					// Transient mismatch while a resize generation is still in flight; the next frame recovers.
					return;
				}

				// GPU-side wait: the copy must not start until the capture device's CopyResource has finished.
				// Deferred via EnqueueLambda because the D3D12 fence wait needs the active context that only
				// exists during command-list playback, not while the render thread is still recording.
				RHICmdList.EnqueueLambda([Output, FenceWaitValue](FRHICommandList& ExecutingCmdList)
				{
					CBWebView2D3D12Bridge::WaitManualFence(ExecutingCmdList, Output->SharedFence.get(), FenceWaitValue);
				});

				RHICmdList.Transition(FRHITransitionInfo(SourceTexture, ERHIAccess::Unknown, ERHIAccess::CopySrc));
				RHICmdList.Transition(FRHITransitionInfo(DestTexture, ERHIAccess::Unknown, ERHIAccess::CopyDest));
				RHICmdList.CopyTexture(SourceTexture, DestTexture, FRHICopyTextureInfo());
				RHICmdList.Transition(FRHITransitionInfo(DestTexture, ERHIAccess::CopyDest, ERHIAccess::SRVMask));
				RHICmdList.Transition(FRHITransitionInfo(SourceTexture, ERHIAccess::CopySrc, ERHIAccess::SRVMask));
			});

		LastUploadTime = InCurrentTime;
		bForceUpload = false;
		OutUpdatedTexture = Texture;
		return true;
	}

	bool SupportsAlphaSampling() const
	{
		// These modes keep a persistent capture-side frame (full staging copy / shared intermediate) on the
		// private D3D11 device. D3D11GpuCopy shares Unreal's immediate context (unsafe off the render thread),
		// so it cannot be sampled.
		return ActiveOutputMode == ECBWebView2WorldOutputMode::CpuReadback ||
			ActiveOutputMode == ECBWebView2WorldOutputMode::D3D12SharedTexture;
	}

	bool TrySampleAlphaAtPoint(const FVector2D& InPoint, uint8& OutAlpha)
	{
		constexpr double SampleReuseSeconds = 0.05;
		constexpr double SampleReuseDistanceSquaredPx = 2.0 * 2.0;

		const double Now = FPlatformTime::Seconds();
		if (bLastAlphaSampleValid && (Now - LastAlphaSampleTime) < SampleReuseSeconds &&
			FVector2D::DistSquared(InPoint, LastAlphaSamplePoint) <= SampleReuseDistanceSquaredPx)
		{
			OutAlpha = LastSampledAlpha;
			return true;
		}

		// Serialize against the capture thread, which drives the same D3D11 immediate context.
		FScopeLock ReadbackScopeLock(&CaptureReadbackCriticalSection);

		winrt::com_ptr<ID3D11DeviceContext> SampleContext;
		winrt::com_ptr<ID3D11Texture2D> SourceTexture;
		FIntPoint SourceSize = FIntPoint::ZeroValue;
		bool bSourceIsCpuReadable = false;
		{
			FScopeLock ScopeLock(&CaptureResourceCriticalSection);
			if (CaptureContext.get() == nullptr || CaptureDevice.get() == nullptr)
			{
				return false;
			}

			if (ActiveOutputMode == ECBWebView2WorldOutputMode::D3D12SharedTexture && SharedIntermediateTexture.get() != nullptr)
			{
				SourceTexture = SharedIntermediateTexture;
				SourceSize = SharedIntermediateSize;
			}
			else if (ActiveOutputMode == ECBWebView2WorldOutputMode::CpuReadback && StagingTexture.get() != nullptr)
			{
				SourceTexture = StagingTexture;
				SourceSize = StagingTextureSize;
				bSourceIsCpuReadable = true;
			}
			else
			{
				return false;
			}

			if (!bSourceIsCpuReadable && AlphaSampleStagingTexture.get() == nullptr)
			{
				D3D11_TEXTURE2D_DESC StagingDesc = {};
				StagingDesc.Width = 1;
				StagingDesc.Height = 1;
				StagingDesc.MipLevels = 1;
				StagingDesc.ArraySize = 1;
				StagingDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
				StagingDesc.SampleDesc.Count = 1;
				StagingDesc.Usage = D3D11_USAGE_STAGING;
				StagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				if (FAILED(CaptureDevice->CreateTexture2D(&StagingDesc, nullptr, AlphaSampleStagingTexture.put())) || AlphaSampleStagingTexture.get() == nullptr)
				{
					AlphaSampleStagingTexture = nullptr;
					return false;
				}
			}

			SampleContext = CaptureContext;
		}

		if (SourceSize.X <= 0 || SourceSize.Y <= 0)
		{
			return false;
		}

		const int32 SampleX = FMath::Clamp(FMath::RoundToInt(InPoint.X), 0, SourceSize.X - 1);
		const int32 SampleY = FMath::Clamp(FMath::RoundToInt(InPoint.Y), 0, SourceSize.Y - 1);

		uint8 Alpha = 0;
		D3D11_MAPPED_SUBRESOURCE MappedResource = {};
		if (bSourceIsCpuReadable)
		{
			// The full-frame staging copy is already CPU-readable; read the single BGRA pixel directly.
			if (FAILED(SampleContext->Map(SourceTexture.get(), 0, D3D11_MAP_READ, 0, &MappedResource)))
			{
				return false;
			}

			Alpha = static_cast<const uint8*>(MappedResource.pData)[SampleY * MappedResource.RowPitch + SampleX * 4 + 3];
			SampleContext->Unmap(SourceTexture.get(), 0);
		}
		else
		{
			D3D11_BOX SourceBox = {};
			SourceBox.left = static_cast<UINT>(SampleX);
			SourceBox.top = static_cast<UINT>(SampleY);
			SourceBox.right = static_cast<UINT>(SampleX + 1);
			SourceBox.bottom = static_cast<UINT>(SampleY + 1);
			SourceBox.back = 1;
			SampleContext->CopySubresourceRegion(AlphaSampleStagingTexture.get(), 0, 0, 0, 0, SourceTexture.get(), 0, &SourceBox);

			if (FAILED(SampleContext->Map(AlphaSampleStagingTexture.get(), 0, D3D11_MAP_READ, 0, &MappedResource)))
			{
				return false;
			}

			Alpha = static_cast<const uint8*>(MappedResource.pData)[3];
			SampleContext->Unmap(AlphaSampleStagingTexture.get(), 0);
		}

		LastAlphaSamplePoint = InPoint;
		LastAlphaSampleTime = Now;
		LastSampledAlpha = Alpha;
		bLastAlphaSampleValid = true;
		OutAlpha = Alpha;
		return true;
	}

	/**
	 * First-frame watchdog. If Windows Graphics Capture never delivers a frame, every output mode would otherwise
	 * leave the freshly created (uninitialized, typically white) placeholder texture on screen forever. Once the
	 * timeout elapses, log one actionable warning and clear the texture to transparent so the failure is obvious
	 * and non-misleading. Returns true (with OutUpdatedTexture set) on the single tick that performs the clear.
	 */
	bool TickFirstFrameWatchdog(UTexture2D*& OutUpdatedTexture)
	{
		if (bHasEverReceivedFrame.load() || bClearedPlaceholderOnTimeout || CaptureStartTime < 0.0)
		{
			return false;
		}

		if (FPlatformTime::Seconds() - CaptureStartTime < NoFrameWatchdogTimeoutSeconds)
		{
			return false;
		}

		// Auto-recovery: if the window-capture target produced nothing, switch once to visual capture, which does not
		// depend on the offscreen window having a live DWM composition surface. This is fully automatic — no setting.
		if (ActiveCaptureSource == ECaptureSource::Window && !bAttemptedVisualSource)
		{
			bAttemptedVisualSource = true;
			UE_LOG(LogWebView2Utils, Warning,
				TEXT("CBWebView2 world received no frame within %.0fs from window capture; switching to visual capture (CreateFromVisual)."),
				NoFrameWatchdogTimeoutSeconds);
			if (StartCapturePipeline(ECaptureSource::Visual))
			{
				// New source armed with a fresh watchdog window; give it a chance before deciding anything else.
				return false;
			}
			UE_LOG(LogWebView2Utils, Warning, TEXT("CBWebView2 world could not switch to visual capture (the composition visual may not be ready)."));
		}

		if (!bLoggedNoFrameTimeout)
		{
			UE_LOG(LogWebView2Utils, Warning,
				TEXT("CBWebView2 world received no captured frame within %.0fs (mode=%s, source=%s). Windows Graphics Capture may be producing blank frames on this machine: ")
				TEXT("check the Windows build, GPU driver, that this is not a Remote Desktop / headless session, and that the Microsoft Edge WebView2 Runtime is installed and up to date. Clearing the widget to transparent."),
				NoFrameWatchdogTimeoutSeconds,
				LexToString(ActiveOutputMode),
				ActiveCaptureSource == ECaptureSource::Visual ? TEXT("Visual") : TEXT("Window"));
			bLoggedNoFrameTimeout = true;
		}

		// Replace the uninitialized placeholder with a fully transparent frame so the widget reads as blank, not white.
		return ClearPlaceholderToTransparent(OutUpdatedTexture);
	}

	bool TickOutput(double InCurrentTime, float InMaxFramesPerSecond, UTexture2D*& OutUpdatedTexture)
	{
		OutUpdatedTexture = nullptr;

		// Keep trying to start the capture pipeline (preferring Visual) until one source works.
		if (TickEnsureCapturePipeline(OutUpdatedTexture))
		{
			return true;
		}

		ApplyMinUpdateInterval(InMaxFramesPerSecond);

		if (TickFirstFrameWatchdog(OutUpdatedTexture))
		{
			return true;
		}

		if (ActiveOutputMode == ECBWebView2WorldOutputMode::D3D11GpuCopy)
		{
			return TickD3D11GpuOutput(InCurrentTime, InMaxFramesPerSecond, OutUpdatedTexture);
		}

		if (ActiveOutputMode == ECBWebView2WorldOutputMode::D3D12SharedTexture)
		{
			return TickD3D12SharedOutput(InCurrentTime, InMaxFramesPerSecond, OutUpdatedTexture);
		}

		if (ShouldThrottle(InCurrentTime, InMaxFramesPerSecond))
		{
			return false;
		}

		FIntPoint UploadSize = FIntPoint::ZeroValue;
		{
			FScopeLock ScopeLock(&PendingFrameCriticalSection);
			if (!bHasPendingFrame || PendingFrameBytes.IsEmpty())
			{
				return false;
			}

			UploadSize = PendingFrameSize;
		}

		if (UploadSize.X <= 0 || UploadSize.Y <= 0)
		{
			return false;
		}

		UTexture2D* Texture = PresentedTexture.Get();
		if (!EnsureTextureMatches(Texture, UploadSize))
		{
			return false;
		}

		PresentedTexture.Reset(Texture);
		TArray<uint8> FrameBytes;
		{
			FScopeLock ScopeLock(&PendingFrameCriticalSection);
			if (!bHasPendingFrame || PendingFrameSize != UploadSize || PendingFrameBytes.IsEmpty())
			{
				return false;
			}

			FrameBytes = MoveTemp(PendingFrameBytes);
			PendingFrameSize = FIntPoint::ZeroValue;
			bHasPendingFrame = false;
		}
		UploadTextureData(Texture, MoveTemp(FrameBytes), UploadSize);
		LastUploadTime = InCurrentTime;
		bForceUpload = false;
		OutUpdatedTexture = Texture;
		return true;
	}
};

FWebView2ContinuousTextureSource::FWebView2ContinuousTextureSource(
	const FString& InInitialUrl,
	const FColor& InBackgroundColor,
	bool bInEnableTransparencyHitTest,
	ECBWebView2WorldOutputMode InOutputMode,
	bool bInAllowCpuFallback,
	bool bInInjectTransparencyHitTestScript)
	: Impl(MakeUnique<FImpl>())
	, InitialUrl(InInitialUrl)
	, BackgroundColor(InBackgroundColor)
	, RequestedOutputMode(InOutputMode)
	, bAllowCpuFallback(bInAllowCpuFallback)
	, bEnableTransparencyHitTest(bInEnableTransparencyHitTest)
	, bInjectTransparencyHitTestScript(bInInjectTransparencyHitTestScript)
{
	if (Impl.IsValid())
	{
		Impl->RequestedOutputMode = RequestedOutputMode;
		Impl->bAllowCpuFallback = bAllowCpuFallback;
	}
}

bool FWebView2ContinuousTextureSource::PredictsAlphaSamplingSupport(ECBWebView2WorldOutputMode InRequestedMode)
{
	switch (InRequestedMode)
	{
	case ECBWebView2WorldOutputMode::CpuReadback:
	case ECBWebView2WorldOutputMode::D3D12SharedTexture:
		// Both terminal modes sample, and D3D12SharedTexture's runtime fallback is CpuReadback which also samples.
		return true;
	case ECBWebView2WorldOutputMode::D3D11GpuCopy:
		return false;
	case ECBWebView2WorldOutputMode::Auto:
	default:
		// Auto resolves to D3D11GpuCopy only on the D3D11 RHI; every other resolution supports sampling.
		return !IsD3D11RHIActive();
	}
}

FWebView2ContinuousTextureSource::~FWebView2ContinuousTextureSource()
{
	Shutdown();
}

bool FWebView2ContinuousTextureSource::Initialize(const FIntPoint& InInitialSize, void* InFocusParentWindow)
{
	if (!Impl.IsValid())
	{
		return false;
	}

	const FIntPoint SafeInitialSize = InInitialSize.ComponentMax(FIntPoint(1, 1));
	if (!Impl->CreateHiddenWindow(SafeInitialSize, bEnableTransparencyHitTest))
	{
		return false;
	}

	// In TextureAlpha hit-test mode the JS transparency script is not injected; passthrough decisions are
	// made on the Unreal side by sampling the captured texture's alpha (TrySampleAlphaAtPoint).
	WebViewWindow = FWebView2Manager::Get().CreateWebView(
		Impl->HiddenWindow,
		FGuid::NewGuid(),
		InitialUrl,
		BackgroundColor,
		bEnableTransparencyHitTest && bInjectTransparencyHitTestScript,
		true,
		Impl->HiddenWindow,
		/*bAllowNonInteractiveElementPassthrough=*/ false);
	if (!WebViewWindow.IsValid())
	{
		Shutdown();
		return false;
	}

	Impl->WebViewWindowWeak = WebViewWindow;

	POINT Offset{0, 0};
	POINT Size{SafeInitialSize.X, SafeInitialSize.Y};
	WebViewWindow->SetBounds(Offset, Size);

	if (!Impl->InitializeCaptureResources())
	{
		Shutdown();
		return false;
	}

	Impl->bForceUpload = true;
	return true;
}

void FWebView2ContinuousTextureSource::Shutdown()
{
	if (WebViewWindow.IsValid())
	{
		WebViewWindow->CloseWindow();
		WebViewWindow.Reset();
	}

	if (Impl.IsValid())
	{
		Impl->ShutdownCaptureResources();
		Impl->PresentedTexture.Reset();
		Impl->DestroyHiddenWindow();
	}
}

void FWebView2ContinuousTextureSource::Resize(const FIntPoint& InSize)
{
	if (!Impl.IsValid())
	{
		return;
	}

	const FIntPoint SafeSize = InSize.ComponentMax(FIntPoint(1, 1));
	Impl->ResizeHiddenWindow(SafeSize);
	if (WebViewWindow.IsValid())
	{
		POINT Offset{0, 0};
		POINT Size{SafeSize.X, SafeSize.Y};
		WebViewWindow->SetBounds(Offset, Size);
	}
}

void FWebView2ContinuousTextureSource::RequestImmediateUpload()
{
	if (Impl.IsValid())
	{
		Impl->bForceUpload = true;
	}
}

void FWebView2ContinuousTextureSource::UpdateInputScreenAnchor(const FVector2D& InScreenSpacePosition, const FVector2D& InLocalWebViewPoint)
{
	if (Impl.IsValid())
	{
		Impl->UpdateInputScreenAnchor(InScreenSpacePosition, InLocalWebViewPoint);
	}
}

void FWebView2ContinuousTextureSource::UpdateImeCaretAnchor(const FVector2D& InScreenSpacePosition, const FVector2D& InLocalWebViewPoint, float InCaretHeight)
{
	if (Impl.IsValid())
	{
		Impl->UpdateImeCaretAnchor(InScreenSpacePosition, InLocalWebViewPoint, InCaretHeight);
	}
}

void FWebView2ContinuousTextureSource::RefreshImeWindowPosition()
{
	if (Impl.IsValid())
	{
		Impl->RefreshImeWindowPosition();
	}
}

void FWebView2ContinuousTextureSource::ReleaseInputScreenAnchor()
{
	if (Impl.IsValid())
	{
		Impl->ReleaseInputScreenAnchor();
	}
}

bool FWebView2ContinuousTextureSource::TickOutput(double InCurrentTime, float InMaxFramesPerSecond, UTexture2D*& OutUpdatedTexture)
{
	return Impl.IsValid() && Impl->TickOutput(InCurrentTime, InMaxFramesPerSecond, OutUpdatedTexture);
}

bool FWebView2ContinuousTextureSource::TrySampleAlphaAtPoint(const FVector2D& InBrowserPoint, uint8& OutAlpha)
{
	return Impl.IsValid() && Impl->TrySampleAlphaAtPoint(InBrowserPoint, OutAlpha);
}

bool FWebView2ContinuousTextureSource::SupportsAlphaSampling() const
{
	return Impl.IsValid() && Impl->SupportsAlphaSampling();
}

UTexture2D* FWebView2ContinuousTextureSource::GetTexture() const
{
	return Impl.IsValid() ? Impl->PresentedTexture.Get() : nullptr;
}

TSharedPtr<FWebView2Window> FWebView2ContinuousTextureSource::GetWebViewWindow() const
{
	return WebViewWindow;
}

ECBWebView2WorldOutputMode FWebView2ContinuousTextureSource::GetRequestedOutputMode() const
{
	return RequestedOutputMode;
}

ECBWebView2WorldOutputMode FWebView2ContinuousTextureSource::GetActiveOutputMode() const
{
	return Impl.IsValid() ? Impl->ActiveOutputMode : ECBWebView2WorldOutputMode::CpuReadback;
}
