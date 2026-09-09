#include "Fishing/Actors/CatFishingRodActor.h"
#include "Character/CatCharacterMovementComponent.h"
#include "Character/CatCharacter.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"

#include "Components/SceneComponent.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/Config/CatFishingFightBalanceDefinition.h"
#include "Fishing/Simulation/CatFishingGroupModel.h"
#include "Fishing/Debug/CatFishingMotionDiagnostics.h"
#include "Fishing/Presentation/CatRodBendComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Logging/CatLog.h"
#include "Net/UnrealNetwork.h"

// 构造流程：创建表现根、权威锚点和默认复制姿态；这里只搭好场景骨架，真实 Actor/Item 身份稍后由服务器初始化。
ACatFishingRodActor::ACatFishingRodActor()
{
	// Transform 仍由服务器权威；原生 Tick 只在 Held 姿态启用，用控制器视角和 Pawn 运动更新规范握把。
	bReplicates = true;
	SetReplicateMovement(true);
	bAlwaysRelevant = false; // 不强制全图相关性，交给引擎按距离/视锥裁剪
	bNetUseOwnerRelevancy = false;
	bOnlyRelevantToOwner = false; // 其他玩家也要能看到这根竿，不能只对 Owner 复制
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	// SceneRoot 是根组件，其余锚点都挂在它下面，整体随 Actor Transform 移动
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	// VisualRoot 承载美术表现（皮肤/特效），与权威判定用的锚点分层，便于蓝图独立驱动视觉
	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	VisualRoot->SetupAttachment(SceneRoot);
	RodBend = CreateDefaultSubobject<UCatRodBendComponent>(TEXT("RodBend"));
	RodBend->SetupAttachment(VisualRoot);
	// RodTip/Stand/Grip 三个锚点分别对应竿尖(挂线)、插竿点、握持点，供表现和玩法逻辑取世界坐标
	RodTipAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("RodTipAnchor"));
	RodTipAnchor->SetupAttachment(SceneRoot);
	StandAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("StandAnchor"));
	StandAnchor->SetupAttachment(SceneRoot);
	RightStandAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("RightStandAnchor"));
	RightStandAnchor->SetupAttachment(SceneRoot);
	LeftStandAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("LeftStandAnchor"));
	LeftStandAnchor->SetupAttachment(SceneRoot);
	GripAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("GripAnchor"));
	GripAnchor->SetupAttachment(SceneRoot);
	// 这些锚点的具体相对位置由权威在 ConfigureCanonicalAnchorsFromAuthority 中设置，不允许蓝图继承时手改
	SceneRoot->bEditableWhenInherited = false;
	RodTipAnchor->bEditableWhenInherited = false;
	StandAnchor->bEditableWhenInherited = false;
	RightStandAnchor->bEditableWhenInherited = false;
	LeftStandAnchor->bEditableWhenInherited = false;
	GripAnchor->bEditableWhenInherited = false;
}

void ACatFishingRodActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority() && PresentationState.PoseMode == ECatFishingRodPoseMode::Held)
	{
		RefreshHeldTransformFromAuthority(DeltaSeconds);
		UpdateUnloadedGroupMotionFromAuthority(DeltaSeconds);
	}
	PublishCarrierConstraintToMovement();
}

void ACatFishingRodActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// 离散身份/姿态、连续约束和不变握把标定各自复制，不从客户端视觉组件推导玩法锚点。
	DOREPLIFETIME(ThisClass, PresentationState);
	DOREPLIFETIME(ThisClass, CarrierConstraintState);
	DOREPLIFETIME(ThisClass, GroupMotionState);
	DOREPLIFETIME_CONDITION(ThisClass, GripCanonicalLocalTransform, COND_InitialOnly);
}

void ACatFishingRodActor::OnRep_GripCanonicalLocalTransform()
{
	GripAnchor->SetRelativeTransform(GripCanonicalLocalTransform);
	UE_LOG(LogCatFishing, Display,
		TEXT("Event=fishing_rod_grip_received Rod=%s RodActorId=%s GripLocation=%s GripRotation=%s World=%s NetMode=%d Authority=false LocalRole=%d"),
		*GetName(), *PresentationState.RodActorId.ToString(EGuidFormats::DigitsWithHyphens),
		*GripCanonicalLocalTransform.GetLocation().ToCompactString(), *GripCanonicalLocalTransform.Rotator().ToCompactString(),
		*GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()));
}

bool ACatFishingRodActor::InitializeAuthoritativeIdentity(const FGuid InRodActorId, const FGuid InItemInstanceId,
	const FName InRodDefinitionId, const FName InRodSkinDefinitionId, APlayerState* InOwnerPlayerState,
	APlayerState* InOperatorPlayerState, const bool bInDeployed, const bool bInBroken)
{
	// 权威身份初始化流程：
	// 1. 先拒绝非服务器、空 ActorId、空 ItemInstanceId、空定义和空 Owner，避免收杆时找不到应归还的实例。
	// 2. 如果身份已经写过，只允许不可变身份相同的重放成功，防止同一个场景 Actor 被复用成另一根竿。
	// 3. 首次写入时构造完整 PresentationState，并立即分发表现变化和请求复制。
	// 只有服务器能设身份；Actor 身份、物品实例身份和定义身份缺一不可，否则收杆时无法证明该还哪一件物品。
	if (!HasAuthority() || !InRodActorId.IsValid() || !InItemInstanceId.IsValid()
		|| InRodDefinitionId.IsNone() || !InOwnerPlayerState)
	{
		return false;
	}
	if (bIdentityInitialized)
	{
		// 幂等保护：身份已初始化过后只比较不可变身份；皮肤、操作位和部署状态由后续权威写口单独提交。
		// 这样既能防止同一个 Actor 承载第二根竿，也不会让旧测试夹具的表现字段重放破坏正式身份。
		return PresentationState.RodActorId == InRodActorId
			&& PresentationState.ItemInstanceId == InItemInstanceId
			&& PresentationState.RodDefinitionId == InRodDefinitionId
			&& PresentationState.OwnerPlayerState == InOwnerPlayerState;
	}

	// 记录变更前状态用于表现事件对比（Previous -> Current）
	const FCatFishingRodPresentationState Previous = PresentationState;
	FCatFishingRodPresentationState Next;
	Next.RodActorId = InRodActorId;
	Next.RodActorRevision = 1; // 首次初始化即为 Revision 1，后续每次权威变更递增
	Next.ItemInstanceId = InItemInstanceId;
	Next.RodDefinitionId = InRodDefinitionId;
	Next.RodSkinDefinitionId = InRodSkinDefinitionId;
	Next.OwnerPlayerState = InOwnerPlayerState;
	Next.OperatorPlayerState = InOperatorPlayerState;
	if (InOperatorPlayerState)
	{
		Next.OperatorPlayerStates.Add(InOperatorPlayerState);
		Next.HolderPlayerState = InOperatorPlayerState;
		Next.PoseMode = ECatFishingRodPoseMode::Held;
	}
	Next.bDeployed = bInDeployed;
	Next.bBroken = bInBroken;
	PrepareOperatorMemberships(Next);
	PresentationState = Next;
	bIdentityInitialized = true;
	// 本地（服务器）立即广播表现变化事件；客户端则依赖下面的 OnRep 触发同样的事件
	QueueOrDispatchPresentationChanged(Previous, PresentationState);
	UpdateUnloadedGroupMotionFromAuthority(0.0);
	PublishCarrierConstraintToMovement();
	ForceNetUpdate(); // 身份初始化是一次性关键事件，强制立即复制，不等下个 tick 窗口
	return true;
}

void ACatFishingRodActor::PrepareOperatorMemberships(FCatFishingRodPresentationState& Next)
{
	Next.OperatorPlayerState = Next.OperatorPlayerStates.IsEmpty() ? nullptr : Next.OperatorPlayerStates[0];
	Next.HolderPlayerState = Next.OperatorPlayerState;
	Next.PoseMode = Next.HolderPlayerState ? ECatFishingRodPoseMode::Held : ECatFishingRodPoseMode::Grounded;
	const bool bRosterChanged = Next.OperatorPlayerStates != PresentationState.OperatorPlayerStates;
	Next.RosterVersion = PresentationState.RosterVersion;
	if (bRosterChanged)
	{
		if (++Next.RosterVersion == 0) ++Next.RosterVersion;
	}
	Next.ControlEpoch = PresentationState.ControlEpoch;
	if (Next.OperatorPlayerState != PresentationState.OperatorPlayerState)
	{
		if (++Next.ControlEpoch == 0) ++Next.ControlEpoch;
	}
	if (!bGroupAnchorInitialized && Next.HolderPlayerState && Next.HolderPlayerState->GetPawn())
	{
		GroupAnchorWorld = Next.HolderPlayerState->GetPawn()->GetActorLocation();
		GroupVelocity = Next.HolderPlayerState->GetPawn()->GetVelocity();
		bGroupAnchorInitialized = true;
	}
	Next.OperatorMemberships.Reset();
	for (APlayerState* Member : Next.OperatorPlayerStates)
	{
		const FCatFishingOperatorMembership* Existing = PresentationState.OperatorMemberships.FindByPredicate(
			[Member](const FCatFishingOperatorMembership& Entry) { return Entry.PlayerState == Member; });
		FCatFishingOperatorMembership Entry;
		if (Existing && Existing->Epoch != 0) Entry = *Existing;
		else
		{
			Entry.PlayerState = Member;
			Entry.Epoch = ++NextMembershipEpoch;
			if (Entry.Epoch == 0) Entry.Epoch = ++NextMembershipEpoch;
		}
		// 名单变化保留同一组根，以幸存者当下身体位置重定基。否则去掉误差不同的成员后，
		// 下一次求平均会让组根（进而鱼竿）跳到另一个位置，等同于隐式瞬移。
		if (bRosterChanged || !Existing)
		{
			Entry.FormationOffsetWorld = Member && Member->GetPawn()
				? Member->GetPawn()->GetActorLocation() - GroupAnchorWorld : FVector::ZeroVector;
		}
		Next.OperatorMemberships.Add(Entry);
	}
}

