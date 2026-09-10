#include "Character/CatCharacterMovementComponent.h"

#include "GameFramework/Character.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Logging/CatLog.h"
#include "Fishing/Debug/CatFishingMotionDiagnostics.h"

namespace
{
	class FCatNetworkPredictionDataClient : public FNetworkPredictionData_Client_Character
	{
	public:
		explicit FCatNetworkPredictionDataClient(const UCharacterMovementComponent& Movement)
			: FNetworkPredictionData_Client_Character(Movement) {}
		virtual FSavedMovePtr AllocateNewMove() override { return FSavedMovePtr(new FCatSavedMove()); }
	};
}

void UCatCharacterMovementComponent::SetExternalTraction(const UObject* Source, const FCatExternalTractionInput& Input)
{
	if (!Source || Input.Direction.ContainsNaN()
		|| !FMath::IsFinite(Input.AccelerationCentimetersPerSecondSquared) || Input.AccelerationCentimetersPerSecondSquared < 0.0
		|| !FMath::IsFinite(Input.BrakingDecelerationCentimetersPerSecondSquared) || Input.BrakingDecelerationCentimetersPerSecondSquared < 0.0
		|| !FMath::IsFinite(Input.SpeedLimitCentimetersPerSecond) || Input.SpeedLimitCentimetersPerSecond < 0.0)
	{
		ClearExternalTraction(Source);
		return;
	}
	TractionSource = Source;
	LiveTraction = Input;
	LiveTraction.Direction = Input.Direction.GetSafeNormal2D();
	LiveTraction.bActive &= !LiveTraction.Direction.IsNearlyZero();
}

void UCatCharacterMovementComponent::ClearExternalTraction(const UObject* Source)
{
	if (TractionSource.Get() != Source) return;
	TractionSource.Reset();
	LiveTraction = FCatExternalTractionInput{};
}

void UCatCharacterMovementComponent::RestoreTractionForSavedMove(const FCatExternalTractionInput& Input)
{
	MovementTraction = Input;
	bUseSavedTraction = true;
}

double UCatCharacterMovementComponent::GetExternalTractionTravelLimit(const FVector& Direction, const double MaximumDistance) const
{
	if (!HasValidData() || !UpdatedPrimitive || !GetWorld() || MovementMode == MOVE_None || UpdatedPrimitive->IsSimulatingPhysics()
		|| HasAnimRootMotion() || CurrentRootMotion.HasOverrideVelocity()
		|| !FMath::IsFinite(MaximumDistance) || MaximumDistance <= 0.0) return 0.0;
	const FVector Axis = ConstrainDirectionToPlane(Direction.GetSafeNormal2D());
	if (Axis.IsNearlyZero()) return 0.0;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FishingTraction), false, CharacterOwner);
	FCollisionResponseParams Response;
	InitCollisionParams(Params, Response);
	FHitResult Hit;
	const FVector Start = UpdatedComponent->GetComponentLocation();
	const bool bBlocked = GetWorld()->SweepSingleByChannel(Hit, Start, Start + Axis * MaximumDistance,
		UpdatedComponent->GetComponentQuat(), UpdatedPrimitive->GetCollisionObjectType(),
		UpdatedPrimitive->GetCollisionShape(), Params, Response);
	// 可行走地面仍由 CMC 的坡面/台阶逻辑处理；探测绝不移动角色或触发重叠事件。
	if (bBlocked && !IsWalkable(Hit) && FVector::DotProduct(Hit.Normal, Axis) < -0.001)
		return FMath::Max(0.0, MaximumDistance * Hit.Time - 0.1);
	return MaximumDistance;
}

FNetworkPredictionData_Client* UCatCharacterMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		const_cast<UCatCharacterMovementComponent*>(this)->ClientPredictionData = new FCatNetworkPredictionDataClient(*this);
	}
	return ClientPredictionData;
}

