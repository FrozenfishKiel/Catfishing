#include "Character/CatCharacterMovementComponent.h"

#include "GameFramework/Character.h"
#include "Engine/World.h"
#include "Logging/CatLog.h"

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
	Super::PerformMovement(DeltaSeconds);
	bUseSavedTraction = false;
	UWorld* World = GetWorld();
	if (!bReplaying && World && CharacterOwner && (bLastTractionActive != MovementTraction.bActive
		|| (MovementTraction.bActive && World->GetTimeSeconds() >= NextTractionDiagnosticSeconds)))
	{
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=fishing_carrier_movement_sample RodActorId=%s Holder=%s Active=%s AccelerationCmS2=%.3f "
				"Velocity=%s ActualDelta=%s MovementMode=%d World=%s NetMode=%d Authority=%s LocalRole=%d Model=CMCForceIntegration"),
			*MovementTraction.SourceId.ToString(), *GetNameSafe(CharacterOwner), MovementTraction.bActive ? TEXT("true") : TEXT("false"),
			MovementTraction.AccelerationCentimetersPerSecondSquared, *Velocity.ToCompactString(),
			*(UpdatedComponent ? UpdatedComponent->GetComponentLocation() - Before : FVector::ZeroVector).ToCompactString(),
			static_cast<int32>(MovementMode), *GetNameSafe(World), static_cast<int32>(World->GetNetMode()),
			CharacterOwner->HasAuthority() ? TEXT("true") : TEXT("false"), static_cast<int32>(CharacterOwner->GetLocalRole()));
		bLastTractionActive = MovementTraction.bActive;
		NextTractionDiagnosticSeconds = World->GetTimeSeconds() + 1.0;
	}
}

void UCatCharacterMovementComponent::CalcVelocity(const float DeltaTime, const float Friction,
	const bool bFluid, const float BrakingDeceleration)
{
	const double PreviousPullSpeed = FVector::DotProduct(Velocity, MovementTraction.Direction);
	Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
	if (!MovementTraction.bActive || DeltaTime <= 0.0f || !HasValidData()
		|| HasAnimRootMotion() || CurrentRootMotion.HasOverrideVelocity() || MovementMode == MOVE_None) return;
	// 地面支撑已在外力生产者中扣除。保留外力速度，不让行走制动再次吃掉同一份牵引；
	// 随后的 SafeMove/SlideAlongSurface 才产生真实位置和碰撞后速度。
	const double IntegratedPullSpeed = FMath::Min(MovementTraction.SpeedLimitCentimetersPerSecond,
		PreviousPullSpeed + MovementTraction.AccelerationCentimetersPerSecondSquared * DeltaTime);
	const double ActualPullSpeed = FVector::DotProduct(Velocity, MovementTraction.Direction);
	if (ActualPullSpeed < IntegratedPullSpeed) Velocity += MovementTraction.Direction * (IntegratedPullSpeed - ActualPullSpeed);
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
