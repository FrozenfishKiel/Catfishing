#include "UI/Shop/CatShopWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "UI/CatUISettings.h"

// 动态按钮初始化流程：保存父 View 和当前商品意图，重建点击绑定；按钮本身不保存任何后端经济事实。
void UCatShopEntryButton::InitializeShopEntry(UCatShopWidget* InOwnerShopWidget, const FName InEntryId,
	const ECatShopUIAction InAction)
{
	OwnerShopWidget = InOwnerShopWidget;
	EntryId = InEntryId;
	Action = InAction;
	OnClicked.RemoveDynamic(this, &ThisClass::HandleShopEntryClicked);
	OnClicked.AddDynamic(this, &ThisClass::HandleShopEntryClicked);
}

// 动态按钮点击流程：校验父 View 和 EntryId 后调用统一请求口；价格、库存和是否允许购买仍由 Model/Controller/服务器裁决。
void UCatShopEntryButton::HandleShopEntryClicked()
{
	UCatShopWidget* ShopWidget = OwnerShopWidget.Get();
	if (!ShopWidget || EntryId.IsNone())
	{
		return;
	}
	if (Action == ECatShopUIAction::ClaimFreeEntry)
	{
		ShopWidget->RequestFreeClaimEntry(EntryId);
	}
	else if (Action == ECatShopUIAction::PurchaseEntry)
	{
		ShopWidget->RequestPurchaseEntry(EntryId);
	}
}

// 构造流程：
// 1. 先把商店根 Widget 设为可聚焦，让通用 UIOnly 输入模式能把键盘焦点真正交给本页面；后续关闭键才会进入 NativeOnKeyDown。
// 2. 再把 Designer 里的关闭按钮接到统一关闭入口；正式货架按钮会在 RenderShop 中按 Entries 动态生成。
void UCatShopWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	if (CloseButton)
	{
		CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCloseClicked);
		CloseButton->OnClicked.AddDynamic(this, &ThisClass::HandleCloseClicked);
		CloseButton->SetIsEnabled(true);
	}
}

// 析构流程：
// 1. 先清掉本次动态生成的商品按钮绑定，防止货架行对象迟到提交过期 EntryId。
// 2. 再解除本 View 绑定到可选关闭按钮上的 UMG 点击事件，防止再次入视口时叠加回调。
// 3. 保留关闭和商品动作意图订阅；RemoveFromParent 只是页面离开视口，PageController::Unbind 才是外部订阅的结束点。
void UCatShopWidget::NativeDestruct()
{
	for (UCatShopEntryButton* Button : DynamicEntryButtons)
	{
		if (Button)
		{
			Button->OnClicked.Clear();
		}
	}
	DynamicEntryButtons.Reset();
	if (CloseButton)
	{
		CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCloseClicked);
	}
	Super::NativeDestruct();
}

