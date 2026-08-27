#include "Fishing/Actors/CatFishingHookActor.h"

#include "CableComponent.h"
#include "Character/CatCharacter.h"
#include "Components/SceneComponent.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/Presentation/CatFishingPresentationSettings.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

ACatFishingHookActor::ACatFishingHookActor()
{
	bReplicates = true;
	SetReplicateMovement(true); // 抛竿飞行轨迹靠引擎内建的 Movement 复制同步到客户端
	bAlwaysRelevant = false;
	bNetUseOwnerRelevancy = false;
	bOnlyRelevantToOwner = false; // 其他玩家也要看见钩/浮标飞出去，不能只对抛竿者复制
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	VisualRoot->SetupAttachment(SceneRoot);
	// 钩子/浮标/鱼饵三个独立锚点，分别挂不同美术资源，互不影响
	HookVisualAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("HookVisualAnchor"));
	HookVisualAnchor->SetupAttachment(VisualRoot);
	BobberVisualAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("BobberVisualAnchor"));
	BobberVisualAnchor->SetupAttachment(VisualRoot);
	BaitVisualAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("BaitVisualAnchor"));
	BaitVisualAnchor->SetupAttachment(VisualRoot);
	// Cable 的起点使用本地绝对坐标锚点：Hook 根节点仍按服务器/网络快照移动，锚点在本机用 60Hz 平滑追赶。
	FishingLineStartAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("FishingLineStartAnchor"));
	FishingLineStartAnchor->SetupAttachment(SceneRoot);
	FishingLineStartAnchor->SetAbsolute(true, true, true);
	// 鱼线是纯本地程序化表现：两端依附已经复制的 Actor/Component，不复制 Cable 粒子，也不参与玩法判定。
	FishingLine = CreateDefaultSubobject<UCableComponent>(TEXT("FishingLine"));
	FishingLine->SetupAttachment(FishingLineStartAnchor);
	FishingLine->bAttachStart = true;
	FishingLine->bAttachEnd = true;
	FishingLine->CableLength = 75.0f; // 小于通常竿钩距离，使占位线保持基本绷直；端点仍始终准确连接。
	FishingLine->NumSegments = 16;
	FishingLine->SubstepTime = 0.01f;
	FishingLine->SolverIterations = 10;
	// 鱼线需要弯曲但不应像橡胶棒；长度约束由固定迭代次数稳定求解，不在松弛/绷紧间切换弯曲刚度。
	FishingLine->bEnableStiffness = false;
	FishingLine->bUseSubstepping = true;
	FishingLine->CableGravityScale = 0.15f;
	FishingLine->CableWidth = 1.25f;
	FishingLine->NumSides = 4;
	FishingLine->bEnableCollision = false;
	FishingLine->bSkipCableUpdateWhenNotVisible = true;
	FishingLine->bResetAfterTeleport = true;
	FishingLine->TeleportDistanceThreshold = 300.0f;
	FishingLine->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FishingLine->SetVisibility(false, true);
	FishingLine->SetHiddenInGame(true);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FishingLineMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (FishingLineMaterial.Succeeded())
	{
		FishingLine->SetMaterial(0, FishingLineMaterial.Object);
	}
	// 抛物线飞行组件：默认不自动启动，只有 BeginAuthoritativeFlight 显式激活时才开始受重力飞行
	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
	ProjectileMovement->bAutoActivate = false;
	ProjectileMovement->ProjectileGravityScale = 1.0f;
}

void ACatFishingHookActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, PresentationState);
}

