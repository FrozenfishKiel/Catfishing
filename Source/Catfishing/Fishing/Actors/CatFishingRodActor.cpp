#include "Fishing/Actors/CatFishingRodActor.h"

#include "Character/CatCharacter.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"

#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "Fishing/Integration/CatFishingPhysicalRodComponent.h"
#include "Interaction/Grab/CatLightPropComponent.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/Config/CatFishingFightBalanceDefinition.h"
#include "Fishing/Debug/CatFishingMotionDiagnostics.h"
#include "Fishing/Presentation/CatRodBendComponent.h"
#include "GameFramework/Character.h"

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
	PhysicsBody = CreateDefaultSubobject<UBoxComponent>(TEXT("PhysicsRodBody"));
	PhysicsBody->SetupAttachment(SceneRoot);
	PhysicsBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PhysicalRod = CreateDefaultSubobject<UCatFishingPhysicalRodComponent>(TEXT("PhysicalRod"));
	LightProp = CreateDefaultSubobject<UCatLightPropComponent>(TEXT("LightProp"));
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
	if (IsUsingPhysicalRod())
	{
		PhysicalRod->FinishPhysicsFrame();
		PhysicalRod->RefreshObservedPose();
		return;
	}
	if (HasAuthority() && PresentationState.PoseMode == ECatFishingRodPoseMode::Held)
	{
		RefreshHeldTransformFromAuthority(DeltaSeconds);
	}
}

void ACatFishingRodActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// 离散身份/姿态、连续约束和不变握把标定各自复制，不从客户端视觉组件推导玩法锚点。
	DOREPLIFETIME(ThisClass, PresentationState);
	DOREPLIFETIME(ThisClass, CarrierConstraintState);
	DOREPLIFETIME_CONDITION(ThisClass, GripCanonicalLocalTransform, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ThisClass, RodTipCanonicalLocalTransform, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(ThisClass, StandCanonicalLocalTransform, COND_InitialOnly);
}

bool ACatFishingRodActor::IsUsingPhysicalRod() const { return PhysicalRod && PhysicalRod->IsReady(); }

bool ACatFishingRodActor::BeginPhysicalHoldFromAuthority(APlayerState* Player, const bool bPositionNewRod)
{
	return IsUsingPhysicalRod() && PhysicalRod->BeginPrimaryHold(Player, bPositionNewRod);
}

void ACatFishingRodActor::ReleasePhysicalPrimaryHoldFromAuthority(APlayerState* Player, const FName Reason)
{
	if (IsUsingPhysicalRod()) PhysicalRod->ReleasePrimaryHold(Player, Reason);
}

void ACatFishingRodActor::RefreshPrimaryControlFromAuthority()
{
	if (IsUsingPhysicalRod()) PhysicalRod->RefreshPrimaryControl();
}

