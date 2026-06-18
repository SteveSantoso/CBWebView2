// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#include "CBWebView2WorldWidget.h"

#include "CBWebView2BlueprintConverters.h"
#include "SCBWebView2World.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

UCBWebView2WorldWidget::UCBWebView2WorldWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, InitialUrl(TEXT("https://www.bing.com"))
	, BackgroundColor(FColor(255, 255, 255, 255))
	, bEnableTransparencyHitTest(false)
	, RefreshRate(60.0f)
	, CurrentUrl(InitialUrl)
{
}

void UCBWebView2WorldWidget::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);

	if (SlateWorldWebView.IsValid())
	{
		SlateWorldWebView->BeginDestroy();
		SlateWorldWebView.Reset();
	}

	LastRenderedTexture = nullptr;
}

TSharedRef<SWidget> UCBWebView2WorldWidget::RebuildWidget()
{
	if (GEngine && GEngine->GameViewport)
	{
		TSharedPtr<SWindow> ParentWindow = GEngine->GameViewport->GetWindow();
		if (ParentWindow.IsValid())
		{
			return SAssignNew(SlateWorldWebView, SCBWebView2World)
				.InitialUrl(InitialUrl)
				.BackgroundColor(BackgroundColor)
				.bEnableTransparencyHitTest(bEnableTransparencyHitTest)
				.RefreshRate(RefreshRate)
				.ParentWindow(ParentWindow)
				.OnMessageReceived(FOnWebView2MessageReceivedNative::CreateUObject(this, &UCBWebView2WorldWidget::HandleMessageReceived))
				.OnNavigationCompleted(FOnWebView2NavigationCompletedNative::CreateUObject(this, &UCBWebView2WorldWidget::HandleNavigationCompleted))
				.OnNavigationStarting(FOnWebView2NavigationStartingNative::CreateUObject(this, &UCBWebView2WorldWidget::HandleNavigationStarted))
				.OnNewWindowRequested(FOnWebView2NewWindowRequestedNative::CreateUObject(this, &UCBWebView2WorldWidget::HandleNewWindowRequested))
				.OnCursorChanged(FOnWebView2CursorChangedNative::CreateUObject(this, &UCBWebView2WorldWidget::HandleCursorChangedNative))
				.OnInputActivationRequested(FOnWebView2InputActivationNative::CreateUObject(this, &UCBWebView2WorldWidget::HandleInputActivationRequestedNative))
				.OnDocumentTitleChanged(FOnWebView2DocumentTitleChangedNative::CreateUObject(this, &UCBWebView2WorldWidget::HandleDocumentTitleChangedNative))
				.OnSourceChanged(FOnWebView2SourceChangedNative::CreateUObject(this, &UCBWebView2WorldWidget::HandleSourceChangedNative))
				.OnCanGoBackChanged(FOnWebView2CanGoBackChangedNative::CreateUObject(this, &UCBWebView2WorldWidget::HandleCanGoBackChangedNative))
				.OnCanGoForwardChanged(FOnWebView2CanGoForwardChangedNative::CreateUObject(this, &UCBWebView2WorldWidget::HandleCanGoForwardChangedNative))
				.OnDownloadStarting(FOnWebView2DownloadEventNative::CreateUObject(this, &UCBWebView2WorldWidget::HandleDownloadStartingNative))
				.OnDownloadUpdated(FOnWebView2DownloadEventNative::CreateUObject(this, &UCBWebView2WorldWidget::HandleDownloadUpdatedNative))
				.OnPrintToPdfCompleted(FOnWebView2PrintToPdfCompletedNative::CreateUObject(this, &UCBWebView2WorldWidget::HandlePrintToPdfCompletedNative))
				.OnMonitoredEvent(FOnWebView2MonitoredEventNative::CreateUObject(this, &UCBWebView2WorldWidget::HandleMonitoredEventNative))
				.OnFrameUpdated(FOnCBWebView2WorldFrameUpdatedNative::CreateUObject(this, &UCBWebView2WorldWidget::HandleFrameUpdated));
		}
	}
	return SNew(SBox)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("CBWebView2World requires a valid GameViewport window")))
		];
}

void UCBWebView2WorldWidget::SynchronizeProperties()
{
	Super::SynchronizeProperties();

	if (SlateWorldWebView.IsValid())
	{
		SlateWorldWebView->SetTransparencyHitTestEnabled(bEnableTransparencyHitTest);
		SlateWorldWebView->SetBackgroundColor(BackgroundColor);
		SlateWorldWebView->SetRefreshRate(RefreshRate);
		SlateWorldWebView->RequestRefresh();
	}
}

void UCBWebView2WorldWidget::LoadURL(const FString& InUrl)
{
	InitialUrl = InUrl;
	CurrentUrl = InUrl;
	if (SlateWorldWebView.IsValid())
	{
		SlateWorldWebView->LoadURL(InUrl);
	}
}

FString UCBWebView2WorldWidget::GetCurrentURL() const
{
	return CurrentUrl.IsEmpty() ? InitialUrl : CurrentUrl;
}

FString UCBWebView2WorldWidget::GetCurrentTitle() const
{
	return CurrentTitle;
}

