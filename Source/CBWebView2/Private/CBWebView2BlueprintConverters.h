// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#pragma once

#include "CBWebView2BlueprintTypes.h"
#include "WebView2Window.h"

namespace CBWebView2BlueprintConverters
{
	inline ECBWebView2CursorType ToBlueprintCursorType(const EMouseCursor::Type InCursorType)
	{
		switch (InCursorType)
		{
		case EMouseCursor::TextEditBeam:
			return ECBWebView2CursorType::TextEditBeam;
		case EMouseCursor::Hand:
			return ECBWebView2CursorType::Hand;
		case EMouseCursor::ResizeLeftRight:
			return ECBWebView2CursorType::ResizeLeftRight;
		case EMouseCursor::ResizeUpDown:
			return ECBWebView2CursorType::ResizeUpDown;
		case EMouseCursor::ResizeSouthEast:
			return ECBWebView2CursorType::ResizeSouthEast;
		case EMouseCursor::ResizeSouthWest:
			return ECBWebView2CursorType::ResizeSouthWest;
		case EMouseCursor::CardinalCross:
			return ECBWebView2CursorType::CardinalCross;
		case EMouseCursor::Crosshairs:
			return ECBWebView2CursorType::Crosshairs;
		case EMouseCursor::GrabHand:
			return ECBWebView2CursorType::GrabHand;
		case EMouseCursor::GrabHandClosed:
			return ECBWebView2CursorType::GrabHandClosed;
		case EMouseCursor::SlashedCircle:
			return ECBWebView2CursorType::SlashedCircle;
		case EMouseCursor::EyeDropper:
			return ECBWebView2CursorType::EyeDropper;
		case EMouseCursor::Custom:
			return ECBWebView2CursorType::Custom;
		default:
			return ECBWebView2CursorType::Default;
		}
	}

	inline ECBWebView2DownloadState ToBlueprintDownloadState(const EWebView2DownloadState InState)
	{
		switch (InState)
		{
		case EWebView2DownloadState::Interrupted:
			return ECBWebView2DownloadState::Interrupted;
		case EWebView2DownloadState::Completed:
			return ECBWebView2DownloadState::Completed;
		default:
			return ECBWebView2DownloadState::InProgress;
		}
	}

	inline FCBWebView2DownloadInfo ToBlueprintDownloadInfo(const FWebView2DownloadInfo& InInfo)
	{
		FCBWebView2DownloadInfo OutInfo;
		OutInfo.Uri = InInfo.Uri;
		OutInfo.MimeType = InInfo.MimeType;
		OutInfo.ResultFilePath = InInfo.ResultFilePath;
		OutInfo.BytesReceived = InInfo.BytesReceived;
		OutInfo.TotalBytesToReceive = InInfo.TotalBytesToReceive;
		OutInfo.State = ToBlueprintDownloadState(InInfo.State);
		return OutInfo;
	}
}
