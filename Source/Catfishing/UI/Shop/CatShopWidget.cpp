#include "UI/Shop/CatShopWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/TextBlock.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "UI/CatUISettings.h"

// 构造流程：
// 1. 先把商店根 Widget 设为可聚焦，让通用 UIOnly 输入模式能把键盘焦点真正交给本页面；后续关闭键才会进入 NativeOnKeyDown。
// 2. 再让旧 WBP 在已有按钮行里补齐鱼漂购买按钮，保证默认商店能拿到开钓必需的鱼漂。
// 3. 最后先移除已有点击绑定，再把 Designer 或运行时按钮接到统一请求入口；动态商品列表仍可完全绕开这些固定按钮。
void UCatShopWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	EnsureDefaultFloatButton();
	if (CloseButton)
	{
		CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCloseClicked);
		CloseButton->OnClicked.AddDynamic(this, &ThisClass::HandleCloseClicked);
	}
	if (PurchaseShopRodT2Button)
	{
		PurchaseShopRodT2Button->OnClicked.RemoveDynamic(this, &ThisClass::HandlePurchaseRodClicked);
		PurchaseShopRodT2Button->OnClicked.AddDynamic(this, &ThisClass::HandlePurchaseRodClicked);
	}
	if (PurchaseBugChumButton)
	{
		PurchaseBugChumButton->OnClicked.RemoveDynamic(this, &ThisClass::HandlePurchaseChumClicked);
		PurchaseBugChumButton->OnClicked.AddDynamic(this, &ThisClass::HandlePurchaseChumClicked);
	}
	if (PurchaseYarnBallFloatButton)
	{
		PurchaseYarnBallFloatButton->OnClicked.RemoveDynamic(this, &ThisClass::HandlePurchaseFloatClicked);
		PurchaseYarnBallFloatButton->OnClicked.AddDynamic(this, &ThisClass::HandlePurchaseFloatClicked);
	}
	if (ClaimFreeBugBaitButton)
	{
		ClaimFreeBugBaitButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleClaimBaitClicked);
		ClaimFreeBugBaitButton->OnClicked.AddDynamic(this, &ThisClass::HandleClaimBaitClicked);
	}
	if (ClaimFreeStarterRodButton)
	{
		ClaimFreeStarterRodButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleClaimStarterRodClicked);
		ClaimFreeStarterRodButton->OnClicked.AddDynamic(this, &ThisClass::HandleClaimStarterRodClicked);
	}
}

// 析构流程：
// 1. 解除本 View 绑定到可选按钮上的 UMG 点击事件，防止再次入视口时叠加回调。
// 2. 保留关闭和商品动作意图订阅；RemoveFromParent 只是页面离开视口，PageController::Unbind 才是外部订阅的结束点。
void UCatShopWidget::NativeDestruct()
{
	if (CloseButton)
	{
		CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCloseClicked);
	}
	if (PurchaseShopRodT2Button)
	{
		PurchaseShopRodT2Button->OnClicked.RemoveDynamic(this, &ThisClass::HandlePurchaseRodClicked);
	}
	if (PurchaseBugChumButton)
	{
		PurchaseBugChumButton->OnClicked.RemoveDynamic(this, &ThisClass::HandlePurchaseChumClicked);
	}
	if (PurchaseYarnBallFloatButton)
	{
		PurchaseYarnBallFloatButton->OnClicked.RemoveDynamic(this, &ThisClass::HandlePurchaseFloatClicked);
	}
	if (ClaimFreeBugBaitButton)
	{
		ClaimFreeBugBaitButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleClaimBaitClicked);
	}
	if (ClaimFreeStarterRodButton)
	{
		ClaimFreeStarterRodButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleClaimStarterRodClicked);
	}
	Super::NativeDestruct();
}

