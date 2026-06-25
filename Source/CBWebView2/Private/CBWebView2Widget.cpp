// Copyright 2026-Present SteveSantoso. All Rights Reserved.
#include "CBWebView2Widget.h"

#include "CBWebView2BlueprintConverters.h"
#include "SCBWebView2.h"

#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	bool IsVisibilityHiddenForWebView(ESlateVisibility InVisibility)
	{
		return InVisibility == ESlateVisibility::Collapsed || InVisibility == ESlateVisibility::Hidden;
	}
}

UCBWebView2Widget::UCBWebView2Widget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, InitialUrl(TEXT("https://www.bing.com"))
	, BackgroundColor(FColor(255, 255, 255, 0))
	, bEnableTransparencyHitTest(false)
	, bAllowNonInteractiveElementPassthrough(false)
	, CurrentUrl(InitialUrl)
{
}

void UCBWebView2Widget::ReleaseSlateResources(bool bReleaseChildren)
{
	UnbindOwnerVisibilityChanged();

	Super::ReleaseSlateResources(bReleaseChildren);

	if (SlateWebView.IsValid())
	{
		SlateWebView->BeginDestroy();
		SlateWebView.Reset();
	}
}

TSharedRef<SWidget> UCBWebView2Widget::RebuildWidget()
{
	if (GEngine && GEngine->GameViewport)
	{
		TSharedPtr<SWindow> ParentWindow = GEngine->GameViewport->GetWindow();
		if (ParentWindow.IsValid())
		{
			TSharedRef<SCBWebView2> NewSlateWebView = SAssignNew(SlateWebView, SCBWebView2)
				.InitialUrl(InitialUrl)
				.BackgroundColor(BackgroundColor)
				.bEnableTransparencyHitTest(bEnableTransparencyHitTest)
				.bAllowNonInteractiveElementPassthrough(bAllowNonInteractiveElementPassthrough)
				.ParentWindow(ParentWindow)
				.OnMessageReceived(FOnWebView2MessageReceivedNative::CreateUObject(this, &UCBWebView2Widget::HandleMessageReceived))
				.OnNavigationCompleted(FOnWebView2NavigationCompletedNative::CreateUObject(this, &UCBWebView2Widget::HandleNavigationCompleted))
				.OnNavigationStarting(FOnWebView2NavigationStartingNative::CreateUObject(this, &UCBWebView2Widget::HandleNavigationStarted))
				.OnNewWindowRequested(FOnWebView2NewWindowRequestedNative::CreateUObject(this, &UCBWebView2Widget::HandleNewWindowRequested))
				.OnCursorChanged(FOnWebView2CursorChangedNative::CreateUObject(this, &UCBWebView2Widget::HandleCursorChangedNative))
				.OnInputActivationRequested(FOnWebView2InputActivationNative::CreateUObject(this, &UCBWebView2Widget::HandleInputActivationRequestedNative))
				.OnDocumentTitleChanged(FOnWebView2DocumentTitleChangedNative::CreateUObject(this, &UCBWebView2Widget::HandleDocumentTitleChangedNative))
				.OnSourceChanged(FOnWebView2SourceChangedNative::CreateUObject(this, &UCBWebView2Widget::HandleSourceChangedNative))
				.OnCanGoBackChanged(FOnWebView2CanGoBackChangedNative::CreateUObject(this, &UCBWebView2Widget::HandleCanGoBackChangedNative))
				.OnCanGoForwardChanged(FOnWebView2CanGoForwardChangedNative::CreateUObject(this, &UCBWebView2Widget::HandleCanGoForwardChangedNative))
				.OnDownloadStarting(FOnWebView2DownloadEventNative::CreateUObject(this, &UCBWebView2Widget::HandleDownloadStartingNative))
				.OnDownloadUpdated(FOnWebView2DownloadEventNative::CreateUObject(this, &UCBWebView2Widget::HandleDownloadUpdatedNative))
				.OnPrintToPdfCompleted(FOnWebView2PrintToPdfCompletedNative::CreateUObject(this, &UCBWebView2Widget::HandlePrintToPdfCompletedNative))
				.OnMonitoredEvent(FOnWebView2MonitoredEventNative::CreateUObject(this, &UCBWebView2Widget::HandleMonitoredEventNative))
				.OnMouseButtonDoubleClick(FOnCBWebView2MouseButtonDoubleClickNative::CreateUObject(this, &UCBWebView2Widget::HandleSlateMouseButtonDoubleClick));

			BindOwnerVisibilityChanged();
			OwnWidgetVisibility = GetVisibility();
			ApplyWebViewVisibility();
			return NewSlateWebView;
		}
	}
	return SNew(SBox)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("CBWebView2 requires a valid GameViewport window")))
		];
}

