#include "UI/CatLakeReachWidget.h"

#include "Components/Button.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"

namespace
{
	constexpr int32 FishGuardVisibleSlotCount = 8;

	// 鱼护动作文本转换流程：只把 UI 枚举转成玩家能看懂的短标签，不参与服务器命令选择。
	FString GetFishGuardActionDisplayText(const ECatUIReachFishGuardAction Action)
	{
		switch (Action)
		{
		case ECatUIReachFishGuardAction::ConsumeSelectedFish:
			return TEXT("吃鱼");
		case ECatUIReachFishGuardAction::TransferSelectedFishToTank:
			return TEXT("转入鱼缸");
		case ECatUIReachFishGuardAction::SacrificeSelectedFish:
			return TEXT("献祭");
		case ECatUIReachFishGuardAction::None:
		default:
			return TEXT("无");
		}
	}

	// 领域错误文本转换流程：只覆盖玩家常见错误，其余枚举保留原名，便于程序员按日志继续定位。
	FString GetDomainErrorDisplayText(const ECatDomainCommandError Error)
	{
		switch (Error)
		{
		case ECatDomainCommandError::None:
			return TEXT("成功");
		case ECatDomainCommandError::InvalidPayload:
			return TEXT("请求内容无效");
		case ECatDomainCommandError::DependencyUnavailable:
			return TEXT("当前缺少可用目标");
		case ECatDomainCommandError::NotFound:
			return TEXT("目标不存在");
		case ECatDomainCommandError::RevisionConflict:
			return TEXT("状态已变化，请再试一次");
		case ECatDomainCommandError::PermissionDenied:
			return TEXT("现在不能这样做");
		case ECatDomainCommandError::CommandsClosed:
			return TEXT("本阶段已关闭");
		case ECatDomainCommandError::AlreadyResolved:
			return TEXT("请求已处理");
		default:
			return UEnum::GetValueAsString(Error);
		}
	}

}

// 初始化流程：只让 UUserWidget 可接收键盘焦点；正式布局和控件树由 WBP 资产提供，C++ 不再构造玩家可见白盒界面。
void UCatLakeReachWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
}