bool ACatFishingRodActor::SetPrimaryOperatorFromAuthority(APlayerState* PlayerOrNull, const int64 ExpectedRevision)
{
	if (!HasAuthority() || !bIdentityInitialized || ExpectedRevision != PresentationState.RodActorRevision
		|| (PlayerOrNull && (PlayerOrNull != PresentationState.OwnerPlayerState || !PresentationState.bDeployed || PresentationState.bBroken))) return false;
	FCatFishingRodPresentationState Next = PresentationState;
	Next.OperatorPlayerStates.Reset();
	if (PlayerOrNull) Next.OperatorPlayerStates.Add(PlayerOrNull);
	if (Next.OperatorPlayerStates == PresentationState.OperatorPlayerStates) return true;
	return CommitAuthoritativeMutation(Next, ExpectedRevision);
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
	if (InOperatorPlayerState && InOperatorPlayerState != InOwnerPlayerState)
	{
		UE_LOG(LogCatFishing, Warning, TEXT("Event=fishing_rod_identity_rejected RodActorId=%s OwnerPlayerId=%d OperatorPlayerId=%d World=%s NetMode=%d Authority=1 LocalRole=%d Reason=OperatorNotOwner"),
			*InRodActorId.ToString(), InOwnerPlayerState->GetPlayerId(), InOperatorPlayerState->GetPlayerId(),
			*GetNameSafe(GetWorld()), int32(GetNetMode()), int32(GetLocalRole()));
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
	// BP_Rod and TestMap still serialize these named reference components. Preserve their
	// historical authoring pose until those assets are migrated; no gameplay reads these offsets.
	FTransform LegacyRightStand = InStand;
	FTransform LegacyLeftStand = InStand;
	LegacyRightStand.AddToTranslation(FVector(0.0, 70.0, 0.0));
	LegacyLeftStand.AddToTranslation(FVector(0.0, -70.0, 0.0));
	RightStandAnchor->SetRelativeTransform(LegacyRightStand);
	LeftStandAnchor->SetRelativeTransform(LegacyLeftStand);
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
	const bool bWasFight = CarrierConstraintState.bFightActive;

	PresentationState = Committed;
	if (bWasFight && bHeldAimInitialized && Previous.HolderPlayerState && PresentationState.HolderPlayerState
		&& Previous.HolderPlayerState != PresentationState.HolderPlayerState)
	{
		bAwaitingNewHolderAim = true;
	}
	if (PresentationState.RosterVersion != Previous.RosterVersion)
	{
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=fishing_rod_roster_committed RodActorId=%s World=%s NetMode=%d Authority=%d LocalRole=%d RosterVersion=%u ControlEpoch=%u OperatorCount=%d Result=Committed"),
			*PresentationState.RodActorId.ToString(), *GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), HasAuthority(), static_cast<int32>(GetLocalRole()),
			PresentationState.RosterVersion, PresentationState.ControlEpoch, PresentationState.OperatorPlayerStates.Num());
	}
	if (PresentationState.HolderPlayerState != Previous.HolderPlayerState
		|| PresentationState.PoseMode != Previous.PoseMode
		|| (!PresentationState.bDeployed && Previous.bDeployed)
		|| (PresentationState.bBroken && !Previous.bBroken))
	{
		ResetAuthoritativeRotationEffort();
		HeldAimInput.Reset();
		CarrierConstraintState = FCatFishingCarrierConstraintState{};
	}
	if (PresentationState.PoseMode != ECatFishingRodPoseMode::Held)
	{
		CarrierConstraintState = FCatFishingCarrierConstraintState{};
		bHeldAimInitialized = false;
		AuthoritativeAimHolder.Reset();
	}
	if (PresentationState.PoseMode != ECatFishingRodPoseMode::Held || !PresentationState.bDeployed || PresentationState.bBroken)
	{
		bAwaitingNewHolderAim = false;
	}
	SetActorTickEnabled(IsUsingPhysicalRod() || PresentationState.PoseMode == ECatFishingRodPoseMode::Held);
	QueueOrDispatchPresentationChanged(Previous, PresentationState);
	ForceNetUpdate();
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
FTransform ACatFishingRodActor::GetRodTipWorldTransform() const
{
	const FTransform Pose = HasAuthority() && IsUsingPhysicalRod() ? PhysicalRod->GetObservedActorTransform() : GetActorTransform();
	return RodTipCanonicalLocalTransform * Pose;
}
FTransform ACatFishingRodActor::GetGripWorldTransform() const
{
	const FTransform Pose = HasAuthority() && IsUsingPhysicalRod() ? PhysicalRod->GetObservedActorTransform() : GetActorTransform();
	return GripCanonicalLocalTransform * Pose;
}

FVector ACatFishingRodActor::GetAuthoritativeRodTipVelocity() const
{
	return HasAuthority() && IsUsingPhysicalRod()
		? PhysicalRod->GetPointVelocity(GetRodTipWorldTransform().GetLocation()) : AuthoritativeRodTipVelocity;
}

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