// 键盘流程：
// 1. 商店打开后输入模式切到 UIOnly，PlayerController 不再接玩法输入，所以关闭键必须由拥有焦点的 Widget 处理。
// 2. 先读取 Model 投给 View 的打开状态；未打开时直接交回父类，避免构造或销毁过程中的迟到按键广播关闭意图。
// 3. 再读取项目配置里的交互确认键；配置缺失时只保留 Escape 作为通用模态关闭出口。
// 4. 命中 Escape 或交互键后只广播 View 意图并返回 Handled，输入恢复和实例销毁仍交给 PageController 与交互组件。
FReply UCatShopWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (!LastShopViewState.bOpen)
	{
		return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
	}

	const UCatUISettings* Settings = GetDefault<UCatUISettings>();
	const FName InteractionKeyName = Settings ? Settings->ResolveInteractionConfirmKeyName() : NAME_None;
	const FName PressedKeyName = InKeyEvent.GetKey().GetFName();
	const bool bPressedCloseKey = PressedKeyName == EKeys::Escape.GetFName()
		|| (!InteractionKeyName.IsNone() && PressedKeyName == InteractionKeyName);
	if (bPressedCloseKey)
	{
		RequestCloseShop();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

// 渲染流程：缓存只读投影，复制 Designer 绑定字段，并按每条商品自己的余额/库存 gate 更新默认按钮。
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
	const auto IsEntryActionEnabled = [&ViewState](const FName EntryId)
	{
		const FCatShopEntryView* Entry = ViewState.Entries.FindByPredicate(
			[EntryId](const FCatShopEntryView& Candidate)
			{
				return Candidate.EntryId == EntryId;
			});
		return Entry && Entry->bActionEnabled;
	};
	if (PurchaseShopRodT2Button)
	{
		PurchaseShopRodT2Button->SetIsEnabled(IsEntryActionEnabled(TEXT("ShopRodT2Order")));
	}
	if (PurchaseBugChumButton)
	{
		PurchaseBugChumButton->SetIsEnabled(IsEntryActionEnabled(TEXT("ShopBugChumOrder")));
	}
	if (PurchaseYarnBallFloatButton)
	{
		PurchaseYarnBallFloatButton->SetIsEnabled(IsEntryActionEnabled(TEXT("ShopFloatYarnBallOrder")));
	}
	if (ClaimFreeBugBaitButton)
	{
		ClaimFreeBugBaitButton->SetIsEnabled(IsEntryActionEnabled(TEXT("FreeBugBaitClaim")));
	}
	if (ClaimFreeStarterRodButton)
	{
		ClaimFreeStarterRodButton->SetIsEnabled(IsEntryActionEnabled(TEXT("FreeStarterRodClaim")));
	}
	BP_RenderShop(LastShopViewState);
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

// 默认按钮流程：二级竿按钮只发固定目录 ID 的购买意图，仍不携带价格或库存。
void UCatShopWidget::HandlePurchaseRodClicked()
{
	RequestPurchaseEntry(TEXT("ShopRodT2Order"));
}

// 默认按钮流程：窝料按钮只发固定目录 ID 的购买意图，仍不携带价格或库存。
void UCatShopWidget::HandlePurchaseChumClicked()
{
	RequestPurchaseEntry(TEXT("ShopBugChumOrder"));
}

// 默认按钮流程：鱼漂按钮只发固定目录 ID 的购买意图；服务器仍按配置价格、库存和权限裁决。
void UCatShopWidget::HandlePurchaseFloatClicked()
{
	RequestPurchaseEntry(TEXT("ShopFloatYarnBallOrder"));
}

// 默认按钮流程：免费鱼饵按钮只发固定目录 ID 的领取意图，服务器会验证它是否属于免费条目。
void UCatShopWidget::HandleClaimBaitClicked()
{
	RequestFreeClaimEntry(TEXT("FreeBugBaitClaim"));
}

// 默认按钮流程：保底竿按钮只发固定目录 ID 的领取意图，服务器会验证它是否属于免费条目。
void UCatShopWidget::HandleClaimStarterRodClicked()
{
	RequestFreeClaimEntry(TEXT("FreeStarterRodClaim"));
}

// 默认关闭按钮流程：复用统一关闭请求入口，避免按钮和蓝图形成两条关闭路径。
void UCatShopWidget::HandleCloseClicked()
{
	RequestCloseShop();
}

// 默认按钮补齐流程：
// 1. 已有新版 WBP 绑定鱼漂按钮时直接复用，避免运行时生成重复控件。
// 2. 旧版冷启动 WBP 至少会有 ShopButtons 按钮行；在这里创建按钮和文本，使现有资产也能购买鱼漂。
// 3. 缺少 WidgetTree 或按钮行时放弃补齐，让动态商品行或蓝图参数入口继续承担商品购买入口。
void UCatShopWidget::EnsureDefaultFloatButton()
{
	if (PurchaseYarnBallFloatButton || !ShopButtons || !WidgetTree)
	{
		return;
	}

	PurchaseYarnBallFloatButton = WidgetTree->ConstructWidget<UButton>(
		UButton::StaticClass(), TEXT("PurchaseYarnBallFloatButton"));
	UTextBlock* ButtonText = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("PurchaseYarnBallFloatButtonText"));
	if (!PurchaseYarnBallFloatButton || !ButtonText)
	{
		PurchaseYarnBallFloatButton = nullptr;
		return;
	}

	ButtonText->SetText(FText::FromString(TEXT("买鱼漂")));
	PurchaseYarnBallFloatButton->AddChild(ButtonText);
	ShopButtons->AddChildToHorizontalBox(PurchaseYarnBallFloatButton);
}