bool ACatFishingRodActor::ConfigureCanonicalAnchorsFromAuthority(const FTransform& InRodTip,
	const FTransform& InStand, const FTransform& InGrip)
{
	// 必须在身份初始化“之前”配置锚点（bIdentityInitialized 为真时拒绝），
	// 且三个 Transform 都不能是 NaN，否则后续世界坐标换算会污染整条竿的表现
	if (!HasAuthority() || bIdentityInitialized || InRodTip.ContainsNaN() || InStand.ContainsNaN() || InGrip.ContainsNaN())
	{
		return false;
	}
	// 保存规范的本地 Transform，供 GetXXXWorldTransform 与 Actor 世界 Transform 相乘得到世界坐标
	RodTipCanonicalLocalTransform = InRodTip;
	StandCanonicalLocalTransform = InStand;
	GripCanonicalLocalTransform = InGrip;
	// 同步应用到实际场景组件上，使编辑器/运行时可视化与权威数据一致
	RodTipAnchor->SetRelativeTransform(InRodTip);
	StandAnchor->SetRelativeTransform(InStand);
	RightStandAnchor->SetRelativeTransform(ResolveOperatorStandLocalTransform(0));
	LeftStandAnchor->SetRelativeTransform(ResolveOperatorStandLocalTransform(1));
	GripAnchor->SetRelativeTransform(InGrip);
	return true;
}

bool ACatFishingRodActor::CommitAuthoritativeMutation(const FCatFishingRodPresentationState& Next,
	const int64 ExpectedRevision)
{
	// 乐观并发控制：调用方必须带上它读到的旧 Revision，若与当前不一致说明状态已被其他写者改过，拒绝本次提交
	if (!HasAuthority() || !bIdentityInitialized || ExpectedRevision != PresentationState.RodActorRevision)
	{
		return false;
	}
	// 身份类字段（Id/DefinitionId/Owner）不可被这条“可变状态”写口覆盖，只保留传入 Next 里的可变部分
	FCatFishingRodPresentationState Committed = Next;
	Committed.RodActorId = PresentationState.RodActorId;
	Committed.ItemInstanceId = PresentationState.ItemInstanceId;
	Committed.RodDefinitionId = PresentationState.RodDefinitionId;
	Committed.OwnerPlayerState = PresentationState.OwnerPlayerState;
	PrepareOperatorMemberships(Committed);
	Committed.RodActorRevision = PresentationState.RodActorRevision + 1; // 每次成功提交 Revision 自增一
	const FCatFishingRodPresentationState Previous = PresentationState;
	const bool bWasGroupFight = CarrierConstraintState.bFightActive
		|| (!GroupMotionState.bUnloadedMovement && (GroupMotionState.bActive || GroupMotionState.bAwaitingSolve));
	const uint32 PreviousAimInputEpoch = CarrierConstraintState.AimInputEpoch;
	PresentationState = Committed;
	if (bWasGroupFight && bHeldAimInitialized && Previous.HolderPlayerState && PresentationState.HolderPlayerState
		&& Previous.HolderPlayerState != PresentationState.HolderPlayerState)
	{
		bAwaitingNewHolderAim = true;
	}
	if (PresentationState.RosterVersion != Previous.RosterVersion)
	{
		ClearCarrierMovementBinding();
		GroupMotionState = FCatFishingGroupMotionState{};
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=fishing_rod_roster_committed RodActorId=%s World=%s NetMode=%d Authority=%d LocalRole=%d RosterVersion=%u ControlEpoch=%u OperatorCount=%d GroupAnchor=%s Result=Committed"),
			*PresentationState.RodActorId.ToString(), *GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), HasAuthority(), static_cast<int32>(GetLocalRole()),
			PresentationState.RosterVersion, PresentationState.ControlEpoch, PresentationState.OperatorPlayerStates.Num(), *GroupAnchorWorld.ToCompactString());
	}
	if (PresentationState.HolderPlayerState != Previous.HolderPlayerState
		|| PresentationState.PoseMode != Previous.PoseMode
		|| (!PresentationState.bDeployed && Previous.bDeployed)
		|| (PresentationState.bBroken && !Previous.bBroken))
	{
		ResetAuthoritativeRotationEffort();
		AuthoritativeRodAngularVelocityRadiansPerSecond = FVector::ZeroVector;
		HeldAimInput.Reset();
		ClearCarrierMovementBinding();
		CarrierConstraintState = FCatFishingCarrierConstraintState{};
		GroupMotionState = FCatFishingGroupMotionState{};
	}
	if (PresentationState.PoseMode != ECatFishingRodPoseMode::Held)
	{
		CarrierConstraintState = FCatFishingCarrierConstraintState{};
		bHeldAimInitialized = false;
		AuthoritativeAimHolder.Reset();
		GroupMotionState = FCatFishingGroupMotionState{};
		bGroupAnchorInitialized = false;
	}
	if (bWasGroupFight && PresentationState.RosterVersion != Previous.RosterVersion
		&& PresentationState.PoseMode == ECatFishingRodPoseMode::Held && PresentationState.bDeployed && !PresentationState.bBroken)
	{
		GroupMotionState.AnchorWorld = GetGroupAnchorWorld();
		GroupMotionState.RosterVersion = PresentationState.RosterVersion;
		GroupMotionState.ControlEpoch = PresentationState.ControlEpoch;
		GroupMotionState.AimInputEpoch = PreviousAimInputEpoch;
		GroupMotionState.bAwaitingSolve = true;
	}
	if (PresentationState.PoseMode != ECatFishingRodPoseMode::Held || !PresentationState.bDeployed || PresentationState.bBroken)
	{
		bAwaitingNewHolderAim = false;
	}
	SetActorTickEnabled(PresentationState.PoseMode == ECatFishingRodPoseMode::Held);
	QueueOrDispatchPresentationChanged(Previous, PresentationState);
	UpdateUnloadedGroupMotionFromAuthority(0.0);
	PublishCarrierConstraintToMovement();
	ForceNetUpdate();
	return true;
}

bool ACatFishingRodActor::SetOperatorFromAuthority(APlayerState* InOperatorPlayerState, const int64 ExpectedRevision)
{
	// 旧接口语义保持为“整组替换”，避免老调用方只清快捷字段却留下数组里的幽灵占位。
	FCatFishingRodPresentationState Next = PresentationState;
	Next.OperatorPlayerStates.Reset();
	if (InOperatorPlayerState)
	{
		Next.OperatorPlayerStates.Add(InOperatorPlayerState);
	}
	return CommitAuthoritativeMutation(Next, ExpectedRevision);
}

bool ACatFishingRodActor::AddOperatorFromAuthority(APlayerState* InOperatorPlayerState,
	const int64 ExpectedRevision, int32& OutSlotIndex)
{
	OutSlotIndex = INDEX_NONE;
	const int32 FreeSlotIndex = GetFirstFreeOperatorSlotIndex();
	if (!InOperatorPlayerState || FreeSlotIndex == INDEX_NONE
		|| PresentationState.OperatorPlayerStates.Contains(InOperatorPlayerState)
		|| !PresentationState.bDeployed || PresentationState.bBroken)
	{
		return false;
	}
	FCatFishingRodPresentationState Next = PresentationState;
	Next.OperatorPlayerStates.Add(InOperatorPlayerState);
	if (!CommitAuthoritativeMutation(Next, ExpectedRevision))
	{
		return false;
	}
	OutSlotIndex = FreeSlotIndex;
	return true;
}

bool ACatFishingRodActor::RemoveOperatorFromAuthority(APlayerState* InOperatorPlayerState,
	const int64 ExpectedRevision, APlayerState*& OutPromotedPrimaryPlayerState)
{
	OutPromotedPrimaryPlayerState = nullptr;
	const int32 ExistingSlotIndex = GetOperatorSlotIndex(InOperatorPlayerState);
	if (ExistingSlotIndex == INDEX_NONE)
	{
		return false;
	}
	FCatFishingRodPresentationState Next = PresentationState;
	Next.OperatorPlayerStates.RemoveAt(ExistingSlotIndex);
	if (!CommitAuthoritativeMutation(Next, ExpectedRevision))
	{
		return false;
	}
	if (ExistingSlotIndex == 0 && !PresentationState.OperatorPlayerStates.IsEmpty())
	{
		OutPromotedPrimaryPlayerState = PresentationState.OperatorPlayerStates[0];
	}
	return true;
}

bool ACatFishingRodActor::SetRodSkinFromAuthority(const FName InRodSkinDefinitionId, const int64 ExpectedRevision)
{
	if (InRodSkinDefinitionId.IsNone()) return false; // 空皮肤 ID 视为非法调用，直接拒绝
	FCatFishingRodPresentationState Next = PresentationState;
	Next.RodSkinDefinitionId = InRodSkinDefinitionId;
	return CommitAuthoritativeMutation(Next, ExpectedRevision);
}