bool ACatFishingHookActor::InitializeAuthoritativeIdentity(const FGuid InFishingSessionId, const FGuid InCastAttemptId)
{
	// 会话 ID 与投竿尝试 ID 都必须有效，且二者不能相等（避免 ID 生成方误传同一个 Guid 造成身份混淆）
	if (!HasAuthority() || !InFishingSessionId.IsValid() || !InCastAttemptId.IsValid() || InFishingSessionId == InCastAttemptId)
	{
		return false;
	}
	if (bIdentityInitialized)
	{
		// 幂等：同一个 Actor 只允许绑定同一对 (SessionId, CastAttemptId)，重复调用不算失败但也不会覆盖
		return PresentationState.FishingSessionId == InFishingSessionId && PresentationState.CastAttemptId == InCastAttemptId;
	}
	const FCatFishingHookPresentationState Previous = PresentationState;
	PresentationState.FishingSessionId = InFishingSessionId;
	PresentationState.CastAttemptId = InCastAttemptId;
	// 一绑定身份就进入“抛竿飞行中”阶段，后续由 BeginAuthoritativeFlight 真正启动物理飞行
	PresentationState.Phase = ECatFishingHookPresentationPhase::CastFlight;
	bIdentityInitialized = true;
	QueueOrDispatchPresentationChanged(Previous, PresentationState);
	ForceNetUpdate();
	return true;
}

const FCatFishingHookPresentationState& ACatFishingHookActor::GetPresentationState() const { return PresentationState; }

// 表现浮动：只动 VisualRoot 相对 Z，权威 Actor Transform 保持不变（复制/判定安全）。
void ACatFishingHookActor::SetPresentationBobOffset(const float OffsetZ)
{
	if (VisualRoot && FMath::IsFinite(OffsetZ))
	{
		if (!bVisualRootBaseLocationInitialized)
		{
			VisualRootBaseRelativeLocation = VisualRoot->GetRelativeLocation();
			bVisualRootBaseLocationInitialized = true;
		}
		VisualRoot->SetRelativeLocation(VisualRootBaseRelativeLocation + FVector(0.0, 0.0, OffsetZ));
	}
}

FVector ACatFishingHookActor::GetPresentationVisualWorldLocation() const
{
	return VisualRoot ? VisualRoot->GetComponentLocation() : GetActorLocation();
}

bool ACatFishingHookActor::SetBobberPresentationModeFromAuthority(const ECatFishingBobberPresentationMode Mode)
{
	if (!HasAuthority() || !bIdentityInitialized)
	{
		return false;
	}
	if (PresentationState.BobberMode == Mode)
	{
		return true;
	}
	const FCatFishingHookPresentationState Previous = PresentationState;
	PresentationState.BobberMode = Mode;
	PresentationState.BobberModeStartedServerTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	QueueOrDispatchPresentationChanged(Previous, PresentationState);
	ForceNetUpdate();
	return true;
}

bool ACatFishingHookActor::SetFishingLinePresentationFromAuthority(
	const double PaidOutLineLengthCentimeters, const double StraightLineDistanceCentimeters,
	const double SlackLineLengthCentimeters, const float NormalizedTension, const bool bLineTaut)
{
	if (!HasAuthority() || !bIdentityInitialized
		|| !FMath::IsFinite(PaidOutLineLengthCentimeters) || PaidOutLineLengthCentimeters < 0.0
		|| !FMath::IsFinite(StraightLineDistanceCentimeters) || StraightLineDistanceCentimeters < 0.0
		|| !FMath::IsFinite(SlackLineLengthCentimeters) || SlackLineLengthCentimeters < 0.0
		|| !FMath::IsFinite(NormalizedTension) || NormalizedTension < 0.0f || NormalizedTension > 1.0f)
	{
		return false;
	}
	const FCatFishingHookPresentationState Previous = PresentationState;
	PresentationState.PaidOutLineLengthCentimeters = PaidOutLineLengthCentimeters;
	PresentationState.StraightLineDistanceCentimeters = StraightLineDistanceCentimeters;
	PresentationState.SlackLineLengthCentimeters = SlackLineLengthCentimeters;
	PresentationState.NormalizedTension = NormalizedTension;
	PresentationState.bLineTaut = bLineTaut;
	QueueOrDispatchPresentationChanged(Previous, PresentationState);
	ForceNetUpdate();
	return true;
}