// 构造流程：先恢复父类 Slate 生命周期，再对关闭、离局、鱼护和商店这些 WBP 可选按钮执行 Remove/Add 配对；没有绑定按钮时仍允许蓝图通过各 Request* 入口广播同一类意图。
void UCatLakeReachWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (CloseButton)
	{
		CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCloseClicked);
		CloseButton->OnClicked.AddDynamic(this, &ThisClass::HandleCloseClicked);
	}
	if (LeaveButton)
	{
		LeaveButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleLeaveClicked);
		LeaveButton->OnClicked.AddDynamic(this, &ThisClass::HandleLeaveClicked);
	}
	if (PreviousFishGuardButton)
	{
		PreviousFishGuardButton->OnClicked.RemoveDynamic(this, &ThisClass::HandlePreviousFishGuardClicked);
		PreviousFishGuardButton->OnClicked.AddDynamic(this, &ThisClass::HandlePreviousFishGuardClicked);
	}
	if (NextFishGuardButton)
	{
		NextFishGuardButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleNextFishGuardClicked);
		NextFishGuardButton->OnClicked.AddDynamic(this, &ThisClass::HandleNextFishGuardClicked);
	}
	if (FishGuardSlot0Button)
	{
		FishGuardSlot0Button->OnClicked.RemoveDynamic(this, &ThisClass::HandleFishGuardSlot0Clicked);
		FishGuardSlot0Button->OnClicked.AddDynamic(this, &ThisClass::HandleFishGuardSlot0Clicked);
	}
	if (FishGuardSlot1Button)
	{
		FishGuardSlot1Button->OnClicked.RemoveDynamic(this, &ThisClass::HandleFishGuardSlot1Clicked);
		FishGuardSlot1Button->OnClicked.AddDynamic(this, &ThisClass::HandleFishGuardSlot1Clicked);
	}
	if (FishGuardSlot2Button)
	{
		FishGuardSlot2Button->OnClicked.RemoveDynamic(this, &ThisClass::HandleFishGuardSlot2Clicked);
		FishGuardSlot2Button->OnClicked.AddDynamic(this, &ThisClass::HandleFishGuardSlot2Clicked);
	}
	if (FishGuardSlot3Button)
	{
		FishGuardSlot3Button->OnClicked.RemoveDynamic(this, &ThisClass::HandleFishGuardSlot3Clicked);
		FishGuardSlot3Button->OnClicked.AddDynamic(this, &ThisClass::HandleFishGuardSlot3Clicked);
	}
	if (FishGuardSlot4Button)
	{
		FishGuardSlot4Button->OnClicked.RemoveDynamic(this, &ThisClass::HandleFishGuardSlot4Clicked);
		FishGuardSlot4Button->OnClicked.AddDynamic(this, &ThisClass::HandleFishGuardSlot4Clicked);
	}
	if (FishGuardSlot5Button)
	{
		FishGuardSlot5Button->OnClicked.RemoveDynamic(this, &ThisClass::HandleFishGuardSlot5Clicked);
		FishGuardSlot5Button->OnClicked.AddDynamic(this, &ThisClass::HandleFishGuardSlot5Clicked);
	}
	if (FishGuardSlot6Button)
	{
		FishGuardSlot6Button->OnClicked.RemoveDynamic(this, &ThisClass::HandleFishGuardSlot6Clicked);
		FishGuardSlot6Button->OnClicked.AddDynamic(this, &ThisClass::HandleFishGuardSlot6Clicked);
	}
	if (FishGuardSlot7Button)
	{
		FishGuardSlot7Button->OnClicked.RemoveDynamic(this, &ThisClass::HandleFishGuardSlot7Clicked);
		FishGuardSlot7Button->OnClicked.AddDynamic(this, &ThisClass::HandleFishGuardSlot7Clicked);
	}
	if (ConsumeFishButton)
	{
		ConsumeFishButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleConsumeFishClicked);
		ConsumeFishButton->OnClicked.AddDynamic(this, &ThisClass::HandleConsumeFishClicked);
	}
	if (TransferFishToTankButton)
	{
		TransferFishToTankButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleTransferFishToTankClicked);
		TransferFishToTankButton->OnClicked.AddDynamic(this, &ThisClass::HandleTransferFishToTankClicked);
	}
	if (SacrificeFishButton)
	{
		SacrificeFishButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleSacrificeFishClicked);
		SacrificeFishButton->OnClicked.AddDynamic(this, &ThisClass::HandleSacrificeFishClicked);
	}
	if (PurchaseShopRodT2Button)
	{
		PurchaseShopRodT2Button->OnClicked.RemoveDynamic(this, &ThisClass::HandlePurchaseShopRodT2Clicked);
		PurchaseShopRodT2Button->OnClicked.AddDynamic(this, &ThisClass::HandlePurchaseShopRodT2Clicked);
	}
	if (PurchaseBugChumButton)
	{
		PurchaseBugChumButton->OnClicked.RemoveDynamic(this, &ThisClass::HandlePurchaseBugChumClicked);
		PurchaseBugChumButton->OnClicked.AddDynamic(this, &ThisClass::HandlePurchaseBugChumClicked);
	}
	if (ClaimFreeBugBaitButton)
	{
		ClaimFreeBugBaitButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleClaimFreeBugBaitClicked);
		ClaimFreeBugBaitButton->OnClicked.AddDynamic(this, &ThisClass::HandleClaimFreeBugBaitClicked);
	}
	if (ClaimFreeStarterRodButton)
	{
		ClaimFreeStarterRodButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleClaimFreeStarterRodClicked);
		ClaimFreeStarterRodButton->OnClicked.AddDynamic(this, &ThisClass::HandleClaimFreeStarterRodClicked);
	}
}