bool ACatFishingRodActor::SetBrokenFromAuthority(const bool bInBroken, const int64 ExpectedRevision)
{
	// 断竿是搏斗失败的惩罚结果之一，这里只负责把状态写进表现层，不涉及耐久扣减本身
	FCatFishingRodPresentationState Next = PresentationState;
	Next.bBroken = bInBroken;
	return CommitAuthoritativeMutation(Next, ExpectedRevision);
}

bool ACatFishingRodActor::SetDeployedFromAuthority(const bool bInDeployed, const int64 ExpectedRevision)
{
	// 插竿/收竿切换，驱动蓝图切换竿的摆放动画与碰撞表现
	FCatFishingRodPresentationState Next = PresentationState;
	Next.bDeployed = bInDeployed;
	return CommitAuthoritativeMutation(Next, ExpectedRevision);
}

const FCatFishingRodPresentationState& ACatFishingRodActor::GetPresentationState() const { return PresentationState; }

// 三个世界 Transform 都是“本地规范 Transform 叠乘 Actor 当前世界 Transform”，随 Actor 移动/旋转自动更新
FTransform ACatFishingRodActor::GetRodTipWorldTransform() const { return RodTipCanonicalLocalTransform * GetActorTransform(); }
FTransform ACatFishingRodActor::GetStandWorldTransform() const { return GetOperatorStandWorldTransform(0); }
FTransform ACatFishingRodActor::GetOperatorInteractionWorldTransform() const
{
	return StandCanonicalLocalTransform * GetActorTransform();
}
FTransform ACatFishingRodActor::GetGripWorldTransform() const { return GripCanonicalLocalTransform * GetActorTransform(); }

APawn* ACatFishingRodActor::GetHolderPawnFromAuthority() const
{
	return HasAuthority() && PresentationState.HolderPlayerState
		? PresentationState.HolderPlayerState->GetPawn() : nullptr;
}

FVector ACatFishingRodActor::GetAuthoritativeRodForwardVector() const
{
	return (GetRodTipWorldTransform().GetLocation() - GetGripWorldTransform().GetLocation())
		.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, GetActorForwardVector());
}

bool ACatFishingRodActor::SetCarrierConstraintFromAuthority(const FVector& PullDirection,
	const double PullAccelerationCentimetersPerSecondSquared,
	const double TargetPullSpeedCentimetersPerSecond,
	const double NormalizedTension, const double ConstraintErrorCentimeters,
	const bool bFightActive, const double MaximumFishTorqueStrengthMeters,
	const double CatTorqueCapacityStrengthMeters, const FVector& RodPullAxis,
	const double PullBrakingDecelerationCentimetersPerSecondSquared, const bool bUseContinuousTraction)
{
	FVector HorizontalDirection(PullDirection.X, PullDirection.Y, 0.0);
	const bool bHasDirection = !HorizontalDirection.ContainsNaN() && HorizontalDirection.Normalize();
	const bool bNeedsDirection = TargetPullSpeedCentimetersPerSecond > UE_DOUBLE_SMALL_NUMBER;
	const bool bValid = HasAuthority()
		&& PresentationState.PoseMode == ECatFishingRodPoseMode::Held
		&& (!bNeedsDirection || bHasDirection)
		&& FMath::IsFinite(PullAccelerationCentimetersPerSecondSquared)
		&& PullAccelerationCentimetersPerSecondSquared >= 0.0
		&& FMath::IsFinite(PullBrakingDecelerationCentimetersPerSecondSquared)
		&& PullBrakingDecelerationCentimetersPerSecondSquared >= 0.0
		&& (!bUseContinuousTraction || bFightActive)
		&& FMath::IsFinite(TargetPullSpeedCentimetersPerSecond)
		&& TargetPullSpeedCentimetersPerSecond >= 0.0
		&& FMath::IsFinite(NormalizedTension)
		&& FMath::IsFinite(ConstraintErrorCentimeters) && ConstraintErrorCentimeters >= 0.0
		&& FMath::IsFinite(MaximumFishTorqueStrengthMeters)
		&& MaximumFishTorqueStrengthMeters >= 0.0
		&& FMath::IsFinite(CatTorqueCapacityStrengthMeters)
		&& CatTorqueCapacityStrengthMeters >= 0.0
		&& !RodPullAxis.ContainsNaN() && !RodPullAxis.IsNearlyZero();
	if (!bValid)
	{
		return false;
	}

	FCatFishingCarrierConstraintState Next;
	Next.ConstraintHolderPlayerState = PresentationState.HolderPlayerState;
	Next.RosterVersion = PresentationState.RosterVersion;
	Next.ControlEpoch = PresentationState.ControlEpoch;
	Next.PullDirection = HorizontalDirection;
	Next.PullAccelerationCentimetersPerSecondSquared =
		static_cast<float>(PullAccelerationCentimetersPerSecondSquared);
	Next.PullBrakingDecelerationCentimetersPerSecondSquared = static_cast<float>(PullBrakingDecelerationCentimetersPerSecondSquared);
	Next.bUseContinuousTraction = bUseContinuousTraction;
	Next.TargetPullSpeedCentimetersPerSecond =
		static_cast<float>(TargetPullSpeedCentimetersPerSecond);
	Next.NormalizedTension = static_cast<float>(FMath::Clamp(NormalizedTension, 0.0, 1.0));
	Next.ConstraintErrorCentimeters = static_cast<float>(ConstraintErrorCentimeters);
	Next.bFightActive = bFightActive;
	// 停止记录保留最后输入域，拒绝同域旧组快照；下一场仍生成更大的新域。
	Next.AimInputEpoch = CarrierConstraintState.AimInputEpoch;
	if (bFightActive)
	{
		Next.AimInputEpoch = CarrierConstraintState.bFightActive
			&& CarrierConstraintState.ConstraintHolderPlayerState == Next.ConstraintHolderPlayerState
			? CarrierConstraintState.AimInputEpoch : ++NextAimInputEpoch;
	}
	Next.RodPullAxis = RodPullAxis.GetSafeNormal();
	Next.MaximumFishTorqueStrengthMeters = static_cast<float>(MaximumFishTorqueStrengthMeters);
	Next.CatTorqueCapacityStrengthMeters = static_cast<float>(CatTorqueCapacityStrengthMeters);
	Next.bActive = Next.TargetPullSpeedCentimetersPerSecond > KINDA_SMALL_NUMBER
		&& Next.PullAccelerationCentimetersPerSecondSquared > KINDA_SMALL_NUMBER;
	if (!Next.bFightActive)
	{
		bAwaitingNewHolderAim = false;
		GroupMotionState = FCatFishingGroupMotionState{};
		ClearCarrierMovementBinding();
		SmoothedRodFishPullStrengthMeters = FVector::ZeroVector;
		AuthoritativeRodAngularVelocityRadiansPerSecond = FVector::ZeroVector;
		HeldAimInput.Reset();
	}
	if (Next.bFightActive != CarrierConstraintState.bFightActive)
	{
		ResetAuthoritativeRotationEffort();
		AuthoritativeRodAngularVelocityRadiansPerSecond = FVector::ZeroVector;
	}
	if (Next.AimInputEpoch != CarrierConstraintState.AimInputEpoch) HeldAimInput.Reset();
	CarrierConstraintState = Next;
	LastConstraintUpdateWorldSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0;
	UpdateUnloadedGroupMotionFromAuthority(0.0);
	PublishCarrierConstraintToMovement();
	ForceNetUpdate();
	return true;
}

void ACatFishingRodActor::ClearCarrierConstraintFromAuthority()
{
	if (!HasAuthority())
	{
		return;
	}
	// 单场结束只移除鱼载荷，成员生命周期由离竿/收竿/销毁负责。
	const uint32 StoppedAimInputEpoch = FMath::Max(CarrierConstraintState.AimInputEpoch, GroupMotionState.AimInputEpoch);
	GroupMotionState = FCatFishingGroupMotionState{};
	SmoothedRodFishPullStrengthMeters = FVector::ZeroVector;
	AuthoritativeRodAngularVelocityRadiansPerSecond = FVector::ZeroVector;
	HeldAimInput.Reset();
	bAwaitingNewHolderAim = false;
	ResetAuthoritativeRotationEffort();
	CarrierConstraintState = FCatFishingCarrierConstraintState{};
	CarrierConstraintState.ConstraintHolderPlayerState = PresentationState.HolderPlayerState;
	CarrierConstraintState.RosterVersion = PresentationState.RosterVersion;
	CarrierConstraintState.ControlEpoch = PresentationState.ControlEpoch;
	CarrierConstraintState.AimInputEpoch = StoppedAimInputEpoch;
	NextRodRotationResistanceDiagnosticWorldSeconds = 0.0;
	bLastRodTorqueBalanced = false;
	UpdateUnloadedGroupMotionFromAuthority(0.0);
	PublishCarrierConstraintToMovement();
	ForceNetUpdate();
}