bool ACatFishingHookActor::BeginAuthoritativeFlight(const FVector& InitialVelocity,
	const FVector& ExpectedLandingWorldPoint)
{
	// 必须已完成身份初始化且尚未定稿过落点（bLandingFinalized 为一次性锁），初速度/预期落点也不能是 NaN
	if (!HasAuthority() || !bIdentityInitialized || bLandingFinalized || InitialVelocity.ContainsNaN()
		|| ExpectedLandingWorldPoint.ContainsNaN())
	{
		return false;
	}
	// 用外部（弹道解算）算好的初速度直接赋值并激活抛物线运动，让引擎物理去演算真实飞行轨迹
	ProjectileMovement->Velocity = InitialVelocity;
	ProjectileMovement->Activate(true);
	// 记录“期望”落点的 Z 高度，作为轮询判断是否已落地的阈值（不是最终写入的权威落点本身）
	PendingAuthoritativeLandingWorldPoint = ExpectedLandingWorldPoint;
	// Hook/Bobber 视觉 Mesh 按规范使用 NoCollision，落点靠有界轮询而不是碰撞事件确认；权威落点本身仍由调用方给定。
	GetWorldTimerManager().SetTimer(LandingPollTimerHandle, this,
		&ACatFishingHookActor::PollAuthoritativeLanding, 0.05f, true);
	return true;
}

void ACatFishingHookActor::PollAuthoritativeLanding()
{
	if (!HasAuthority() || bLandingFinalized)
	{
		// 落点已经在别处定稿（例如提前被打断），及时清掉计时器避免野调用
		GetWorldTimerManager().ClearTimer(LandingPollTimerHandle);
		return;
	}
	// 用当前物理飞行位置的高度和目标落点高度比较：一旦降到目标水面高度以下即视为落地
	if (GetActorLocation().Z <= PendingAuthoritativeLandingWorldPoint.Z)
	{
		GetWorldTimerManager().ClearTimer(LandingPollTimerHandle);
		FinalizeAuthoritativeLandingOnce(true, PendingAuthoritativeLandingWorldPoint);
	}
}

bool ACatFishingHookActor::FinalizeAuthoritativeLandingOnce(const bool bSucceeded,
	const FVector& LandingWorldPoint)
{
	// bLandingFinalized 保证落点只会被定稿一次，重复调用（无论成功/失败）都直接拒绝
	if (!HasAuthority() || !bIdentityInitialized || bLandingFinalized || LandingWorldPoint.ContainsNaN())
	{
		return false;
	}
	bLandingFinalized = true;
	// 停止物理飞行并把 Actor 位置强制吸附到权威落点，消除轮询帧误差，保证落点精确一致
	ProjectileMovement->StopMovementImmediately();
	ProjectileMovement->Deactivate();
	SetActorLocation(LandingWorldPoint);
	const FCatFishingHookPresentationState Previous = PresentationState;
	// 落地成功进入 Landed（等待鱼咬钩），失败（例如落在非法区域）进入 Failed 供表现层播放对应反馈
	PresentationState.Phase = bSucceeded ? ECatFishingHookPresentationPhase::Landed : ECatFishingHookPresentationPhase::Failed;
	if (bSucceeded)
	{
		// 搏斗前也要给 Cable 一份真实的线长基线。此前等待咬钩阶段只有端点距离能把 75cm 占位线撑直，
		// DisplayedFishingLineLength 仍停在占位值；上钩后的第一份 L_paid 会让积累的余线成段跳出。
		// 落水时先发布 L_paid=D、Slack=0，利用等待咬钩的时间平滑收敛；不改变任何权威玩法范围。
		if (const ACatFishingRodActor* Rod = Cast<ACatFishingRodActor>(GetOwner()))
		{
			const double LandedLineLength = FVector::Distance(
				Rod->GetRodTipWorldTransform().GetLocation(), LandingWorldPoint);
			if (FMath::IsFinite(LandedLineLength))
			{
				PresentationState.PaidOutLineLengthCentimeters = LandedLineLength;
				PresentationState.StraightLineDistanceCentimeters = LandedLineLength;
				PresentationState.SlackLineLengthCentimeters = 0.0;
				PresentationState.NormalizedTension = 0.0f;
				PresentationState.bLineTaut = true;
			}
		}
	}
	QueueOrDispatchPresentationChanged(Previous, PresentationState);
	ForceNetUpdate();
	return true;
}

void ACatFishingHookActor::DeferInitialPresentationFromAuthority()
{
	// 服务器在 Actor 生成后、BeginPlay 前调用，用于显式压住第一次表现事件的派发，
	// 让调用方有机会先把身份/初速度等都设置齐全，再统一发布，避免半初始化状态被蓝图看到
	if (HasAuthority() && !HasActorBegunPlay()) bPresentationDeferred = true;
}

