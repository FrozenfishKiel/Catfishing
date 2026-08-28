#include "Fishing/Actors/CatFishEncounterActor.h"

#include "Components/StateTreeComponent.h"
#include "Components/SceneComponent.h"
#include "Fishing/Simulation/CatFishingFightRunner.h"
#include "Net/UnrealNetwork.h"
#include "StateTree.h"

ACatFishEncounterActor::ACatFishEncounterActor()
{
	bReplicates = true; // 需要向所有客户端同步鱼的表现状态。
	SetReplicateMovement(true); // 位置/朝向随内置 Movement 复制通道同步，配合下方 SetActorLocation 的 TeleportPhysics 使用。
	bAlwaysRelevant = false; // 不强制常驻相关性，按引擎默认的距离/可见性剔除规则复制，减少不必要的网络开销。
	bNetUseOwnerRelevancy = false; // 不借用 Owner 的相关性规则（本 Actor 没有单一“属主玩家”概念）。
	bOnlyRelevantToOwner = false; // 所有客户端都需要看到鱼，而非只有 Owner 可见。
	PrimaryActorTick.bCanEverTick = false; // 纯粹由服务器权威事件（InitializeAuthoritativeIdentity/ApplyFightStepFromAuthority）驱动状态，不需要 Tick。
	PrimaryActorTick.bStartWithTickEnabled = false;
	// 根组件只作挂点，不承载视觉资源，方便蓝图子类把美术骨架挂在 VisualRoot 下而不影响根变换语义。
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	VisualRoot->SetupAttachment(SceneRoot);
	FishBehaviorStateTree = CreateDefaultSubobject<UStateTreeComponent>(TEXT("FishBehaviorStateTree"));
	FishBehaviorStateTree->SetStartLogicAutomatically(false);
}

void ACatFishEncounterActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// 唯一复制属性：客户端只需要这一份只读表现状态即可驱动本地呈现，不下发任何权威玩法数据。
	DOREPLIFETIME(ThisClass, PresentationState);
}

bool ACatFishEncounterActor::InitializeAuthoritativeIdentity(const FGuid InFishingSessionId, const FGuid InCastAttemptId,
	const FName InFishDefinitionId, const double InInitialLineLength, const double InVisualScale)
{
	if (!HasAuthority() || !InFishingSessionId.IsValid() || !InCastAttemptId.IsValid()
		|| InFishingSessionId == InCastAttemptId || InFishDefinitionId.IsNone()
		|| !FMath::IsFinite(InInitialLineLength) || InInitialLineLength < 0.0
		|| !FMath::IsFinite(InVisualScale) || InVisualScale <= 0.0)
	{
		// 非服务器调用、身份参数无效（两个 Guid 不能相同/未设置）、鱼种未指定或初始线长非法一律拒绝初始化。
		return false;
	}
	if (bIdentityInitialized)
	{
		// 身份只能设置一次；重复调用只在传入值与已登记身份完全一致时才算“幂等成功”，否则视为非法覆盖并失败。
		return PresentationState.FishingSessionId == InFishingSessionId
			&& PresentationState.CastAttemptId == InCastAttemptId
			&& PresentationState.FishDefinitionId == InFishDefinitionId;
	}
	// 保存旧状态用于表现变化通知（Previous -> Current 对比）。
	const FCatFishEncounterPresentationState Previous = PresentationState;
	PresentationState.FishingSessionId = InFishingSessionId;
	PresentationState.CastAttemptId = InCastAttemptId;
	PresentationState.FishDefinitionId = InFishDefinitionId;
	PresentationState.VisualScale = InVisualScale;
	PresentationState.MotionIntent = ECatFishMotionIntent::None; // 初始尚未产生任何搏斗运动意图。
	PresentationState.CurrentLineLength = InInitialLineLength;
	bIdentityInitialized = true; // 标记身份已锁定，后续调用只能走上面的幂等分支。
	ApplyVisualScale();
	QueueOrDispatchPresentationChanged(Previous, PresentationState); // 按 BeginPlay 时序决定立即广播还是先排队。
	ForceNetUpdate(); // 立即触发一次网络复制，不等下个复制周期，保证表现尽快到达客户端。
	return true;
}