bool ACatFishingRodActor::SetFightConstraintObservationFromAuthority(const FVector& PullDirection,
	const double NormalizedTension, const double ConstraintErrorCentimeters,
	const bool bFightActive, const double MaximumFishTorqueStrengthMeters,
	const double CatTorqueCapacityStrengthMeters, const FVector& RodPullAxis)
{
	FVector HorizontalDirection(PullDirection.X, PullDirection.Y, 0.0);
	if (!HorizontalDirection.ContainsNaN()) HorizontalDirection.Normalize();
	const bool bValid = HasAuthority()
		&& (!bFightActive || PresentationState.PoseMode == ECatFishingRodPoseMode::Held)
		&& !PullDirection.ContainsNaN()
		&& FMath::IsFinite(NormalizedTension)
		&& FMath::IsFinite(ConstraintErrorCentimeters) && ConstraintErrorCentimeters >= 0.0
		&& FMath::IsFinite(MaximumFishTorqueStrengthMeters)
		&& MaximumFishTorqueStrengthMeters >= 0.0
		&& FMath::IsFinite(CatTorqueCapacityStrengthMeters)
		&& CatTorqueCapacityStrengthMeters >= 0.0
		&& !RodPullAxis.ContainsNaN() && !RodPullAxis.IsNearlyZero();
	if (!bValid)
	{
		UE_LOG(LogCatFishing, Warning, TEXT("Event=fishing_fight_constraint_observation_rejected RodActorId=%s ControlEpoch=%u World=%s NetMode=%d Authority=%d LocalRole=%d Reason=InvalidAuthorityPoseOrValues"),
			*PresentationState.RodActorId.ToString(), PresentationState.ControlEpoch, *GetNameSafe(GetWorld()),
			int32(GetNetMode()), HasAuthority(), int32(GetLocalRole()));
		return false;
	}

	FCatFishingCarrierConstraintState Next;
	Next.ConstraintHolderPlayerState = PresentationState.HolderPlayerState;
	Next.RosterVersion = PresentationState.RosterVersion;
	Next.ControlEpoch = PresentationState.ControlEpoch;
	Next.PullDirection = HorizontalDirection;
	// Deprecated reflected movement fields retain their zero defaults; this writes observations only.
	Next.NormalizedTension = static_cast<float>(FMath::Clamp(NormalizedTension, 0.0, 1.0));
	Next.ConstraintErrorCentimeters = static_cast<float>(ConstraintErrorCentimeters);
	Next.bFightActive = bFightActive;
	// 停止记录保留最后输入域，拒绝同域迟到输入；下一场仍生成更大的新域。
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

	if (!Next.bFightActive)
	{
		bAwaitingNewHolderAim = false;
		HeldAimInput.Reset();
	}
	if (Next.bFightActive != CarrierConstraintState.bFightActive)
	{
		ResetAuthoritativeRotationEffort();
	}
	if (Next.AimInputEpoch != CarrierConstraintState.AimInputEpoch) HeldAimInput.Reset();
	if (Next.bFightActive != CarrierConstraintState.bFightActive || Next.AimInputEpoch != CarrierConstraintState.AimInputEpoch)
		UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_fight_constraint_observation_changed RodActorId=%s ControlEpoch=%u AimInputEpoch=%u FightActive=%d NormalizedTension=%.3f ErrorCm=%.3f World=%s NetMode=%d Authority=1 LocalRole=%d"),
			*PresentationState.RodActorId.ToString(), Next.ControlEpoch, Next.AimInputEpoch, Next.bFightActive,
			Next.NormalizedTension, Next.ConstraintErrorCentimeters, *GetNameSafe(GetWorld()), int32(GetNetMode()), int32(GetLocalRole()));
	CarrierConstraintState = Next;
	LastConstraintUpdateWorldSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0;
	ForceNetUpdate();
	return true;
}

