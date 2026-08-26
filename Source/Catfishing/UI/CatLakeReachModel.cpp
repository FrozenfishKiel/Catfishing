#include "UI/CatLakeReachModel.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "Character/CatCharacter.h"
#include "Condition/CatConditionComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Growth/CatGrowthComponent.h"
#include "Items/CatContainerReplicationComponent.h"
#include "Online/CatOnlineSubsystem.h"
#include "Profile/CatProfileSubsystem.h"
#include "TimerManager.h"
#include "UI/CatFishingViewBridge.h"
#include "UI/CatUISettings.h"

// 绑定流程：
// 1. 先清掉上一套来源，避免同一个 Model 在换 Pawn 时同时订阅两个 World。
// 2. 校验 LocalPlayer、Controller、Character、ASC 和 World 都属于同一条 Lake 生命周期；失败直接返回 false。
// 3. 创建 FishingBridge，并保存 ASC、Condition、Equipment、Growth、GameState、商店快照、鱼护、Profile 和命令结果源的弱引用。
// 4. 对每个只读来源成对订阅完整变化通知，并监听 Controller 的鱼护动作结果和当前 World 生成的 FishingSession。
// 5. 定位当前会话并发布首份 ViewState；任何后续变化都只触发完整重读，不在 Model 中拼增量状态。
bool UCatLakeReachModel::Bind(ULocalPlayer* InLocalPlayer, APlayerController* InController, ACatCharacter* InCharacter)
{
	Unbind();
	if (!InLocalPlayer || !InController || !InCharacter || InController->GetPawn() != InCharacter
		|| InCharacter->GetWorld() != InController->GetWorld())
	{
		return false;
	}

	UAbilitySystemComponent* AbilitySystem = InCharacter->GetAbilitySystemComponent();
	if (!AbilitySystem)
	{
		return false;
	}

	FishingViewBridge = NewObject<UCatFishingViewBridge>(this);
	if (!FishingViewBridge)
	{
		return false;
	}

	BoundLocalPlayer = InLocalPlayer;
	BoundPlayerController = InController;
	BoundLakeASC = AbilitySystem;
	BoundCondition = InCharacter->GetConditionComponent();
	BoundGrowth = InCharacter->GetGrowthComponent();
	BoundEquipment = InCharacter->GetEquipmentComponent();
	BoundLakeWorld = InCharacter->GetWorld();
	BoundGameState = BoundLakeWorld.IsValid() ? BoundLakeWorld->GetGameState<ACatfishingGameState>() : nullptr;
	BoundPersonalFishGuard = InCharacter->FindComponentByClass<UCatContainerReplicationComponent>();
	BoundProfile = InLocalPlayer->GetSubsystem<UCatProfileSubsystem>();
	if (ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(InController))
	{
		BoundFishingCommand = CatController->GetFishingCommandComponent();
		CampCommandResultHandle = CatController->OnCampCommandResultReceived.AddUObject(
			this, &ThisClass::HandleCampCommandResult);
		SacrificeResultHandle = CatController->OnSacrificeResultReceived.AddUObject(
			this, &ThisClass::HandleSacrificeResult);
		FishConsumeResultHandle = CatController->OnFishConsumeResultReceived.AddUObject(
			this, &ThisClass::HandleFishConsumeResult);
	}

	PoisonChangedHandle = AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetPoisonAttribute())
		.AddUObject(this, &ThisClass::HandleLakeAttributeChanged);
	FishingStrengthChangedHandle = AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetFishingStrengthAttribute())
		.AddUObject(this, &ThisClass::HandleLakeAttributeChanged);
	FightStaminaChangedHandle = AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetFightStaminaAttribute())
		.AddUObject(this, &ThisClass::HandleLakeAttributeChanged);
	if (UCatConditionComponent* Conditions = BoundCondition.Get())
	{
		ConditionChangedHandle = Conditions->OnSnapshotChanged.AddUObject(this, &ThisClass::HandleLakeSnapshotChanged);
	}
	if (UCatGrowthComponent* Growth = BoundGrowth.Get())
	{
		GrowthChangedHandle = Growth->OnSnapshotChanged.AddUObject(this, &ThisClass::HandleLakeSnapshotChanged);
	}
	if (UCatEquipmentComponent* Equipment = BoundEquipment.Get())
	{
		EquipmentChangedHandle = Equipment->OnSnapshotChanged.AddUObject(this, &ThisClass::HandleLakeSnapshotChanged);
	}
	if (ACatfishingGameState* GameState = BoundGameState.Get())
	{
		RunChangedHandle = GameState->OnRunPublicStateChanged.AddUObject(this, &ThisClass::HandleLakeSnapshotChanged);
		HelpChangedHandle = GameState->OnHelpSignalChanged.AddUObject(this, &ThisClass::HandleLakeSnapshotChanged);
		ShopEconomyChangedHandle = GameState->OnShopEconomySnapshotChanged.AddUObject(this, &ThisClass::HandleLakeSnapshotChanged);
	}
	if (UCatContainerReplicationComponent* FishGuard = BoundPersonalFishGuard.Get())
	{
		FishGuardChangedHandle = FishGuard->OnSnapshotChanged.AddUObject(this, &ThisClass::HandleLakeSnapshotChanged);
	}
	if (UCatProfileSubsystem* Profile = BoundProfile.Get())
	{
		FishCollectionChangedHandle = Profile->OnFishCollectionChanged.AddUObject(this, &ThisClass::HandleLakeSnapshotChanged);
	}
	if (UCatFishingCommandComponent* FishingCommand = BoundFishingCommand.Get())
	{
		FishingCommand->OnResultReceived.AddDynamic(this, &ThisClass::HandleFishingCommandResult);
	}
	FishingViewChangedHandle = FishingViewBridge->OnViewStateChanged.AddUObject(
		this, &ThisClass::HandleFishingViewStateChanged);
	if (UWorld* World = BoundLakeWorld.Get())
	{
		ActorSpawnedHandle = World->AddOnActorSpawnedHandler(
			FOnActorSpawned::FDelegate::CreateUObject(this, &ThisClass::HandleWorldActorSpawned));
	}

	RefreshFishingSessionBinding();
	Refresh();
	return true;
}