const FCatFishEncounterPresentationState& ACatFishEncounterActor::GetPresentationState() const { return PresentationState; }

bool ACatFishEncounterActor::StartFishBehaviorFromAuthority(UStateTree* BehaviorStateTree,
	UCatFishingFightRunner* FightRunner)
{
	if (!HasAuthority() || !bIdentityInitialized || !BehaviorStateTree || !FightRunner
		|| !FishBehaviorStateTree || FishBehaviorStateTree->IsRunning())
	{
		return false;
	}
	AuthorityFightRunner = FightRunner;
	FishBehaviorStateTree->SetStateTree(BehaviorStateTree);
	bBehaviorStartupInProgress = true;
	FishBehaviorStateTree->StartLogic();
	bBehaviorStartupInProgress = false;
	if (!FishBehaviorStateTree->IsRunning())
	{
		AuthorityFightRunner.Reset();
		return false;
	}
	return true;
}

void ACatFishEncounterActor::StopFishBehaviorFromAuthority()
{
	if (!HasAuthority())
	{
		return;
	}
	if (FishBehaviorStateTree && FishBehaviorStateTree->IsRunning())
	{
		FishBehaviorStateTree->StopLogic(TEXT("Fishing fight stopped"));
	}
	bBehaviorStartupInProgress = false;
	AuthorityFightRunner.Reset();
}

bool ACatFishEncounterActor::BeginBehaviorStateFromStateTree(const ECatFishMotionIntent MotionIntent,
	double& OutDurationSeconds)
{
	OutDurationSeconds = 0.0;
	return HasAuthority() && FishBehaviorStateTree
		&& (FishBehaviorStateTree->IsRunning() || bBehaviorStartupInProgress)
		&& AuthorityFightRunner.IsValid()
		&& AuthorityFightRunner->BeginBehaviorStateFromStateTree(MotionIntent, OutDurationSeconds);
}

// 直接返回 VisualRoot 的世界变换位置：三个偏移和朝向都已经烘在组件变换里，调试绘制不需要自己重算一遍。
FVector ACatFishEncounterActor::GetVisualWorldLocation() const
{
	return VisualRoot ? VisualRoot->GetComponentLocation() : GetActorLocation();
}

void ACatFishEncounterActor::ApplyVisualScale()
{
	// BeginPlay 前蓝图子类的组件基准变换可能尚未完成；等 Actor 完整就绪后再冻结基准 Scale。
	if (!HasActorBegunPlay() || !VisualRoot)
	{
		return;
	}
	if (!bVisualRootBaseScaleCaptured)
	{
		VisualRootBaseRelativeScale = VisualRoot->GetRelativeScale3D();
		bVisualRootBaseScaleCaptured = true;
	}
	const double Scale = FMath::IsFinite(PresentationState.VisualScale) && PresentationState.VisualScale > 0.0
		? PresentationState.VisualScale : 1.0;
	VisualRoot->SetRelativeScale3D(VisualRootBaseRelativeScale * Scale);
}

void ACatFishEncounterActor::ApplyVisualPose()
{
	if (!VisualRoot)
	{
		return;
	}
	// AutoHauling 在玩法上表示鱼已经力竭、只会被收线拖动。这个状态属于复制的 PresentationState，
	// 所以服务器和每个客户端都会独立应用同一侧翻角；VisualRoot 旋转不会污染权威 Actor 朝向。
	const float VisualRoll = PresentationState.MotionIntent == ECatFishMotionIntent::AutoHauling
		? ExhaustedVisualRollDegrees : 0.0f;
	VisualRoot->SetRelativeLocationAndRotation(
		FVector(static_cast<double>(VisualForwardOffsetCentimeters),
			static_cast<double>(VisualRightOffsetCentimeters),
			-static_cast<double>(FMath::Max(0.0f, VisualDepthCentimeters))),
		FRotator(0.0, VisualYawOffsetDegrees, VisualRoll));
}

