#include "UI/Inventory/CatInventoryModel.h"

#include "Character/CatCharacter.h"
#include "Engine/World.h"
#include "Equipment/CatEquipmentComponent.h"
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
		case ECatInventoryAction::MoveObjectBetweenContainers:
			return TEXT("移动物体");
		case ECatInventoryAction::SacrificeSelectedFish:
			return TEXT("献祭");
		case ECatInventoryAction::None:
		default:
			return TEXT("无");
		}
	}

	// 领域错误标签流程：常见错误转中文，其他枚举保留原名方便按日志定位。
	FString GetInventoryDomainErrorDisplayText(const ECatDomainCommandError Error)
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
		case ECatDomainCommandError::InvalidPhase:
			return TEXT("当前动作还不能执行");
		case ECatDomainCommandError::CapacityExceeded:
			return TEXT("目标已满");
		case ECatDomainCommandError::PolicyUndecided:
			return TEXT("规则暂未开放");
		case ECatDomainCommandError::AlreadyResolved:
			return TEXT("请求已处理");
		default:
			return UEnum::GetValueAsString(Error);
		}
	}

	// 容器物体类别标签流程：只服务背包文本和调试反馈，不参与服务器策略选择。
	FString GetContainedObjectKindDisplayText(const ECatContainedObjectKind Kind)
	{
		switch (Kind)
		{
		case ECatContainedObjectKind::Fish:
			return TEXT("鱼");
		case ECatContainedObjectKind::Equipment:
			return TEXT("装备");
		case ECatContainedObjectKind::Consumable:
			return TEXT("耗材");
		case ECatContainedObjectKind::Unknown:
		default:
			return TEXT("物体");
		}
	}

	// 装备定义展示流程：未同步或未装配时给玩家可读占位，避免把 None 暴露成内部 ID。
	FString GetDefinitionDisplayText(const FName DefinitionId, const TCHAR* EmptyText)
	{
		return DefinitionId.IsNone() ? FString(EmptyText) : DefinitionId.ToString();
	}

	// 当前装备文案流程：只读 Equipment 快照中的三件套和竿耐久；没有同步时明确说明等待装备数据。
	FText MakeEquipmentText(const FCatEquipmentLoadoutSnapshot& Equipment, const bool bEquipmentAvailable)
	{
		if (!bEquipmentAvailable)
		{
			return FText::FromString(TEXT("当前装备：等待同步"));
		}
		const FString RodState = Equipment.bRodBroken ? TEXT("，已断") : TEXT("");
		return FText::FromString(FString::Printf(TEXT("当前装备：鱼竿 %s（耐久 %.0f%s） | 鱼饵 %s | 鱼漂 %s"),
			*GetDefinitionDisplayText(Equipment.RodDefinitionId, TEXT("未装备")),
			Equipment.RodDurability,
			*RodState,
			*GetDefinitionDisplayText(Equipment.BaitDefinitionId, TEXT("未装备")),
			*GetDefinitionDisplayText(Equipment.FloatDefinitionId, TEXT("未装备"))));
	}

	// 耗材文案流程：只列出数量大于 0 的 RunConsumable 栈；空栈说明玩家当前没有可花的鱼饵或窝料。
	FText MakeConsumablesText(const FCatEquipmentLoadoutSnapshot& Equipment, const bool bEquipmentAvailable)
	{
		if (!bEquipmentAvailable)
		{
			return FText::FromString(TEXT("随身耗材：等待同步"));
		}
		TArray<FString> Parts;
		for (const FCatRunConsumableStack& Stack : Equipment.Consumables)
		{
			if (!Stack.DefinitionId.IsNone() && Stack.Quantity > 0)
			{
				Parts.Add(FString::Printf(TEXT("%s x%d"), *Stack.DefinitionId.ToString(), Stack.Quantity));
			}
		}
		return Parts.IsEmpty()
			? FText::FromString(TEXT("随身耗材：暂无鱼饵或窝料"))
			: FText::FromString(FString::Printf(TEXT("随身耗材：%s"), *FString::Join(Parts, TEXT("，"))));
	}

	// 有效耗材计数流程：与耗材文案使用同一口径，只统计有定义且数量大于 0 的局内耗材栈。
	int32 CountActiveConsumableStacks(const FCatEquipmentLoadoutSnapshot& Equipment)
	{
		int32 Count = 0;
		for (const FCatRunConsumableStack& Stack : Equipment.Consumables)
		{
			if (!Stack.DefinitionId.IsNone() && Stack.Quantity > 0)
			{
				++Count;
			}
		}
		return Count;
	}
}

