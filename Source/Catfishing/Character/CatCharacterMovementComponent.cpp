#include "Character/CatCharacterMovementComponent.h"

#include "GameFramework/Character.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Logging/CatLog.h"
#include "Fishing/Debug/CatFishingMotionDiagnostics.h"
#include "Fishing/Simulation/CatFishingGroupModel.h"

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
	if (!Source || Input.Direction.ContainsNaN() || Input.GroupDesiredVelocity.ContainsNaN()
		|| Input.GroupLateralAcceleration.ContainsNaN() || Input.FormationCorrectionVelocity.ContainsNaN()
		|| (Input.bGroupDriven && (!Input.SourceId.IsValid() || Input.RosterVersion == 0
			|| Input.ControlEpoch == 0 || Input.MembershipEpoch == 0))
		|| !FMath::IsFinite(Input.AccelerationCentimetersPerSecondSquared) || Input.AccelerationCentimetersPerSecondSquared < 0.0
		|| !FMath::IsFinite(Input.BrakingDecelerationCentimetersPerSecondSquared) || Input.BrakingDecelerationCentimetersPerSecondSquared < 0.0
		|| !FMath::IsFinite(Input.SpeedLimitCentimetersPerSecond) || Input.SpeedLimitCentimetersPerSecond < 0.0)
	{
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fishing_group_traction_rejected RodActorId=%s World=%s NetMode=%d Authority=%d LocalRole=%d RosterVersion=%u ControlEpoch=%u MembershipEpoch=%u Result=InvalidInput"),
			*Input.SourceId.ToString(), *GetNameSafe(GetWorld()), GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : INDEX_NONE,
			CharacterOwner && CharacterOwner->HasAuthority(), CharacterOwner ? static_cast<int32>(CharacterOwner->GetLocalRole()) : INDEX_NONE,
			Input.RosterVersion, Input.ControlEpoch, Input.MembershipEpoch);
		ClearExternalTraction(Source);
		return;
	}
	if (TractionSource.IsValid() && TractionSource.Get() != Source)
	{
		if (UWorld* World = GetWorld(); World && World->GetTimeSeconds() >= NextSourceConflictDiagnosticSeconds)
		{
			UE_LOG(LogCatFishing, Warning,
				TEXT("Event=fishing_group_traction_source_conflict RodActorId=%s BoundRodActorId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Result=KeepCurrentSource"),
				*Input.SourceId.ToString(), *LiveTraction.SourceId.ToString(), *GetNameSafe(World), static_cast<int32>(World->GetNetMode()),
				CharacterOwner && CharacterOwner->HasAuthority(), CharacterOwner ? static_cast<int32>(CharacterOwner->GetLocalRole()) : INDEX_NONE);
			NextSourceConflictDiagnosticSeconds = World->GetTimeSeconds() + 0.5;
		}
		return;
	}
	TractionSource = Source;
	LiveTraction = Input;
	LiveTraction.Direction = Input.Direction.GetSafeNormal2D();
	LiveTraction.bActive &= !LiveTraction.Direction.IsNearlyZero();
}

FVector UCatCharacterMovementComponent::GetAcceptedFishingMoveIntent() const
{
	return GetMaxAcceleration() > UE_SMALL_NUMBER
		? (Acceleration / GetMaxAcceleration()).GetClampedToMaxSize(1.0) : FVector::ZeroVector;
}

