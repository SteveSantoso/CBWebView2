// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#include "WebView2InternalMessage.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool FCBWebView2InternalMessage::ToLegacyMessage(FString& OutLegacyMessage) const
{
	if (IsEditableFocus())
	{
		OutLegacyMessage = FString::Printf(TEXT("EditableFocus:%d"), bValue ? 1 : 0);
		return true;
	}

	if (IsHitTest())
	{
		OutLegacyMessage = FString::Printf(TEXT("IsClickable:%d"), bValue ? 1 : 0);
		return true;
	}

	return false;
}

bool FCBWebView2InternalMessageParser::TryParseJson(const FString& Message, FCBWebView2InternalMessage& OutMessage)
{
	TSharedPtr<FJsonObject> JsonObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return false;
	}

	bool bIsInternal = false;
	if (!JsonObject->TryGetBoolField(TEXT("__cbwebview2"), bIsInternal) || !bIsInternal)
	{
		return false;
	}

	FString Type;
	if (!JsonObject->TryGetStringField(TEXT("type"), Type))
	{
		return false;
	}

	OutMessage = FCBWebView2InternalMessage();
	OutMessage.RawType = Type;
	OutMessage.Type = ParseType(Type);
	if (OutMessage.Type == ECBWebView2InternalMessageType::None)
	{
		return false;
	}

	JsonObject->TryGetBoolField(TEXT("value"), OutMessage.bValue);

	double X = 0.0;
	double Y = 0.0;
	double Height = 18.0;
	double ViewportWidth = 0.0;
	double ViewportHeight = 0.0;
	double DevicePixelRatio = 1.0;
	JsonObject->TryGetNumberField(TEXT("x"), X);
	JsonObject->TryGetNumberField(TEXT("y"), Y);
	JsonObject->TryGetNumberField(TEXT("height"), Height);
	JsonObject->TryGetNumberField(TEXT("viewportWidth"), ViewportWidth);
	JsonObject->TryGetNumberField(TEXT("viewportHeight"), ViewportHeight);
	JsonObject->TryGetNumberField(TEXT("devicePixelRatio"), DevicePixelRatio);

	OutMessage.Point = FVector2D(X, Y);
	OutMessage.ViewportSize = FVector2D(
		FMath::Max(static_cast<float>(ViewportWidth), 0.0f),
		FMath::Max(static_cast<float>(ViewportHeight), 0.0f));
	OutMessage.Height = FMath::Max(static_cast<float>(Height), 1.0f);
	OutMessage.DevicePixelRatio = FMath::Max(static_cast<float>(DevicePixelRatio), 1.0f);
	return true;
}

bool FCBWebView2InternalMessageParser::TryConvertJsonToLegacyMessage(const FString& Message, FString& OutLegacyMessage)
{
	FCBWebView2InternalMessage InternalMessage;
	return TryParseJson(Message, InternalMessage) && InternalMessage.ToLegacyMessage(OutLegacyMessage);
}

bool FCBWebView2InternalMessageParser::TryReadLegacyBoolMessage(const FString& Message, const TCHAR* Prefix, bool& bOutValue)
{
	const int32 PrefixIndex = Message.Find(Prefix);
	if (PrefixIndex == INDEX_NONE)
	{
		return false;
	}

	FString ValueText = Message.RightChop(PrefixIndex + FCString::Strlen(Prefix));
	ValueText.TrimStartAndEndInline();
	ValueText.ReplaceInline(TEXT("\""), TEXT(""));
	ValueText.ReplaceInline(TEXT("'"), TEXT(""));
	bOutValue = ValueText.Equals(TEXT("1")) || ValueText.Equals(TEXT("true"), ESearchCase::IgnoreCase);
	return true;
}

ECBWebView2InternalMessageType FCBWebView2InternalMessageParser::ParseType(const FString& Type)
{
	if (Type.Equals(TEXT("hitTest"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("clickable"), ESearchCase::IgnoreCase))
	{
		return ECBWebView2InternalMessageType::HitTest;
	}

	if (Type.Equals(TEXT("editableFocus"), ESearchCase::IgnoreCase))
	{
		return ECBWebView2InternalMessageType::EditableFocus;
	}

	if (Type.Equals(TEXT("imeCaret"), ESearchCase::IgnoreCase))
	{
		return ECBWebView2InternalMessageType::ImeCaret;
	}

	return ECBWebView2InternalMessageType::None;
}
