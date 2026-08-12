#include "UI/CatSurvivalWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"

// 初始化流程：父类先建立 Widget 生命周期；若无现成 Root，则创建标题、Hunger/Fatigue 专用文本与一个多源状态摘要文本并挂到纵向容器，整个树不保存玩法引用。
void UCatSurvivalWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree->RootWidget)
	{
		return;
	}

	UVerticalBox* RootBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SurvivalRoot"));
	UTextBlock* TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SurvivalTitle"));
	HungerText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("HungerValue"));
	FatigueText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("FatigueValue"));
	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("GameplayStatus"));
	TitleText->SetText(FText::FromString(TEXT("Catfishing Lake Status")));
	RootBox->AddChildToVerticalBox(TitleText);
	RootBox->AddChildToVerticalBox(HungerText);
	RootBox->AddChildToVerticalBox(FatigueText);
	RootBox->AddChildToVerticalBox(StatusText);
	WidgetTree->RootWidget = RootBox;
}

// 渲染流程：先确认三个文本节点齐全，再单独写 Hunger/Fatigue，最后从传入 DTO 一次格式化 Poison/搏斗资源、Condition、Equipment、Run/Environment 与 Help；Widget 不保存 Model，不推导阈值或反向写玩法。
void UCatSurvivalWidget::Render(const FCatSurvivalViewState& ViewState)
{
	if (!HungerText || !FatigueText || !StatusText)
	{
		return;
	}
	HungerText->SetText(FText::FromString(FString::Printf(TEXT("Hunger: %.2f"), ViewState.Hunger)));
	FatigueText->SetText(FText::FromString(FString::Printf(TEXT("Fatigue: %.2f"), ViewState.Fatigue)));
	const FString Status = FString::Printf(
		TEXT("Poison: %.2f  FishingStrength: %.2f  FightStamina: %.2f\nWet: %s  Downed: %s  Recovery: %s\n")
		TEXT("Rod: %s  Durability: %.2f  Broken: %s\nRun: Day %d  %s  Weather: %s  Time: %s\nHelp: %s  Global: %s"),
		ViewState.Poison, ViewState.FishingStrength, ViewState.FightStamina,
		ViewState.Condition.bWet ? TEXT("true") : TEXT("false"),
		ViewState.Condition.bDowned ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(ViewState.Condition.RecoveryMode),
		*ViewState.Equipment.RodDefinitionId.ToString(), ViewState.Equipment.RodDurability,
		ViewState.Equipment.bRodBroken ? TEXT("true") : TEXT("false"),
		ViewState.Run.Phase.DayIndex, *UEnum::GetValueAsString(ViewState.Run.Phase.Phase),
		*UEnum::GetValueAsString(ViewState.Run.Environment.Weather),
		*UEnum::GetValueAsString(ViewState.Run.Environment.TimeOfDay),
		*UEnum::GetValueAsString(ViewState.HelpSignal.Kind), ViewState.HelpSignal.bGlobal ? TEXT("true") : TEXT("false"));
	StatusText->SetText(FText::FromString(Status));
}