void ACatFishingRodActor::OnRep_CarrierConstraintState()
{
	PublishCarrierConstraintToMovement();
	UWorld* World = GetWorld();
	LastConstraintUpdateWorldSeconds = World ? World->GetTimeSeconds() : -1.0;
	if (World && (bLastReceivedFightActive != CarrierConstraintState.bFightActive
		|| (CarrierConstraintState.bFightActive && LastConstraintUpdateWorldSeconds >= NextCarrierReceiptDiagnosticWorldSeconds)))
	{
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=fishing_carrier_constraint_received RodActorId=%s Frame=%llu WorldTime=%.6f FightActive=%s ContinuousTraction=%s "
				"MovementBound=%s SnapshotHolder=%s CurrentHolder=%s AccelerationCmS2=%.3f BrakingDecelerationCmS2=%.3f "
				"PullAxis=%s FishTorque=%.3f CatTorque=%.3f ObservedRotation=%s GripLocation=%s World=%s NetMode=%d Authority=%s LocalRole=%d RosterVersion=%u ControlEpoch=%u GroupRosterVersion=%u GroupActive=%d"),
			*PresentationState.RodActorId.ToString(EGuidFormats::DigitsWithHyphens), GFrameCounter, LastConstraintUpdateWorldSeconds,
			CarrierConstraintState.bFightActive ? TEXT("true") : TEXT("false"),
			CarrierConstraintState.bUseContinuousTraction ? TEXT("true") : TEXT("false"),
			!GroupMovements.IsEmpty() ? TEXT("true") : TEXT("false"),
			*GetNameSafe(CarrierConstraintState.ConstraintHolderPlayerState), *GetNameSafe(PresentationState.HolderPlayerState),
			CarrierConstraintState.PullAccelerationCentimetersPerSecondSquared, CarrierConstraintState.PullBrakingDecelerationCentimetersPerSecondSquared,
			*FVector(CarrierConstraintState.RodPullAxis).ToCompactString(), CarrierConstraintState.MaximumFishTorqueStrengthMeters,
			CarrierConstraintState.CatTorqueCapacityStrengthMeters, *GetActorRotation().ToCompactString(),
			*GetGripWorldTransform().GetLocation().ToCompactString(), *GetNameSafe(World), static_cast<int32>(World->GetNetMode()),
			HasAuthority() ? TEXT("true") : TEXT("false"), static_cast<int32>(GetLocalRole()),
			PresentationState.RosterVersion, PresentationState.ControlEpoch, GroupMotionState.RosterVersion, GroupMotionState.bActive);
		NextCarrierReceiptDiagnosticWorldSeconds = LastConstraintUpdateWorldSeconds + CatFishingMotionDiagnostics::SampleIntervalSeconds();
	}
	bLastReceivedFightActive = CarrierConstraintState.bFightActive;
}

void ACatFishingRodActor::ClearCarrierMovementBinding()
{
	for (const auto& WeakMovement : GroupMovements)
	{
		if (UCatCharacterMovementComponent* MemberMovement = WeakMovement.Get())
		{
			MemberMovement->ClearExternalTraction(this);
			PrimaryActorTick.RemovePrerequisite(MemberMovement, MemberMovement->PrimaryComponentTick);
		}
	}
	GroupMovements.Reset();
}

void ACatFishingRodActor::ResetAuthoritativeRotationEffort()
{
	const uint64 NextEpoch = AuthoritativeRotationEffort.Epoch + 1;
	AuthoritativeRotationEffort = FCatFishingRodRotationEffortSnapshot{};
	AuthoritativeRotationEffort.Epoch = NextEpoch;
}

void ACatFishingRodActor::PublishCarrierConstraintToMovement()
{
	const bool bCurrentConstraint = CarrierConstraintState.RosterVersion == PresentationState.RosterVersion
		&& CarrierConstraintState.ControlEpoch == PresentationState.ControlEpoch
		&& CarrierConstraintState.ConstraintHolderPlayerState == PresentationState.HolderPlayerState;
	const bool bCanBindMembers = PresentationState.PoseMode == ECatFishingRodPoseMode::Held
		&& PresentationState.bDeployed && !PresentationState.bBroken && !PresentationState.OperatorPlayerStates.IsEmpty();
	// 名单先到也立即接管个人移动；鱼载荷和无载快照必须各自在相同域内完整匹配。
	const bool bCurrentGroupMotion = bCanBindMembers && GroupMotionState.bActive
		&& (GroupMotionState.bUnloadedMovement != CarrierConstraintState.bFightActive)
		&& GroupMotionState.RosterVersion != 0 && GroupMotionState.RosterVersion == PresentationState.RosterVersion
		&& GroupMotionState.ControlEpoch == PresentationState.ControlEpoch
		&& GroupMotionState.AimInputEpoch == CarrierConstraintState.AimInputEpoch
		&& bCurrentConstraint;
	if (bCanBindMembers)
	{
		TArray<AActor*> CollisionPeers;
		for (APlayerState* Player : PresentationState.OperatorPlayerStates)
			if (Player && Player->GetPawn()) CollisionPeers.Add(Player->GetPawn());
		TArray<TWeakObjectPtr<UCatCharacterMovementComponent>> CurrentMovements;
		for (const FCatFishingOperatorMembership& Entry : PresentationState.OperatorMemberships)
		{
			ACharacter* Member = Entry.PlayerState ? Cast<ACharacter>(Entry.PlayerState->GetPawn()) : nullptr;
			UCatCharacterMovementComponent* MemberMovement = Member ? Cast<UCatCharacterMovementComponent>(Member->GetCharacterMovement()) : nullptr;
			if (!MemberMovement || Entry.Epoch == 0 || (!HasAuthority() && !Member->IsLocallyControlled())) continue;
			FCatExternalTractionInput Input;
			Input.SourceId = PresentationState.RodActorId;
			Input.RosterVersion = PresentationState.RosterVersion;
			Input.ControlEpoch = PresentationState.ControlEpoch;
			Input.MembershipEpoch = Entry.Epoch;
			Input.AimInputEpoch = bCurrentGroupMotion ? GroupMotionState.AimInputEpoch
				: FMath::Max(CarrierConstraintState.AimInputEpoch, GroupMotionState.AimInputEpoch);
			Input.bGroupDriven = true;
			Input.bWaitingForGroupSolve = !bCurrentGroupMotion;
			if (bCurrentGroupMotion)
			{
				Input.bUnloadedMovement = GroupMotionState.bUnloadedMovement;
				Input.GroupUnloadedVelocity = GroupMotionState.UnloadedVelocity;
				if (!Input.bUnloadedMovement)
				{
					Input.Direction = FVector(CarrierConstraintState.PullDirection).GetSafeNormal2D(UE_SMALL_NUMBER, FVector::ForwardVector);
					Input.AccelerationCentimetersPerSecondSquared = CarrierConstraintState.PullAccelerationCentimetersPerSecondSquared;
					Input.BrakingDecelerationCentimetersPerSecondSquared = CarrierConstraintState.PullBrakingDecelerationCentimetersPerSecondSquared;
					Input.SpeedLimitCentimetersPerSecond = FMath::Max(static_cast<double>(CarrierConstraintState.TargetPullSpeedCentimetersPerSecond), GroupMotionState.DesiredVelocity.Size());
					Input.bActive = CarrierConstraintState.bUseContinuousTraction || CarrierConstraintState.bActive;
				}
				Input.GroupDesiredVelocity = GroupMotionState.DesiredVelocity;
				Input.GroupLateralAcceleration = GroupMotionState.LateralAcceleration;
				const FVector Anchor = HasAuthority() ? GetGroupAnchorWorld() : GroupMotionState.AnchorWorld;
				FVector Error = Anchor + Entry.FormationOffsetWorld - Member->GetActorLocation();
				Error.Z = 0.0;
				Input.FormationCorrectionVelocity = (Error * 3.0).GetClampedToMaxSize(60.0);
			}
			const FCatExternalTractionInput Before = MemberMovement->GetExternalTraction();
			MemberMovement->SetExternalTraction(this, Input);
			if (MemberMovement->GetExternalTraction().SourceId != Input.SourceId) continue;
			MemberMovement->SetFishingGroupCollisionPeers(this, CollisionPeers);
			CurrentMovements.Add(MemberMovement);
			PrimaryActorTick.AddPrerequisite(MemberMovement, MemberMovement->PrimaryComponentTick);
			if (!Before.bGroupDriven || Before.bWaitingForGroupSolve != Input.bWaitingForGroupSolve
				|| Before.bUnloadedMovement != Input.bUnloadedMovement || Before.RosterVersion != Input.RosterVersion)
			{
				UE_LOG(LogCatFishing, Log,
					TEXT("Event=fishing_group_movement_binding RodActorId=%s PlayerId=%d World=%s NetMode=%d Authority=%d LocalRole=%d RosterVersion=%u ControlEpoch=%u MembershipEpoch=%u Waiting=%d Unloaded=%d Result=Bound"),
					*Input.SourceId.ToString(), Entry.PlayerState->GetPlayerId(), *GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), HasAuthority(), static_cast<int32>(GetLocalRole()),
					Input.RosterVersion, Input.ControlEpoch, Input.MembershipEpoch, Input.bWaitingForGroupSolve, Input.bUnloadedMovement);
			}
		}
		for (const auto& Previous : GroupMovements)
		{
			if (!CurrentMovements.Contains(Previous))
			{
				if (UCatCharacterMovementComponent* Removed = Previous.Get())
				{
					Removed->ClearExternalTraction(this);
					PrimaryActorTick.RemovePrerequisite(Removed, Removed->PrimaryComponentTick);
				}
			}
		}
		GroupMovements = MoveTemp(CurrentMovements);
		return;
	}
	ClearCarrierMovementBinding();
}