void ACatFishingRodActor::ClearFightConstraintAndLoadFromAuthority(const FGuid SessionId)
{
	if (!HasAuthority())
	{
		return;
	}
	UCatFishingService* Service = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr;
	const ACatFishingSession* ActiveSession = Service ? Service->FindActiveSessionByRod(this) : nullptr;
	if (SessionId.IsValid() && ActiveSession && ActiveSession->GetSnapshot().FishingSessionId != SessionId)
	{
		UE_LOG(LogCatFishing, Warning, TEXT("Event=fishing_fight_constraint_clear_rejected SessionId=%s ActiveSessionId=%s RodActorId=%s World=%s NetMode=%d Authority=1 LocalRole=%d Reason=SessionDomain"),
			*SessionId.ToString(), *ActiveSession->GetSnapshot().FishingSessionId.ToString(), *PresentationState.RodActorId.ToString(),
			*GetNameSafe(GetWorld()), int32(GetNetMode()), int32(GetLocalRole()));
		return;
	}
	if (IsUsingPhysicalRod()) PhysicalRod->ClearLineLoad(SessionId);
	// 单场结束移除鱼载荷；唯一主控生命周期由离竿/收竿/销毁负责。
	const uint32 StoppedAimInputEpoch = CarrierConstraintState.AimInputEpoch;
	HeldAimInput.Reset();
	bAwaitingNewHolderAim = false;
	ResetAuthoritativeRotationEffort();
	CarrierConstraintState = FCatFishingCarrierConstraintState{};
	CarrierConstraintState.ConstraintHolderPlayerState = PresentationState.HolderPlayerState;
	CarrierConstraintState.RosterVersion = PresentationState.RosterVersion;
	CarrierConstraintState.ControlEpoch = PresentationState.ControlEpoch;
	CarrierConstraintState.AimInputEpoch = StoppedAimInputEpoch;
	ForceNetUpdate();
}

void ACatFishingRodActor::OnRep_CarrierConstraintState()
{
	UWorld* World = GetWorld();
	LastConstraintUpdateWorldSeconds = World ? World->GetTimeSeconds() : -1.0;
	if (World && (bLastReceivedFightActive != CarrierConstraintState.bFightActive
		|| (CarrierConstraintState.bFightActive && LastConstraintUpdateWorldSeconds >= NextCarrierReceiptDiagnosticWorldSeconds)))
	{
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=fishing_fight_constraint_received RodActorId=%s Frame=%llu WorldTime=%.6f FightActive=%s "
				"PhysicalReceiver=%s SnapshotHolder=%s CurrentHolder=%s NormalizedTension=%.3f ErrorCm=%.3f "
				"PullAxis=%s FishTorque=%.3f CatTorque=%.3f ObservedRotation=%s GripLocation=%s World=%s NetMode=%d Authority=%s LocalRole=%d RosterVersion=%u ControlEpoch=%u"),
			*PresentationState.RodActorId.ToString(EGuidFormats::DigitsWithHyphens), GFrameCounter, LastConstraintUpdateWorldSeconds,
			CarrierConstraintState.bFightActive ? TEXT("true") : TEXT("false"),
			IsUsingPhysicalRod() ? TEXT("true") : TEXT("false"),
			*GetNameSafe(CarrierConstraintState.ConstraintHolderPlayerState), *GetNameSafe(PresentationState.HolderPlayerState),
			CarrierConstraintState.NormalizedTension, CarrierConstraintState.ConstraintErrorCentimeters,
			*FVector(CarrierConstraintState.RodPullAxis).ToCompactString(), CarrierConstraintState.MaximumFishTorqueStrengthMeters,
			CarrierConstraintState.CatTorqueCapacityStrengthMeters, *GetActorRotation().ToCompactString(),
			*GetGripWorldTransform().GetLocation().ToCompactString(), *GetNameSafe(World), static_cast<int32>(World->GetNetMode()),
			HasAuthority() ? TEXT("true") : TEXT("false"), static_cast<int32>(GetLocalRole()),
			PresentationState.RosterVersion, PresentationState.ControlEpoch);
		NextCarrierReceiptDiagnosticWorldSeconds = LastConstraintUpdateWorldSeconds + CatFishingMotionDiagnostics::SampleIntervalSeconds();
	}
	bLastReceivedFightActive = CarrierConstraintState.bFightActive;
}

