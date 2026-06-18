// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#include "WebView2CapturePreviewOutputProvider.h"

#include "Async/Async.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "WebView2Window.h"

#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <combaseapi.h>
#include <objidl.h>
#include "Windows/HideWindowsPlatformTypes.h"

FWebView2CapturePreviewOutputProvider::FWebView2CapturePreviewOutputProvider(TWeakPtr<FWebView2Window> InOwnerWindow)
	: OwnerWindow(InOwnerWindow)
{
}

bool FWebView2CapturePreviewOutputProvider::IsOutputAvailable() const
{
	const TSharedPtr<FWebView2Window> Window = OwnerWindow.Pin();
	return Window.IsValid() && Window->WebView != nullptr;
}

bool FWebView2CapturePreviewOutputProvider::IsContinuousOutput() const
{
	return false;
}

bool FWebView2CapturePreviewOutputProvider::SupportsTransparency() const
{
	return false;
}

FIntPoint FWebView2CapturePreviewOutputProvider::GetOutputSize() const
{
	if (LastCapturedTexture.IsValid())
	{
		return FIntPoint(LastCapturedTexture->GetSizeX(), LastCapturedTexture->GetSizeY());
	}

	return RequestedOutputSize;
}

EWebView2OutputPath FWebView2CapturePreviewOutputProvider::GetOutputPath() const
{
	return EWebView2OutputPath::DiagnosticCapturePreview;
}

void FWebView2CapturePreviewOutputProvider::RequestOutputResize(const FIntPoint& InSize)
{
	RequestedOutputSize = InSize;
}

void FWebView2CapturePreviewOutputProvider::InvalidateOutput()
{
	LastCapturedTexture.Reset();
	RequestedOutputSize = FIntPoint::ZeroValue;
	bCaptureInFlight = false;
}

UTexture2D* FWebView2CapturePreviewOutputProvider::GetLastCapturedTexture() const
{
	return LastCapturedTexture.Get();
}