// 解绑流程：
// 1. 从 ASC、Condition、Equipment、Growth、GameState、商店、鱼护、Profile、命令组件和 Controller 结果源移除绑定时保存的委托句柄。
// 2. 停止观察 FishingSession，再解绑 FishingBridge 与 World ActorSpawned 监听。
// 3. 清全部弱引用、句柄、菜单状态、最近命令结果、鱼护选择/pending/result 和 ViewState；不再广播空态，因为 PageController 和 WBP 根会在同一 Detach 流程里被移除。
void UCatLakeReachModel::Unbind()
{
	if (UAbilitySystemComponent* AbilitySystem = BoundLakeASC.Get())
	{
		AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetPoisonAttribute()).Remove(PoisonChangedHandle);
		AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetFishingStrengthAttribute()).Remove(FishingStrengthChangedHandle);
		AbilitySystem->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetFightStaminaAttribute()).Remove(FightStaminaChangedHandle);
	}
	if (UCatConditionComponent* Conditions = BoundCondition.Get())
	{
		Conditions->OnSnapshotChanged.Remove(ConditionChangedHandle);
	}
	if (UCatGrowthComponent* Growth = BoundGrowth.Get())
	{
		Growth->OnSnapshotChanged.Remove(GrowthChangedHandle);
	}
	if (UCatEquipmentComponent* Equipment = BoundEquipment.Get())
	{
		Equipment->OnSnapshotChanged.Remove(EquipmentChangedHandle);
	}
	if (ACatfishingGameState* GameState = BoundGameState.Get())
	{
		GameState->OnRunPublicStateChanged.Remove(RunChangedHandle);
		GameState->OnHelpSignalChanged.Remove(HelpChangedHandle);
		GameState->OnShopEconomySnapshotChanged.Remove(ShopEconomyChangedHandle);
	}
	if (UCatContainerReplicationComponent* FishGuard = BoundPersonalFishGuard.Get())
	{
		FishGuard->OnSnapshotChanged.Remove(FishGuardChangedHandle);
	}
	if (UCatProfileSubsystem* Profile = BoundProfile.Get())
	{
		Profile->OnFishCollectionChanged.Remove(FishCollectionChangedHandle);
	}
	if (UCatFishingCommandComponent* FishingCommand = BoundFishingCommand.Get())
	{
		FishingCommand->OnResultReceived.RemoveDynamic(this, &ThisClass::HandleFishingCommandResult);
	}
	if (ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(BoundPlayerController.Get()))
	{
		CatController->OnCampCommandResultReceived.Remove(CampCommandResultHandle);
		CatController->OnSacrificeResultReceived.Remove(SacrificeResultHandle);
		CatController->OnFishConsumeResultReceived.Remove(FishConsumeResultHandle);
	}
	StopObservingFishingSessionLifecycle();
	if (FishingViewBridge)
	{
		FishingViewBridge->OnViewStateChanged.Remove(FishingViewChangedHandle);
		FishingViewBridge->UnbindSession();
	}
	if (UWorld* World = BoundLakeWorld.Get(); World && ActorSpawnedHandle.IsValid())
	{
		World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
	}

	PoisonChangedHandle.Reset();
	FishingStrengthChangedHandle.Reset();
	FightStaminaChangedHandle.Reset();
	ConditionChangedHandle.Reset();
	GrowthChangedHandle.Reset();
	EquipmentChangedHandle.Reset();
	RunChangedHandle.Reset();
	HelpChangedHandle.Reset();
	ShopEconomyChangedHandle.Reset();
	FishGuardChangedHandle.Reset();
	FishCollectionChangedHandle.Reset();
	FishingViewChangedHandle.Reset();
	CampCommandResultHandle.Reset();
	SacrificeResultHandle.Reset();
	FishConsumeResultHandle.Reset();
	ActorSpawnedHandle.Reset();
	BoundLocalPlayer.Reset();
	BoundPlayerController.Reset();
	BoundLakeASC.Reset();
	BoundCondition.Reset();
	BoundGrowth.Reset();
	BoundEquipment.Reset();
	BoundGameState.Reset();
	BoundLakeWorld.Reset();
	BoundPersonalFishGuard.Reset();
	BoundProfile.Reset();
	BoundFishingCommand.Reset();
	FishingViewBridge = nullptr;
	LastFishingCommandResult = FCatFishingCommandResult();
	bHasFishingCommandResult = false;
	SelectedFishGuardIndex = INDEX_NONE;
	PendingFishGuardAction = ECatUIReachFishGuardAction::None;
	PendingFishGuardRequestId.Invalidate();
	bFishGuardActionPending = false;
	LastFishGuardAction = ECatUIReachFishGuardAction::None;
	LastFishGuardCommandResult = FCatDomainCommandResult();
	bHasFishGuardCommandResult = false;
	LastFishGuardSacrificeResult = FCatSacrificeResult();
	LastFishGuardConsumeResult = FCatFishConsumeResult();
	bMenuOpen = false;
	ViewState = FCatUIReachViewState();
}