bool ACatFishingRodActor::CanRebaseHeldAimFromAuthority(APlayerState* Player,
	const FCatFishingRodAimSample& Sample) const
{
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	return HasAuthority() && Player && Player == PresentationState.HolderPlayerState
		&& PresentationState.PoseMode == ECatFishingRodPoseMode::Held
		&& PresentationState.bDeployed && !PresentationState.bBroken
		&& CarrierConstraintState.bFightActive && bHeldAimInitialized
		&& GetHolderPawnFromAuthority()
		&& AuthoritativeAimHolder.Get() == GetHolderPawnFromAuthority()
		&& Sample.IsValid() && Sample.Sequence > HeldAimInput.GetLastRebaseSequence()
		// 首帧约束复制可能尚未抵达拥有客户端；没有客户端域时按本次已授权的 Session 补建。
		// 已明确携带旧域的输入则必须拒绝，不能把上一场右键迁入当前搏斗。
		&& (!Sample.RodActorId.IsValid() || Sample.RodActorId == PresentationState.RodActorId)
		&& (Sample.InputEpoch == 0 || Sample.InputEpoch == CarrierConstraintState.AimInputEpoch)
		&& (!bAwaitingNewHolderAim || (Sample.RodActorId == PresentationState.RodActorId
			&& Sample.InputEpoch != 0 && Sample.InputEpoch == CarrierConstraintState.AimInputEpoch))
		&& FMath::IsFinite(Settings->HeldRodMinimumPitchDegrees)
		&& FMath::IsFinite(Settings->HeldRodMaximumPitchDegrees)
		&& Settings->HeldRodMinimumPitchDegrees <= Settings->HeldRodMaximumPitchDegrees;
}

void ACatFishingRodActor::RebaseHeldAimFromAuthority(APlayerState* Player,
	const FCatFishingRodAimSample& Sample, const FGuid RequestId, const int64 InputSequence)
{
	if (!CanRebaseHeldAimFromAuthority(Player, Sample)) return;
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	const FRotator PreviousRequested = HeldAimInput.IsRebased() ? HeldAimInput.GetRequestedAim()
		: AuthoritativeHeldAimRotation;
	if (!HeldAimInput.Rebase(Sample, AuthoritativeHeldAimRotation,
		Settings->HeldRodMinimumPitchDegrees, Settings->HeldRodMaximumPitchDegrees, GetWorld()->GetTimeSeconds())) return;
	bAwaitingNewHolderAim = false;
	UE_LOG(LogCatFishing, Display,
		TEXT("Event=fishing_rod_aim_rebased RequestId=%s InputSequence=%lld RodActorId=%s AimInputEpoch=%u "
			"AimSequence=%lld LatestAimSequence=%lld PreviousRequested=%s ActualAim=%s RequestedAim=%s "
			"PlayerId=%d World=%s NetMode=%d Authority=true LocalRole=%d"),
		*RequestId.ToString(), InputSequence, *PresentationState.RodActorId.ToString(), CarrierConstraintState.AimInputEpoch,
		Sample.Sequence, HeldAimInput.GetLastSequence(), *PreviousRequested.ToCompactString(),
		*AuthoritativeHeldAimRotation.ToCompactString(), *HeldAimInput.GetRequestedAim().ToCompactString(),
		Player->GetPlayerId(), *GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()));
}

bool ACatFishingRodActor::AcceptHeldAimSampleFromAuthority(APlayerState* Player,
	const FCatFishingRodAimSample& Sample)
{
	if (!HasAuthority() || !Player || Player != PresentationState.HolderPlayerState
		|| !PresentationState.bDeployed || PresentationState.bBroken
		|| PresentationState.PoseMode != ECatFishingRodPoseMode::Held || !CarrierConstraintState.bFightActive
		|| Sample.RodActorId != PresentationState.RodActorId || Sample.InputEpoch == 0
		|| Sample.InputEpoch != CarrierConstraintState.AimInputEpoch) return false;
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	if (bAwaitingNewHolderAim)
	{
		if (!HeldAimInput.AcceptSample(Sample, AuthoritativeHeldAimRotation,
			Settings->HeldRodMinimumPitchDegrees, Settings->HeldRodMaximumPitchDegrees, GetWorld()->GetTimeSeconds())) return false;
		bAwaitingNewHolderAim = false;
		AuthoritativeAimHolder = GetHolderPawnFromAuthority();
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=fishing_rod_handoff_aim_accepted RodActorId=%s PlayerId=%d AimInputEpoch=%u AimSequence=%lld ControlEpoch=%u ActualAim=%s World=%s NetMode=%d Authority=%d LocalRole=%d Result=Rebased"),
			*PresentationState.RodActorId.ToString(), Player->GetPlayerId(), CarrierConstraintState.AimInputEpoch, Sample.Sequence,
			PresentationState.ControlEpoch, *AuthoritativeHeldAimRotation.ToCompactString(), *GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), HasAuthority(), static_cast<int32>(GetLocalRole()));
		return true;
	}
	return HeldAimInput.AcceptSample(Sample, AuthoritativeHeldAimRotation,
		Settings->HeldRodMinimumPitchDegrees, Settings->HeldRodMaximumPitchDegrees, GetWorld()->GetTimeSeconds());
}

bool ACatFishingRodActor::GetRotationPredictionFromAuthority(const double DeltaSeconds, FCatFishingRodRotationPrediction& OutPrediction) const
{
	OutPrediction = {};
	APawn* HolderPawn = GetHolderPawnFromAuthority();
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	if (!HasAuthority() || PresentationState.PoseMode != ECatFishingRodPoseMode::Held
		|| !HolderPawn || !Settings
		|| !FMath::IsFinite(Settings->HeldRodMinimumPitchDegrees)
		|| !FMath::IsFinite(Settings->HeldRodMaximumPitchDegrees)
		|| Settings->HeldRodMinimumPitchDegrees > Settings->HeldRodMaximumPitchDegrees
		|| !FMath::IsFinite(Settings->HeldRodMaximumAngularSpeedDegreesPerSecond)
		|| Settings->HeldRodMaximumAngularSpeedDegreesPerSecond <= 0.0
		|| !FMath::IsFinite(Settings->HeldRodAngularResistanceResponseSeconds)
		|| Settings->HeldRodAngularResistanceResponseSeconds <= 0.0
		|| !FMath::IsFinite(Settings->HeldRodAngularInertiaSeconds)
		|| Settings->HeldRodAngularInertiaSeconds <= 0.0
		|| !FMath::IsFinite(Settings->HeldRodFishPullSmoothingSeconds)
		|| Settings->HeldRodFishPullSmoothingSeconds <= 0.0
		|| !FMath::IsFinite(Settings->HeldRodLoadedAngularDampingRatio)
		|| Settings->HeldRodLoadedAngularDampingRatio < 0.0
		|| Settings->HeldRodGripOffsetCentimeters.ContainsNaN())
	{
		return false;
	}

	FRotator RequestedAimRotation = HolderPawn->GetController()
		? HolderPawn->GetController()->GetControlRotation() : HolderPawn->GetActorRotation();
	const bool bMouseDriveActive = CarrierConstraintState.bFightActive && !bAwaitingNewHolderAim
		&& AuthoritativeAimHolder.Get() == HolderPawn && HeldAimInput.IsMouseActive(GetWorld()->GetTimeSeconds());
	if (bAwaitingNewHolderAim && bHeldAimInitialized)
	{
		RequestedAimRotation = AuthoritativeHeldAimRotation;
	}
	else if (CarrierConstraintState.bFightActive && bHeldAimInitialized)
	{
		// 搏斗中的 ControlRotation 仅服务视角；停手/超时后不得恢复其积压目标。
		RequestedAimRotation = bMouseDriveActive ? HeldAimInput.GetRequestedAim() : AuthoritativeHeldAimRotation;
	}
	RequestedAimRotation.Pitch = FMath::ClampAngle(RequestedAimRotation.Pitch,
		Settings->HeldRodMinimumPitchDegrees, Settings->HeldRodMaximumPitchDegrees);
	RequestedAimRotation.Roll = 0.0;
	FCatFishingRodRotationInput& RotationInput = OutPrediction.Input;
	RotationInput.CurrentAim = AuthoritativeHeldAimRotation;
	RotationInput.RequestedAim = RequestedAimRotation;
	RotationInput.bCatDriveActive = bMouseDriveActive;
	RotationInput.PullAxis = CarrierConstraintState.RodPullAxis;
	RotationInput.PreviousSmoothedFishPullStrengthMeters = SmoothedRodFishPullStrengthMeters;
	RotationInput.PreviousAngularVelocityRadiansPerSecond = AuthoritativeRodAngularVelocityRadiansPerSecond;
	RotationInput.CatTorqueCapacity = CarrierConstraintState.CatTorqueCapacityStrengthMeters;
	RotationInput.MaximumFishTorque = CarrierConstraintState.MaximumFishTorqueStrengthMeters;
	RotationInput.MaximumAngularSpeedDegreesPerSecond = Settings->HeldRodMaximumAngularSpeedDegreesPerSecond;
	RotationInput.ResponseSeconds = Settings->HeldRodAngularResistanceResponseSeconds;
	RotationInput.AngularInertiaSeconds = Settings->HeldRodAngularInertiaSeconds;
	RotationInput.FishPullSmoothingSeconds = Settings->HeldRodFishPullSmoothingSeconds;
	RotationInput.LoadedAngularDampingRatio = Settings->HeldRodLoadedAngularDampingRatio;
	RotationInput.MinimumPitchDegrees = Settings->HeldRodMinimumPitchDegrees;
	RotationInput.MaximumPitchDegrees = Settings->HeldRodMaximumPitchDegrees;
	RotationInput.DeltaSeconds = DeltaSeconds;
	OutPrediction.HolderWorldPosition = GetGroupAnchorWorld();
	OutPrediction.TipOffsetInAimSpace = Settings->HeldRodGripOffsetCentimeters
		+ GripCanonicalLocalTransform.InverseTransformPosition(RodTipCanonicalLocalTransform.GetLocation());
	OutPrediction.bHoldActualAim = bAwaitingNewHolderAim;
	OutPrediction.bValid = true;
	return true;
}

