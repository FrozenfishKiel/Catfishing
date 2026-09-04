#include "Fishing/Actors/CatFishingHookActor.h"

#include "Character/CatCharacter.h"
#include "Components/SceneComponent.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/Presentation/CatFishingLineCurveComponent.h"
#include "Fishing/Presentation/CatFishingPresentationSettings.h"
#include "GameFramework/GameStateBase.h"
#include "Logging/CatLog.h"
#include "Logging/CatLogContext.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

ACatFishingHookActor::ACatFishingHookActor()
{
	bReplicates = true;
	SetReplicateMovement(true); // 落水后使用移动复制；飞行期间复制冻结弹道，避免逐包跳动。
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
	// 曲线起点使用本地绝对坐标锚点：Hook 根节点仍按服务器/网络快照移动，锚点在本机平滑追赶。
	FishingLineStartAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("FishingLineStartAnchor"));
	FishingLineStartAnchor->SetupAttachment(SceneRoot);
	FishingLineStartAnchor->SetAbsolute(true, true, true);
	// 曲线只消费已复制端点和线长，无粒子模拟，不向玩法提供反力。
	FishingLineCurve = CreateDefaultSubobject<UCatFishingLineCurveComponent>(TEXT("FishingLineCurve"));
	FishingLineCurve->SetupAttachment(FishingLineStartAnchor);
	FishingLineCurve->SetVisibility(false, true);
	FishingLineCurve->SetHiddenInGame(true);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FishingLineMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (FishingLineMaterial.Succeeded())
	{
		FishingLineCurve->SetMaterial(0, FishingLineMaterial.Object);
	}
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

bool ACatFishingHookActor::BeginAuthoritativeFlight(const FVector& ExpectedLandingWorldPoint)
{
	FCatFishingCastTrajectory Trajectory;
	if (!HasAuthority() || !bIdentityInitialized || bLandingFinalized || !GetWorld()
		|| PresentationState.CastTrajectory.DurationSeconds > 0.0
		|| !Trajectory.Initialize(GetActorLocation(), ExpectedLandingWorldPoint,
			GetWorld()->GetGravityZ(), GetWorld()->GetTimeSeconds()))
	{
		UE_LOG(LogCatFishing, Warning, TEXT("Event=cast_flight_rejected World=%s WorldNetMode=%d Authority=%d Role=%s Actor=%s Session=%s CastAttempt=%s Error=InvalidFlight"),
			*GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), *UEnum::GetValueAsString(GetLocalRole()), *GetName(),
			*PresentationState.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *PresentationState.CastAttemptId.ToString(EGuidFormats::DigitsWithHyphens));
		return false;
	}
	const FCatFishingHookPresentationState Previous = PresentationState;
	PresentationState.CastTrajectory = Trajectory;
	SetReplicateMovement(false);
	QueueOrDispatchPresentationChanged(Previous, PresentationState);
	RefreshCastFlight();
	ForceNetUpdate();
	UE_LOG(LogCatFishing, Log, TEXT("Event=cast_flight_started World=%s WorldNetMode=%d Authority=%d Role=%s Actor=%s Session=%s CastAttempt=%s Origin=%s Landing=%s Velocity=%s Duration=%.3f %s"),
		*GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), *UEnum::GetValueAsString(GetLocalRole()), *GetName(),
		*PresentationState.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *PresentationState.CastAttemptId.ToString(EGuidFormats::DigitsWithHyphens),
		*Trajectory.Origin.ToString(), *Trajectory.Landing.ToString(), *Trajectory.InitialVelocity.ToString(),
		Trajectory.DurationSeconds, *CatLogContext::BuildControllerFields(GetInstigatorController()));
	return true;
}

void ACatFishingHookActor::RefreshCastFlight()
{
	if (!GetWorld()) return;
	if (PresentationState.Phase != ECatFishingHookPresentationPhase::CastFlight
		|| PresentationState.CastTrajectory.DurationSeconds <= 0.0)
	{
		GetWorldTimerManager().ClearTimer(CastFlightTimerHandle);
		return;
	}
	if (!GetWorldTimerManager().IsTimerActive(CastFlightTimerHandle))
	{
		GetWorldTimerManager().SetTimer(CastFlightTimerHandle, this, &ThisClass::UpdateCastFlight, 1.0f / 60.0f, true);
	}
	UpdateCastFlight();
}