// 绑定状态查询流程：只承认 Controller、ASC、World 和 FishingBridge 同时有效；单个弱引用存活不足以说明 Model 仍可发布正式 ViewState。
bool UCatLakeReachModel::IsBound() const
{
	return BoundLocalPlayer.IsValid()
		&& BoundPlayerController.IsValid()
		&& BoundLakeASC.IsValid()
		&& BoundLakeWorld.IsValid()
		&& FishingViewBridge != nullptr;
}

// 菜单状态流程：PageController 是唯一写者；Model 只把该布尔值合入下一份 ViewState 并广播给 View。
void UCatLakeReachModel::SetMenuOpen(const bool bOpen)
{
	if (bMenuOpen == bOpen)
	{
		return;
	}
	bMenuOpen = bOpen;
	Refresh();
}

// 鱼护选择流程：使用最近发布的鱼护快照计算新下标；空列表、零偏移或越界后无变化时保持当前 ViewState 不广播。
bool UCatLakeReachModel::SelectFishGuardEntryByOffset(const int32 Offset)
{
	const int32 FishCount = ViewState.PersonalFishGuard.Fish.Num();
	if (FishCount <= 0 || Offset == 0)
	{
		return false;
	}
	const int32 CurrentIndex = SelectedFishGuardIndex == INDEX_NONE ? 0 : SelectedFishGuardIndex;
	const int32 NextIndex = FMath::Clamp(CurrentIndex + Offset, 0, FishCount - 1);
	if (SelectedFishGuardIndex == NextIndex)
	{
		return false;
	}
	SelectedFishGuardIndex = NextIndex;
	Refresh();
	return true;
}