bool ACatFishEncounterActor::ApplyFightStepFromAuthority(const ECatFishMotionIntent MotionIntent,
	const double CurrentLineLength, const FVector& FishWorldPosition, const float StepDeltaSeconds,
	const float FishLineAlignment, const float NormalizedLineLoad,
	const float IntendedSwimSpeedCentimetersPerSecond, const bool bStrongConfrontation)
{
	// [FishLogic 4/5：权威落位与多人表现]
	// Simulator 给出事实，本 Actor 只负责在服务器应用 Transform/表现快照；位置和 PresentationState 再复制给客户端。
	if (!HasAuthority() || !bIdentityInitialized || FishWorldPosition.ContainsNaN()
		|| !FMath::IsFinite(CurrentLineLength) || CurrentLineLength < 0.0
		|| !FMath::IsFinite(FishLineAlignment) || FishLineAlignment < -1.0f || FishLineAlignment > 1.0f
		|| !FMath::IsFinite(NormalizedLineLoad) || NormalizedLineLoad < 0.0f || NormalizedLineLoad > 1.0f
		|| !FMath::IsFinite(IntendedSwimSpeedCentimetersPerSecond)
		|| IntendedSwimSpeedCentimetersPerSecond < 0.0f)
	{
		// 必须已经完成身份初始化才允许推进搏斗表现；位置/线长必须是合法有限值，防止把 NaN/负数同步给客户端。
		return false;
	}
	const FCatFishEncounterPresentationState Previous = PresentationState;
	PresentationState.MotionIntent = MotionIntent; // 更新鱼当前的运动意图（平静/向外挣扎/自动收线中）供表现层驱动动画。
	// 复制行为层选中的自由游速，而不是根据最终 Actor 位移反推；鱼被线端或岸线挡住时仍应猛烈甩尾。
	PresentationState.IntendedSwimSpeedCentimetersPerSecond = IntendedSwimSpeedCentimetersPerSecond;
	PresentationState.CurrentLineLength = CurrentLineLength; // 更新鱼与浮标/竿之间的当前线长，供表现层估算张力/位置。
	PresentationState.FishLineAlignment = FishLineAlignment;
	PresentationState.NormalizedLineLoad = NormalizedLineLoad;
	PresentationState.bStrongConfrontation = bStrongConfrontation;
	ApplyVisualPose();

	// 朝向跟随实际游动方向：取本步位移的水平分量求偏航角。
	// 只写 Actor 旋转（随 SetReplicateMovement 一起复制），玩法判定（线长/近岸/抄网半圆）全部只用位置，旋转不参与任何裁决。
	// 只转偏航不转俯仰：鱼贴着水面走，Z 的微小抖动会让 Pitch 疯狂跳动。
	const FVector MoveDelta = FishWorldPosition - GetActorLocation();
	constexpr double MinimumMoveCentimeters = 1.0; // 位移过小（僵持不动）时保持上一帧朝向，避免噪声导致乱转。
	if (FVector2D(MoveDelta.X, MoveDelta.Y).SizeSquared() >= MinimumMoveCentimeters * MinimumMoveCentimeters)
	{
		const double TargetYaw = FMath::RadiansToDegrees(FMath::Atan2(MoveDelta.Y, MoveDelta.X));
		FRotator NewRotation = GetActorRotation();
		if (!bFacingInitialized || StepDeltaSeconds <= 0.0f || MaximumTurnRateDegreesPerSecond <= 0.0f)
		{
			NewRotation.Yaw = TargetYaw; // 首次落位（或未提供步长）直接对准，不做插值。
		}
		else
		{
			// 限速转向：每步最多转 MaximumTurnRateDegreesPerSecond * 步长 度；FixedTurn 自带角度环绕处理。
			NewRotation.Yaw = FMath::FixedTurn(NewRotation.Yaw, TargetYaw,
				MaximumTurnRateDegreesPerSecond * StepDeltaSeconds);
		}
		NewRotation.Pitch = 0.0;
		NewRotation.Roll = 0.0;
		SetActorRotation(NewRotation, ETeleportType::TeleportPhysics);
		bFacingInitialized = true;
	}

	// 用 TeleportPhysics 直接落位而非物理模拟移动：鱼的位置由服务器权威搏斗模拟计算，这里只是把结果“摆”过去。
	SetActorLocation(FishWorldPosition, false, nullptr, ETeleportType::TeleportPhysics);
	QueueOrDispatchPresentationChanged(Previous, PresentationState);
	ForceNetUpdate(); // 搏斗中每一步都需要尽快同步，不等待默认复制频率。
	return true;
}