// 绑定流程：
// 1. 先解绑旧来源，避免换 Pawn 后同一个 Model 同时订阅两只鱼护。
// 2. 校验 LocalPlayer、Controller、Character 和个人鱼护复制组件完整；Equipment 与外部容器上下文作为可选只读源接入。
// 3. 订阅鱼护、Equipment 快照变化和 PlayerController 的吃鱼、容器移动、献祭结果。
// 4. 发布首份 ViewState，让背包第一次打开时已有鱼、当前鱼竿和耗材数据。
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
	if (UCatEquipmentComponent* Equipment = InCharacter->GetEquipmentComponent())
	{
		BoundEquipment = Equipment;
		EquipmentChangedHandle = Equipment->OnSnapshotChanged.AddUObject(this, &ThisClass::HandleEquipmentSnapshotChanged);
	}
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
	ClearExternalContainerBindings();
	if (UCatEquipmentComponent* Equipment = BoundEquipment.Get())
	{
		Equipment->OnSnapshotChanged.Remove(EquipmentChangedHandle);
	}
	if (ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(BoundPlayerController.Get()))
	{
		CatController->OnCampCommandResultReceived.Remove(CampCommandResultHandle);
		CatController->OnSacrificeResultReceived.Remove(SacrificeResultHandle);
		CatController->OnFishConsumeResultReceived.Remove(FishConsumeResultHandle);
	}
	FishGuardChangedHandle.Reset();
	EquipmentChangedHandle.Reset();
	CampCommandResultHandle.Reset();
	SacrificeResultHandle.Reset();
	FishConsumeResultHandle.Reset();
	BoundLocalPlayer.Reset();
	BoundPlayerController.Reset();
	BoundPersonalFishGuard.Reset();
	BoundEquipment.Reset();
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

// 外部容器上下文流程：
// 1. 先解除旧外部容器订阅，保证一次打开只显示本次交互对象贡献的容器集合。
// 2. 跳过空组件、个人鱼护本身和重复组件，避免同一容器在 WrapBox 中出现两份。
// 3. 为每个外部容器订阅复制变化；任意容器刷新都会让背包重建整份 Slots。
void UCatInventoryModel::SetExternalContainerContexts(
	const TArray<UCatContainerReplicationComponent*>& InExternalContainers)
{
	ClearExternalContainerBindings();
	const UCatContainerReplicationComponent* PersonalGuard = BoundPersonalFishGuard.Get();
	TSet<const UCatContainerReplicationComponent*> SeenComponents;
	for (UCatContainerReplicationComponent* Container : InExternalContainers)
	{
		if (!Container || Container == PersonalGuard || SeenComponents.Contains(Container))
		{
			continue;
		}
		SeenComponents.Add(Container);
		FExternalContainerBinding& Binding = BoundExternalContainers.AddDefaulted_GetRef();
		Binding.Component = Container;
		Binding.SnapshotChangedHandle = Container->OnSnapshotChanged.AddUObject(
			this, &ThisClass::HandleExternalContainerSnapshotChanged);
	}
	Refresh();
}

// 外部容器清理流程：普通背包打开时显式清空上下文；如果当前已经没有外部绑定，只刷新一次以清掉旧 UI 投影。
void UCatInventoryModel::ClearExternalContainerContexts()
{
	ClearExternalContainerBindings();
	Refresh();
}

// 格子选择流程：只接受当前背包显示范围内的下标；空格和装备槽允许被选择用于说明，但只有对应来源会产生动作 gate。
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