// 鱼护提交标记流程：记录动作和 RequestId，清除上一条鱼护反馈，并发布 pending 状态；真正成功或失败只能由服务器结果回包关闭。
void UCatLakeReachModel::MarkFishGuardActionSubmitted(const ECatUIReachFishGuardAction Action, const FGuid RequestId)
{
	if (Action == ECatUIReachFishGuardAction::None || !RequestId.IsValid())
	{
		return;
	}
	PendingFishGuardAction = Action;
	PendingFishGuardRequestId = RequestId;
	bFishGuardActionPending = true;
	LastFishGuardAction = ECatUIReachFishGuardAction::None;
	LastFishGuardCommandResult = FCatDomainCommandResult();
	bHasFishGuardCommandResult = false;
	LastFishGuardSacrificeResult = FCatSacrificeResult();
	LastFishGuardConsumeResult = FCatFishConsumeResult();
	Refresh();
}

// 本地拒绝流程：只有 PageController 无法构造正式服务器命令时使用；它关闭 pending 并发布拒绝结果，但不会触碰鱼护快照。
void UCatLakeReachModel::MarkFishGuardActionRejected(const ECatUIReachFishGuardAction Action,
	const FGuid RequestId, const ECatDomainCommandError Error, const int64 Revision)
{
	if (Action == ECatUIReachFishGuardAction::None || !RequestId.IsValid())
	{
		return;
	}
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	Result.Error = Error;
	Result.Revision = Revision;
	PendingFishGuardAction = ECatUIReachFishGuardAction::None;
	PendingFishGuardRequestId.Invalidate();
	bFishGuardActionPending = false;
	LastFishGuardAction = Action;
	LastFishGuardCommandResult = Result;
	bHasFishGuardCommandResult = true;
	Refresh();
}

