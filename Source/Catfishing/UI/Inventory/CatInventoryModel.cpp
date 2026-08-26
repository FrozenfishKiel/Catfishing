#include "UI/Inventory/CatInventoryModel.h"

#include "Character/CatCharacter.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/PlayerController.h"
#include "Items/CatContainerReplicationComponent.h"
#include "UI/CatUISettings.h"

namespace
{
	// 背包动作标签流程：只把动作枚举转成人类可读反馈，不参与服务器命令选择。
	FString GetInventoryActionDisplayText(const ECatInventoryAction Action)
	{
		switch (Action)
		{
		case ECatInventoryAction::ConsumeSelectedFish:
			return TEXT("吃鱼");
		case ECatInventoryAction::TransferSelectedFishToTank:
			return TEXT("转入鱼缸");
		case ECatInventoryAction::SacrificeSelectedFish:
			return TEXT("献祭");
		case ECatInventoryAction::None:
		default:
			return TEXT("无");
		}
	}

	// 领域错误标签流程：常见错误转中文，其他枚举保留原名方便按日志定位。
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

// 绑定流程：
// 1. 先解绑旧来源，避免换 Pawn 后同一个 Model 同时订阅两只鱼护。
// 2. 校验 LocalPlayer、Controller、Character 和个人鱼护复制组件完整。
// 3. 订阅鱼护快照变化和 PlayerController 的吃鱼/转缸/献祭结果。
// 4. 发布首份 ViewState，让背包第一次打开时已有格子数据。
bool UCatInventoryModel::Bind(ULocalPlayer* InLocalPlayer, APlayerController* InController,
	ACatCharacter* InCharacter)
{
	Unbind();
	if (!InLocalPlayer || !InController || !InCharacter || InController->GetPawn() != InCharacter)
	{
		return false;
	}
	UCatContainerReplicationComponent* FishGuard = InCharacter->FindComponentByClass<UCatContainerReplicationComponent>();
	if (!FishGuard)
	{
		return false;
	}

	BoundLocalPlayer = InLocalPlayer;
	BoundPlayerController = InController;
	BoundPersonalFishGuard = FishGuard;
	FishGuardChangedHandle = FishGuard->OnSnapshotChanged.AddUObject(this, &ThisClass::HandleFishGuardSnapshotChanged);
	if (ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(InController))
	{
		CampCommandResultHandle = CatController->OnCampCommandResultReceived.AddUObject(
			this, &ThisClass::HandleCampCommandResult);
		SacrificeResultHandle = CatController->OnSacrificeResultReceived.AddUObject(
			this, &ThisClass::HandleSacrificeResult);
		FishConsumeResultHandle = CatController->OnFishConsumeResultReceived.AddUObject(
			this, &ThisClass::HandleFishConsumeResult);
	}
	Refresh();
	return true;
}

// 解绑流程：按保存的句柄解除鱼护和 Controller 结果订阅，再清空选择、pending、结果和 ViewState，避免旧角色事实跨 Pawn 泄漏。
void UCatInventoryModel::Unbind()
{
	if (UCatContainerReplicationComponent* FishGuard = BoundPersonalFishGuard.Get())
	{
		FishGuard->OnSnapshotChanged.Remove(FishGuardChangedHandle);
	}
	if (ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(BoundPlayerController.Get()))
	{
		CatController->OnCampCommandResultReceived.Remove(CampCommandResultHandle);
		CatController->OnSacrificeResultReceived.Remove(SacrificeResultHandle);
		CatController->OnFishConsumeResultReceived.Remove(FishConsumeResultHandle);
	}
	FishGuardChangedHandle.Reset();
	CampCommandResultHandle.Reset();
	SacrificeResultHandle.Reset();
	FishConsumeResultHandle.Reset();
	BoundLocalPlayer.Reset();
	BoundPlayerController.Reset();
	BoundPersonalFishGuard.Reset();
	bOpen = false;
	SelectedSlotIndex = INDEX_NONE;
	PendingAction = ECatInventoryAction::None;
	PendingRequestId.Invalidate();
	bActionPending = false;
	LastAction = ECatInventoryAction::None;
	LastCommandResult = FCatDomainCommandResult();
	bHasCommandResult = false;
	LastConsumeResult = FCatFishConsumeResult();
	LastSacrificeResult = FCatSacrificeResult();
	ViewState = FCatInventoryViewState();
}

// 绑定状态查询流程：同时要求本地玩家、Controller 和鱼护复制出口有效，单个弱引用存活不代表背包可刷新。
bool UCatInventoryModel::IsBound() const
{
	return BoundLocalPlayer.IsValid()
		&& BoundPlayerController.IsValid()
		&& BoundPersonalFishGuard.IsValid();
}

// 打开状态流程：PageController 是唯一写者；状态变化后重建 ViewState，让 WBP 可见性和键盘关闭条件同步。
void UCatInventoryModel::SetOpen(const bool bInOpen)
{
	if (bOpen == bInOpen)
	{
		return;
	}
	bOpen = bInOpen;
	Refresh();
}

// 格子选择流程：只接受当前后端容量范围内的下标；空格允许被选择用于上下文表现，但不会产生可提交动作。
bool UCatInventoryModel::SelectSlot(const int32 SlotIndex)
{
	if (SlotIndex < 0 || SlotIndex >= ViewState.SlotCount || SelectedSlotIndex == SlotIndex)
	{
		return false;
	}
	SelectedSlotIndex = SlotIndex;
	Refresh();
	return true;
}

// 提交标记流程：记录动作和 RequestId，清空旧结果并刷新 pending 展示；终态只能由服务器结果或本地拒绝关闭。
void UCatInventoryModel::MarkActionSubmitted(const ECatInventoryAction Action, const FGuid RequestId)
{
	if (Action == ECatInventoryAction::None || !RequestId.IsValid())
	{
		return;
	}
	PendingAction = Action;
	PendingRequestId = RequestId;
	bActionPending = true;
	LastAction = ECatInventoryAction::None;
	LastCommandResult = FCatDomainCommandResult();
	bHasCommandResult = false;
	LastConsumeResult = FCatFishConsumeResult();
	LastSacrificeResult = FCatSacrificeResult();
	Refresh();
}

// 本地拒绝流程：PageController 无法构造正式服务器命令时关闭 pending 并发布结构化错误；它不会改鱼护数组。
void UCatInventoryModel::MarkActionRejected(const ECatInventoryAction Action, const FGuid RequestId,
	const ECatDomainCommandError Error, const int64 Revision)
{
	if (Action == ECatInventoryAction::None || !RequestId.IsValid())
	{
		return;
	}
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	Result.Error = Error;
	Result.Revision = Revision;
	PendingAction = ECatInventoryAction::None;
	PendingRequestId.Invalidate();
	bActionPending = false;
	LastAction = Action;
	LastCommandResult = Result;
	bHasCommandResult = true;
	Refresh();
}

// 刷新流程：
// 1. 从个人鱼护复制组件读取容量、Revision 和鱼数组。
// 2. 按容量生成 Slots，容量缺失时回退到当前鱼数量，保证格子数量仍来自后端快照。
// 3. 裁剪当前选择并派生可提交动作 gate、摘要文本、选中鱼文本和结果文本。
// 4. 从 UI Settings 解析既有 InputContext 的背包开关键名，最后广播完整投影。
void UCatInventoryModel::Refresh()
{
	FCatInventoryViewState NewState;
	if (const UCatContainerReplicationComponent* FishGuard = BoundPersonalFishGuard.Get())
	{
		NewState.PersonalFishGuard = FishGuard->GetSnapshot();
	}
	NewState.SlotCount = NewState.PersonalFishGuard.Capacity > 0
		? NewState.PersonalFishGuard.Capacity
		: NewState.PersonalFishGuard.Fish.Num();
	if (NewState.SlotCount <= 0 && NewState.PersonalFishGuard.ContainerId.IsValid())
	{
		NewState.SlotCount = 1;
	}
	if (SelectedSlotIndex >= NewState.SlotCount)
	{
		SelectedSlotIndex = NewState.SlotCount > 0 ? NewState.SlotCount - 1 : INDEX_NONE;
	}
	NewState.SelectedSlotIndex = SelectedSlotIndex;
	NewState.Slots.Reserve(NewState.SlotCount);
	for (int32 SlotIndex = 0; SlotIndex < NewState.SlotCount; ++SlotIndex)
	{
		NewState.Slots.Add(MakeSlotView(NewState.PersonalFishGuard, SlotIndex));
	}
	NewState.bHasSelectedFish = SelectedSlotIndex != INDEX_NONE
		&& NewState.PersonalFishGuard.Fish.IsValidIndex(SelectedSlotIndex);
	if (NewState.bHasSelectedFish)
	{
		NewState.SelectedFish = NewState.PersonalFishGuard.Fish[SelectedSlotIndex];
	}
	NewState.bOpen = bOpen;
	NewState.bActionPending = bActionPending;
	NewState.PendingAction = PendingAction;
	NewState.PendingRequestId = PendingRequestId;
	NewState.LastAction = LastAction;
	NewState.LastCommandResult = LastCommandResult;
	NewState.bHasCommandResult = bHasCommandResult;
	NewState.LastConsumeResult = LastConsumeResult;
	NewState.LastSacrificeResult = LastSacrificeResult;
	NewState.bCanSubmitAction = bOpen && NewState.bHasSelectedFish && !bActionPending;
	if (const UCatUISettings* Settings = GetDefault<UCatUISettings>())
	{
		NewState.ToggleKeyName = Settings->ResolveInventoryToggleKeyName();
	}
	NewState.SummaryText = FText::FromString(FString::Printf(TEXT("个人鱼护：%d/%d 条鱼，版本 %lld"),
		NewState.PersonalFishGuard.Fish.Num(),
		NewState.SlotCount,
		NewState.PersonalFishGuard.Revision));
	NewState.SelectedFishText = NewState.bHasSelectedFish
		? FText::FromString(FString::Printf(TEXT("选中：第 %d 格，%s，%.2f 千克"),
			SelectedSlotIndex + 1,
			*NewState.SelectedFish.FishDefinitionId.ToString(),
			NewState.SelectedFish.WeightKilograms))
		: FText::FromString(TEXT("当前没有选中鱼：点击鱼护格子选择。"));
	if (bActionPending)
	{
		NewState.ResultText = FText::FromString(FString::Printf(TEXT("背包操作：%s，等待服务器确认"),
			*GetInventoryActionDisplayText(PendingAction)));
	}
	else if (bHasCommandResult)
	{
		NewState.ResultText = FText::FromString(FString::Printf(TEXT("背包操作：%s，%s，版本 %lld"),
			*GetInventoryActionDisplayText(LastAction),
			*GetDomainErrorDisplayText(LastCommandResult.Error),
			LastCommandResult.Revision));
	}
	else
	{
		NewState.ResultText = FText::FromString(TEXT("背包操作：暂无"));
	}
	ViewState = MoveTemp(NewState);
	OnViewStateChanged.Broadcast();
}

// ViewState 读取流程：返回最近发布的只读投影；调用方不能通过它拿到任何后端对象引用。
const FCatInventoryViewState& UCatInventoryModel::GetViewState() const
{
	return ViewState;
}

// 鱼护变化流程：复制组件只发“完整快照变了”通知；Model 统一重读，不在事件里拼增量。
void UCatInventoryModel::HandleFishGuardSnapshotChanged()
{
	Refresh();
}

// 转缸结果流程：只匹配当前 pending 的转缸 RequestId；匹配后关闭 pending、缓存结果并重读鱼护。
void UCatInventoryModel::HandleCampCommandResult(const FCatDomainCommandResult& Result)
{
	if (!IsPendingResult(ECatInventoryAction::TransferSelectedFishToTank, Result.RequestId))
	{
		return;
	}
	PendingAction = ECatInventoryAction::None;
	PendingRequestId.Invalidate();
	bActionPending = false;
	LastAction = ECatInventoryAction::TransferSelectedFishToTank;
	LastCommandResult = Result;
	bHasCommandResult = true;
	Refresh();
}

// 献祭结果流程：只匹配当前 pending 的献祭 RequestId；详细协议结果和公共结果头分别保存供 View 展示。
void UCatInventoryModel::HandleSacrificeResult(const FCatSacrificeResult& Result)
{
	if (!IsPendingResult(ECatInventoryAction::SacrificeSelectedFish, Result.RequestId))
	{
		return;
	}
	FCatDomainCommandResult PublicResult;
	PublicResult.RequestId = Result.RequestId;
	PublicResult.bCommitted = Result.bCompleted;
	PublicResult.Error = Result.Error;
	PublicResult.Revision = Result.ItemsRevision;
	PendingAction = ECatInventoryAction::None;
	PendingRequestId.Invalidate();
	bActionPending = false;
	LastAction = ECatInventoryAction::SacrificeSelectedFish;
	LastSacrificeResult = Result;
	LastCommandResult = PublicResult;
	bHasCommandResult = true;
	Refresh();
}

// 吃鱼结果流程：只匹配当前 pending 的吃鱼 RequestId；匹配后缓存 Items 终态并刷新鱼护、身体相关表现。
void UCatInventoryModel::HandleFishConsumeResult(const FCatFishConsumeResult& Result)
{
	if (!IsPendingResult(ECatInventoryAction::ConsumeSelectedFish, Result.Command.RequestId))
	{
		return;
	}
	PendingAction = ECatInventoryAction::None;
	PendingRequestId.Invalidate();
	bActionPending = false;
	LastAction = ECatInventoryAction::ConsumeSelectedFish;
	LastConsumeResult = Result;
	LastCommandResult = Result.Command;
	bHasCommandResult = true;
	Refresh();
}

// pending 匹配流程：动作、pending 标记和 RequestId 同时一致才消费结果；其他 UI 或旧请求的回包直接忽略。
bool UCatInventoryModel::IsPendingResult(const ECatInventoryAction Action, const FGuid RequestId) const
{
	return bActionPending
		&& PendingAction == Action
		&& RequestId.IsValid()
		&& PendingRequestId == RequestId;
}

// 格子投影流程：从后端 Snapshot 的容量和鱼数组生成稳定文本；空格不含鱼实例，选中态只来自 Model 当前下标。
FCatInventorySlotView UCatInventoryModel::MakeSlotView(const FCatContainerSnapshot& Snapshot,
	const int32 SlotIndex) const
{
	FCatInventorySlotView Slot;
	Slot.SlotIndex = SlotIndex;
	Slot.bSelected = SlotIndex == SelectedSlotIndex;
	Slot.bOccupied = Snapshot.Fish.IsValidIndex(SlotIndex);
	if (Slot.bOccupied)
	{
		Slot.Fish = Snapshot.Fish[SlotIndex];
		Slot.DisplayText = FText::FromString(FString::Printf(TEXT("第 %d 格\n%s\n%.2f kg"),
			SlotIndex + 1,
			*Slot.Fish.FishDefinitionId.ToString(),
			Slot.Fish.WeightKilograms));
	}
	else
	{
		Slot.DisplayText = FText::FromString(FString::Printf(TEXT("第 %d 格\n空"), SlotIndex + 1));
	}
	return Slot;
}
