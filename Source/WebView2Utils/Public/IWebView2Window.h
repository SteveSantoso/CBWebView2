// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/** Current document loading state. */
enum class ECBWebView2DocumentState : uint8
{
	NoDocument,
	Loading,
	Completed,
	Error
};

/**
 * Abstract interface for the native WebView window.
 *
 * This layer isolates Slate / UMG from the concrete WebView2 host implementation.
 */
class IWebView2Window
{
public:
	virtual ~IWebView2Window() = default;

	/** Navigation control. */
	virtual void LoadURL(const FString& InUrl) = 0;
	virtual void GoForward() const = 0;
	virtual void GoBack() const = 0;
	virtual void Reload() const = 0;
	virtual void Stop() const = 0;

	/** Layout and visibility control. */
	virtual void SetBounds(const RECT& InRect) = 0;
	virtual void SetBounds(const POINT& InOffset, const POINT& InSize) = 0;
	virtual RECT GetBounds() const = 0;

	virtual void SetVisible(ESlateVisibility InVisibility) = 0;
	virtual ESlateVisibility GetVisible() const = 0;

	/** Current document loading state. */
	virtual ECBWebView2DocumentState GetDocumentLoadingState() const = 0;

	/** Input focus and mouse/keyboard message forwarding. */
	virtual void MoveFocus(bool bFocus) = 0;
	virtual void SendMouseButton(const FVector2D& Point, bool bIsLeft, bool bIsDown) = 0;
	virtual void SendMouseDoubleClick(const FVector2D& Point, bool bIsLeft) = 0;
	virtual void SendMouseMove(const FVector2D& Point, bool bIsLeftButtonDown = false, bool bIsRightButtonDown = false, bool bIsMiddleButtonDown = false) = 0;
	virtual void SendMouseWheel(const FVector2D& Point, float Delta) = 0;
	virtual void SendKeyboardMessage(uint32 Msg, uint64 WParam, int64 LParam) = 0;
};