// ViewState 刷新流程：
// 1. 校验 Controller 当前 Pawn、ASC 和 World 仍与绑定时相同；换 World 或换 Pawn 时直接发布默认空态以避免旧事实渲染。
// 2. 读取三项 ASC 属性以及 Condition、Growth、Equipment、Run、Help、Shop、鱼护、Profile 和 Online 可选快照。
// 3. 从鱼护快照裁剪当前选择，并合入 pending 与最近鱼护动作结果，让 View 能证明服务器回包。
// 4. FishingBridge 只有仍绑定会话时才提供 Fishing DTO；最近命令结果由单独标记决定是否展示。
// 5. 写入菜单、键名和离局 gate，最后广播完整快照变化。
void UCatLakeReachModel::Refresh()
{
	UAbilitySystemComponent* AbilitySystem = BoundLakeASC.Get();
	const APlayerController* Controller = BoundPlayerController.Get();
	ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr;
	UWorld* LakeWorld = BoundLakeWorld.Get();
	if (!Character || !AbilitySystem || !LakeWorld || Character->GetWorld() != LakeWorld
		|| Character->GetAbilitySystemComponent() != AbilitySystem)
	{
		ViewState = FCatUIReachViewState();
		OnViewStateChanged.Broadcast();
		return;
	}

	FCatUIReachViewState NewState;
	NewState.Poison = AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetPoisonAttribute());
	NewState.FishingStrength = AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetFishingStrengthAttribute());
	NewState.FightStamina = AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	if (const UCatConditionComponent* Conditions = BoundCondition.Get())
	{
		NewState.Condition = Conditions->GetSnapshot();
	}
	if (const UCatGrowthComponent* Growth = BoundGrowth.Get())
	{
		NewState.Growth = Growth->GetSnapshot();
	}
	if (const UCatEquipmentComponent* Equipment = BoundEquipment.Get())
	{
		NewState.Equipment = Equipment->GetSnapshot();
	}
	if (const ACatfishingGameState* GameState = BoundGameState.Get())
	{
		NewState.Run = GameState->GetRunPublicState();
		NewState.HelpSignal = GameState->GetLastHelpSignal();
		NewState.ShopEconomy = GameState->GetShopEconomySnapshot();
		NewState.bShopEconomyAvailable = true;
	}
	if (FishingViewBridge && FishingViewBridge->GetBoundSession())
	{
		NewState.Fishing = FishingViewBridge->GetViewState();
		NewState.bHasFishingSession = true;
	}
	NewState.LastFishingCommandResult = LastFishingCommandResult;
	NewState.bHasFishingCommandResult = bHasFishingCommandResult;
	if (const UCatContainerReplicationComponent* FishGuard = BoundPersonalFishGuard.Get())
	{
		NewState.PersonalFishGuard = FishGuard->GetSnapshot();
	}
	const int32 FishCount = NewState.PersonalFishGuard.Fish.Num();
	if (FishCount <= 0)
	{
		SelectedFishGuardIndex = INDEX_NONE;
	}
	else if (SelectedFishGuardIndex == INDEX_NONE)
	{
		SelectedFishGuardIndex = 0;
	}
	else
	{
		SelectedFishGuardIndex = FMath::Clamp(SelectedFishGuardIndex, 0, FishCount - 1);
	}
	NewState.SelectedFishGuardIndex = SelectedFishGuardIndex;
	NewState.bHasSelectedFishGuardFish = SelectedFishGuardIndex != INDEX_NONE
		&& NewState.PersonalFishGuard.Fish.IsValidIndex(SelectedFishGuardIndex);
	if (NewState.bHasSelectedFishGuardFish)
	{
		NewState.SelectedFishGuardFish = NewState.PersonalFishGuard.Fish[SelectedFishGuardIndex];
	}
	NewState.bCanSelectPreviousFishGuardEntry = NewState.bHasSelectedFishGuardFish && SelectedFishGuardIndex > 0;
	NewState.bCanSelectNextFishGuardEntry = NewState.bHasSelectedFishGuardFish
		&& SelectedFishGuardIndex + 1 < FishCount;
	NewState.bFishGuardActionPending = bFishGuardActionPending;
	NewState.PendingFishGuardAction = PendingFishGuardAction;
	NewState.PendingFishGuardRequestId = PendingFishGuardRequestId;
	NewState.LastFishGuardAction = LastFishGuardAction;
	NewState.LastFishGuardCommandResult = LastFishGuardCommandResult;
	NewState.bHasFishGuardCommandResult = bHasFishGuardCommandResult;
	NewState.LastFishGuardSacrificeResult = LastFishGuardSacrificeResult;
	NewState.LastFishGuardConsumeResult = LastFishGuardConsumeResult;
	if (const UCatProfileSubsystem* Profile = BoundProfile.Get())
	{
		NewState.bFishCollectionAvailable = Profile->GetFishCollectionSnapshot(NewState.FishCollection);
	}
	NewState.bMenuOpen = bMenuOpen;
	NewState.bCanSubmitSelectedFishGuardAction = NewState.bMenuOpen
		&& NewState.bHasSelectedFishGuardFish
		&& !NewState.bFishGuardActionPending;
	if (const UCatUISettings* Settings = GetDefault<UCatUISettings>())
	{
		NewState.MenuToggleKeyName = Settings->ResolveLakeMenuToggleKeyName();
	}
	if (const ULocalPlayer* LocalPlayer = BoundLocalPlayer.Get())
	{
		if (const UGameInstance* GameInstance = LocalPlayer->GetGameInstance())
		{
			if (const UCatOnlineSubsystem* Online = GameInstance->GetSubsystem<UCatOnlineSubsystem>())
			{
				NewState.bCanRequestOnlineLeave = CanRequestOnlineLeaveFromLake(Online->GetSnapshot());
			}
		}
	}

	ViewState = MoveTemp(NewState);
	OnViewStateChanged.Broadcast();
}