void UCBWebView2WorldWidget::ExecuteScript(const FString& Script, FOnCBWebView2ScriptExecuted Callback)
{
	if (SlateWorldWebView.IsValid())
	{
		FOnCBWebView2WorldScriptCallback NativeCallback;
		if (Callback.IsBound())
		{
			NativeCallback.BindLambda([Callback](const FString& Result)
			{
				if (Callback.IsBound())
				{
					Callback.Execute(Result);
				}
			});
		}

		SlateWorldWebView->ExecuteScript(Script, NativeCallback);
	}
}

void UCBWebView2WorldWidget::GoForward()
{
	if (SlateWorldWebView.IsValid())
	{
		SlateWorldWebView->GoForward();
	}
}

void UCBWebView2WorldWidget::GoBack()
{
	if (SlateWorldWebView.IsValid())
	{
		SlateWorldWebView->GoBack();
	}
}

void UCBWebView2WorldWidget::Reload()
{
	if (SlateWorldWebView.IsValid())
	{
		SlateWorldWebView->Reload();
	}
}

void UCBWebView2WorldWidget::StopLoading()
{
	if (SlateWorldWebView.IsValid())
	{
		SlateWorldWebView->Stop();
	}
}

void UCBWebView2WorldWidget::OpenDevToolsWindow()
{
	if (SlateWorldWebView.IsValid())
	{
		SlateWorldWebView->OpenDevToolsWindow();
	}
}

void UCBWebView2WorldWidget::SetBackgroundColorEx(FColor InBackgroundColor)
{
	BackgroundColor = InBackgroundColor;
	if (SlateWorldWebView.IsValid())
	{
		SlateWorldWebView->SetBackgroundColor(InBackgroundColor);
	}
}

void UCBWebView2WorldWidget::SetRefreshRateEx(float InRefreshRate)
{
	RefreshRate = FMath::Max(0.0f, InRefreshRate);
	if (SlateWorldWebView.IsValid())
	{
		SlateWorldWebView->SetRefreshRate(RefreshRate);
	}
}

void UCBWebView2WorldWidget::RequestRefresh()
{
	if (SlateWorldWebView.IsValid())
	{
		SlateWorldWebView->RequestRefresh();
	}
}

void UCBWebView2WorldWidget::HandleMessageReceived(const FString& Message)
{
	OnMessageReceived.Broadcast(Message);
}

void UCBWebView2WorldWidget::HandleNavigationCompleted(bool bSuccess)
{
	if (bSuccess && SlateWorldWebView.IsValid())
	{
		SlateWorldWebView->RequestRefresh();
	}

	OnLoadCompleted.Broadcast(bSuccess);
}

void UCBWebView2WorldWidget::HandleNavigationStarted(const FString& Url)
{
	CurrentUrl = Url;
	OnLoadStarted.Broadcast(Url);
}

void UCBWebView2WorldWidget::HandleNewWindowRequested(const FString& Url)
{
	OnNewWindowRequested.Broadcast(Url);
}

void UCBWebView2WorldWidget::HandleCursorChangedNative(EMouseCursor::Type CursorType)
{
	OnCursorChanged.Broadcast(CBWebView2BlueprintConverters::ToBlueprintCursorType(CursorType));
}

void UCBWebView2WorldWidget::HandleInputActivationRequestedNative()
{
	OnInputActivationRequested.Broadcast();
}

void UCBWebView2WorldWidget::HandleDocumentTitleChangedNative(const FString& Title)
{
	CurrentTitle = Title;
	OnDocumentTitleChanged.Broadcast(Title);
}

void UCBWebView2WorldWidget::HandleSourceChangedNative(const FString& Url)
{
	CurrentUrl = Url;
	OnSourceChanged.Broadcast(Url);
}

void UCBWebView2WorldWidget::HandleCanGoBackChangedNative(bool bCanGoBackValue)
{
	OnCanGoBackChanged.Broadcast(bCanGoBackValue);
}

void UCBWebView2WorldWidget::HandleCanGoForwardChangedNative(bool bCanGoForwardValue)
{
	OnCanGoForwardChanged.Broadcast(bCanGoForwardValue);
}

void UCBWebView2WorldWidget::HandleDownloadStartingNative(const FWebView2DownloadInfo& DownloadInfo)
{
	OnDownloadStarting.Broadcast(CBWebView2BlueprintConverters::ToBlueprintDownloadInfo(DownloadInfo));
}

void UCBWebView2WorldWidget::HandleDownloadUpdatedNative(const FWebView2DownloadInfo& DownloadInfo)
{
	OnDownloadUpdated.Broadcast(CBWebView2BlueprintConverters::ToBlueprintDownloadInfo(DownloadInfo));
}

void UCBWebView2WorldWidget::HandlePrintToPdfCompletedNative(bool bSuccess, const FString& OutputPath)
{
	OnPrintToPdfCompleted.Broadcast(bSuccess, OutputPath);
}

void UCBWebView2WorldWidget::HandleFrameUpdated(UTexture2D* Texture)
{
	LastRenderedTexture = Texture;
}

void UCBWebView2WorldWidget::HandleMonitoredEventNative(const FCBWebView2MonitoredEvent& EventInfo)
{
	OnMonitoredEvent.Broadcast(EventInfo);
}