void ACatFishingHookActor::PublishInitialPresentationFromAuthority()
{
	if (!HasAuthority()) return;
	bPresentationDeferred = false;
	// 解除延迟后，如果 BeginPlay 已经跑过且期间确实攒了待发事件，这里补发一次
	if (bHasPendingPresentationNotification && HasActorBegunPlay())
	{
		bHasPendingPresentationNotification = false;
		DispatchPresentationChanged(PendingPreviousPresentationState, PendingCurrentPresentationState);
	}
}

void ACatFishingHookActor::BeginPlay()
{
	Super::BeginPlay();
	VisualRootBaseRelativeLocation = VisualRoot ? VisualRoot->GetRelativeLocation() : FVector::ZeroVector;
	bVisualRootBaseLocationInitialized = true;
	if (GetNetMode() == NM_DedicatedServer)
	{
		// 专用服务器没有渲染需求，也不应支付 Cable 粒子模拟开销。
		FishingLine->SetComponentTickEnabled(false);
		FishingLine->SetVisibility(false, true);
		FishingLine->SetHiddenInGame(true);
	}
	else
	{
		if (UMaterialInstanceDynamic* Material = FishingLine->CreateDynamicMaterialInstance(0))
		{
			Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.72f, 0.78f, 0.82f, 1.0f));
		}
		RefreshFishingLineAttachment();
	}
	RefreshBobberPresentationTimer();
	// 只有在没有被显式 Defer（等待权威继续初始化）时，才在这里把积压事件补发出去；
	// 否则要等 PublishInitialPresentationFromAuthority 显式放行
	if (bHasPendingPresentationNotification && !bPresentationDeferred)
	{
		bHasPendingPresentationNotification = false;
		DispatchPresentationChanged(PendingPreviousPresentationState, PendingCurrentPresentationState);
	}
}

void ACatFishingHookActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(FishingLinePresentationTimerHandle);
	GetWorldTimerManager().ClearTimer(BobberPresentationTimerHandle);
	GetWorldTimerManager().ClearTimer(LandingPollTimerHandle);
	Super::EndPlay(EndPlayReason);
}

void ACatFishingHookActor::OnRep_Owner()
{
	Super::OnRep_Owner();
	RefreshFishingLineAttachment();
}

void ACatFishingHookActor::OnRep_Instigator()
{
	Super::OnRep_Instigator();
	TryPlayCastMontageFromPresentation();
}

void ACatFishingHookActor::OnRep_PresentationState(const FCatFishingHookPresentationState& Previous)
{
	// 客户端复制回调的唯一入口，走和服务器本地相同的排队/派发逻辑
	QueueOrDispatchPresentationChanged(Previous, PresentationState);
}

void ACatFishingHookActor::QueueOrDispatchPresentationChanged(const FCatFishingHookPresentationState& Previous,
	const FCatFishingHookPresentationState& Current)
{
	// 两种情况都要排队而不能立即派发：Actor 还没 BeginPlay，或权威显式要求延迟
	if (!HasActorBegunPlay() || bPresentationDeferred)
	{
		if (!bHasPendingPresentationNotification)
		{
			// 同上：只在首次排队时锁定最早的 Previous，保证补发时呈现单次完整跳变
			PendingPreviousPresentationState = Previous;
			bHasPendingPresentationNotification = true;
		}
		PendingCurrentPresentationState = Current;
		return;
	}
	DispatchPresentationChanged(Previous, Current);
}

void ACatFishingHookActor::DispatchPresentationChanged(const FCatFishingHookPresentationState& Previous,
	const FCatFishingHookPresentationState& Current)
{
	// 初始复制包到达后 Owner 与 PresentationState 都已具备，此处再接一次可覆盖 BeginPlay 时 Owner 尚未解析的情况。
	RefreshFishingLineAttachment();
	RefreshFishingLineShape();
	// 只在服务器已经创建成功的 Hook 真正进入 CastFlight 时播投杆动作；输入按下和瞄准不会经过这里。
	TryPlayCastMontageFromPresentation();
	RefreshBobberPresentationTimer();
	// 唯一对外通知点：蓝图据此播放钩子从 CastFlight -> Landed/Failed 等阶段切换的表现
	BP_OnHookPresentationChanged(Previous, Current);
}