bool ACatFishingRodActor::RefreshHeldTransformFromAuthority(const double DeltaSeconds)
{
	// 显式刷新与 Tick 共用入口，服务/测试直接请求刷新时也必须读取碰撞后的成员位置。
	RefreshGroupAnchorFromAuthority();
	if (HasAuthority() && HeldAimInput.ExpireInput(GetWorld()->GetTimeSeconds(), AuthoritativeHeldAimRotation))
	{
		UE_LOG(LogCatFishing, Display,
			TEXT("Event=fishing_rod_mouse_drive_timeout RodActorId=%s AimInputEpoch=%u AimSequence=%lld TimeoutSeconds=%.3f PlayerId=%d World=%s NetMode=%d Authority=true LocalRole=%d Result=ActiveTorqueStopped"),
			*PresentationState.RodActorId.ToString(), CarrierConstraintState.AimInputEpoch, HeldAimInput.GetLastSequence(),
			FCatFishingRodAimState::InputTimeoutSeconds, PresentationState.HolderPlayerState ? PresentationState.HolderPlayerState->GetPlayerId() : INDEX_NONE,
			*GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()));
	}
	FCatFishingRodRotationPrediction Prediction;
	if (!GetRotationPredictionFromAuthority(DeltaSeconds, Prediction)) return false;
	APawn* HolderPawn = GetHolderPawnFromAuthority();
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();

	const FVector PreviousTip = GetRodTipWorldTransform().GetLocation();
	const FRotator RequestedAimRotation = Prediction.Input.RequestedAim;
	const FRotator PreviousAimForDiagnostic = AuthoritativeHeldAimRotation;
	FCatFishingRodRotationResult RotationStep;
	const bool bNewHolder = AuthoritativeAimHolder.Get() != HolderPawn;
	if (bAwaitingNewHolderAim && bHeldAimInitialized)
	{
		if (bNewHolder) ResetAuthoritativeRotationEffort();
		AuthoritativeAimHolder = HolderPawn;
		SmoothedRodFishPullStrengthMeters = FVector::ZeroVector;
		AuthoritativeRodAngularVelocityRadiansPerSecond = FVector::ZeroVector;
	}
	else if (!bHeldAimInitialized || bNewHolder || !CarrierConstraintState.bFightActive)
	{
		if (bNewHolder)
		{
			ResetAuthoritativeRotationEffort();
		}
		HeldAimInput.Reset();
		if (!bNewHolder || !bHeldAimInitialized) AuthoritativeHeldAimRotation = RequestedAimRotation;
		SmoothedRodFishPullStrengthMeters = FVector::ZeroVector;
		AuthoritativeRodAngularVelocityRadiansPerSecond = FVector::ZeroVector;
		bHeldAimInitialized = true;
		AuthoritativeAimHolder = HolderPawn;
	}
	else if (FMath::IsFinite(DeltaSeconds) && DeltaSeconds > UE_DOUBLE_SMALL_NUMBER)
	{
		const FCatFishingRodRotationInput& RotationInput = Prediction.Input;
		RotationStep = FCatFishingRodResistanceModel::StepRotation(RotationInput);
		if (!RotationStep.bSucceeded) return false;
		AuthoritativeRotationEffort.ExertionSquaredSeconds += RotationStep.CatExertionSquaredSeconds;
		AuthoritativeRotationEffort.PositiveWorkRadians += RotationStep.CatPositiveWorkRadians;
		AuthoritativeRotationEffort.IntegratedSeconds += RotationStep.IntegratedSeconds;
		AuthoritativeHeldAimRotation = RotationStep.ActualAim;
		SmoothedRodFishPullStrengthMeters = RotationStep.SmoothedFishPullStrengthMeters;
		// 俯仰触界后的方向和角速度由同一纯模型共同裁决，预测也消费这套结果。
		AuthoritativeRodAngularVelocityRadiansPerSecond = RotationStep.AngularVelocityRadiansPerSecond;
	}
	const FRotator AimRotation = AuthoritativeHeldAimRotation;
	const FVector GripLocation = GetGroupAnchorWorld()
		+ AimRotation.RotateVector(Settings->HeldRodGripOffsetCentimeters);
	const FTransform DesiredGripTransform(AimRotation.Quaternion(), GripLocation);
	const FTransform DesiredActorTransform = GripCanonicalLocalTransform.Inverse() * DesiredGripTransform;
	SetActorTransform(DesiredActorTransform, false, nullptr, ETeleportType::TeleportPhysics);
	const FVector CurrentTip = GetRodTipWorldTransform().GetLocation();
	AuthoritativeRodTipVelocity = FMath::IsFinite(DeltaSeconds) && DeltaSeconds > UE_DOUBLE_SMALL_NUMBER
		? (CurrentTip - PreviousTip) / DeltaSeconds : FVector::ZeroVector;
	AuthoritativeHolderVelocity = GetGroupVelocity();
	// 仅诊断观察，不参与下一帧是否允许转动的裁决。
	const bool bTorqueBalanced = RotationStep.bSucceeded
		&& Prediction.Input.bCatDriveActive && RotationStep.AngularSpeedDegreesPerSecond < 0.1
		&& !AuthoritativeHeldAimRotation.Equals(RequestedAimRotation, 1.0);
	UWorld* World = GetWorld();
	const double WorldSeconds = World ? World->GetTimeSeconds() : 0.0;
	if (World && CarrierConstraintState.bFightActive
		&& (bTorqueBalanced != bLastRodTorqueBalanced
			|| WorldSeconds >= NextRodRotationResistanceDiagnosticWorldSeconds))
	{
		UE_LOG(LogCatFishing, Display,
			TEXT("Event=fishing_rod_rotation_resistance_sample RodActorId=%s RequestedYaw=%.2f ActualYaw=%.2f "
				"RequestedPitch=%.2f ActualPitch=%.2f AngularSpeed=%.3f NetTorque=%s "
				"MaximumFishTorque=%.3f CatTorqueCapacity=%.3f TorqueBalanced=%s "
				"AimRebased=%s AimInputEpoch=%u AimSequence=%lld MouseDriveActive=%s "
				"PullAxis=%s AppliedFishPull=%s FishPullSmoothingSeconds=%.3f LoadedAngularDampingRatio=%.3f AppliedAngularDampingMultiplier=%.3f "
				"AngularVelocityRadS=%s AngularAccelerationRadS2=%s AngularInertiaSeconds=%.3f PitchLimited=%s "
				"RotationEffortEpoch=%llu RotationExertionSquaredSeconds=%.3f RotationPositiveWorkRadians=%.3f RotationIntegratedSeconds=%.3f "
				"HolderPlayerId=%d Holder=%s World=%s NetMode=%d Authority=true LocalRole=%d "
				"Frame=%llu WorldTime=%.6f DeltaSeconds=%.6f DeltaYaw=%.5f DeltaPitch=%.5f "
				"HolderLocation=%s HolderVelocityCmS=%s RodTip=%s RodTipVelocityCmS=%s ConstraintAgeSeconds=%.6f Integrated=%s"),
			*PresentationState.RodActorId.ToString(EGuidFormats::DigitsWithHyphens),
			RequestedAimRotation.Yaw, AimRotation.Yaw, RequestedAimRotation.Pitch, AimRotation.Pitch,
			RotationStep.AngularSpeedDegreesPerSecond, *RotationStep.NetTorque.ToCompactString(),
			CarrierConstraintState.MaximumFishTorqueStrengthMeters,
			CarrierConstraintState.CatTorqueCapacityStrengthMeters,
			bTorqueBalanced ? TEXT("true") : TEXT("false"),
			HeldAimInput.IsRebased() ? TEXT("true") : TEXT("false"), CarrierConstraintState.AimInputEpoch, HeldAimInput.GetLastSequence(),
			Prediction.Input.bCatDriveActive ? TEXT("true") : TEXT("false"),
			*FVector(CarrierConstraintState.RodPullAxis).ToCompactString(),
			*SmoothedRodFishPullStrengthMeters.ToCompactString(), Settings->HeldRodFishPullSmoothingSeconds,
			Settings->HeldRodLoadedAngularDampingRatio, RotationStep.AppliedAngularDampingMultiplier,
			*AuthoritativeRodAngularVelocityRadiansPerSecond.ToCompactString(),
			*RotationStep.AngularAccelerationRadiansPerSecondSquared.ToCompactString(), Settings->HeldRodAngularInertiaSeconds,
			RotationStep.bHitPitchLimit ? TEXT("true") : TEXT("false"),
			AuthoritativeRotationEffort.Epoch, AuthoritativeRotationEffort.ExertionSquaredSeconds,
			AuthoritativeRotationEffort.PositiveWorkRadians, AuthoritativeRotationEffort.IntegratedSeconds,
			PresentationState.HolderPlayerState->GetPlayerId(),
			*GetNameSafe(HolderPawn), *GetNameSafe(World), static_cast<int32>(World->GetNetMode()),
			static_cast<int32>(GetLocalRole()), GFrameCounter, WorldSeconds, DeltaSeconds,
			FMath::FindDeltaAngleDegrees(PreviousAimForDiagnostic.Yaw, AimRotation.Yaw),
			FMath::FindDeltaAngleDegrees(PreviousAimForDiagnostic.Pitch, AimRotation.Pitch),
			*HolderPawn->GetActorLocation().ToCompactString(), *AuthoritativeHolderVelocity.ToCompactString(),
			*CurrentTip.ToCompactString(), *AuthoritativeRodTipVelocity.ToCompactString(),
			LastConstraintUpdateWorldSeconds >= 0.0 ? WorldSeconds - LastConstraintUpdateWorldSeconds : -1.0,
			RotationStep.bSucceeded ? TEXT("true") : TEXT("false"));
		NextRodRotationResistanceDiagnosticWorldSeconds = WorldSeconds + CatFishingMotionDiagnostics::SampleIntervalSeconds();
		bLastRodTorqueBalanced = bTorqueBalanced;
	}
	return !CurrentTip.ContainsNaN() && !AuthoritativeRodTipVelocity.ContainsNaN()
		&& !AuthoritativeHolderVelocity.ContainsNaN();
}

