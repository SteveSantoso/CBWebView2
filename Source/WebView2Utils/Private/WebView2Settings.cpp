// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#include "WebView2Settings.h"

#include "WebView2Log.h"

#if WITH_EDITOR
#include "UObject/UnrealType.h"
#endif

UWebView2Settings::UWebView2Settings()
{
}

FName UWebView2Settings::GetCategoryName() const
{
	return TEXT("Plugins");
}

const UWebView2Settings* UWebView2Settings::Get()
{
	return GetDefault<UWebView2Settings>();
}

#if WITH_EDITOR
void UWebView2Settings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (DoesChangeRequireEditorRestart(PropertyChangedEvent))
	{
		const FName PropertyName = PropertyChangedEvent.Property
			? PropertyChangedEvent.Property->GetFName()
			: (PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None);

		UE_LOG(
			LogWebView2Utils,
			Warning,
			TEXT("CBWebView2 setting %s has changed. The new value is applied when creating the WebView2 Environment/Controller and requires an editor restart to take full effect."),
			PropertyName != NAME_None ? *PropertyName.ToString() : TEXT("<Unknown>"));
	}
}

bool UWebView2Settings::DoesChangeRequireEditorRestart(const FPropertyChangedEvent& PropertyChangedEvent) const
{
	const FName MemberPropertyName = PropertyChangedEvent.MemberProperty
		? PropertyChangedEvent.MemberProperty->GetFName()
		: NAME_None;

	return MemberPropertyName == GET_MEMBER_NAME_CHECKED(UWebView2Settings, Mode)
		|| MemberPropertyName == GET_MEMBER_NAME_CHECKED(UWebView2Settings, Environment)
		|| MemberPropertyName == GET_MEMBER_NAME_CHECKED(UWebView2Settings, Controller);
}
#endif