void ACatFishingHookActor::UpdateCastFlight()
{
	const FCatFishingCastTrajectory& Flight = PresentationState.CastTrajectory;
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	const double Now = !HasAuthority() && GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	SetActorLocation(Flight.Evaluate(Now));
	if (Now >= Flight.StartedServerTime + Flight.DurationSeconds)
	{
		GetWorldTimerManager().ClearTimer(CastFlightTimerHandle);
		if (HasAuthority()) FinalizeAuthoritativeLandingOnce(true, Flight.Landing);
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
	GetWorldTimerManager().ClearTimer(CastFlightTimerHandle);
	SetActorLocation(LandingWorldPoint);
	SetReplicateMovement(true);
	const FCatFishingHookPresentationState Previous = PresentationState;
	// 落地成功进入 Landed（等待鱼咬钩），失败（例如落在非法区域）进入 Failed 供表现层播放对应反馈
	PresentationState.Phase = bSucceeded ? ECatFishingHookPresentationPhase::Landed : ECatFishingHookPresentationPhase::Failed;
	if (bSucceeded)
	{
		// 落水时发布 L_paid=D、Slack=0，给搏斗前的曲线一份真实基线；不改变权威玩法范围。
		// 飞行中尚无已放线快照时只按端点距离绘制，不保留旧 Cable 的占位长度制造假余线。
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
	UE_LOG(LogCatFishing, Log, TEXT("Event=cast_flight_landed World=%s WorldNetMode=%d Authority=%d Role=%s Actor=%s Session=%s CastAttempt=%s Succeeded=%d Landing=%s %s"),
		*GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), *UEnum::GetValueAsString(GetLocalRole()), *GetName(),
		*PresentationState.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *PresentationState.CastAttemptId.ToString(EGuidFormats::DigitsWithHyphens), bSucceeded,
		*LandingWorldPoint.ToString(), *CatLogContext::BuildControllerFields(GetInstigatorController()));
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
		// 专用服务器不生成表现网格。
		FishingLineCurve->SetVisibility(false, true);
		FishingLineCurve->SetHiddenInGame(true);
	}
	else
	{
		if (UMaterialInstanceDynamic* Material = FishingLineCurve->CreateDynamicMaterialInstance(0))
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
	GetWorldTimerManager().ClearTimer(CastFlightTimerHandle);
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
	if (Previous.Phase != PresentationState.Phase || Previous.CastTrajectory.DurationSeconds != PresentationState.CastTrajectory.DurationSeconds)
	{
		UE_LOG(LogCatFishing, Log, TEXT("Event=cast_flight_received World=%s WorldNetMode=%d Authority=%d Role=%s Actor=%s Session=%s CastAttempt=%s Phase=%s Landing=%s Duration=%.3f %s"),
			*GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), *UEnum::GetValueAsString(GetLocalRole()), *GetName(),
			*PresentationState.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *PresentationState.CastAttemptId.ToString(EGuidFormats::DigitsWithHyphens),
			*UEnum::GetValueAsString(PresentationState.Phase), *PresentationState.CastTrajectory.Landing.ToString(),
			PresentationState.CastTrajectory.DurationSeconds, *CatLogContext::BuildControllerFields(GetInstigatorController()));
	}
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
	RefreshCastFlight();
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
	// 首次 Owner 复制可能早于 BeginPlay；由 BeginPlay 或后续复制回调补接。
	if (!HasActorBegunPlay() || !FishingLineCurve || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	ACatFishingRodActor* Rod = Cast<ACatFishingRodActor>(GetOwner());
	if (!IsValid(Rod))
	{
		FishingLineEndAnchor.Reset();
		FishingLineCurve->SetVisibility(false, true);
		FishingLineCurve->SetHiddenInGame(true);
		FishingLineCurve->ClearCurve();
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
		FishingLineEndAnchor.Reset();
		FishingLineCurve->SetVisibility(false, true);
		FishingLineCurve->SetHiddenInGame(true);
		FishingLineCurve->ClearCurve();
		RefreshFishingLinePresentationTimer();
		return;
	}

	// RodTipMarker/原生 RodTipAnchor 的组件原点就是线端点，不混入权威锚点偏移。
	FishingLineEndAnchor = EndComponent;
	const bool bShouldShow = PresentationState.Phase != ECatFishingHookPresentationPhase::Unconfigured;
	FishingLineCurve->SetVisibility(bShouldShow, true);
	FishingLineCurve->SetHiddenInGame(!bShouldShow);
	RefreshFishingLineShape();
	RefreshFishingLinePresentationTimer();
}

void ACatFishingHookActor::RefreshFishingLineShape()
{
	if (!FishingLineCurve || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// 抛投阶段尚无战斗快照，绘制长度以下游端点距离为下限。
	if (PresentationState.PaidOutLineLengthCentimeters <= KINDA_SMALL_NUMBER
		&& PresentationState.StraightLineDistanceCentimeters <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	const double DirectDistance = FMath::Max(0.0,
		static_cast<double>(PresentationState.StraightLineDistanceCentimeters));
	const double PaidOut = PresentationState.PaidOutLineLengthCentimeters > 0.0
		? PresentationState.PaidOutLineLengthCentimeters : DirectDistance;
	// 这里只更新目标，本地定时器吸收服务器固定步和网络包造成的长度阶跃。
	TargetFishingLineLengthCentimeters = FMath::Max(PaidOut, DirectDistance);
}

void ACatFishingHookActor::RefreshFishingLinePresentationTimer()
{
	UWorld* World = GetWorld();
	if (!World || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const bool bShouldUpdate = FishingLineCurve && FishingLineStartAnchor && VisualRoot
		&& FishingLineEndAnchor.IsValid()
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
	USceneComponent* EndComponent = FishingLineEndAnchor.Get();
	if (!World || GetNetMode() == NM_DedicatedServer || !FishingLineCurve || !FishingLineStartAnchor
		|| !VisualRoot || !EndComponent)
	{
		if (World && FishingLineCurve && !EndComponent)
		{
			FishingLineCurve->ClearCurve();
			FishingLineCurve->SetVisibility(false, true);
			World->GetTimerManager().ClearTimer(FishingLinePresentationTimerHandle);
			bFishingLineSmoothingInitialized = false;
		}
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
	double DeltaSeconds = World->GetDeltaSeconds();
	if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0)
	{
		DeltaSeconds = Settings ? Settings->FishingLineVisualUpdateIntervalSeconds : 1.0 / 60.0;
	}

	const FVector TargetStart = VisualRoot->GetComponentLocation();
	const bool bInitializeSmoothing = !bFishingLineSmoothingInitialized;
	if (bInitializeSmoothing)
	{
		FishingLineStartAnchor->SetWorldLocation(TargetStart);
		DisplayedFishingLineLengthCentimeters = TargetFishingLineLengthCentimeters;
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
	}

	const FVector StartWorldPosition = FishingLineStartAnchor->GetComponentLocation();
	const FVector EndWorldPosition = EndComponent->GetComponentLocation();
	const int32 Segments = FMath::Clamp(Settings ? Settings->FishingLineCurveSegments : 64, 4, 256);
	const double Width = FMath::Clamp(SafeNonNegative(Settings ? Settings->FishingLineWidthCentimeters : 1.25, 1.25), 0.01, 10.0);
	const bool bUpdated = FishingLineCurve->UpdateCurve(StartWorldPosition, EndWorldPosition,
		DisplayedFishingLineLengthCentimeters, Segments, Width);
	if (!bUpdated)
	{
		if (!bFishingLineCurveUpdateFailed)
		{
			UE_LOG(LogCatFishing, Warning, TEXT("Event=fishing_line_curve_rejected World=%s WorldNetMode=%d Authority=%d Role=%s Actor=%s Session=%s CastAttempt=%s Error=InvalidGeometry Start=%s End=%s PaidLengthCm=%.2f"),
				*GetNameSafe(World), GetNetMode(), HasAuthority(), *UEnum::GetValueAsString(GetLocalRole()), *GetName(),
				*PresentationState.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *PresentationState.CastAttemptId.ToString(EGuidFormats::DigitsWithHyphens),
				*StartWorldPosition.ToString(), *EndWorldPosition.ToString(), DisplayedFishingLineLengthCentimeters);
		}
		bFishingLineCurveUpdateFailed = true;
		return;
	}
	if (bInitializeSmoothing || bFishingLineCurveUpdateFailed)
	{
		UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_line_curve_configured World=%s WorldNetMode=%d Authority=%d Role=%s Actor=%s Rod=%s Session=%s CastAttempt=%s Renderer=LengthMatchedCurve Segments=%d WidthCm=%.3f PaidLengthCm=%.2f CurveLengthCm=%.2f Result=Applied %s"),
			*GetNameSafe(World), GetNetMode(), HasAuthority(), *UEnum::GetValueAsString(GetLocalRole()),
			*GetName(), *GetNameSafe(GetOwner()),
			*PresentationState.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			*PresentationState.CastAttemptId.ToString(EGuidFormats::DigitsWithHyphens),
			Segments, Width, DisplayedFishingLineLengthCentimeters, FishingLineCurve->GetCurveLengthCentimeters(),
			*CatLogContext::BuildControllerFields(GetInstigatorController()));
	}
	bFishingLineCurveUpdateFailed = false;
}