void UCatCharacterMovementComponent::PerformMovement(const float DeltaSeconds)
{
	const bool bReplaying = bUseSavedTraction;
	if (!bReplaying) MovementTraction = TractionSource.IsValid() ? LiveTraction : FCatExternalTractionInput{};
	const FVector Before = UpdatedComponent ? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
	const FVector VelocityBefore = Velocity;
	{
		// 牵引与固定步张力互相反馈；低帧率不能把整段外力一次积分成大位移。
		// 实时移动和 SavedMove 共用相同的 CMC 子步规则，碰撞/滑动仍由引擎执行。
		const float TractionStep = MovementTraction.bActive ? FMath::Min(MaxSimulationTimeStep, 1.0f / 120.0f) : MaxSimulationTimeStep;
		TGuardValue<float> StepGuard(MaxSimulationTimeStep, TractionStep);
		TGuardValue<int32> IterationGuard(MaxSimulationIterations, MovementTraction.bActive
			? FMath::Max(MaxSimulationIterations, FMath::CeilToInt(FMath::Clamp(DeltaSeconds, 0.0f, 0.25f) / TractionStep) + 1)
			: MaxSimulationIterations);
		Super::PerformMovement(DeltaSeconds);
	}
	bUseSavedTraction = false;
	UWorld* World = GetWorld();
	if (World && CharacterOwner && ((!bReplaying && (bLastTractionActive != MovementTraction.bActive
		|| (MovementTraction.bActive && World->GetTimeSeconds() >= NextTractionDiagnosticSeconds)))
		|| (bReplaying && CatFishingMotionDiagnostics::IsDetailedEnabled() && MovementTraction.bActive
			&& World->GetTimeSeconds() >= NextReplayDiagnosticSeconds)))
	{
		// 来源已清除的退出帧仍关联最后一根竿，仅用于日志，不恢复任何失效牵引。
		const FGuid DiagnosticSourceId = MovementTraction.SourceId.IsValid()
			? MovementTraction.SourceId : LastTractionDiagnosticSourceId;
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=fishing_carrier_movement_sample RodActorId=%s Holder=%s Active=%s AccelerationCmS2=%.3f BrakingDecelerationCmS2=%.3f "
				"Velocity=%s ActualDelta=%s MovementMode=%d World=%s NetMode=%d Authority=%s LocalRole=%d Model=CMCForceIntegration "
				"Frame=%llu WorldTime=%.6f DeltaSeconds=%.6f Replay=%s VelocityBefore=%s LocationBefore=%s InputAccelerationCmS2=%s "
				"PullDirection=%s SpeedLimitCmS=%.3f"),
			*DiagnosticSourceId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(CharacterOwner), MovementTraction.bActive ? TEXT("true") : TEXT("false"),
			MovementTraction.AccelerationCentimetersPerSecondSquared, MovementTraction.BrakingDecelerationCentimetersPerSecondSquared, *Velocity.ToCompactString(),
			*(UpdatedComponent ? UpdatedComponent->GetComponentLocation() - Before : FVector::ZeroVector).ToCompactString(),
			static_cast<int32>(MovementMode), *GetNameSafe(World), static_cast<int32>(World->GetNetMode()),
			CharacterOwner->HasAuthority() ? TEXT("true") : TEXT("false"), static_cast<int32>(CharacterOwner->GetLocalRole()),
			GFrameCounter, World->GetTimeSeconds(), DeltaSeconds, bReplaying ? TEXT("true") : TEXT("false"),
			*VelocityBefore.ToCompactString(), *Before.ToCompactString(), *Acceleration.ToCompactString(),
			*MovementTraction.Direction.ToCompactString(), MovementTraction.SpeedLimitCentimetersPerSecond);
		if (bReplaying)
		{
			NextReplayDiagnosticSeconds = World->GetTimeSeconds() + CatFishingMotionDiagnostics::SampleIntervalSeconds();
		}
		else
		{
			LastTractionDiagnosticSourceId = DiagnosticSourceId;
			bLastTractionActive = MovementTraction.bActive;
			NextTractionDiagnosticSeconds = World->GetTimeSeconds() + CatFishingMotionDiagnostics::SampleIntervalSeconds();
		}
	}
}

void UCatCharacterMovementComponent::CalcVelocity(const float DeltaTime, const float Friction,
	const bool bFluid, const float BrakingDeceleration)
{
	const double PreviousPullSpeed = FVector::DotProduct(Velocity, MovementTraction.Direction);
	Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
	if (!MovementTraction.bActive || DeltaTime <= 0.0f || !HasValidData()
		|| HasAnimRootMotion() || CurrentRootMotion.HasOverrideVelocity() || MovementMode == MOVE_None) return;
	// 支撑与拉力来自同一权威计算。过平衡点时连续减速，不能突然切回普通行走急刹。
	// 减速只作用于向鱼运动；静止不会反向滑走，主动远离鱼仍走原行走/制动规则。
	const double PullAcceleration = MovementTraction.AccelerationCentimetersPerSecondSquared;
	if (PreviousPullSpeed < 0.0 && PullAcceleration <= 0.0) return;
	double IntegratedPullSpeed = PreviousPullSpeed + (PullAcceleration
		- MovementTraction.BrakingDecelerationCentimetersPerSecondSquared) * DeltaTime;
	if (PreviousPullSpeed >= 0.0) IntegratedPullSpeed = FMath::Max(0.0, IntegratedPullSpeed);
	if (PullAcceleration > 0.0) IntegratedPullSpeed = FMath::Min(MovementTraction.SpeedLimitCentimetersPerSecond, IntegratedPullSpeed);
	const double ActualPullSpeed = FVector::DotProduct(Velocity, MovementTraction.Direction);
	// 无输入时替换沿线制动，保留其余轴；有输入时保留引擎完成的主动加速，牵引仅提供下限。
	if ((Acceleration.IsNearlyZero() && !bHasRequestedVelocity) || ActualPullSpeed < IntegratedPullSpeed)
	{
		Velocity += MovementTraction.Direction * (IntegratedPullSpeed - ActualPullSpeed);
	}
}

void FCatSavedMove::Clear()
{
	Super::Clear();
	Traction = FCatExternalTractionInput{};
}

void FCatSavedMove::SetMoveFor(ACharacter* Character, const float InDeltaTime, const FVector& NewAccel,
	FNetworkPredictionData_Client_Character& ClientData)
{
	Super::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);
	Traction = CastChecked<UCatCharacterMovementComponent>(Character->GetCharacterMovement())->GetExternalTraction();
}

void FCatSavedMove::PrepMoveFor(ACharacter* Character)
{
	Super::PrepMoveFor(Character);
	CastChecked<UCatCharacterMovementComponent>(Character->GetCharacterMovement())->RestoreTractionForSavedMove(Traction);
}

bool FCatSavedMove::CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, const float MaxDelta) const
{
	// 受力与碰撞积分依赖原始步长；搏斗移动禁止合并，避免吞掉松/绷线切换或重放成大步长。
	return !Traction.bActive && !static_cast<const FCatSavedMove*>(NewMove.Get())->Traction.bActive
		&& Super::CanCombineWith(NewMove, Character, MaxDelta);
}