// ViewState 读取流程：返回 Model 最近发布的完整 DTO；调用方不获得任何订阅句柄或领域写入口。
const FCatUIReachViewState& UCatLakeReachModel::GetViewState() const
{
	return ViewState;
}

// Lake 离局判断流程：沿用 Online 的会话身份与空闲条件；它只决定按钮意图是否可见，不执行 RequestLeave。
bool UCatLakeReachModel::CanRequestOnlineLeaveFromLake(const FCatOnlineSnapshot& Snapshot)
{
	return Snapshot.WorldState == ECatOnlineWorldState::Lake
		&& Snapshot.SessionRole != ECatOnlineSessionRole::None
		&& Snapshot.ActiveOperation == ECatOnlineOperation::None;
}

// 测试读取流程：只暴露当前 Bridge 指针用于自动化确认绑定/解绑，不让正式 View 绕过 Model 渲染。
UCatFishingViewBridge* UCatLakeReachModel::GetFishingViewBridgeForTests() const
{
	return FishingViewBridge;
}

// 测试读取流程：只暴露当前 ASC 弱引用用于证明绑定对象正确，不开放属性写入口。
UAbilitySystemComponent* UCatLakeReachModel::GetBoundAbilitySystemForTests() const
{
	return BoundLakeASC.Get();
}

// 测试读取流程：只暴露当前观察 Session 以验证生命周期回调清理，不延长 Actor 生命周期。
ACatFishingSession* UCatLakeReachModel::GetObservedFishingSessionForTests() const
{
	return ObservedFishingSession.Get();
}

// 属性变化流程：事件只表示某个 ASC 事实已变；忽略单项载荷后重读所有来源，避免增量顺序形成 UI 私有状态。
void UCatLakeReachModel::HandleLakeAttributeChanged(const FOnAttributeChangeData& ChangeData)
{
	(void)ChangeData;
	Refresh();
}

// 快照变化流程：所有 Query 来源都只提供“需要重读”信号；统一重建整份 DTO，避免商店、鱼护和 HUD 分别维护 UI 私有状态。
void UCatLakeReachModel::HandleLakeSnapshotChanged()
{
	Refresh();
}

// Fishing 投影流程：Bridge 已经用完整 Session Snapshot 更新自身；忽略事件载荷后重建统一 ViewState。
void UCatLakeReachModel::HandleFishingViewStateChanged(const FCatFishingViewState& InViewState)
{
	(void)InViewState;
	Refresh();
}

// 命令结果流程：复制一条只用于展示的结构化终态，标记反馈可见；再定位可能由该命令创建的 Session，最后刷新统一 ViewState。
void UCatLakeReachModel::HandleFishingCommandResult(const FCatFishingCommandResult& Result)
{
	LastFishingCommandResult = Result;
	bHasFishingCommandResult = true;
	RefreshFishingSessionBinding();
	Refresh();
}

// 营地结果流程：只认当前 pending 的转缸 RequestId；匹配后关闭 pending、缓存公共结果并重读鱼护快照。
void UCatLakeReachModel::HandleCampCommandResult(const FCatDomainCommandResult& Result)
{
	if (!IsPendingFishGuardResult(ECatUIReachFishGuardAction::TransferSelectedFishToTank, Result.RequestId))
	{
		return;
	}
	PendingFishGuardAction = ECatUIReachFishGuardAction::None;
	PendingFishGuardRequestId.Invalidate();
	bFishGuardActionPending = false;
	LastFishGuardAction = ECatUIReachFishGuardAction::TransferSelectedFishToTank;
	LastFishGuardCommandResult = Result;
	bHasFishGuardCommandResult = true;
	Refresh();
}