// 销毁流程：
// 1. 解除 WBP 可选按钮对本对象的动态绑定，避免同一 View 再次进入视口时重复响应点击。
// 2. 保留 OnCloseRequested 等外部意图订阅；RemoveFromParent 只代表本次菜单离开视口，PageController::Unbind 才负责结束外部订阅生命周期。
void UCatLakeReachWidget::NativeDestruct()
{
	if (CloseButton)
	{
		CloseButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCloseClicked);
	}
	if (LeaveButton)
	{
		LeaveButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleLeaveClicked);
	}
	if (PreviousFishGuardButton)
	{
		PreviousFishGuardButton->OnClicked.RemoveDynamic(this, &ThisClass::HandlePreviousFishGuardClicked);
	}
	if (NextFishGuardButton)
	{
		NextFishGuardButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleNextFishGuardClicked);
	}
	if (FishGuardSlot0Button)
	{
		FishGuardSlot0Button->OnClicked.RemoveDynamic(this, &ThisClass::HandleFishGuardSlot0Clicked);
	}
	if (FishGuardSlot1Button)
	{
		FishGuardSlot1Button->OnClicked.RemoveDynamic(this, &ThisClass::HandleFishGuardSlot1Clicked);
	}
	if (FishGuardSlot2Button)
	{
		FishGuardSlot2Button->OnClicked.RemoveDynamic(this, &ThisClass::HandleFishGuardSlot2Clicked);
	}
	if (FishGuardSlot3Button)
	{
		FishGuardSlot3Button->OnClicked.RemoveDynamic(this, &ThisClass::HandleFishGuardSlot3Clicked);
	}
	if (FishGuardSlot4Button)
	{
		FishGuardSlot4Button->OnClicked.RemoveDynamic(this, &ThisClass::HandleFishGuardSlot4Clicked);
	}
	if (FishGuardSlot5Button)
	{
		FishGuardSlot5Button->OnClicked.RemoveDynamic(this, &ThisClass::HandleFishGuardSlot5Clicked);
	}
	if (FishGuardSlot6Button)
	{
		FishGuardSlot6Button->OnClicked.RemoveDynamic(this, &ThisClass::HandleFishGuardSlot6Clicked);
	}
	if (FishGuardSlot7Button)
	{
		FishGuardSlot7Button->OnClicked.RemoveDynamic(this, &ThisClass::HandleFishGuardSlot7Clicked);
	}
	if (ConsumeFishButton)
	{
		ConsumeFishButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleConsumeFishClicked);
	}
	if (TransferFishToTankButton)
	{
		TransferFishToTankButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleTransferFishToTankClicked);
	}
	if (SacrificeFishButton)
	{
		SacrificeFishButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleSacrificeFishClicked);
	}
	if (PurchaseShopRodT2Button)
	{
		PurchaseShopRodT2Button->OnClicked.RemoveDynamic(this, &ThisClass::HandlePurchaseShopRodT2Clicked);
	}
	if (PurchaseBugChumButton)
	{
		PurchaseBugChumButton->OnClicked.RemoveDynamic(this, &ThisClass::HandlePurchaseBugChumClicked);
	}
	if (ClaimFreeBugBaitButton)
	{
		ClaimFreeBugBaitButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleClaimFreeBugBaitClicked);
	}
	if (ClaimFreeStarterRodButton)
	{
		ClaimFreeStarterRodButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleClaimFreeStarterRodClicked);
	}
	Super::NativeDestruct();
}