// 本地拒绝流程：PageController 无法构造正式服务器命令时关闭 pending 并发布结构化错误；它不会改任何容器数组。
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
// 1. 从个人鱼护和当前外部容器复制组件读取容量、Revision 和容器物体投影。
// 2. 从 Equipment 读取当前装配和随身耗材，保持背包玩家概念完整。
// 3. 先生成当前鱼竿槽，再生成容器槽，让 WrapBox 能同时证明手上装备和鱼护内容。
// 4. 裁剪当前选择并派生鱼动作 gate、跨容器拖拽提示、摘要文本、当前选择文本和结果文本。
// 5. 从 UI Settings 解析既有 InputContext 的背包开关键名，最后广播完整投影。
void UCatInventoryModel::Refresh()
{
	FCatInventoryViewState NewState;
	if (const UCatContainerReplicationComponent* FishGuard = BoundPersonalFishGuard.Get())
	{
		NewState.PersonalFishGuard = FishGuard->GetSnapshot();
	}
	if (const UCatEquipmentComponent* Equipment = BoundEquipment.Get())
	{
		NewState.Equipment = Equipment->GetSnapshot();
		NewState.bEquipmentAvailable = true;
	}

	int32 DisplaySlotIndex = 0;
	auto AddContainerToView = [this, &NewState, &DisplaySlotIndex](const FCatContainerSnapshot& Snapshot,
		const FText& DisplayName, const bool bPrimaryPersonalContainer)
	{
		if (!Snapshot.ContainerId.IsValid())
		{
			return;
		}
		const int32 ObjectCount = CatItems::GetContainedObjectCount(Snapshot);
		const int32 SparseSlotCount = FMath::Max(Snapshot.Fish.Num(), Snapshot.Objects.Num());
		const int32 DeclaredSlotCount = Snapshot.Capacity > 0 ? Snapshot.Capacity : SparseSlotCount;
		const int32 EffectiveSlotCount = FMath::Max(FMath::Max(DeclaredSlotCount, ObjectCount), 1);
		FCatInventoryContainerView ContainerView;
		ContainerView.Snapshot = Snapshot;
		ContainerView.DisplayName = DisplayName;
		ContainerView.FirstSlotIndex = DisplaySlotIndex;
		ContainerView.SlotCount = EffectiveSlotCount;
		ContainerView.bPrimaryPersonalContainer = bPrimaryPersonalContainer;
		NewState.Containers.Add(ContainerView);
		const FString ContainerDisplayName = DisplayName.ToString();
		for (int32 ContainerSlotIndex = 0; ContainerSlotIndex < EffectiveSlotCount; ++ContainerSlotIndex)
		{
			NewState.Slots.Add(MakeSlotView(Snapshot, ContainerSlotIndex,
				DisplaySlotIndex++, *ContainerDisplayName));
		}
	};

	if (NewState.bEquipmentAvailable)
	{
		NewState.Slots.Add(MakeCurrentRodSlotView(NewState.Equipment, DisplaySlotIndex++));
	}
	AddContainerToView(NewState.PersonalFishGuard, FText::FromString(TEXT("鱼护")), true);
	int32 ExternalContainerIndex = 0;
	for (const FExternalContainerBinding& Binding : BoundExternalContainers)
	{
		const UCatContainerReplicationComponent* Component = Binding.Component.Get();
		if (!Component)
		{
			continue;
		}
		const FCatContainerSnapshot Snapshot = Component->GetSnapshot();
		AddContainerToView(Snapshot, MakeContainerDisplayName(Snapshot, ExternalContainerIndex), false);
		++ExternalContainerIndex;
	}
	NewState.SlotCount = NewState.Slots.Num();
	NewState.bHasExternalContainers = NewState.Containers.Num() > 1;
	if (SelectedSlotIndex >= NewState.SlotCount)
	{
		SelectedSlotIndex = NewState.SlotCount > 0 ? NewState.SlotCount - 1 : INDEX_NONE;
	}
	NewState.SelectedSlotIndex = SelectedSlotIndex;
	FCatInventorySlotView* SelectedSlot = SelectedSlotIndex != INDEX_NONE && NewState.Slots.IsValidIndex(SelectedSlotIndex)
		? &NewState.Slots[SelectedSlotIndex] : nullptr;
	NewState.bHasSelectedObject = SelectedSlot && SelectedSlot->SlotSource == ECatInventorySlotSource::ContainerObject
		&& SelectedSlot->bOccupied
		&& SelectedSlot->ObjectKind != ECatContainedObjectKind::Unknown
		&& SelectedSlot->ObjectInstanceId.IsValid();
	NewState.bSelectedObjectInPersonalContainer = NewState.bHasSelectedObject && SelectedSlot
		&& SelectedSlot->ContainerKind == ECatContainerKind::PersonalGuard;
	if (NewState.bHasSelectedObject && SelectedSlot)
	{
		NewState.SelectedObject = SelectedSlot->Object;
	}
	NewState.bHasSelectedFish = SelectedSlot && SelectedSlot->SlotSource == ECatInventorySlotSource::ContainerObject
		&& SelectedSlot->bOccupied
		&& SelectedSlot->ObjectKind == ECatContainedObjectKind::Fish
		&& SelectedSlot->Fish.FishInstanceId.IsValid();
	NewState.bSelectedFishInPersonalGuard = NewState.bHasSelectedFish && SelectedSlot
		&& SelectedSlot->ContainerKind == ECatContainerKind::PersonalGuard
		&& SelectedSlot->bOccupied;
	if (NewState.bHasSelectedFish && SelectedSlot)
	{
		NewState.SelectedFish = SelectedSlot->Fish;
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
	NewState.bCanSubmitAction = bOpen && NewState.bSelectedFishInPersonalGuard && !bActionPending;
	if (const UCatUISettings* Settings = GetDefault<UCatUISettings>())
	{
		NewState.ToggleKeyName = Settings->ResolveInventoryToggleKeyName();
	}
	NewState.EquipmentText = MakeEquipmentText(NewState.Equipment, NewState.bEquipmentAvailable);
	NewState.ConsumablesText = MakeConsumablesText(NewState.Equipment, NewState.bEquipmentAvailable);
	TArray<FString> ContainerSummaries;
	ContainerSummaries.Reserve(NewState.Containers.Num());
	for (const FCatInventoryContainerView& ContainerView : NewState.Containers)
	{
		ContainerSummaries.Add(FString::Printf(TEXT("%s %d/%d"),
			*ContainerView.DisplayName.ToString(),
			CatItems::GetContainedObjectCount(ContainerView.Snapshot),
			ContainerView.SlotCount));
	}
	const FString ContainerSummary = ContainerSummaries.IsEmpty()
		? FString(TEXT("容器 0/0")) : FString::Join(ContainerSummaries, TEXT(" | "));
	NewState.SummaryText = FText::FromString(FString::Printf(TEXT("背包：%s | 随身耗材 %d 种"),
		*ContainerSummary,
		CountActiveConsumableStacks(NewState.Equipment)));
	if (SelectedSlot && SelectedSlot->SlotSource == ECatInventorySlotSource::CurrentRod)
	{
		const FString OccupiedText = SelectedSlot->bOccupied
			? GetDefinitionDisplayText(SelectedSlot->EquipmentDefinitionId, TEXT("空"))
			: FString(TEXT("空"));
		if (SelectedSlot->bOccupied)
		{
			const FString RodState = NewState.Equipment.bRodBroken ? TEXT("，已断") : TEXT("");
			NewState.SelectedFishText = FText::FromString(FString::Printf(
				TEXT("选中：当前鱼竿槽，%s，耐久 %.0f%s"),
				*OccupiedText,
				NewState.Equipment.RodDurability,
				*RodState));
		}
		else
		{
			NewState.SelectedFishText = FText::FromString(TEXT("选中：当前鱼竿槽，空"));
		}
	}
	else if (NewState.bHasSelectedFish && SelectedSlot)
	{
		FText SelectedContainerName = FText::FromString(TEXT("容器"));
		for (const FCatInventoryContainerView& ContainerView : NewState.Containers)
		{
			if (ContainerView.Snapshot.ContainerId == SelectedSlot->ContainerId)
			{
				SelectedContainerName = ContainerView.DisplayName;
				break;
			}
		}
		const TCHAR* HintLabel = NewState.bHasExternalContainers
			? TEXT("拖到其他格子可整理或跨容器移动")
			: TEXT("拖到其他鱼护格子可整理");
		NewState.SelectedFishText = FText::FromString(FString::Printf(TEXT("选中：%s第 %d 格，%s，%.2f 千克；%s"),
			*SelectedContainerName.ToString(),
			SelectedSlot->ContainerSlotIndex + 1,
			*NewState.SelectedFish.FishDefinitionId.ToString(),
			NewState.SelectedFish.WeightKilograms,
			HintLabel));
	}
	else if (NewState.bHasSelectedObject && SelectedSlot)
	{
		FText SelectedContainerName = FText::FromString(TEXT("容器"));
		for (const FCatInventoryContainerView& ContainerView : NewState.Containers)
		{
			if (ContainerView.Snapshot.ContainerId == SelectedSlot->ContainerId)
			{
				SelectedContainerName = ContainerView.DisplayName;
				break;
			}
		}
		const TCHAR* HintLabel = NewState.bHasExternalContainers
			? TEXT("拖到其他格子可整理或跨容器移动")
			: TEXT("拖到其他背包格子可整理");
		NewState.SelectedFishText = FText::FromString(FString::Printf(TEXT("选中：%s第 %d 格，%s %s；%s"),
			*SelectedContainerName.ToString(),
			SelectedSlot->ContainerSlotIndex + 1,
			*GetContainedObjectKindDisplayText(SelectedSlot->ObjectKind),
			*SelectedSlot->Object.DefinitionId.ToString(),
			HintLabel));
	}
	else
	{
		NewState.SelectedFishText = NewState.bHasExternalContainers
			? FText::FromString(TEXT("背包操作：点击格子可查看，拖拽可整理或跨容器移动。"))
			: FText::FromString(TEXT("背包操作：当前没有选中物体，点击鱼护格子后可执行鱼动作，也可拖拽整理格子。"));
	}
	if (bActionPending)
	{
		NewState.ResultText = FText::FromString(FString::Printf(TEXT("背包操作：%s，等待服务器确认"),
			*GetInventoryActionDisplayText(PendingAction)));
	}
	else if (bHasCommandResult)
	{
		NewState.ResultText = FText::FromString(FString::Printf(TEXT("背包操作：%s，%s，版本 %lld"),
			*GetInventoryActionDisplayText(LastAction),
			*GetInventoryDomainErrorDisplayText(LastCommandResult.Error),
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

// Equipment 变化流程：装备、耗材、耐久都以完整快照为准；事件只触发重读，背包不缓存增量。
void UCatInventoryModel::HandleEquipmentSnapshotChanged()
{
	Refresh();
}

// 外部容器变化流程：外部容器只通知“公开快照变了”；Model 统一重读所有容器，避免在事件里维护局部增量。
void UCatInventoryModel::HandleExternalContainerSnapshotChanged()
{
	Refresh();
}

// 营地/容器结果流程：只匹配当前 pending 的跨容器物体移动 RequestId；匹配后关闭 pending、缓存结果并重读全部容器。
void UCatInventoryModel::HandleCampCommandResult(const FCatDomainCommandResult& Result)
{
	if (!IsPendingResult(ECatInventoryAction::MoveObjectBetweenContainers, Result.RequestId))
	{
		return;
	}
	const ECatInventoryAction CompletedAction = PendingAction;
	PendingAction = ECatInventoryAction::None;
	PendingRequestId.Invalidate();
	bActionPending = false;
	LastAction = CompletedAction;
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

// 格子投影流程：从后端 Snapshot 的容量和通用对象投影生成稳定文本；空格不含实例，选中态只来自 Model 当前全局显示下标。
FCatInventorySlotView UCatInventoryModel::MakeSlotView(const FCatContainerSnapshot& Snapshot,
	const int32 ContainerSlotIndex, const int32 DisplaySlotIndex, const TCHAR* ContainerDisplayName) const
{
	FCatInventorySlotView Slot;
	Slot.SlotIndex = DisplaySlotIndex;
	Slot.SlotSource = ECatInventorySlotSource::ContainerObject;
	Slot.ContainerKind = Snapshot.Kind;
	Slot.ContainerId = Snapshot.ContainerId;
	Slot.ContainerRevision = Snapshot.Revision;
	Slot.ContainerSlotIndex = ContainerSlotIndex;
	Slot.bSelected = DisplaySlotIndex == SelectedSlotIndex;
	Slot.bOccupied = CatItems::TryGetContainedObjectAt(Snapshot, ContainerSlotIndex, Slot.Object);
	if (Slot.bOccupied)
	{
		Slot.ObjectKind = Slot.Object.ObjectKind;
		Slot.ObjectInstanceId = Slot.Object.ObjectInstanceId;
		Slot.bCanDrag = Slot.ObjectKind != ECatContainedObjectKind::Unknown && Slot.ObjectInstanceId.IsValid();
		if (Slot.ObjectKind == ECatContainedObjectKind::Fish)
		{
			Slot.Fish = Slot.Object.Fish;
			Slot.DisplayText = FText::FromString(FString::Printf(TEXT("%s\n第 %d 格\n%s\n%.2f kg"),
				ContainerDisplayName,
				ContainerSlotIndex + 1,
				*Slot.Fish.FishDefinitionId.ToString(),
				Slot.Fish.WeightKilograms));
		}
		else
		{
			Slot.DisplayText = FText::FromString(FString::Printf(TEXT("%s\n第 %d 格\n%s\n%s x%d"),
				ContainerDisplayName,
				ContainerSlotIndex + 1,
				*GetContainedObjectKindDisplayText(Slot.ObjectKind),
				*Slot.Object.DefinitionId.ToString(),
				FMath::Max(1, Slot.Object.StackQuantity)));
		}
	}
	else
	{
		Slot.DisplayText = FText::FromString(FString::Printf(TEXT("%s\n第 %d 格\n空"),
			ContainerDisplayName,
			ContainerSlotIndex + 1));
	}
	return Slot;
}

// 当前鱼竿槽投影流程：从 Equipment 快照生成一个只读格子；它证明本人手上的竿和耐久，但不参与 Items 容器拖拽。
FCatInventorySlotView UCatInventoryModel::MakeCurrentRodSlotView(
	const FCatEquipmentLoadoutSnapshot& Equipment, const int32 DisplaySlotIndex) const
{
	FCatInventorySlotView Slot;
	Slot.SlotIndex = DisplaySlotIndex;
	Slot.SlotSource = ECatInventorySlotSource::CurrentRod;
	Slot.bSelected = DisplaySlotIndex == SelectedSlotIndex;
	Slot.EquipmentKind = ECatEquipmentKind::Rod;
	Slot.EquipmentDefinitionId = Equipment.RodDefinitionId;
	Slot.bOccupied = !Slot.EquipmentDefinitionId.IsNone();
	Slot.ObjectKind = Slot.bOccupied ? ECatContainedObjectKind::Equipment : ECatContainedObjectKind::Unknown;
	if (Slot.bOccupied)
	{
		const FString RodState = Equipment.bRodBroken ? TEXT("，已断") : TEXT("");
		Slot.DisplayText = FText::FromString(FString::Printf(TEXT("当前鱼竿\n鱼竿槽\n%s\n耐久 %.0f%s"),
			*Slot.EquipmentDefinitionId.ToString(),
			Equipment.RodDurability,
			*RodState));
	}
	else
	{
		Slot.DisplayText = FText::FromString(TEXT("当前鱼竿\n鱼竿槽\n空"));
	}
	return Slot;
}

// 容器名称流程：个人鱼护保留玩家熟悉叫法，其他容器统一按“外部容器 N”命名；未来具体容器可在上下文层提供自定义显示名。
FText UCatInventoryModel::MakeContainerDisplayName(const FCatContainerSnapshot& Snapshot,
	const int32 ExternalContainerIndex)
{
	if (Snapshot.Kind == ECatContainerKind::PersonalGuard)
	{
		return FText::FromString(TEXT("鱼护"));
	}
	return FText::FromString(FString::Printf(TEXT("外部容器 %d"), ExternalContainerIndex + 1));
}

// 外部绑定清理流程：逐个从原复制组件移除本 Model 的委托句柄，再清空数组；组件失效时只丢本地记录，不延长 Actor 生命周期。
void UCatInventoryModel::ClearExternalContainerBindings()
{
	for (FExternalContainerBinding& Binding : BoundExternalContainers)
	{
		if (UCatContainerReplicationComponent* Component = Binding.Component.Get())
		{
			Component->OnSnapshotChanged.Remove(Binding.SnapshotChangedHandle);
		}
		Binding.SnapshotChangedHandle.Reset();
	}
	BoundExternalContainers.Reset();
}