void UCBWebView2Widget::SetVisibility(ESlateVisibility InVisibility)
{
	OwnWidgetVisibility = InVisibility;
	Super::SetVisibility(InVisibility);
	ApplyWebViewVisibility();
}

void UCBWebView2Widget::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	OwnWidgetVisibility = GetVisibility();
	if (SlateWebView.IsValid())
	{
		SlateWebView->SetTransparencyHitTestEnabled(bEnableTransparencyHitTest);
		SlateWebView->SetAllowNonInteractiveElementPassthrough(bAllowNonInteractiveElementPassthrough);
		SlateWebView->SetBackgroundColor(BackgroundColor);
	}
	ApplyWebViewVisibility();
}

void UCBWebView2Widget::BindOwnerVisibilityChanged()
{
	UUserWidget* Owner = GetTypedOuter<UUserWidget>();
	if (BoundVisibilityOwner.Get() == Owner)
	{
		if (Owner)
		{
			Owner->OnVisibilityChanged.AddUniqueDynamic(this, &UCBWebView2Widget::HandleOwnerVisibilityChanged);
		}
		return;
	}

	UnbindOwnerVisibilityChanged();

	BoundVisibilityOwner = Owner;
	if (Owner)
	{
		Owner->OnVisibilityChanged.AddUniqueDynamic(this, &UCBWebView2Widget::HandleOwnerVisibilityChanged);
	}
}

void UCBWebView2Widget::UnbindOwnerVisibilityChanged()
{
	if (UUserWidget* Owner = BoundVisibilityOwner.Get())
	{
		Owner->OnVisibilityChanged.RemoveDynamic(this, &UCBWebView2Widget::HandleOwnerVisibilityChanged);
	}
	BoundVisibilityOwner.Reset();
}

void UCBWebView2Widget::ApplyWebViewVisibility()
{
	if (SlateWebView.IsValid())
	{
		SlateWebView->SetWebViewVisibility(GetEffectiveWebViewVisibility());
	}
}

ESlateVisibility UCBWebView2Widget::GetEffectiveWebViewVisibility() const
{
	if (IsVisibilityHiddenForWebView(OwnWidgetVisibility))
	{
		return OwnWidgetVisibility;
	}

	const UUserWidget* Owner = BoundVisibilityOwner.Get();
	if (!Owner)
	{
		return OwnWidgetVisibility;
	}

	const ESlateVisibility OwnerVisibility = Owner->GetVisibility();
	if (IsVisibilityHiddenForWebView(OwnerVisibility) || OwnerVisibility == ESlateVisibility::HitTestInvisible)
	{
		return OwnerVisibility;
	}

	return OwnWidgetVisibility;
}

void UCBWebView2Widget::HandleOwnerVisibilityChanged(ESlateVisibility InVisibility)
{
	(void)InVisibility;
	ApplyWebViewVisibility();
}

FEventReply UCBWebView2Widget::OnMouseButtonDoubleClick_Implementation(FGeometry MyGeometry, const FPointerEvent& MouseEvent)
{
	return FEventReply(false);
}

FReply UCBWebView2Widget::HandleSlateMouseButtonDoubleClick(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FEventReply EventReply = OnMouseButtonDoubleClick(MyGeometry, MouseEvent);
	OnMouseButtonDoubleClickEvent.Broadcast(MyGeometry, MouseEvent);
	return EventReply.NativeReply;
}

void UCBWebView2Widget::LoadURL(const FString& InUrl)
{
	InitialUrl = InUrl;
	CurrentUrl = InUrl;

	if (SlateWebView.IsValid())
	{
		SlateWebView->LoadURL(InUrl);
	}
}

FString UCBWebView2Widget::GetCurrentURL() const
{
	return CurrentUrl.IsEmpty() ? InitialUrl : CurrentUrl;
}

FString UCBWebView2Widget::GetCurrentTitle() const
{
	return CurrentTitle;
}

void UCBWebView2Widget::ExecuteScript(const FString& Script, FOnCBWebView2ScriptExecuted Callback)
{
	if (SlateWebView.IsValid())
	{
		SlateWebView->ExecuteScript(Script, FOnCBWebView2ScriptCallback::CreateLambda([Callback](const FString& Result)
		{
			if (Callback.IsBound())
			{
				Callback.Execute(Result);
			}
		}));
	}
}

void UCBWebView2Widget::GoForward()
{
	if (SlateWebView.IsValid())
	{
		SlateWebView->GoForward();
	}
}

void UCBWebView2Widget::GoBack()
{
	if (SlateWebView.IsValid())
	{
		SlateWebView->GoBack();
	}
}