// 键盘流程：仅在菜单已展开且输入键名等于 PageController 最近投影的菜单键时请求关闭并消费；其余输入保持 UUserWidget 默认传播。
FReply UCatLakeReachWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (bRenderedMenuOpen && !RenderedMenuToggleKeyName.IsNone()
		&& InKeyEvent.GetKey().GetFName() == RenderedMenuToggleKeyName)
	{
		RequestCloseMenu();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

// 渲染流程：
// 1. 缓存 native DTO，保留 C++ 测试和 native-only FishCollection 副本。
// 2. 转换为蓝图安全 DTO，其中 Profile 图鉴记录只复制成 UI 专用条目，不暴露 SaveGame 结构。
// 3. 更新给 WBP Designer 属性绑定读取的中文展示文本、鱼护/商店按钮 gate 与 Visibility 投影；C++ 不写任何 TextBlock 或菜单容器。
// 4. 更新键盘关闭、离局和商店提交 gate 缓存，再触发可选 BP_RenderViewState，让蓝图图表补动画或复杂表现。
void UCatLakeReachWidget::Render(const FCatUIReachViewState& ViewState)
{
	LastNativeViewState = ViewState;
	LastBlueprintViewState = MakeBlueprintViewState(ViewState);
	BlueprintPoisonValue = LastBlueprintViewState.Poison;
	BlueprintPoisonText = FText::FromString(FString::Printf(TEXT("中毒值：%.0f"), LastBlueprintViewState.Poison));
	BlueprintFishingStrengthValue = LastBlueprintViewState.FishingStrength;
	BlueprintFishingStrengthText = FText::FromString(FString::Printf(TEXT("钓鱼力量：%.0f"), LastBlueprintViewState.FishingStrength));
	BlueprintFightStaminaValue = LastBlueprintViewState.FightStamina;
	BlueprintFightStaminaText = FText::FromString(FString::Printf(TEXT("搏斗体力：%.0f"), LastBlueprintViewState.FightStamina));
	BlueprintFishGuardCount = LastBlueprintViewState.PersonalFishGuard.Fish.Num();
	BlueprintFishGuardCountText = BlueprintFishGuardCount > 0
		? FText::FromString(FString::Printf(TEXT("个人鱼护：%d 条鱼"), BlueprintFishGuardCount))
		: FText::FromString(TEXT("个人鱼护：空（钓到鱼后会出现在这里）"));
	UpdateBlueprintFishGuardSlotProjection();
	BlueprintSelectedFishGuardIndex = LastBlueprintViewState.SelectedFishGuardIndex;
	BlueprintSelectedFishDefinitionId = LastBlueprintViewState.SelectedFishGuardFish.FishDefinitionId;
	BlueprintSelectedFishText = LastBlueprintViewState.bHasSelectedFishGuardFish
		? FText::FromString(FString::Printf(TEXT("选中鱼：%s，%.2f 千克（第 %d/%d 格）"),
			*LastBlueprintViewState.SelectedFishGuardFish.FishDefinitionId.ToString(),
			LastBlueprintViewState.SelectedFishGuardFish.WeightKilograms,
			LastBlueprintViewState.SelectedFishGuardIndex + 1,
			BlueprintFishGuardCount))
		: FText::FromString(TEXT("当前没有选中鱼：先钓到鱼，鱼会进入鱼护格子。"));
	BlueprintSelectedFishWeightKilograms = LastBlueprintViewState.SelectedFishGuardFish.WeightKilograms;
	bBlueprintHasSelectedFishGuardFish = LastBlueprintViewState.bHasSelectedFishGuardFish;
	bBlueprintCanSelectPreviousFishGuardEntry = LastBlueprintViewState.bCanSelectPreviousFishGuardEntry;
	bBlueprintCanSelectNextFishGuardEntry = LastBlueprintViewState.bCanSelectNextFishGuardEntry;
	bBlueprintFishGuardActionEnabled = LastBlueprintViewState.bCanSubmitSelectedFishGuardAction;
	BlueprintFishGuardActionVisibility = LastBlueprintViewState.bHasSelectedFishGuardFish
		? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	bBlueprintFishGuardActionPending = LastBlueprintViewState.bFishGuardActionPending;
	BlueprintFishGuardResultRevision = LastBlueprintViewState.LastFishGuardCommandResult.Revision;
	BlueprintFishGuardResultError = LastBlueprintViewState.LastFishGuardCommandResult.Error;
	if (LastBlueprintViewState.bFishGuardActionPending)
	{
		BlueprintFishGuardResultText = FText::FromString(FString::Printf(TEXT("最近鱼护操作：%s，等待服务器确认"),
			*GetFishGuardActionDisplayText(LastBlueprintViewState.PendingFishGuardAction)));
	}
	else if (LastBlueprintViewState.bHasFishGuardCommandResult)
	{
		BlueprintFishGuardResultText = FText::FromString(FString::Printf(TEXT("最近鱼护操作：%s，%s，版本 %lld"),
			*GetFishGuardActionDisplayText(LastBlueprintViewState.LastFishGuardAction),
			*GetDomainErrorDisplayText(LastBlueprintViewState.LastFishGuardCommandResult.Error),
			LastBlueprintViewState.LastFishGuardCommandResult.Revision));
	}
	else
	{
		BlueprintFishGuardResultText = FText::FromString(TEXT("最近鱼护操作：暂无"));
	}
	BlueprintFishCollectionCount = LastBlueprintViewState.FishCollectionEntries.Num();
	if (!LastBlueprintViewState.bFishCollectionAvailable)
	{
		BlueprintFishCollectionText = FText::FromString(TEXT("图鉴：正在等待本地记录"));
	}
	else
	{
		BlueprintFishCollectionText = FText::FromString(FString::Printf(TEXT("图鉴：%d 条已记录"), BlueprintFishCollectionCount));
	}
	BlueprintShopSummaryText = LastBlueprintViewState.bShopEconomyAvailable
		? FText::FromString(FString::Printf(TEXT("商店：团队公款 %d，钱包版本 %lld，流水 %d 笔"),
			LastBlueprintViewState.ShopEconomy.Balance,
			LastBlueprintViewState.ShopEconomy.WalletRevision,
			LastBlueprintViewState.ShopEconomy.Transactions.Num()))
		: FText::FromString(TEXT("商店：等待团队钱包数据"));
	bBlueprintShopActionEnabled = LastBlueprintViewState.bMenuOpen && LastBlueprintViewState.bShopEconomyAvailable;
	BlueprintMenuVisibility = LastBlueprintViewState.bMenuOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	BlueprintLeaveVisibility = LastBlueprintViewState.bCanRequestOnlineLeave ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	bBlueprintLeaveEnabled = LastBlueprintViewState.bMenuOpen && LastBlueprintViewState.bCanRequestOnlineLeave;
	bRenderedMenuOpen = ViewState.bMenuOpen;
	bRenderedCanRequestOnlineLeave = ViewState.bCanRequestOnlineLeave;
	bRenderedCanSelectPreviousFishGuardEntry = ViewState.bCanSelectPreviousFishGuardEntry;
	bRenderedCanSelectNextFishGuardEntry = ViewState.bCanSelectNextFishGuardEntry;
	bRenderedCanSubmitSelectedFishGuardAction = ViewState.bCanSubmitSelectedFishGuardAction;
	bRenderedCanSubmitShopAction = bBlueprintShopActionEnabled;
	RenderedFishGuardCount = BlueprintFishGuardCount;
	RenderedSelectedFishGuardIndex = ViewState.SelectedFishGuardIndex;
	RenderedMenuToggleKeyName = ViewState.MenuToggleKeyName;
	BP_RenderViewState(LastBlueprintViewState);
}

// 关闭请求流程：只广播关闭意图；PageController 负责判断当前菜单状态并恢复 Enhanced Input、焦点和鼠标。
void UCatLakeReachWidget::RequestCloseMenu()
{
	OnCloseRequested.Broadcast();
}

// 离局请求流程：只在菜单仍展开且最近完整投影允许离局时广播纯意图；实际 RequestLeave、Run teardown 与旅行都留给 Online 管线。
void UCatLakeReachWidget::RequestLeaveLake()
{
	if (bRenderedMenuOpen && bRenderedCanRequestOnlineLeave)
	{
		OnLeaveRequested.Broadcast();
	}
}

// 选择前移流程：只有菜单仍展开且最近投影允许前移时才广播偏移；Model 会按最新鱼护快照重新裁剪下标。
void UCatLakeReachWidget::RequestSelectPreviousFishGuardEntry()
{
	if (bRenderedMenuOpen && bRenderedCanSelectPreviousFishGuardEntry)
	{
		OnFishGuardSelectionRequested.Broadcast(-1);
	}
}

// 选择后移流程：只有菜单仍展开且最近投影允许后移时才广播偏移；Widget 不读取鱼数组或缓存目标鱼 ID。
void UCatLakeReachWidget::RequestSelectNextFishGuardEntry()
{
	if (bRenderedMenuOpen && bRenderedCanSelectNextFishGuardEntry)
	{
		OnFishGuardSelectionRequested.Broadcast(1);
	}
}

// 格子选择流程：先用最近 Render 的菜单状态和鱼护数量拒绝空格/旧点击；再把目标格转成相对偏移，让 Model 继续按权威快照裁剪。
void UCatLakeReachWidget::RequestSelectFishGuardSlot(const int32 SlotIndex)
{
	if (!bRenderedMenuOpen || SlotIndex < 0 || SlotIndex >= RenderedFishGuardCount || SlotIndex >= FishGuardVisibleSlotCount)
	{
		return;
	}
	const int32 CurrentIndex = RenderedSelectedFishGuardIndex == INDEX_NONE ? 0 : RenderedSelectedFishGuardIndex;
	const int32 Offset = SlotIndex - CurrentIndex;
	if (Offset != 0)
	{
		OnFishGuardSelectionRequested.Broadcast(Offset);
	}
}

// 吃鱼请求流程：复用统一鱼护动作过滤；服务器结果回来前 View 不移除本地鱼条目。
void UCatLakeReachWidget::RequestConsumeSelectedFish()
{
	RequestFishGuardAction(ECatUIReachFishGuardAction::ConsumeSelectedFish);
}

// 转缸请求流程：复用统一鱼护动作过滤；共享鱼缸和版本号由 PageController 重读正式来源。
void UCatLakeReachWidget::RequestTransferSelectedFishToTank()
{
	RequestFishGuardAction(ECatUIReachFishGuardAction::TransferSelectedFishToTank);
}

// 献祭请求流程：复用统一鱼护动作过滤；献祭协议载荷由 PageController 从当前 Model 选择构造。
void UCatLakeReachWidget::RequestSacrificeSelectedFish()
{
	RequestFishGuardAction(ECatUIReachFishGuardAction::SacrificeSelectedFish);
}

// 二级鱼竿购买请求流程：复用统一商店动作过滤；按钮只表达玩家意图，不提供价格或库存。
void UCatLakeReachWidget::RequestPurchaseShopRodT2()
{
	RequestShopAction(ECatUIReachShopAction::PurchaseShopRodT2);
}

// 虫虫窝料购买请求流程：复用统一商店动作过滤；服务器结果通过公开经济快照和装备/耗材链路体现。
void UCatLakeReachWidget::RequestPurchaseBugChum()
{
	RequestShopAction(ECatUIReachShopAction::PurchaseBugChum);
}

// 免费虫虫鱼饵请求流程：复用统一商店动作过滤；免费白名单不在 View 中判断。
void UCatLakeReachWidget::RequestClaimFreeBugBait()
{
	RequestShopAction(ECatUIReachShopAction::ClaimFreeBugBait);
}

// 免费保底鱼竿请求流程：复用统一商店动作过滤；View 不直接写装备状态。
void UCatLakeReachWidget::RequestClaimFreeStarterRod()
{
	RequestShopAction(ECatUIReachShopAction::ClaimFreeStarterRod);
}

// 蓝图状态读取流程：返回最近一次 Render 生成的蓝图安全副本；延迟动画读取它不会触碰 Model 或玩法对象。
FCatUIReachBlueprintViewState UCatLakeReachWidget::GetLastBlueprintViewState() const
{
	return LastBlueprintViewState;
}

// native 状态读取流程：返回最近一次 Render 输入；测试用它确认 native-only 图鉴记录仍被 Model 保留。
const FCatUIReachViewState& UCatLakeReachWidget::GetLastNativeViewState() const
{
	return LastNativeViewState;
}

// 点击流程：把 WBP 可选按钮点击收口到同一个蓝图可调用入口；View 不直接访问 PlayerController。
void UCatLakeReachWidget::HandleCloseClicked()
{
	RequestCloseMenu();
}

// 离局点击流程：把 WBP 可选按钮点击收口到同一个蓝图可调用入口；Online gate 由最近 ViewState 缓存过滤。
void UCatLakeReachWidget::HandleLeaveClicked()
{
	RequestLeaveLake();
}

// 上一条点击流程：把 WBP 点击收口到蓝图可调用入口；按钮重建或蓝图直接调用都经过同一 gate。
void UCatLakeReachWidget::HandlePreviousFishGuardClicked()
{
	RequestSelectPreviousFishGuardEntry();
}

// 下一条点击流程：把 WBP 点击收口到蓝图可调用入口；按钮重建或蓝图直接调用都经过同一 gate。
void UCatLakeReachWidget::HandleNextFishGuardClicked()
{
	RequestSelectNextFishGuardEntry();
}

// 第 1 格点击流程：把具体按钮收口到统一格子选择入口；目标下标只用于计算偏移，不构造命令载荷。
void UCatLakeReachWidget::HandleFishGuardSlot0Clicked()
{
	RequestSelectFishGuardSlot(0);
}

// 第 2 格点击流程：把具体按钮收口到统一格子选择入口；目标下标只用于计算偏移，不构造命令载荷。
void UCatLakeReachWidget::HandleFishGuardSlot1Clicked()
{
	RequestSelectFishGuardSlot(1);
}

// 第 3 格点击流程：把具体按钮收口到统一格子选择入口；目标下标只用于计算偏移，不构造命令载荷。
void UCatLakeReachWidget::HandleFishGuardSlot2Clicked()
{
	RequestSelectFishGuardSlot(2);
}

// 第 4 格点击流程：把具体按钮收口到统一格子选择入口；目标下标只用于计算偏移，不构造命令载荷。
void UCatLakeReachWidget::HandleFishGuardSlot3Clicked()
{
	RequestSelectFishGuardSlot(3);
}

// 第 5 格点击流程：把具体按钮收口到统一格子选择入口；目标下标只用于计算偏移，不构造命令载荷。
void UCatLakeReachWidget::HandleFishGuardSlot4Clicked()
{
	RequestSelectFishGuardSlot(4);
}

// 第 6 格点击流程：把具体按钮收口到统一格子选择入口；目标下标只用于计算偏移，不构造命令载荷。
void UCatLakeReachWidget::HandleFishGuardSlot5Clicked()
{
	RequestSelectFishGuardSlot(5);
}

// 第 7 格点击流程：把具体按钮收口到统一格子选择入口；目标下标只用于计算偏移，不构造命令载荷。
void UCatLakeReachWidget::HandleFishGuardSlot6Clicked()
{
	RequestSelectFishGuardSlot(6);
}

// 第 8 格点击流程：把具体按钮收口到统一格子选择入口；目标下标只用于计算偏移，不构造命令载荷。
void UCatLakeReachWidget::HandleFishGuardSlot7Clicked()
{
	RequestSelectFishGuardSlot(7);
}

// 吃鱼点击流程：只表达玩家想吃当前选中鱼；不在 View 中修改鱼护数量或身体数值。
void UCatLakeReachWidget::HandleConsumeFishClicked()
{
	RequestConsumeSelectedFish();
}

// 转缸点击流程：只表达玩家想把当前选中鱼转入共享鱼缸；不在 View 中访问 Camp Actor。
void UCatLakeReachWidget::HandleTransferFishToTankClicked()
{
	RequestTransferSelectedFishToTank();
}

// 献祭点击流程：只表达玩家想献祭当前选中鱼；不在 View 中访问 SacrificeCoordinator。
void UCatLakeReachWidget::HandleSacrificeFishClicked()
{
	RequestSacrificeSelectedFish();
}

// 二级鱼竿点击流程：把 WBP 按钮点击收口到蓝图可调用入口，避免按钮和蓝图图表形成两套逻辑。
void UCatLakeReachWidget::HandlePurchaseShopRodT2Clicked()
{
	RequestPurchaseShopRodT2();
}

// 虫虫窝料点击流程：把 WBP 按钮点击收口到蓝图可调用入口，订单提交仍由 PageController 负责。
void UCatLakeReachWidget::HandlePurchaseBugChumClicked()
{
	RequestPurchaseBugChum();
}

// 免费虫虫鱼饵点击流程：把 WBP 按钮点击收口到蓝图可调用入口，免费资格仍由后端目录裁决。
void UCatLakeReachWidget::HandleClaimFreeBugBaitClicked()
{
	RequestClaimFreeBugBait();
}

// 免费保底鱼竿点击流程：把 WBP 按钮点击收口到蓝图可调用入口，装备交付仍等待服务器确认。
void UCatLakeReachWidget::HandleClaimFreeStarterRodClicked()
{
	RequestClaimFreeStarterRod();
}

// 鱼护动作过滤流程：先拒绝空动作、关闭菜单、无选择或 pending 期间的迟到点击；通过后只广播动作枚举，让 PageController 从 Model 重建可信命令。
void UCatLakeReachWidget::RequestFishGuardAction(const ECatUIReachFishGuardAction Action)
{
	if (Action != ECatUIReachFishGuardAction::None && bRenderedMenuOpen && bRenderedCanSubmitSelectedFishGuardAction)
	{
		OnFishGuardActionRequested.Broadcast(Action);
	}
}

// 商店动作过滤流程：先拒绝空动作、关闭菜单或缺商店快照的迟到点击；通过后只广播动作枚举，让 PageController 从 Model 重建钱包 Revision。
void UCatLakeReachWidget::RequestShopAction(const ECatUIReachShopAction Action)
{
	if (Action != ECatUIReachShopAction::None && bRenderedMenuOpen && bRenderedCanSubmitShopAction)
	{
		OnShopActionRequested.Broadcast(Action);
	}
}

// 鱼护格子投影流程：
// 1. 逐格读取最近蓝图安全 ViewState 中的鱼护数组，空格输出稳定占位文本。
// 2. 每个格子的可点击状态只看“菜单打开 + 该格存在实物鱼”，防止玩家点击空格发出无意义意图。
// 3. 这些字段只给 WBP Designer 绑定读取，不创建控件，也不缓存任何可写鱼实例引用。
void UCatLakeReachWidget::UpdateBlueprintFishGuardSlotProjection()
{
	BlueprintFishGuardSlot0Text = MakeBlueprintFishGuardSlotText(0);
	BlueprintFishGuardSlot1Text = MakeBlueprintFishGuardSlotText(1);
	BlueprintFishGuardSlot2Text = MakeBlueprintFishGuardSlotText(2);
	BlueprintFishGuardSlot3Text = MakeBlueprintFishGuardSlotText(3);
	BlueprintFishGuardSlot4Text = MakeBlueprintFishGuardSlotText(4);
	BlueprintFishGuardSlot5Text = MakeBlueprintFishGuardSlotText(5);
	BlueprintFishGuardSlot6Text = MakeBlueprintFishGuardSlotText(6);
	BlueprintFishGuardSlot7Text = MakeBlueprintFishGuardSlotText(7);

	const bool bMenuOpen = LastBlueprintViewState.bMenuOpen;
	const int32 FishCount = LastBlueprintViewState.PersonalFishGuard.Fish.Num();
	bBlueprintFishGuardSlot0Enabled = bMenuOpen && FishCount > 0;
	bBlueprintFishGuardSlot1Enabled = bMenuOpen && FishCount > 1;
	bBlueprintFishGuardSlot2Enabled = bMenuOpen && FishCount > 2;
	bBlueprintFishGuardSlot3Enabled = bMenuOpen && FishCount > 3;
	bBlueprintFishGuardSlot4Enabled = bMenuOpen && FishCount > 4;
	bBlueprintFishGuardSlot5Enabled = bMenuOpen && FishCount > 5;
	bBlueprintFishGuardSlot6Enabled = bMenuOpen && FishCount > 6;
	bBlueprintFishGuardSlot7Enabled = bMenuOpen && FishCount > 7;
}

// 单格文本流程：先确认格子范围和实物鱼是否存在；有鱼时展示定义 ID 与重量，选中格额外标记“选中”。
FText UCatLakeReachWidget::MakeBlueprintFishGuardSlotText(const int32 SlotIndex) const
{
	const int32 DisplayIndex = SlotIndex + 1;
	if (SlotIndex < 0 || SlotIndex >= FishGuardVisibleSlotCount
		|| !LastBlueprintViewState.PersonalFishGuard.Fish.IsValidIndex(SlotIndex))
	{
		return FText::FromString(FString::Printf(TEXT("第 %d 格\n空"), DisplayIndex));
	}

	const FCatFishInstance& Fish = LastBlueprintViewState.PersonalFishGuard.Fish[SlotIndex];
	const FString SelectionText = SlotIndex == LastBlueprintViewState.SelectedFishGuardIndex
		? TEXT("选中") : TEXT("有鱼");
	return FText::FromString(FString::Printf(TEXT("第 %d 格\n%s\n%s %.2f kg"),
		DisplayIndex,
		*SelectionText,
		*Fish.FishDefinitionId.ToString(),
		Fish.WeightKilograms));
}

// DTO 转换流程：复制所有蓝图支持字段；Profile 的 native-only FishCollection 逐条转为 UI 专用条目，避免为展示修改 durable 合同。
FCatUIReachBlueprintViewState UCatLakeReachWidget::MakeBlueprintViewState(const FCatUIReachViewState& ViewState)
{
	FCatUIReachBlueprintViewState BlueprintState;
	BlueprintState.Poison = ViewState.Poison;
	BlueprintState.FishingStrength = ViewState.FishingStrength;
	BlueprintState.FightStamina = ViewState.FightStamina;
	BlueprintState.Condition = ViewState.Condition;
	BlueprintState.Growth = ViewState.Growth;
	BlueprintState.Equipment = ViewState.Equipment;
	BlueprintState.Run = ViewState.Run;
	BlueprintState.HelpSignal = ViewState.HelpSignal;
	BlueprintState.ShopEconomy = ViewState.ShopEconomy;
	BlueprintState.bShopEconomyAvailable = ViewState.bShopEconomyAvailable;
	BlueprintState.Fishing = ViewState.Fishing;
	BlueprintState.bHasFishingSession = ViewState.bHasFishingSession;
	BlueprintState.LastFishingCommandResult = ViewState.LastFishingCommandResult;
	BlueprintState.bHasFishingCommandResult = ViewState.bHasFishingCommandResult;
	BlueprintState.PersonalFishGuard = ViewState.PersonalFishGuard;
	BlueprintState.SelectedFishGuardIndex = ViewState.SelectedFishGuardIndex;
	BlueprintState.SelectedFishGuardFish = ViewState.SelectedFishGuardFish;
	BlueprintState.bHasSelectedFishGuardFish = ViewState.bHasSelectedFishGuardFish;
	BlueprintState.bCanSelectPreviousFishGuardEntry = ViewState.bCanSelectPreviousFishGuardEntry;
	BlueprintState.bCanSelectNextFishGuardEntry = ViewState.bCanSelectNextFishGuardEntry;
	BlueprintState.bCanSubmitSelectedFishGuardAction = ViewState.bCanSubmitSelectedFishGuardAction;
	BlueprintState.bFishGuardActionPending = ViewState.bFishGuardActionPending;
	BlueprintState.PendingFishGuardAction = ViewState.PendingFishGuardAction;
	BlueprintState.PendingFishGuardRequestId = ViewState.PendingFishGuardRequestId;
	BlueprintState.LastFishGuardAction = ViewState.LastFishGuardAction;
	BlueprintState.LastFishGuardCommandResult = ViewState.LastFishGuardCommandResult;
	BlueprintState.bHasFishGuardCommandResult = ViewState.bHasFishGuardCommandResult;
	BlueprintState.LastFishGuardSacrificeResult = ViewState.LastFishGuardSacrificeResult;
	BlueprintState.LastFishGuardConsumeResult = ViewState.LastFishGuardConsumeResult;
	BlueprintState.FishCollectionEntries = ViewState.FishCollectionEntries;
	if (BlueprintState.FishCollectionEntries.IsEmpty() && !ViewState.FishCollection.IsEmpty())
	{
		BlueprintState.FishCollectionEntries.Reserve(ViewState.FishCollection.Num());
		for (const FCatFishCollectionRecord& Record : ViewState.FishCollection)
		{
			FCatUIReachFishCollectionEntry Entry;
			Entry.FishDefinitionId = Record.FishDefinitionId;
			Entry.State = Record.State;
			Entry.BestWeightKilograms = Record.BestWeightKilograms;
			Entry.EncounterCount = Record.EncounterCount;
			BlueprintState.FishCollectionEntries.Add(Entry);
		}
	}
	BlueprintState.bFishCollectionAvailable = ViewState.bFishCollectionAvailable;
	BlueprintState.bMenuOpen = ViewState.bMenuOpen;
	BlueprintState.MenuToggleKeyName = ViewState.MenuToggleKeyName;
	BlueprintState.bCanRequestOnlineLeave = ViewState.bCanRequestOnlineLeave;
	return BlueprintState;
}