void ACatFishingRodActor::ResetAuthoritativeRotationEffort()
{
	const uint64 NextEpoch = AuthoritativeRotationEffort.Epoch + 1;
	AuthoritativeRotationEffort = FCatFishingRodRotationEffortSnapshot{};
	AuthoritativeRotationEffort.Epoch = NextEpoch;
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

bool ACatFishingRodActor::GetControlObservationFromAuthority(FCatFishingRodControlObservation& OutObservation) const
{
	OutObservation = {};
	if (!HasAuthority() || !IsUsingPhysicalRod()) return false;
	OutObservation.ActualAim = GetGripWorldTransform().Rotator();
	OutObservation.AngularVelocityRadiansPerSecond = PhysicalRod->GetAngularVelocityRadiansPerSecond();
	OutObservation.bWaitingForNewHolder = bAwaitingNewHolderAim;
	OutObservation.bMouseDriveActive = !bAwaitingNewHolderAim && CarrierConstraintState.bFightActive
		&& HeldAimInput.IsMouseActive(GetWorld()->GetTimeSeconds());
	OutObservation.RequestedAim = OutObservation.bMouseDriveActive ? HeldAimInput.GetRequestedAim() : OutObservation.ActualAim;
	return true;
}

bool ACatFishingRodActor::RefreshHeldTransformFromAuthority(const double DeltaSeconds)
{
	if (!HasAuthority() || !IsUsingPhysicalRod()) return false;
	PhysicalRod->RefreshObservedPose();
	return true;
}

uint32 ACatFishingRodActor::GetOperatorMembershipEpoch(APlayerState* PlayerState) const
{
	const FCatFishingOperatorMembership* Member = PresentationState.OperatorMemberships.FindByPredicate(
		[PlayerState](const FCatFishingOperatorMembership& Entry) { return Entry.PlayerState == PlayerState; });
	return Member ? Member->Epoch : 0;
}

void ACatFishingRodActor::StopHeldAimInputFromAuthority(APlayerState* Player)
{
	if (HasAuthority() && Player && Player == PresentationState.HolderPlayerState)
		HeldAimInput.StopInput(AuthoritativeHeldAimRotation);
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

// BeginPlay 流程：先完成 Actor 自身进入 World 的初始化，再补发权威身份可能提前排队的表现变化；没有积压时不触发蓝图事件。
void ACatFishingRodActor::BeginPlay()
{
	Super::BeginPlay();
	PhysicalRod->Initialize(PhysicsBody, GripCanonicalLocalTransform, RodTipCanonicalLocalTransform);
	SetTickGroup(TG_PostPhysics);
	SetActorTickEnabled(true);
	SetActorTickEnabled(IsUsingPhysicalRod() || PresentationState.PoseMode == ECatFishingRodPoseMode::Held);
	// 身份可能在 Actor BeginPlay 之前就由权威初始化完毕（生成时序问题），
	// 那时事件被推迟到这里；BeginPlay 后再把积压的“上一次变化”补发一次。
	if (bHasPendingPresentationNotification)
	{
		bHasPendingPresentationNotification = false;
		DispatchPresentationChanged(PendingPreviousPresentationState, PendingCurrentPresentationState);
	}
}

// EndPlay 流程：权威端先从 FishingService 注销这根已部署鱼竿，再交还给父类清理；客户端或无 Owner 的临时 Actor 不写服务登记。
void ACatFishingRodActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (PhysicalRod) PhysicalRod->ReleaseAllConnections(TEXT("RodEndPlay"));
	CarrierConstraintState = FCatFishingCarrierConstraintState{};
	ResetAuthoritativeRotationEffort();
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
	// Previous 由引擎在应用新值前自动传入旧值，蓝图可以据此区分皮肤、部署或操作位变化。
	if (Previous.PoseMode != PresentationState.PoseMode
		|| Previous.HolderPlayerState != PresentationState.HolderPlayerState
		|| Previous.RodActorId != PresentationState.RodActorId)
	{
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=fishing_rod_pose_received RodActorId=%s RodActorRevision=%lld Pose=%s Holder=%s OperatorCount=%d World=%s NetMode=%d Authority=%s LocalRole=%d RosterVersion=%u ControlEpoch=%u"),
			*PresentationState.RodActorId.ToString(), PresentationState.RodActorRevision,
			*UEnum::GetValueAsString(PresentationState.PoseMode), *GetNameSafe(PresentationState.HolderPlayerState),
			PresentationState.OperatorPlayerStates.Num(), *GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()),
			HasAuthority() ? TEXT("true") : TEXT("false"), static_cast<int32>(GetLocalRole()),
			PresentationState.RosterVersion, PresentationState.ControlEpoch);
	}
	QueueOrDispatchPresentationChanged(Previous, PresentationState);
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
	SetActorTickEnabled(IsUsingPhysicalRod() || Current.PoseMode == ECatFishingRodPoseMode::Held);
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