void ACatFishEncounterActor::DeferInitialPresentationFromAuthority()
{
	// 只有服务器且 Actor 尚未 BeginPlay 时才需要推迟：确保 InitializeAuthoritativeIdentity 触发的首次表现通知
	// 会被排队到 BeginPlay 之后才真正下发，避免蓝图事件在 Actor 还没准备好（如尚未挂载好子组件）时被调用。
	if (HasAuthority() && !HasActorBegunPlay()) bPresentationDeferred = true;
}

void ACatFishEncounterActor::PublishInitialPresentationFromAuthority()
{
	if (!HasAuthority()) return;
	bPresentationDeferred = false; // 解除延迟标记，允许后续状态变化立即分发。
	if (bHasPendingPresentationNotification && HasActorBegunPlay())
	{
		// 把此前排队的“首次表现状态”一次性补发出去。
		bHasPendingPresentationNotification = false;
		DispatchPresentationChanged(PendingPreviousPresentationState, PendingCurrentPresentationState);
	}
}

void ACatFishEncounterActor::BeginPlay()
{
	Super::BeginPlay();
	if (VisualRoot)
	{
		VisualRootBaseRelativeScale = VisualRoot->GetRelativeScale3D();
		bVisualRootBaseScaleCaptured = true;
	}
	// 表现修正只动 VisualRoot（Mesh 挂在它下面），权威 Actor 的位置/朝向保持不变，判定完全不受影响：
	//   位置 = 前后(X) / 左右(Y) / 下沉(-Z) 三轴微调，在 Actor 本地空间（前 = 鱼真实游动方向）；
	//   旋转 = 修正美术资源的前向轴，使 Mesh 的视觉正面对齐 Actor 前向。
	// 注意相对位置在父组件空间求值，不受这里的相对旋转影响，所以"前后左右"的语义不会被 YawOffset 扭转。
	ApplyVisualPose();
	ApplyVisualScale();
	if (bHasPendingPresentationNotification && !bPresentationDeferred)
	{
		// BeginPlay 完成、且没有被显式要求继续延迟时，把 BeginPlay 之前排队的表现通知补发出去
		// （典型场景：客户端 OnRep 在 Actor 完全 BeginPlay 之前就先到达了）。
		bHasPendingPresentationNotification = false;
		DispatchPresentationChanged(PendingPreviousPresentationState, PendingCurrentPresentationState);
	}
}

void ACatFishEncounterActor::OnRep_PresentationState(const FCatFishEncounterPresentationState& Previous)
{
	// 客户端复制回调：引擎已经把 PresentationState 覆写为最新值，这里只需要用回调参数里的旧值对比分发。
	ApplyVisualScale();
	ApplyVisualPose();
	QueueOrDispatchPresentationChanged(Previous, PresentationState);
}

void ACatFishEncounterActor::QueueOrDispatchPresentationChanged(const FCatFishEncounterPresentationState& Previous,
	const FCatFishEncounterPresentationState& Current)
{
	if (!HasActorBegunPlay() || bPresentationDeferred)
	{
		// Actor 还没准备好接收表现事件：排队缓存，且只保留“最早的 Previous + 最新的 Current”，
		// 这样补发时能一次性反映从最初状态到当前状态的净变化，不丢首尾信息也不重复触发多次。
		if (!bHasPendingPresentationNotification)
		{
			PendingPreviousPresentationState = Previous;
			bHasPendingPresentationNotification = true;
		}
		PendingCurrentPresentationState = Current;
		return;
	}
	DispatchPresentationChanged(Previous, Current);
}

void ACatFishEncounterActor::DispatchPresentationChanged(const FCatFishEncounterPresentationState& Previous,
	const FCatFishEncounterPresentationState& Current)
{
	// 唯一对外通知口：转发给蓝图可实现事件，由表现层（动画/特效/UI）决定如何响应状态变化。
	BP_OnFishPresentationChanged(Previous, Current);
}