// 献祭结果流程：只认当前 pending 的献祭 RequestId；匹配后把协议阶段保存在详细结果中，同时给 View 一个公共结果头用于统一反馈。
void UCatLakeReachModel::HandleSacrificeResult(const FCatSacrificeResult& Result)
{
	if (!IsPendingFishGuardResult(ECatUIReachFishGuardAction::SacrificeSelectedFish, Result.RequestId))
	{
		return;
	}
	FCatDomainCommandResult PublicResult;
	PublicResult.RequestId = Result.RequestId;
	PublicResult.bCommitted = Result.bCompleted;
	PublicResult.Error = Result.Error;
	PublicResult.Revision = Result.ItemsRevision;
	PendingFishGuardAction = ECatUIReachFishGuardAction::None;
	PendingFishGuardRequestId.Invalidate();
	bFishGuardActionPending = false;
	LastFishGuardAction = ECatUIReachFishGuardAction::SacrificeSelectedFish;
	LastFishGuardSacrificeResult = Result;
	LastFishGuardCommandResult = PublicResult;
	bHasFishGuardCommandResult = true;
	Refresh();
}

// 进食结果流程：只认当前 pending 的吃鱼 RequestId；匹配后缓存 Items 终态并重读鱼护和身体/成长快照。
void UCatLakeReachModel::HandleFishConsumeResult(const FCatFishConsumeResult& Result)
{
	if (!IsPendingFishGuardResult(ECatUIReachFishGuardAction::ConsumeSelectedFish, Result.Command.RequestId))
	{
		return;
	}
	PendingFishGuardAction = ECatUIReachFishGuardAction::None;
	PendingFishGuardRequestId.Invalidate();
	bFishGuardActionPending = false;
	LastFishGuardAction = ECatUIReachFishGuardAction::ConsumeSelectedFish;
	LastFishGuardConsumeResult = Result;
	LastFishGuardCommandResult = Result.Command;
	bHasFishGuardCommandResult = true;
	Refresh();
}

// pending 匹配流程：同时比较动作类型、pending 标记和 RequestId；旧回包、其他 Camp 命令或其他 UI 动作不会污染当前反馈。
bool UCatLakeReachModel::IsPendingFishGuardResult(const ECatUIReachFishGuardAction Action,
	const FGuid RequestId) const
{
	return bFishGuardActionPending
		&& PendingFishGuardAction == Action
		&& RequestId.IsValid()
		&& PendingFishGuardRequestId == RequestId;
}

// Actor 生成流程：只响应当前绑定 World 的 FishingSession，并把重新定位推迟到下一帧，让网络初始复制先填充 FisherPlayerState。
void UCatLakeReachModel::HandleWorldActorSpawned(AActor* SpawnedActor)
{
	UWorld* World = BoundLakeWorld.Get();
	if (!Cast<ACatFishingSession>(SpawnedActor) || !World || SpawnedActor->GetWorld() != World)
	{
		return;
	}
	World->GetTimerManager().SetTimerForNextTick(this, &ThisClass::RefreshFishingSessionBinding);
}

// Session 调和流程：
// 1. 按当前 Controller 的公开 PlayerState 查找唯一非终态复制会话，Bridge 查找不会返回 terminal Session。
// 2. 若当前 Bridge 仍绑定一个 terminal 但未 EndPlay 的会话，继续保留它，让终态复制窗口能被 UI 展示。
// 3. 只有发现新非终态候选、或当前非终态会话确实消失时，才通过统一 helper 切换 Bridge 与生命周期观察。
void UCatLakeReachModel::RefreshFishingSessionBinding()
{
	if (!FishingViewBridge)
	{
		return;
	}
	APlayerController* Controller = BoundPlayerController.Get();
	APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
	ACatFishingSession* Session = UCatFishingViewBridge::FindFishingSessionForPlayerState(
		BoundLakeWorld.Get(), PlayerState);
	ACatFishingSession* CurrentSession = FishingViewBridge->GetBoundSession();
	if (CurrentSession == Session)
	{
		ObserveFishingSessionLifecycle(CurrentSession);
		return;
	}
	if (!Session && CurrentSession && CurrentSession->IsTerminal())
	{
		ObserveFishingSessionLifecycle(CurrentSession);
		return;
	}
	SetFishingViewSession(Session);
}