void UCBWebView2Widget::Reload()
{
	if (SlateWebView.IsValid())
	{
		SlateWebView->Reload();
	}
}

void UCBWebView2Widget::StopLoading()
{
	if (SlateWebView.IsValid())
	{
		SlateWebView->Stop();
	}
}

void UCBWebView2Widget::OpenDevToolsWindow()
{
	if (SlateWebView.IsValid())
	{
		SlateWebView->OpenDevToolsWindow();
	}
}

void UCBWebView2Widget::PrintToPdf(const FString& OutputPath, bool bLandscape)
{
	if (SlateWebView.IsValid())
	{
		SlateWebView->PrintToPdf(OutputPath, bLandscape);
	}
}

void UCBWebView2Widget::CaptureDiagnosticPreview()
{
	if (!SlateWebView.IsValid())
	{
		HandleDiagnosticPreviewCaptured(false, nullptr, TEXT("The Slate WebView has not been created yet."));
		return;
	}

	SlateWebView->CaptureDiagnosticPreview(
		FOnCBWebView2DiagnosticPreviewCapturedNative::CreateUObject(this, &UCBWebView2Widget::HandleDiagnosticPreviewCaptured));
}

void UCBWebView2Widget::SetBackgroundColorEx(FColor InBackgroundColor)
{
	BackgroundColor = InBackgroundColor;
	if (SlateWebView.IsValid())
	{
		SlateWebView->SetBackgroundColor(InBackgroundColor);
	}
}

void UCBWebView2Widget::SetWebViewVisibility(ESlateVisibility InVisibility)
{
	SetVisibility(InVisibility);
}

void UCBWebView2Widget::HandleMessageReceived(const FString& Message)
{
	// Blueprint receives webpage postMessage and transparency-detection messages through this single entry point.
	OnMessageReceived.Broadcast(Message);
}

void UCBWebView2Widget::HandleNavigationCompleted(bool bSuccess)
{
	OnLoadCompleted.Broadcast(bSuccess);
}

void UCBWebView2Widget::HandleNavigationStarted(const FString& Url)
{
	CurrentUrl = Url;
	OnLoadStarted.Broadcast(Url);
}

void UCBWebView2Widget::HandleNewWindowRequested(const FString& Url)
{
	OnNewWindowRequested.Broadcast(Url);
}

void UCBWebView2Widget::HandleCursorChangedNative(EMouseCursor::Type CursorType)
{
	OnCursorChanged.Broadcast(CBWebView2BlueprintConverters::ToBlueprintCursorType(CursorType));
}

void UCBWebView2Widget::HandleInputActivationRequestedNative()
{
	OnInputActivationRequested.Broadcast();
}

void UCBWebView2Widget::HandleDocumentTitleChangedNative(const FString& Title)
{
	CurrentTitle = Title;
	OnDocumentTitleChanged.Broadcast(Title);
}

void UCBWebView2Widget::HandleSourceChangedNative(const FString& Url)
{
	CurrentUrl = Url;
	OnSourceChanged.Broadcast(Url);
}

void UCBWebView2Widget::HandleCanGoBackChangedNative(bool bCanGoBackValue)
{
	OnCanGoBackChanged.Broadcast(bCanGoBackValue);
}

void UCBWebView2Widget::HandleCanGoForwardChangedNative(bool bCanGoForwardValue)
{
	OnCanGoForwardChanged.Broadcast(bCanGoForwardValue);
}

void UCBWebView2Widget::HandleDownloadStartingNative(const FWebView2DownloadInfo& DownloadInfo)
{
	OnDownloadStarting.Broadcast(CBWebView2BlueprintConverters::ToBlueprintDownloadInfo(DownloadInfo));
}

void UCBWebView2Widget::HandleDownloadUpdatedNative(const FWebView2DownloadInfo& DownloadInfo)
{
	OnDownloadUpdated.Broadcast(CBWebView2BlueprintConverters::ToBlueprintDownloadInfo(DownloadInfo));
}

void UCBWebView2Widget::HandlePrintToPdfCompletedNative(bool bSuccess, const FString& OutputPath)
{
	OnPrintToPdfCompleted.Broadcast(bSuccess, OutputPath);
}

void UCBWebView2Widget::HandleDiagnosticPreviewCaptured(bool bSuccess, UTexture2D* Texture, const FString& ErrorMessage)
{
	LastDiagnosticPreviewTexture = bSuccess ? Texture : nullptr;
	OnDiagnosticPreviewCaptured.Broadcast(bSuccess, Texture, ErrorMessage);
}

void UCBWebView2Widget::HandleMonitoredEventNative(const FCBWebView2MonitoredEvent& EventInfo)
{
	OnMonitoredEvent.Broadcast(EventInfo);
}