void UCatCharacterMovementComponent::ClearExternalTraction(const UObject* Source)
{
	if (TractionSource.Get() != Source) return;
	TractionSource.Reset();
	LiveTraction = FCatExternalTractionInput{};
	MovementTraction = FCatExternalTractionInput{};
	bUseSavedTraction = false;
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
	else if ((MovementTraction.bGroupDriven || LiveTraction.bGroupDriven) && (!TractionSource.IsValid()
		|| MovementTraction.SourceId != LiveTraction.SourceId
		|| MovementTraction.bGroupDriven != LiveTraction.bGroupDriven
		|| MovementTraction.bWaitingForGroupSolve != LiveTraction.bWaitingForGroupSolve
		|| MovementTraction.RosterVersion != LiveTraction.RosterVersion
		|| MovementTraction.ControlEpoch != LiveTraction.ControlEpoch
		|| MovementTraction.AimInputEpoch != LiveTraction.AimInputEpoch
		|| MovementTraction.MembershipEpoch != LiveTraction.MembershipEpoch))
	{
		// 离队后或重新加入后的旧 SavedMove 不能重新绑定上一轮握持。
		UWorld* World = GetWorld();
		if (World && World->GetTimeSeconds() >= NextReplayDiagnosticSeconds)
		{
			UE_LOG(LogCatFishing, Log,
				TEXT("Event=fishing_group_saved_move_rejected RodActorId=%s World=%s NetMode=%d Authority=%d LocalRole=%d SavedRosterVersion=%u RosterVersion=%u SavedControlEpoch=%u ControlEpoch=%u SavedMembershipEpoch=%u MembershipEpoch=%u SavedAimInputEpoch=%u AimInputEpoch=%u Waiting=%d Result=UseCurrentMovementDomain"),
				*MovementTraction.SourceId.ToString(), *GetNameSafe(World), static_cast<int32>(World->GetNetMode()),
				CharacterOwner && CharacterOwner->HasAuthority(), CharacterOwner ? static_cast<int32>(CharacterOwner->GetLocalRole()) : INDEX_NONE,
				MovementTraction.RosterVersion, LiveTraction.RosterVersion, MovementTraction.ControlEpoch, LiveTraction.ControlEpoch,
				MovementTraction.MembershipEpoch, LiveTraction.MembershipEpoch, MovementTraction.AimInputEpoch, LiveTraction.AimInputEpoch,
				LiveTraction.bWaitingForGroupSolve);
			NextReplayDiagnosticSeconds = World->GetTimeSeconds() + 0.5;
		}
		MovementTraction = TractionSource.IsValid() ? LiveTraction : FCatExternalTractionInput{};
	}
	const FVector Before = UpdatedComponent ? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
	const FVector VelocityBefore = Velocity;
	{
		// 牵引与固定步张力互相反馈；低帧率不能把整段外力一次积分成大位移。
		// 实时移动和 SavedMove 共用相同的 CMC 子步规则，碰撞/滑动仍由引擎执行。
		const bool bTractionMovement = MovementTraction.bActive || MovementTraction.bGroupDriven;
		const float TractionStep = bTractionMovement ? FMath::Min(MaxSimulationTimeStep, 1.0f / 120.0f) : MaxSimulationTimeStep;
		TGuardValue<float> StepGuard(MaxSimulationTimeStep, TractionStep);
		TGuardValue<int32> IterationGuard(MaxSimulationIterations, bTractionMovement
			? FMath::Max(MaxSimulationIterations, FMath::CeilToInt(FMath::Clamp(DeltaSeconds, 0.0f, 0.25f) / TractionStep) + 1)
			: MaxSimulationIterations);
		Super::PerformMovement(DeltaSeconds);
	}
	if (MovementTraction.bGroupDriven && !MovementTraction.bWaitingForGroupSolve && DeltaSeconds > 0.0f && HasValidData()
		&& !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && MovementMode != MOVE_None)
	{
		// 队形误差是一次受碰撞限制的位置修正，不是动力速度。若加入 Velocity，CMC 多子步
		// 会把前一步修正再次积分成惯性，站位误差固定时也会越走越快。
		FVector Correction = ConstrainDirectionToPlane(MovementTraction.FormationCorrectionVelocity);
		Correction.Z = 0.0;
		const FVector CorrectionDelta = Correction * DeltaSeconds;
		if (!CorrectionDelta.IsNearlyZero())
		{
			FHitResult Hit;
			SafeMoveUpdatedComponent(CorrectionDelta, UpdatedComponent->GetComponentQuat(), true, Hit);
			if (Hit.IsValidBlockingHit())
			{
				SlideAlongSurface(CorrectionDelta, 1.0f - Hit.Time, Hit.Normal, Hit, true);
			}
		}
	}
	bUseSavedTraction = false;
	UWorld* World = GetWorld();
	if (World && CharacterOwner && ((!bReplaying && (bLastTractionActive != MovementTraction.bActive
		|| (MovementTraction.bActive && World->GetTimeSeconds() >= NextTractionDiagnosticSeconds)))
		|| (bReplaying && CatFishingMotionDiagnostics::IsDetailedEnabled() && MovementTraction.bActive
			&& World->GetTimeSeconds() >= NextReplayDiagnosticSeconds)))
	{
		// 来源已清除的退出帧仍关联最后一根竿，仅用于日志，不恢复任何旧牵引。
		const FGuid DiagnosticSourceId = MovementTraction.SourceId.IsValid()
			? MovementTraction.SourceId : LastTractionDiagnosticSourceId;
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=fishing_carrier_movement_sample RodActorId=%s Holder=%s Active=%s AccelerationCmS2=%.3f BrakingDecelerationCmS2=%.3f "
				"Velocity=%s ActualDelta=%s MovementMode=%d World=%s NetMode=%d Authority=%s LocalRole=%d Model=CMCForceIntegration "
				"Frame=%llu WorldTime=%.6f DeltaSeconds=%.6f Replay=%s VelocityBefore=%s LocationBefore=%s InputAccelerationCmS2=%s "
				"PullDirection=%s SpeedLimitCmS=%.3f GroupDriven=%d RosterVersion=%u ControlEpoch=%u MembershipEpoch=%u FormationCorrectionCmS=%s"),
			*DiagnosticSourceId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(CharacterOwner), MovementTraction.bActive ? TEXT("true") : TEXT("false"),
			MovementTraction.AccelerationCentimetersPerSecondSquared, MovementTraction.BrakingDecelerationCentimetersPerSecondSquared, *Velocity.ToCompactString(),
			*(UpdatedComponent ? UpdatedComponent->GetComponentLocation() - Before : FVector::ZeroVector).ToCompactString(),
			static_cast<int32>(MovementMode), *GetNameSafe(World), static_cast<int32>(World->GetNetMode()),
			CharacterOwner->HasAuthority() ? TEXT("true") : TEXT("false"), static_cast<int32>(CharacterOwner->GetLocalRole()),
			GFrameCounter, World->GetTimeSeconds(), DeltaSeconds, bReplaying ? TEXT("true") : TEXT("false"),
			*VelocityBefore.ToCompactString(), *Before.ToCompactString(), *Acceleration.ToCompactString(),
			*MovementTraction.Direction.ToCompactString(), MovementTraction.SpeedLimitCentimetersPerSecond,
			MovementTraction.bGroupDriven, MovementTraction.RosterVersion, MovementTraction.ControlEpoch, MovementTraction.MembershipEpoch,
			*MovementTraction.FormationCorrectionVelocity.ToCompactString());
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
	if (MovementTraction.bGroupDriven && MovementTraction.bWaitingForGroupSolve)
	{
		TGuardValue<FVector> InputGuard(Acceleration, FVector::ZeroVector);
		const bool bPreviousRequestedVelocity = bHasRequestedVelocity;
		const bool bPreviousForceMaxAccel = bForceMaxAccel;
		bHasRequestedVelocity = bForceMaxAccel = false;
		Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
		bHasRequestedVelocity = bPreviousRequestedVelocity;
		bForceMaxAccel = bPreviousForceMaxAccel;
		return;
	}
	if (MovementTraction.bGroupDriven && DeltaTime > 0.0f && HasValidData()
		&& !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && MovementMode != MOVE_None)
	{
		const FVector Axis = MovementTraction.Direction.GetSafeNormal2D();
		const FVector PreviousVelocity = Velocity;
		// 禁用个人的第二份行走加速；组结果通过同一个 CMC 速度/碰撞入口落位。
		{
			TGuardValue<FVector> InputGuard(Acceleration, FVector::ZeroVector);
			const bool bPreviousRequestedVelocity = bHasRequestedVelocity;
			const bool bPreviousForceMaxAccel = bForceMaxAccel;
			bHasRequestedVelocity = bForceMaxAccel = false;
			Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
			bHasRequestedVelocity = bPreviousRequestedVelocity;
			bForceMaxAccel = bPreviousForceMaxAccel;
		}
		const FVector Target = MovementTraction.GroupDesiredVelocity;
		const double DesiredAlong = FVector::DotProduct(Target, Axis);
		const double PreviousAlong = FVector::DotProduct(PreviousVelocity, Axis);
		const double NetAcceleration = MovementTraction.AccelerationCentimetersPerSecondSquared
			- MovementTraction.BrakingDecelerationCentimetersPerSecondSquared;
		double Along = PreviousAlong + NetAcceleration * DeltaTime;
		// 静止支撑只能减速；主动后退才允许越过零速度。
		Along = FMath::Max(FMath::Min(0.0, DesiredAlong), Along);
		const double AlongLimit = FMath::Max(FMath::Max(0.0, DesiredAlong), MovementTraction.SpeedLimitCentimetersPerSecond);
		if (NetAcceleration > 0.0) Along = FMath::Min(AlongLimit, Along);
		FVector Lateral = PreviousVelocity - Axis * PreviousAlong;
		Lateral.Z = 0.0;
		const FVector LateralTarget = Target - Axis * DesiredAlong;
		const double EffectiveFriction = FMath::Max(0.0f,
			(bUseSeparateBrakingFriction ? BrakingFriction : Friction) * BrakingFrictionFactor);
		const FVector PreviousLateral = Lateral;
		FCatFishingGroupModel::IntegrateLateralVelocity(PreviousLateral, LateralTarget,
			MovementTraction.GroupLateralAcceleration, DeltaTime, EffectiveFriction, BrakingDeceleration, Lateral);
		const double Vertical = Velocity.Z;
		Velocity = Axis * Along + Lateral;
		Velocity.Z = Vertical;
		return;
	}
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
	return !Traction.bActive && !Traction.bGroupDriven
		&& !static_cast<const FCatSavedMove*>(NewMove.Get())->Traction.bGroupDriven
		&& !static_cast<const FCatSavedMove*>(NewMove.Get())->Traction.bActive
		&& Super::CanCombineWith(NewMove, Character, MaxDelta);
}
