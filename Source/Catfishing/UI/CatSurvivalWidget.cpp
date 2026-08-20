#include "UI/CatSurvivalWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"

// 初始化流程：父类先建立 Widget 生命周期；若无现成 Root，则创建标题与一个多源状态摘要文本并挂到纵向容器，整个树不保存玩法引用。
void UCatSurvivalWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree->RootWidget)
	{
		return;
	}

	UVerticalBox* RootBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SurvivalRoot"));
	UTextBlock* TitleText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("SurvivalTitle"));
	StatusText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("GameplayStatus"));
	TitleText->SetText(FText::FromString(TEXT("Catfishing Lake Status")));
	RootBox->AddChildToVerticalBox(TitleText);
	RootBox->AddChildToVerticalBox(StatusText);
	WidgetTree->RootWidget = RootBox;
}

// 渲染流程：先确认 Render 会写到的每个文本节点都已构造——NativeOnInitialized 在 RootWidget 已存在时会提前返回，那条路
// 径下这些指针仍是空的，所以这道守卫是真实防线而不是形式检查；
// 通过后从传入 DTO 一次格式化 Poison/搏斗资源、Condition、Equipment（含随身耗材栈逐项数量）、Run/Environment 与
// Help；Widget 不保存 Model，不推导阈值或反向写玩法。
void UCatSurvivalWidget::Render(const FCatSurvivalViewState& ViewState)
{
	if (!StatusText)
	{
		return;
	}
	// 耗材栈按"定义 x 数量"逐项列出；空栈显示 none，让人在 PIE 里一眼看出免费领饵/买窝料有没有真的落到猫身上。
	FString Consumables;
	for (const FCatRunConsumableStack& Stack : ViewState.Equipment.Consumables)
	{
		Consumables += FString::Printf(TEXT("%s%s x%d"), Consumables.IsEmpty() ? TEXT("") : TEXT(", "),
			*Stack.DefinitionId.ToString(), Stack.Quantity);
	}
	const FString Status = FString::Printf(
		TEXT("Poison: %.2f  FishingStrength: %.2f  FightStamina: %.2f\nWet: %s  Downed: %s  Recovery: %s\n")
		TEXT("Rod: %s  Durability: %.2f  Broken: %s  Bait: %s  Float: %s\nConsumables: %s\nRun: Day %d  %s  Weather: %s  Time: %s\nHelp: %s  Global: %s"),
		ViewState.Poison, ViewState.FishingStrength, ViewState.FightStamina,
		ViewState.Condition.bWet ? TEXT("true") : TEXT("false"),
		ViewState.Condition.bDowned ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(ViewState.Condition.RecoveryMode),
		*ViewState.Equipment.RodDefinitionId.ToString(), ViewState.Equipment.RodDurability,
		ViewState.Equipment.bRodBroken ? TEXT("true") : TEXT("false"),
		*ViewState.Equipment.BaitDefinitionId.ToString(), *ViewState.Equipment.FloatDefinitionId.ToString(),
		Consumables.IsEmpty() ? TEXT("none") : *Consumables,
		ViewState.Run.Phase.DayIndex, *UEnum::GetValueAsString(ViewState.Run.Phase.Phase),
		*UEnum::GetValueAsString(ViewState.Run.Environment.Weather),
		*UEnum::GetValueAsString(ViewState.Run.Environment.TimeOfDay),
		*UEnum::GetValueAsString(ViewState.HelpSignal.Kind), ViewState.HelpSignal.bGlobal ? TEXT("true") : TEXT("false"));
	StatusText->SetText(FText::FromString(Status));
}