void FWebView2CapturePreviewOutputProvider::RequestDiagnosticPreview(FOnWebView2DiagnosticPreviewReady Callback)
{
	const TSharedPtr<FWebView2Window> Window = OwnerWindow.Pin();
	if (!Window.IsValid() || Window->WebView == nullptr)
	{
		if (Callback)
		{
			Callback(false, nullptr, TEXT("The WebView is not initialized yet, so a diagnostic capture cannot be generated."));
		}
		return;
	}

	if (bCaptureInFlight)
	{
		if (Callback)
		{
			Callback(false, nullptr, TEXT("The previous diagnostic capture is still being processed."));
		}
		return;
	}

	bCaptureInFlight = true;

	Microsoft::WRL::ComPtr<IStream> ImageStream;
	const HRESULT StreamResult = ::CreateStreamOnHGlobal(nullptr, true, &ImageStream);
	if (FAILED(StreamResult) || !ImageStream)
	{
		bCaptureInFlight = false;
		if (Callback)
		{
			Callback(false, nullptr, FString::Printf(TEXT("Failed to create the capture stream. HRESULT=0x%08x"), StreamResult));
		}
		return;
	}

	const TWeakPtr<FWebView2CapturePreviewOutputProvider> WeakProvider = AsShared();
	const TSharedRef<FOnWebView2DiagnosticPreviewReady, ESPMode::ThreadSafe> CallbackRef = MakeShared<FOnWebView2DiagnosticPreviewReady, ESPMode::ThreadSafe>(MoveTemp(Callback));

	Window->WebView->CapturePreview(
		COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG,
		ImageStream.Get(),
		Microsoft::WRL::Callback<ICoreWebView2CapturePreviewCompletedHandler>(
			[WeakProvider, ImageStream, CallbackRef](HRESULT ErrorCode) -> HRESULT
			{
				TArray<uint8> EncodedBytes;
				FString ErrorMessage;
				bool bSuccess = SUCCEEDED(ErrorCode);

				if (bSuccess)
				{
					const TSharedPtr<FWebView2CapturePreviewOutputProvider> Provider = WeakProvider.Pin();
					if (!Provider.IsValid() || !Provider->ReadStreamToBytes(ImageStream.Get(), EncodedBytes, ErrorMessage))
					{
						bSuccess = false;
						if (ErrorMessage.IsEmpty())
						{
							ErrorMessage = TEXT("Failed to read the diagnostic capture stream.");
						}
					}
				}
				else
				{
					ErrorMessage = FString::Printf(TEXT("CapturePreview failed. HRESULT=0x%08x"), ErrorCode);
				}

				AsyncTask(ENamedThreads::GameThread, [WeakProvider, CallbackRef, bSuccess, EncodedBytes = MoveTemp(EncodedBytes), ErrorMessage = MoveTemp(ErrorMessage)]() mutable
				{
					const TSharedPtr<FWebView2CapturePreviewOutputProvider> Provider = WeakProvider.Pin();
					if (!Provider.IsValid())
					{
						return;
					}

					Provider->bCaptureInFlight = false;

					UTexture2D* PreviewTexture = nullptr;
					bool bFinalSuccess = bSuccess;

					if (bFinalSuccess)
					{
						PreviewTexture = FImageUtils::ImportBufferAsTexture2D(EncodedBytes);
						if (!PreviewTexture)
						{
							bFinalSuccess = false;
							if (ErrorMessage.IsEmpty())
							{
								ErrorMessage = TEXT("Capture data was produced, but conversion to a UE texture failed.");
							}
						}
					}

					if (bFinalSuccess && PreviewTexture)
					{
						PreviewTexture->SetFlags(RF_Transient);
						Provider->LastCapturedTexture.Reset(PreviewTexture);
						Provider->RequestedOutputSize = FIntPoint(PreviewTexture->GetSizeX(), PreviewTexture->GetSizeY());
					}
					else
					{
						Provider->LastCapturedTexture.Reset();
					}

					if (*CallbackRef)
					{
						(*CallbackRef)(bFinalSuccess, PreviewTexture, ErrorMessage);
					}
				});

				return S_OK;
			})
			.Get());
}

bool FWebView2CapturePreviewOutputProvider::ReadStreamToBytes(IStream* ImageStream, TArray<uint8>& OutBytes, FString& OutErrorMessage) const
{
	if (!ImageStream)
	{
		OutErrorMessage = TEXT("The capture stream is null.");
		return false;
	}

	STATSTG StreamStats = {};
	const HRESULT StatResult = ImageStream->Stat(&StreamStats, STATFLAG_NONAME);
	if (FAILED(StatResult))
	{
		OutErrorMessage = FString::Printf(TEXT("Failed to read capture stream metadata. HRESULT=0x%08x"), StatResult);
		return false;
	}

	if (StreamStats.cbSize.QuadPart <= 0 || StreamStats.cbSize.QuadPart > MAX_int32)
	{
		OutErrorMessage = TEXT("The capture stream size is invalid.");
		return false;
	}

	LARGE_INTEGER SeekOrigin = {};
	const HRESULT SeekResult = ImageStream->Seek(SeekOrigin, STREAM_SEEK_SET, nullptr);
	if (FAILED(SeekResult))
	{
		OutErrorMessage = FString::Printf(TEXT("Failed to reset the capture stream position. HRESULT=0x%08x"), SeekResult);
		return false;
	}

	OutBytes.SetNumUninitialized(static_cast<int32>(StreamStats.cbSize.QuadPart));
	ULONG BytesRead = 0;
	const HRESULT ReadResult = ImageStream->Read(OutBytes.GetData(), OutBytes.Num(), &BytesRead);
	if (FAILED(ReadResult) || BytesRead == 0)
	{
		OutBytes.Reset();
		OutErrorMessage = FString::Printf(TEXT("Failed to read capture stream contents. HRESULT=0x%08x"), ReadResult);
		return false;
	}

	OutBytes.SetNum(static_cast<int32>(BytesRead), EAllowShrinking::No);
	return true;
}