// 预览键盘流程：先于商品按钮消费关闭键；命中后仍只广播关闭意图，让 PageController 和交互组件成对恢复输入与销毁页面。
FReply UCatShopWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (ShouldCloseShopFromKey(InKeyEvent))
	{
		RequestCloseShop();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

// 键盘流程：商店根页拿到焦点时复用同一关闭键判断；预览未命中的按键继续交给父类。
FReply UCatShopWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (ShouldCloseShopFromKey(InKeyEvent))
	{
		RequestCloseShop();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

// 渲染流程：
// 1. 缓存只读投影，复制 Designer 绑定字段；有商品容器时按 Entries 重建动态按钮，商品点击不从控件名字推断。
// 2. BP 渲染扩展执行后重新保证商店根、商品容器和关闭按钮可用，因为购买 pending 只应该锁商品动作，不能把页面生命周期出口一起锁住。
// 3. pending 期间把键盘焦点拉回商店根页，避免商品按钮在点击后被重建或禁用时截断 Escape/交互键关闭。
void UCatShopWidget::RenderShop(const FCatShopViewState& ViewState)
{
	LastShopViewState = ViewState;
	BlueprintWalletText = ViewState.WalletText;
	BlueprintResultText = ViewState.ResultText;
	BlueprintEntries = ViewState.Entries;
	TArray<FString> Lines;
	Lines.Reserve(ViewState.Entries.Num());
	for (const FCatShopEntryView& Entry : ViewState.Entries)
	{
		Lines.Add(Entry.DisplayText.ToString());
	}
	BlueprintEntriesText = FText::FromString(FString::Join(Lines, TEXT("\n")));
	if (WalletTextBlock)
	{
		WalletTextBlock->SetText(BlueprintWalletText);
	}
	if (ResultTextBlock)
	{
		ResultTextBlock->SetText(BlueprintResultText);
	}
	if (EntriesTextBlock)
	{
		EntriesTextBlock->SetText(BlueprintEntriesText);
	}
	if (ShopButtons)
	{
		RebuildDynamicEntryButtons(ViewState);
		ShopButtons->SetIsEnabled(true);
	}
	BP_RenderShop(LastShopViewState);
	SetIsEnabled(true);
	if (CloseButton)
	{
		CloseButton->SetIsEnabled(true);
	}
	if (ViewState.bOpen && ViewState.bActionPending)
	{
		SetKeyboardFocus();
	}
}

// 状态读取流程：返回最近商店投影；调用者不能通过它取得公款或商品写口。
const FCatShopViewState& UCatShopWidget::GetLastShopViewState() const
{
	return LastShopViewState;
}

// 购买请求流程：只允许有效 EntryId 出口；价格、库存和公款并发版本由 PageController 与服务器后端补齐。
void UCatShopWidget::RequestPurchaseEntry(const FName EntryId)
{
	if (EntryId.IsNone())
	{
		return;
	}
	OnEntryActionRequested.Broadcast(EntryId, ECatShopUIAction::PurchaseEntry);
}

// 免费领取流程：只允许有效 EntryId 出口；是否真免费由服务器 ShopEconomy 白名单裁决。
void UCatShopWidget::RequestFreeClaimEntry(const FName EntryId)
{
	if (EntryId.IsNone())
	{
		return;
	}
	OnEntryActionRequested.Broadcast(EntryId, ECatShopUIAction::ClaimFreeEntry);
}

// 关闭请求流程：只广播关闭意图，输入模式和 Widget 生命周期由 PageController/交互组件处理。
void UCatShopWidget::RequestCloseShop()
{
	OnCloseRequested.Broadcast();
}

// 默认关闭按钮流程：复用统一关闭请求入口，避免按钮和蓝图形成两条关闭路径。
void UCatShopWidget::HandleCloseClicked()
{
	RequestCloseShop();
}

// 动态货架流程：
// 1. 缺少商品容器或 WidgetTree 时不重建动态按钮；文本投影和蓝图渲染扩展仍会继续承接展示。
// 2. 正常情况下先清空商品容器里的旧内容；这个容器只表达表驱动货架，直接放在这里的关闭按钮会保留为页面出口。
// 3. 逐条读取 Model 投影生成按钮文本和动作类型；免费/购买来自 bFreeClaim，不按价格重新推断。
// 4. 每个按钮只保存 EntryId 与动作类型，点击后仍走 Widget 的统一意图广播，保持 View 不碰交易后端。
void UCatShopWidget::RebuildDynamicEntryButtons(const FCatShopViewState& ViewState)
{
	if (!ShopButtons || !WidgetTree)
	{
		return;
	}
	for (int32 ChildIndex = ShopButtons->GetChildrenCount() - 1; ChildIndex >= 0; --ChildIndex)
	{
		UWidget* Child = ShopButtons->GetChildAt(ChildIndex);
		if (!Child || Child == CloseButton)
		{
			continue;
		}
		if (UCatShopEntryButton* EntryButton = Cast<UCatShopEntryButton>(Child))
		{
			EntryButton->OnClicked.Clear();
		}
		ShopButtons->RemoveChildAt(ChildIndex);
	}
	DynamicEntryButtons.Reset();

	for (const FCatShopEntryView& Entry : ViewState.Entries)
	{
		UCatShopEntryButton* EntryButton = WidgetTree->ConstructWidget<UCatShopEntryButton>(
			UCatShopEntryButton::StaticClass(), MakeUniqueObjectName(WidgetTree, UCatShopEntryButton::StaticClass(),
				FName(TEXT("ShopEntryButton"))));
		UTextBlock* ButtonText = WidgetTree->ConstructWidget<UTextBlock>(
			UTextBlock::StaticClass(), MakeUniqueObjectName(WidgetTree, UTextBlock::StaticClass(),
				FName(TEXT("ShopEntryButtonText"))));
		if (!EntryButton || !ButtonText)
		{
			continue;
		}
		const FString ButtonLabel = FString::Printf(TEXT("%s  %s"),
			*Entry.ActionText.ToString(), *Entry.DisplayText.ToString());
		ButtonText->SetText(FText::FromString(ButtonLabel));
		ButtonText->SetAutoWrapText(true);
		EntryButton->AddChild(ButtonText);
		EntryButton->SetIsEnabled(Entry.bActionEnabled);
		EntryButton->InitializeShopEntry(this, Entry.EntryId,
			Entry.bFreeClaim ? ECatShopUIAction::ClaimFreeEntry : ECatShopUIAction::PurchaseEntry);
		ShopButtons->AddChild(EntryButton);
		DynamicEntryButtons.Add(EntryButton);
	}
}

// 关闭键判断流程：
// 1. 先要求商店处于打开投影，避免构造或移出视口期间的迟到按键误触发关闭。
// 2. Escape 始终作为模态 UI 兜底关闭键。
// 3. 再接受项目配置里的交互键和背包键，让按 E 打开的商店能按 E 关闭，也能用常规库存键退出页面。
bool UCatShopWidget::ShouldCloseShopFromKey(const FKeyEvent& InKeyEvent) const
{
	if (!LastShopViewState.bOpen)
	{
		return false;
	}
	const FName PressedKeyName = InKeyEvent.GetKey().GetFName();
	if (PressedKeyName == EKeys::Escape.GetFName())
	{
		return true;
	}
	const UCatUISettings* Settings = GetDefault<UCatUISettings>();
	const FName InteractionKeyName = Settings ? Settings->ResolveInteractionConfirmKeyName() : NAME_None;
	const FName InventoryKeyName = Settings ? Settings->ResolveInventoryToggleKeyName() : NAME_None;
	return (!InteractionKeyName.IsNone() && PressedKeyName == InteractionKeyName)
		|| (!InventoryKeyName.IsNone() && PressedKeyName == InventoryKeyName);
}