bool ACatFishingRodActor::PlaceOnGroundFromAuthority(const FTransform& GroundTransform)
{
	if (!HasAuthority() || PresentationState.PoseMode != ECatFishingRodPoseMode::Grounded
		|| GroundTransform.ContainsNaN())
	{
		return false;
	}
	SetActorTransform(GroundTransform, false, nullptr, ETeleportType::TeleportPhysics);
	AuthoritativeRodTipVelocity = FVector::ZeroVector;
	AuthoritativeHolderVelocity = FVector::ZeroVector;
	SmoothedRodFishPullStrengthMeters = FVector::ZeroVector;
	AuthoritativeRodAngularVelocityRadiansPerSecond = FVector::ZeroVector;
	bHeldAimInitialized = false;
	HeldAimInput.Reset();
	AuthoritativeAimHolder.Reset();
	ResetAuthoritativeRotationEffort();
	CarrierConstraintState = FCatFishingCarrierConstraintState{};
	ClearCarrierMovementBinding();
	SetActorTickEnabled(false);
	ForceNetUpdate();
	return true;
}

uint32 ACatFishingRodActor::GetOperatorMembershipEpoch(APlayerState* PlayerState) const
{
	const FCatFishingOperatorMembership* Member = PresentationState.OperatorMemberships.FindByPredicate(
		[PlayerState](const FCatFishingOperatorMembership& Entry) { return Entry.PlayerState == PlayerState; });
	return Member ? Member->Epoch : 0;
}

FVector ACatFishingRodActor::GetGroupAnchorWorld() const
{
	const APawn* Holder = GetHolderPawnFromAuthority();
	return bGroupAnchorInitialized ? GroupAnchorWorld : Holder ? Holder->GetActorLocation() : GetActorLocation();
}

FVector ACatFishingRodActor::GetGroupVelocity() const
{
	const APawn* Holder = GetHolderPawnFromAuthority();
	return bGroupAnchorInitialized ? GroupVelocity : Holder ? Holder->GetVelocity() : FVector::ZeroVector;
}

void ACatFishingRodActor::RefreshGroupAnchorFromAuthority()
{
	if (!HasAuthority()) return;
	FVector Position = FVector::ZeroVector, Velocity = FVector::ZeroVector;
	int32 Count = 0;
	for (const auto& Entry : PresentationState.OperatorMemberships)
	{
		const APawn* Member = Entry.PlayerState ? Entry.PlayerState->GetPawn() : nullptr;
		if (!Member) continue;
		Position += Member->GetActorLocation() - Entry.FormationOffsetWorld;
		Velocity += Member->GetVelocity();
		++Count;
	}
	if (Count > 0)
	{
		GroupAnchorWorld = Position / Count;
		GroupVelocity = Velocity / Count;
		bGroupAnchorInitialized = true;
	}
}

void ACatFishingRodActor::UpdateUnloadedGroupMotionFromAuthority(const double DeltaSeconds)
{
	if (!HasAuthority() || IsActorBeingDestroyed() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.0
		|| PresentationState.PoseMode != ECatFishingRodPoseMode::Held || !PresentationState.bDeployed
		|| PresentationState.bBroken || PresentationState.OperatorMemberships.IsEmpty()
		|| CarrierConstraintState.bFightActive || GroupMotionState.bAwaitingSolve) return;

	// 无载携竿不依赖会话运行 gate。只读同一平衡资产的协作系数，不启动鱼模拟或体力账单。
	const UCatFishingFightBalanceDefinition* Balance = GetDefault<UCatFishingSettings>()->FightBalanceDefinition.LoadSynchronous();
	if (!Balance) Balance = GetDefault<UCatFishingFightBalanceDefinition>();
	FCatFightGroupInput GroupInput;
	GroupInput.HelperStrengthMultiplier = Balance->HelperStrengthMultiplier;
	TArray<UCatCharacterMovementComponent*, TInlineAllocator<4>> Movements;
	double AccelerationLimit = TNumericLimits<double>::Max();
	double BrakingLimit = TNumericLimits<double>::Max();
	for (const FCatFishingOperatorMembership& Entry : PresentationState.OperatorMemberships)
	{
		ACatCharacter* Member = Entry.PlayerState ? Cast<ACatCharacter>(Entry.PlayerState->GetPawn()) : nullptr;
		UCatCharacterMovementComponent* Movement = Member ? Cast<UCatCharacterMovementComponent>(Member->GetCharacterMovement()) : nullptr;
		UCatAbilitySystemComponent* ASC = Member ? Member->GetCatAbilitySystemComponent() : nullptr;
		if (!Movement || !ASC) continue; // Pawn 尚未建立时仍发布占位域，后续 Tick 补齐身体。
		FCatFightGroupParticipantInput& Input = GroupInput.Participants.AddDefaulted_GetRef();
		Input.FishingStrength = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFishingStrengthAttribute());
		Input.CurrentStamina = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
		Input.MaximumStamina = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute());
		Input.MoveIntentWorld = Movement->GetAcceptedFishingMoveIntent();
		Input.MaximumMoveSpeedCentimetersPerSecond = Movement->GetMaxSpeed();
		Input.bPrimary = Entry.PlayerState == PresentationState.HolderPlayerState;
		Movements.Add(Movement);
		AccelerationLimit = FMath::Min(AccelerationLimit, static_cast<double>(Movement->GetMaxAcceleration()));
		BrakingLimit = FMath::Min(BrakingLimit, static_cast<double>(Movement->GetMaxBrakingDeceleration()));
	}
	FCatFightGroupResult Result;
	const bool bSolved = FCatFishingGroupModel::ComputeForces(GroupInput, Result);
	if (!bSolved && !bLastUnloadedSolveRejected)
	{
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fishing_group_unloaded_solve_rejected RodActorId=%s World=%s NetMode=%d Authority=%d LocalRole=%d RosterVersion=%u ControlEpoch=%u Result=HoldFormationInvalidProperties"),
			*PresentationState.RodActorId.ToString(), *GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), HasAuthority(),
			static_cast<int32>(GetLocalRole()), PresentationState.RosterVersion, PresentationState.ControlEpoch);
	}
	bLastUnloadedSolveRejected = !bSolved;
	const FVector Target = bSolved ? Result.DesiredVelocityCentimetersPerSecond : FVector::ZeroVector;
	const double StepSeconds = FMath::Min(DeltaSeconds, 0.25);
	FVector Velocity(GroupVelocity.X, GroupVelocity.Y, 0.0);
	if (!bSolved || Movements.IsEmpty()) Velocity = FVector::ZeroVector;
	else if (StepSeconds > 0.0)
	{
		const double Rate = Target.IsNearlyZero() && BrakingLimit > 0.0 ? BrakingLimit : AccelerationLimit;
		Velocity += (Target - Velocity).GetClampedToMaxSize(FMath::Max(0.0, Rate) * StepSeconds);
		const double Distance = Velocity.Size() * StepSeconds;
		if (Distance > UE_DOUBLE_SMALL_NUMBER)
		{
			double AllowedDistance = Distance;
			for (const UCatCharacterMovementComponent* Movement : Movements)
				AllowedDistance = FMath::Min(AllowedDistance, Movement->GetExternalTractionTravelLimit(Velocity, Distance, true));
			Velocity *= AllowedDistance / Distance;
		}
	}
	// 与载荷快照同域发布；停止时保留末次 AimInputEpoch，旧搏斗快照不能恢复鱼力。
	CarrierConstraintState.ConstraintHolderPlayerState = PresentationState.HolderPlayerState;
	CarrierConstraintState.RosterVersion = PresentationState.RosterVersion;
	CarrierConstraintState.ControlEpoch = PresentationState.ControlEpoch;
	GroupMotionState = FCatFishingGroupMotionState{};
	GroupMotionState.AnchorWorld = GetGroupAnchorWorld();
	GroupMotionState.DesiredVelocity = Target;
	GroupMotionState.UnloadedVelocity = Velocity;
	GroupMotionState.RosterVersion = PresentationState.RosterVersion;
	GroupMotionState.ControlEpoch = PresentationState.ControlEpoch;
	GroupMotionState.AimInputEpoch = CarrierConstraintState.AimInputEpoch;
	GroupMotionState.bActive = true;
	GroupMotionState.bUnloadedMovement = true;
}