// 会话切换流程：先让旧 Session 的 Actor 生命周期委托与 Bridge 解绑，再绑定新 Session 并观察它的 EndPlay/Destroyed；绑定失败或传空都会刷新为无活动会话。
void UCatLakeReachModel::SetFishingViewSession(ACatFishingSession* Session)
{
	if (!FishingViewBridge)
	{
		StopObservingFishingSessionLifecycle();
		return;
	}
	if (FishingViewBridge->GetBoundSession() == Session)
	{
		ObserveFishingSessionLifecycle(Session);
		return;
	}

	StopObservingFishingSessionLifecycle();
	if (Session && FishingViewBridge->BindSession(Session))
	{
		ObserveFishingSessionLifecycle(Session);
		return;
	}

	FishingViewBridge->UnbindSession();
	Refresh();
}

// 生命周期观察流程：先用对象身份键过滤同一会话的重复绑定；新会话到来时移除旧动态委托，再保存弱引用和 FObjectKey。
// Destroyed 与 EndPlay 都进同一收口以覆盖复制销毁和 World 移除；FObjectKey 只用于销毁期来源比对，不持有 Actor。
void UCatLakeReachModel::ObserveFishingSessionLifecycle(ACatFishingSession* Session)
{
	const FObjectKey SessionKey(Session);
	if (ObservedFishingSessionKey == SessionKey)
	{
		return;
	}
	StopObservingFishingSessionLifecycle();
	if (!Session)
	{
		return;
	}
	ObservedFishingSession = Session;
	ObservedFishingSessionKey = SessionKey;
	Session->OnDestroyed.AddDynamic(this, &ThisClass::HandleFishingSessionDestroyed);
	Session->OnEndPlay.AddDynamic(this, &ThisClass::HandleFishingSessionEndPlay);
}

// 生命周期解绑流程：旧会话仍有效时从同一个 Actor 移除 Destroyed 与 EndPlay；Actor 已经进入销毁时只清本地弱引用和身份键。
void UCatLakeReachModel::StopObservingFishingSessionLifecycle()
{
	if (ACatFishingSession* Session = ObservedFishingSession.Get())
	{
		Session->OnDestroyed.RemoveDynamic(this, &ThisClass::HandleFishingSessionDestroyed);
		Session->OnEndPlay.RemoveDynamic(this, &ThisClass::HandleFishingSessionEndPlay);
	}
	ObservedFishingSession.Reset();
	ObservedFishingSessionKey = FObjectKey();
}

// Destroyed 回调流程：Destroyed 与 EndPlay 可能在同一次销毁中先后到达；统一收口会用当前观察身份键去重并过滤旧会话。
void UCatLakeReachModel::HandleFishingSessionDestroyed(AActor* DestroyedActor)
{
	HandleFishingSessionLifecycleEnded(DestroyedActor);
}

// EndPlay 回调流程：EndPlayReason 只表示 Actor 离开 World 的原因；UIReach 不根据原因推导玩法结果，只确认当前会话已经不可继续展示为活动会话。
void UCatLakeReachModel::HandleFishingSessionEndPlay(AActor* Actor, EEndPlayReason::Type EndPlayReason)
{
	(void)EndPlayReason;
	HandleFishingSessionLifecycleEnded(Actor);
}

// 会话结束收口流程：先用 FObjectKey 确认广播源仍是当前观察会话，过滤旧会话和 Destroyed/EndPlay 双触发；再解除观察、解绑 Bridge 并重建 ViewState。
void UCatLakeReachModel::HandleFishingSessionLifecycleEnded(AActor* SessionActor)
{
	if (!SessionActor || ObservedFishingSessionKey != FObjectKey(SessionActor))
	{
		return;
	}
	StopObservingFishingSessionLifecycle();
	if (FishingViewBridge)
	{
		FishingViewBridge->UnbindSession();
	}
	Refresh();
}
