#include "UI/CatInteractionWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Interaction/CatInteractable.h"

void UCatInteractionWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree->RootWidget)
	{
		return;
	}
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("InteractionRoot"));
	CrosshairDot = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CrosshairDot"));
	PromptText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("InteractionPrompt"));
	CrosshairDot->SetBrushColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.75f));
	CrosshairDot->SetPadding(FMargin(0.0f));
	PromptText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	PromptText->SetShadowOffset(FVector2D(1.0f, 1.0f));
	PromptText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.9f));
	PromptText->SetJustification(ETextJustify::Center);
	PromptText->SetVisibility(ESlateVisibility::Collapsed);

	UCanvasPanelSlot* DotSlot = Root->AddChildToCanvas(CrosshairDot);
	DotSlot->SetAnchors(FAnchors(0.5f, 0.5f));
	DotSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	DotSlot->SetPosition(FVector2D::ZeroVector);
	DotSlot->SetSize(FVector2D(4.0f, 4.0f));

	UCanvasPanelSlot* PromptSlot = Root->AddChildToCanvas(PromptText);
	PromptSlot->SetAnchors(FAnchors(0.5f, 0.5f));
	PromptSlot->SetAlignment(FVector2D(0.5f, 0.0f));
	PromptSlot->SetPosition(FVector2D(0.0f, 20.0f));
	PromptSlot->SetAutoSize(true);
	WidgetTree->RootWidget = Root;
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UCatInteractionWidget::RenderTarget(AActor* Target)
{
	if (!CrosshairDot || !PromptText)
	{
		return;
	}
	const bool bInteractable = IsValid(Target)
		&& Target->GetClass()->ImplementsInterface(UCatInteractable::StaticClass());
	const FText Prompt = bInteractable
		? ICatInteractable::Execute_GetInteractionPrompt(Target) : FText::GetEmpty();
	CrosshairDot->SetBrushColor(bInteractable
		? FLinearColor::White : FLinearColor(1.0f, 1.0f, 1.0f, 0.75f));
	PromptText->SetText(Prompt);
	PromptText->SetVisibility(Prompt.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}
