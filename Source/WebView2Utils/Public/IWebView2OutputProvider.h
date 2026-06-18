// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UTexture2D;

/** Output path classification for the current provider. */
enum class EWebView2OutputPath : uint8
{
	None,
	DiagnosticCapturePreview
};

/** Diagnostic output callback. */
using FOnWebView2DiagnosticPreviewReady = TFunction<void(bool, UTexture2D*, const FString&)>;

/**
 * Abstract interface for a WebView output provider.
 *
 * Experimental interface used in Phase 0 to validate output paths.
 * It only describes output resources and diagnostic capture capability,
 * and does not own navigation, input, or focus logic.
 */
class WEBVIEW2UTILS_API IWebView2OutputProvider
{
public:
	virtual ~IWebView2OutputProvider() = default;

	/** Whether there is currently consumable output. */
	virtual bool IsOutputAvailable() const = 0;
	/** Whether the current output can be treated as continuous output. */
	virtual bool IsContinuousOutput() const = 0;
	/** Whether the current output path preserves transparency. */
	virtual bool SupportsTransparency() const = 0;
	/** Current output size. */
	virtual FIntPoint GetOutputSize() const = 0;
	/** Type of the current output path. */
	virtual EWebView2OutputPath GetOutputPath() const = 0;

	/** Request an output resize. */
	virtual void RequestOutputResize(const FIntPoint& InSize) = 0;
	/** Explicitly invalidate the current output. */
	virtual void InvalidateOutput() = 0;

	/** Last captured texture used only for Phase 0 diagnostics. */
	virtual UTexture2D* GetLastCapturedTexture() const = 0;
	/** Capture request used only for Phase 0 diagnostics. */
	virtual void RequestDiagnosticPreview(FOnWebView2DiagnosticPreviewReady Callback) = 0;
};