void ACatFishingHookActor::RefreshBobberPresentationTimer()
{
	if (!GetWorld() || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const bool bNeedsContinuousUpdate = PresentationState.Phase == ECatFishingHookPresentationPhase::Landed
		&& (PresentationState.BobberMode == ECatFishingBobberPresentationMode::Calm
			|| PresentationState.BobberMode == ECatFishingBobberPresentationMode::BiteWarning);
	if (bNeedsContinuousUpdate)
	{
		if (!GetWorldTimerManager().IsTimerActive(BobberPresentationTimerHandle))
		{
			GetWorldTimerManager().SetTimer(BobberPresentationTimerHandle, this,
				&ThisClass::UpdateBobberPresentation, 1.0f / 60.0f, true);
		}
	}
	else
	{
		GetWorldTimerManager().ClearTimer(BobberPresentationTimerHandle);
	}
	UpdateBobberPresentation();
}

void ACatFishingHookActor::UpdateBobberPresentation()
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	double OffsetZ = 0.0;
	if (PresentationState.Phase == ECatFishingHookPresentationPhase::Landed)
	{
		const UCatFishingPresentationSettings* Settings = GetDefault<UCatFishingPresentationSettings>();
		const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
		const double ServerNow = GameState ? GameState->GetServerWorldTimeSeconds()
			: (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
		const double Elapsed = FMath::Max(0.0, ServerNow - PresentationState.BobberModeStartedServerTime);
		switch (PresentationState.BobberMode)
		{
		case ECatFishingBobberPresentationMode::Calm:
			if (Settings && FMath::IsFinite(Settings->BobberCalmAmplitudeCentimeters)
				&& FMath::IsFinite(Settings->BobberCalmFrequencyHz))
			{
				OffsetZ = FMath::Max(0.0, Settings->BobberCalmAmplitudeCentimeters)
					* FMath::Sin(Elapsed * 2.0 * UE_DOUBLE_PI * FMath::Max(0.0, Settings->BobberCalmFrequencyHz));
			}
			break;
		case ECatFishingBobberPresentationMode::BiteWarning:
			if (Settings && FMath::IsFinite(Settings->BobberWarningAmplitudeCentimeters)
				&& FMath::IsFinite(Settings->BobberWarningFrequencyHz))
			{
				OffsetZ = FMath::Max(0.0, Settings->BobberWarningAmplitudeCentimeters)
					* FMath::Sin(Elapsed * 2.0 * UE_DOUBLE_PI * FMath::Max(0.0, Settings->BobberWarningFrequencyHz));
			}
			break;
		case ECatFishingBobberPresentationMode::Sunk:
			if (Settings && FMath::IsFinite(Settings->BobberBiteSinkDepthCentimeters))
			{
				OffsetZ = -FMath::Max(0.0, Settings->BobberBiteSinkDepthCentimeters);
			}
			break;
		default:
			break;
		}
	}
	SetPresentationBobOffset(static_cast<float>(OffsetZ));
}

void ACatFishingHookActor::TryPlayCastMontageFromPresentation()
{
	if (bCastMontagePlayed || GetNetMode() == NM_DedicatedServer
		|| PresentationState.Phase != ECatFishingHookPresentationPhase::CastFlight)
	{
		return;
	}
	if (ACatCharacter* Character = Cast<ACatCharacter>(GetInstigator()))
	{
		// 只有确实开始播放才锁一次性标志；若 Instigator/动画实例尚未就绪，后续复制回调仍可补试。
		bCastMontagePlayed = Character->PlayFishingCastMontageFromPresentation();
	}
}

void ACatFishingHookActor::RefreshFishingLineAttachment()
{
	if (!FishingLine || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	ACatFishingRodActor* Rod = Cast<ACatFishingRodActor>(GetOwner());
	if (!IsValid(Rod))
	{
		FishingLine->SetVisibility(false, true);
		FishingLine->SetHiddenInGame(true);
		RefreshFishingLinePresentationTimer();
		return;
	}

	USceneComponent* EndComponent = nullptr;
	TInlineComponentArray<USceneComponent*> Components(Rod);
	for (USceneComponent* Component : Components)
	{
		if (Component && Component->ComponentHasTag(TEXT("RodTipMarker")))
		{
			EndComponent = Component;
			break;
		}
	}
	// 没放蓝图表现标记时回退原生 RodTipAnchor；再不济才回退根组件，始终保证端点可解析。
	if (!EndComponent)
	{
		for (USceneComponent* Component : Components)
		{
			if (Component && Component->GetFName() == TEXT("RodTipAnchor"))
			{
				EndComponent = Component;
				break;
			}
		}
	}
	if (!EndComponent)
	{
		EndComponent = Rod->GetRootComponent();
	}
	if (!EndComponent)
	{
		FishingLine->SetVisibility(false, true);
		FishingLine->SetHiddenInGame(true);
		RefreshFishingLinePresentationTimer();
		return;
	}

	if (FishingLine->GetAttachedComponent() != EndComponent)
	{
		FishingLine->SetAttachEndToComponent(EndComponent);
	}
	// RodTipMarker/原生 RodTipAnchor 的组件原点就是线端点；不要再混入权威锚点偏移，否则会绕过蓝图视觉校准。
	FishingLine->EndLocation = FVector::ZeroVector;
	const bool bShouldShow = PresentationState.Phase != ECatFishingHookPresentationPhase::Unconfigured;
	FishingLine->SetVisibility(bShouldShow, true);
	FishingLine->SetHiddenInGame(!bShouldShow);
	RefreshFishingLineShape();
	RefreshFishingLinePresentationTimer();
}

void ACatFishingHookActor::RefreshFishingLineShape()
{
	if (!FishingLine || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// 抛投阶段尚未产生战斗模拟快照，保留构造器/蓝图配置的预览长度，避免 Cable 暂时缩成 0。
	if (PresentationState.PaidOutLineLengthCentimeters <= KINDA_SMALL_NUMBER
		&& PresentationState.StraightLineDistanceCentimeters <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	const double DirectDistance = FMath::Max(0.0,
		static_cast<double>(PresentationState.StraightLineDistanceCentimeters));
	const double PaidOut = PresentationState.PaidOutLineLengthCentimeters > 0.0
		? PresentationState.PaidOutLineLengthCentimeters : DirectDistance;
	// 这里只更新目标，不直接改 CableLength；本地 60Hz 定时器负责吸收服务器固定步和网络包造成的阶跃。
	TargetFishingLineLengthCentimeters = FMath::Max(PaidOut, DirectDistance);
	const double SlackRatio = PaidOut > UE_DOUBLE_SMALL_NUMBER
		? FMath::Clamp(PresentationState.SlackLineLengthCentimeters / PaidOut, 0.0, 1.0) : 0.0;
	TargetFishingLineSlackRatio = SlackRatio;
}

void ACatFishingHookActor::RefreshFishingLinePresentationTimer()
{
	UWorld* World = GetWorld();
	if (!World || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const bool bShouldUpdate = FishingLine && FishingLineStartAnchor && VisualRoot
		&& FishingLine->GetAttachedComponent()
		&& PresentationState.Phase != ECatFishingHookPresentationPhase::Unconfigured;
	if (!bShouldUpdate)
	{
		World->GetTimerManager().ClearTimer(FishingLinePresentationTimerHandle);
		bFishingLineSmoothingInitialized = false;
		return;
	}

	const UCatFishingPresentationSettings* Settings = GetDefault<UCatFishingPresentationSettings>();
	const double ConfiguredInterval = Settings ? Settings->FishingLineVisualUpdateIntervalSeconds : 1.0 / 60.0;
	const float Interval = static_cast<float>(FMath::IsFinite(ConfiguredInterval) && ConfiguredInterval >= 0.005
		? ConfiguredInterval : 1.0 / 60.0);
	if (!World->GetTimerManager().IsTimerActive(FishingLinePresentationTimerHandle))
	{
		World->GetTimerManager().SetTimer(FishingLinePresentationTimerHandle, this,
			&ThisClass::UpdateFishingLinePresentation, Interval, true);
	}
	UpdateFishingLinePresentation();
}

void ACatFishingHookActor::UpdateFishingLinePresentation()
{
	UWorld* World = GetWorld();
	USceneComponent* EndComponent = FishingLine ? FishingLine->GetAttachedComponent() : nullptr;
	if (!World || GetNetMode() == NM_DedicatedServer || !FishingLine || !FishingLineStartAnchor
		|| !VisualRoot || !EndComponent)
	{
		return;
	}

	const UCatFishingPresentationSettings* Settings = GetDefault<UCatFishingPresentationSettings>();
	const auto SafeNonNegative = [](const double Value, const double Fallback)
	{
		return FMath::IsFinite(Value) && Value >= 0.0 ? Value : Fallback;
	};
	const double EndpointSpeed = SafeNonNegative(
		Settings ? Settings->FishingLineEndpointInterpolationSpeed : 18.0, 18.0);
	const double LengthSpeed = SafeNonNegative(
		Settings ? Settings->FishingLineLengthInterpolationSpeed : 14.0, 14.0);
	const double SlackSpeed = SafeNonNegative(
		Settings ? Settings->FishingLineSlackInterpolationSpeed : 10.0, 10.0);
	double DeltaSeconds = World->GetDeltaSeconds();
	if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0)
	{
		DeltaSeconds = Settings ? Settings->FishingLineVisualUpdateIntervalSeconds : 1.0 / 60.0;
	}

	const FVector TargetStart = VisualRoot->GetComponentLocation();
	if (!bFishingLineSmoothingInitialized)
	{
		FishingLineStartAnchor->SetWorldLocation(TargetStart);
		DisplayedFishingLineLengthCentimeters = TargetFishingLineLengthCentimeters;
		DisplayedFishingLineSlackRatio = TargetFishingLineSlackRatio;
		bFishingLineSmoothingInitialized = true;
	}
	else
	{
		FishingLineStartAnchor->SetWorldLocation(FMath::VInterpTo(
			FishingLineStartAnchor->GetComponentLocation(), TargetStart,
			static_cast<float>(DeltaSeconds), static_cast<float>(EndpointSpeed)));
		DisplayedFishingLineLengthCentimeters = FMath::FInterpTo(
			DisplayedFishingLineLengthCentimeters, TargetFishingLineLengthCentimeters,
			DeltaSeconds, LengthSpeed);
		DisplayedFishingLineSlackRatio = FMath::FInterpTo(
			DisplayedFishingLineSlackRatio, TargetFishingLineSlackRatio,
			DeltaSeconds, SlackSpeed);
	}

	const FVector EndWorldPosition = EndComponent->GetComponentTransform().TransformPosition(FishingLine->EndLocation);
	const double SmoothedDirectDistance = FVector::Distance(
		FishingLineStartAnchor->GetComponentLocation(), EndWorldPosition);
	// 表现线长永远不能短于两个平滑端点的直线距离，否则粒子约束会被瞬间压缩并形成“果冻波”。
	FishingLine->CableLength = static_cast<float>(FMath::Max(
		DisplayedFishingLineLengthCentimeters, SmoothedDirectDistance));

	const double Substep = Settings ? Settings->FishingLineSimulationSubstepSeconds : 0.01;
	FishingLine->SubstepTime = static_cast<float>(FMath::Clamp(
		FMath::IsFinite(Substep) ? Substep : 0.01, 0.005, 0.1));
	FishingLine->SolverIterations = FMath::Clamp(
		Settings ? Settings->FishingLineSolverIterations : 10, 1, 16);
	FishingLine->bUseSubstepping = true;
	FishingLine->bEnableStiffness = false;
	const double TautGravity = SafeNonNegative(
		Settings ? Settings->FishingLineTautGravityScale : 0.08, 0.08);
	const double SlackGravity = SafeNonNegative(
		Settings ? Settings->FishingLineSlackGravityScale : 1.0, 1.0);
	FishingLine->CableGravityScale = static_cast<float>(FMath::Lerp(
		TautGravity, SlackGravity, FMath::Clamp(DisplayedFishingLineSlackRatio, 0.0, 1.0)));
}