bool ACatFishingRodActor::SetGroupMotionFromAuthority(const FVector& DesiredVelocity, const FVector& LateralAcceleration)
{
	if (!HasAuthority() || DesiredVelocity.ContainsNaN() || LateralAcceleration.ContainsNaN()
		|| PresentationState.OperatorPlayerStates.IsEmpty() || PresentationState.PoseMode != ECatFishingRodPoseMode::Held
		|| !PresentationState.bDeployed || PresentationState.bBroken || !CarrierConstraintState.bFightActive
		|| CarrierConstraintState.RosterVersion != PresentationState.RosterVersion
		|| CarrierConstraintState.ControlEpoch != PresentationState.ControlEpoch) return false;
	GroupMotionState.AnchorWorld = GetGroupAnchorWorld();
	GroupMotionState.DesiredVelocity = DesiredVelocity;
	GroupMotionState.LateralAcceleration = LateralAcceleration;
	GroupMotionState.RosterVersion = PresentationState.RosterVersion;
	GroupMotionState.ControlEpoch = PresentationState.ControlEpoch;
	GroupMotionState.AimInputEpoch = CarrierConstraintState.AimInputEpoch;
	GroupMotionState.bActive = true;
	GroupMotionState.bAwaitingSolve = false;
	GroupMotionState.bUnloadedMovement = false;
	GroupMotionState.UnloadedVelocity = FVector::ZeroVector;
	PublishCarrierConstraintToMovement();
	ForceNetUpdate();
	return true;
}

FTransform ACatFishingRodActor::ResolveOperatorStandLocalTransform(const int32 SlotIndex) const
{
	int32 MaximumSlots = 0;
	double Spacing = 0.0;
	if (SlotIndex < 0 || !GetDefault<UCatFishingSettings>()->TryGetRodOperatorLayout(MaximumSlots, Spacing)
		|| SlotIndex >= MaximumSlots)
	{
		return StandCanonicalLocalTransform;
	}
	// 0/1 是最靠近中心的右/左；2/3 是外侧第二对。数组扩容时无需改变复制结构和占位算法。
	const double PairDistance = (static_cast<double>(SlotIndex / 2) + 0.5) * Spacing;
	const double LateralOffset = SlotIndex % 2 == 0 ? PairDistance : -PairDistance;
	FTransform SlotTransform = StandCanonicalLocalTransform;
	SlotTransform.AddToTranslation(FVector(0.0, LateralOffset, 0.0));
	return SlotTransform;
}

FTransform ACatFishingRodActor::GetOperatorStandWorldTransform(const int32 SlotIndex) const
{
	return ResolveOperatorStandLocalTransform(SlotIndex) * GetActorTransform();
}

int32 ACatFishingRodActor::GetOperatorCount() const
{
	return PresentationState.OperatorPlayerStates.Num();
}

int32 ACatFishingRodActor::GetOperatorSlotIndex(APlayerState* PlayerState) const
{
	return PlayerState ? PresentationState.OperatorPlayerStates.IndexOfByKey(PlayerState) : INDEX_NONE;
}

bool ACatFishingRodActor::IsPrimaryOperator(APlayerState* PlayerState) const
{
	return PlayerState && PresentationState.OperatorPlayerState == PlayerState;
}

int32 ACatFishingRodActor::GetFirstFreeOperatorSlotIndex() const
{
	int32 MaximumSlots = 0;
	double Spacing = 0.0;
	return GetDefault<UCatFishingSettings>()->TryGetRodOperatorLayout(MaximumSlots, Spacing)
		&& PresentationState.OperatorPlayerStates.Num() < MaximumSlots
		? PresentationState.OperatorPlayerStates.Num() : INDEX_NONE;
}

// BeginPlay 流程：先完成 Actor 自身进入 World 的初始化，再补发权威身份可能提前排队的表现变化；没有积压时不触发蓝图事件。
void ACatFishingRodActor::BeginPlay()
{
	Super::BeginPlay();
	SetActorTickEnabled(PresentationState.PoseMode == ECatFishingRodPoseMode::Held);
	// 身份可能在 Actor BeginPlay 之前就由权威初始化完毕（生成时序问题），
	// 那时事件被推迟到这里；BeginPlay 后再把积压的“上一次变化”补发一次。
	if (bHasPendingPresentationNotification)
	{
		bHasPendingPresentationNotification = false;
		DispatchPresentationChanged(PendingPreviousPresentationState, PendingCurrentPresentationState);
	}
	PublishCarrierConstraintToMovement();
}

// EndPlay 流程：权威端先从 FishingService 注销这根已部署鱼竿，再交还给父类清理；客户端或无 Owner 的临时 Actor 不写服务登记。
void ACatFishingRodActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GroupMotionState = FCatFishingGroupMotionState{};
	CarrierConstraintState = FCatFishingCarrierConstraintState{};
	ResetAuthoritativeRotationEffort();
	ClearCarrierMovementBinding();
	SmoothedRodFishPullStrengthMeters = FVector::ZeroVector;
	AuthoritativeRodAngularVelocityRadiansPerSecond = FVector::ZeroVector;
	// 只有权威端且已绑定 Owner 时才需要清理服务里的“已部署鱼竿”登记，避免野指针残留。
	if (HasAuthority() && PresentationState.OwnerPlayerState)
	{
		if (UWorld* World = GetWorld())
		{
			if (UCatFishingService* Fishing = World->GetSubsystem<UCatFishingService>())
			{
				Fishing->UnregisterDeployedRod(PresentationState.OwnerPlayerState, this);
			}
		}
	}
	// 服务登记已经清掉后再调用父类 EndPlay，避免注销路径读到组件或 World 进入半清理状态。
	Super::EndPlay(EndPlayReason);
}

// 复制回调流程：客户端收到 PresentationState 后只把前后状态交给表现分发层；它不修改权威身份、库存实例或操作位数组。
void ACatFishingRodActor::OnRep_PresentationState(const FCatFishingRodPresentationState& Previous)
{
	if (Previous.RosterVersion != PresentationState.RosterVersion || Previous.ControlEpoch != PresentationState.ControlEpoch
		|| Previous.PoseMode != PresentationState.PoseMode || Previous.HolderPlayerState != PresentationState.HolderPlayerState
		|| Previous.bDeployed != PresentationState.bDeployed || Previous.bBroken != PresentationState.bBroken)
	{
		ClearCarrierMovementBinding();
	}
	// Previous 由引擎在应用新值前自动传入旧值，蓝图可以据此区分皮肤、部署或操作位变化。
	if (Previous.PoseMode != PresentationState.PoseMode
		|| Previous.HolderPlayerState != PresentationState.HolderPlayerState
		|| Previous.RodActorId != PresentationState.RodActorId)
	{
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=fishing_rod_pose_received RodActorId=%s RodActorRevision=%lld Pose=%s Holder=%s OperatorCount=%d World=%s NetMode=%d Authority=%s LocalRole=%d RosterVersion=%u ControlEpoch=%u GroupRosterVersion=%u GroupActive=%d"),
			*PresentationState.RodActorId.ToString(), PresentationState.RodActorRevision,
			*UEnum::GetValueAsString(PresentationState.PoseMode), *GetNameSafe(PresentationState.HolderPlayerState),
			PresentationState.OperatorPlayerStates.Num(), *GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()),
			HasAuthority() ? TEXT("true") : TEXT("false"), static_cast<int32>(GetLocalRole()),
			PresentationState.RosterVersion, PresentationState.ControlEpoch, GroupMotionState.RosterVersion, GroupMotionState.bActive);
	}
	QueueOrDispatchPresentationChanged(Previous, PresentationState);
	PublishCarrierConstraintToMovement();
}

void ACatFishingRodActor::QueueOrDispatchPresentationChanged(const FCatFishingRodPresentationState& Previous,
	const FCatFishingRodPresentationState& Current)
{
	if (!HasActorBegunPlay())
	{
		// BeginPlay 前不能安全触发蓝图事件（组件/资源可能还没就绪），先把变化排队
		if (!bHasPendingPresentationNotification)
		{
			// 只在“第一次”排队时记录 Previous，保证积压期间多次变化最终仍呈现“最早前值 -> 最新值”的单次跳变
			PendingPreviousPresentationState = Previous;
			bHasPendingPresentationNotification = true;
		}
		PendingCurrentPresentationState = Current;
		return;
	}
	DispatchPresentationChanged(Previous, Current);
}

void ACatFishingRodActor::DispatchPresentationChanged(const FCatFishingRodPresentationState& Previous,
	const FCatFishingRodPresentationState& Current)
{
	if (Current.PoseMode != ECatFishingRodPoseMode::Held
		|| Current.HolderPlayerState != Previous.HolderPlayerState
		|| Current.RosterVersion != Previous.RosterVersion || Current.ControlEpoch != Previous.ControlEpoch)
	{
		ClearCarrierMovementBinding();
		SmoothedRodFishPullStrengthMeters = FVector::ZeroVector;
	}
	SetActorTickEnabled(Current.PoseMode == ECatFishingRodPoseMode::Held);
	// 收竿后 Actor 还要活满一个终态复制窗（见 UCatFishingService::PackRod）才销毁，
	// 期间必须立刻从视觉和碰撞上消失，否则玩家会看到一根杵着不走、还挡路的幽灵竿。
	// 放在分发路径而不是权威写口：服务器与每个客户端各自在"得知"这次变化的那一刻本地执行；
	// bHidden 虽是复制属性，但 bActorEnableCollision 不是，只有本地各自执行才能保证两者一致。
	// 判据用 Current 的绝对状态而非 Previous->Current 跃迁：中途加入的客户端首帧 Previous 是默认结构体，
	// 跃迁判据会漏掉正处于死亡窗口里的竿；库存拒绝收回时也必须按恢复后的部署事实重新显示并开启碰撞。
	if (Current.RodActorId.IsValid())
	{
		SetActorHiddenInGame(!Current.bDeployed);
		SetActorEnableCollision(Current.bDeployed);
	}
	// 先应用皮肤（视觉资源切换），再广播通用状态变化事件给蓝图做其余表现响应
	BP_ApplyRodSkin(Current.RodSkinDefinitionId);
	BP_OnRodPresentationChanged(Previous, Current);
}
