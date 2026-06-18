// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

enum class ECBWebView2InternalMessageType : uint8
{
	None,
	HitTest,
	EditableFocus,
	ImeCaret
};

struct WEBVIEW2UTILS_API FCBWebView2InternalMessage
{
	ECBWebView2InternalMessageType Type = ECBWebView2InternalMessageType::None;
	FString RawType;
	bool bValue = false;
	FVector2D Point = FVector2D::ZeroVector;
	FVector2D ViewportSize = FVector2D::ZeroVector;
	float Height = 18.0f;
	float DevicePixelRatio = 1.0f;

	bool IsHitTest() const { return Type == ECBWebView2InternalMessageType::HitTest; }
	bool IsEditableFocus() const { return Type == ECBWebView2InternalMessageType::EditableFocus; }
	bool IsImeCaret() const { return Type == ECBWebView2InternalMessageType::ImeCaret; }
	bool ToLegacyMessage(FString& OutLegacyMessage) const;
};

class WEBVIEW2UTILS_API FCBWebView2InternalMessageParser
{
public:
	static bool TryParseJson(const FString& Message, FCBWebView2InternalMessage& OutMessage);
	static bool TryConvertJsonToLegacyMessage(const FString& Message, FString& OutLegacyMessage);
	static bool TryReadLegacyBoolMessage(const FString& Message, const TCHAR* Prefix, bool& bOutValue);

private:
	static ECBWebView2InternalMessageType ParseType(const FString& Type);
};
