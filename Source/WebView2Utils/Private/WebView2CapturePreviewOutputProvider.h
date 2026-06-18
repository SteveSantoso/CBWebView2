// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IWebView2OutputProvider.h"

#include "UObject/StrongObjectPtr.h"

class FWebView2Window;
struct IStream;

/**
 * Diagnostic output provider built on top of CapturePreview.
 *
 * It does not provide continuous output. It only converts the current
 * WebView frame into a temporary UE texture so the capture, material,
 * and world-space presentation pipeline can be validated.
 */
class FWebView2CapturePreviewOutputProvider final
	: public IWebView2OutputProvider
	, public TSharedFromThis<FWebView2CapturePreviewOutputProvider>
{
public:
	explicit FWebView2CapturePreviewOutputProvider(TWeakPtr<FWebView2Window> InOwnerWindow);

	virtual bool IsOutputAvailable() const override;
	virtual bool IsContinuousOutput() const override;
	virtual bool SupportsTransparency() const override;
	virtual FIntPoint GetOutputSize() const override;
	virtual EWebView2OutputPath GetOutputPath() const override;
	virtual void RequestOutputResize(const FIntPoint& InSize) override;
	virtual void InvalidateOutput() override;
	virtual UTexture2D* GetLastCapturedTexture() const override;
	virtual void RequestDiagnosticPreview(FOnWebView2DiagnosticPreviewReady Callback) override;

private:
	bool ReadStreamToBytes(IStream* ImageStream, TArray<uint8>& OutBytes, FString& OutErrorMessage) const;

	TWeakPtr<FWebView2Window> OwnerWindow;
	TStrongObjectPtr<UTexture2D> LastCapturedTexture;
	FIntPoint RequestedOutputSize = FIntPoint::ZeroValue;
	bool bCaptureInFlight = false;
};