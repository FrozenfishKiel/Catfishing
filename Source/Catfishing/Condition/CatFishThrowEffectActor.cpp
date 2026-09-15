#include "Condition/CatFishThrowEffectActor.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/CatCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Logging/CatLog.h"
#include "Net/UnrealNetwork.h"

ACatFishThrowEffectActor::ACatFishThrowEffectActor()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = false;
	Barrier = CreateDefaultSubobject<USphereComponent>(TEXT("RepelBarrier"));
	SetRootComponent(Barrier);
	Barrier->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Barrier->SetCollisionResponseToAllChannels(ECR_Ignore);
	Barrier->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Barrier->SetNotifyRigidBodyCollision(true);
	Barrier->OnComponentHit.AddDynamic(this, &ThisClass::HandleBarrierHit);
}

bool ACatFishThrowEffectActor::InitializeFromAuthority(const FCatFishThrowEffect& Effect,
	ACatCharacter* Target, const FGuid FishInstanceId)
{
	if (!HasAuthority() || EndsServerTime != 0.0 || !FishInstanceId.IsValid()
		|| (Effect.Kind != ECatFishThrowEffectKind::KnockbackStartle && Effect.Kind != ECatFishThrowEffectKind::RepelAura) || !Effect.IsRuntimeEffectReady()
		|| !Effect.ReactionMontage.LoadSynchronous()
		|| (Effect.Kind == ECatFishThrowEffectKind::KnockbackStartle && !Target))
	{
		UE_LOG(LogCatCharacter, Warning, TEXT("Event=fish_throw_effect_unavailable FishInstanceId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Reason=MissingRadiusDurationMontageOrTarget"),
			*FishInstanceId.ToString(), *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole());
		return false;
	}
	ActiveEffect = Effect;
	HitTarget = Target;
	SourceFishInstanceId = FishInstanceId;
	EndsServerTime = GetWorld()->GetTimeSeconds() + Effect.DurationSeconds;
	SetLifeSpan(Effect.DurationSeconds);
	if (Effect.Kind == ECatFishThrowEffectKind::RepelAura)
	{
		for (TActorIterator<ACatCharacter> It(GetWorld()); It; ++It)
		{
			if (FVector::DistSquared(It->GetActorLocation(), GetActorLocation()) <= FMath::Square(Effect.EffectRadiusCentimeters))
			{
				ReactedCharacters.Add(*It);
			}
		}
	}
	OnRep_Effect();
	ForceNetUpdate();
	UE_LOG(LogCatCharacter, Log, TEXT("Event=fish_throw_effect_started FishInstanceId=%s Kind=%d Target=%s RadiusCm=%.3f DurationSeconds=%.3f World=%s NetMode=%d Authority=1 LocalRole=%d"),
		*FishInstanceId.ToString(), int32(Effect.Kind), *GetNameSafe(Target), Effect.EffectRadiusCentimeters,
		Effect.DurationSeconds, *GetNameSafe(GetWorld()), GetNetMode(), GetLocalRole());
	return true;
}

void ACatFishThrowEffectActor::OnRep_Effect()
{
	Barrier->SetSphereRadius(ActiveEffect.EffectRadiusCentimeters);
	Barrier->SetCollisionEnabled(ActiveEffect.Kind == ECatFishThrowEffectKind::RepelAura
		? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	if (HitTarget && ActiveEffect.Kind == ECatFishThrowEffectKind::KnockbackStartle)
		PlayReactionLocally(HitTarget);
	for (ACatCharacter* Character : ReactedCharacters) PlayReactionLocally(Character);
	UE_LOG(LogCatCharacter, Log, TEXT("Event=fish_throw_effect_observed FishInstanceId=%s Kind=%d World=%s NetMode=%d Authority=%d LocalRole=%d"),
		*SourceFishInstanceId.ToString(), int32(ActiveEffect.Kind), *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole());
}

void ACatFishThrowEffectActor::PlayReactionLocally(ACatCharacter* Character)
{
	if (!Character || LocalReactedCharacters.Contains(Character)) return;
	UAnimMontage* Montage = ActiveEffect.ReactionMontage.LoadSynchronous();
	if (!Montage || !Character->GetMesh() || !Character->GetMesh()->GetAnimInstance())
	{
		UE_LOG(LogCatCharacter, Warning, TEXT("Event=fish_throw_presentation_unavailable FishInstanceId=%s Target=%s World=%s NetMode=%d Reason=MontageOrAnimInstanceUnset"),
			*SourceFishInstanceId.ToString(), *GetNameSafe(Character), *GetNameSafe(GetWorld()), GetNetMode());
		return;
	}
	const auto* State = GetWorld()->GetGameState();
	const double Now = State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	if (EndsServerTime <= Now) return;
	// 只读正式动画；不虚构击退距离、掉物距离阈值或新的伤害/倒地后果。
	if (Character->PlayAnimMontage(Montage) > 0.0f)
	{
		LocalReactedCharacters.Add(Character);
		UE_LOG(LogCatCharacter, Log, TEXT("Event=fish_throw_presentation_started FishInstanceId=%s Target=%s World=%s NetMode=%d Authority=%d LocalRole=%d"),
			*SourceFishInstanceId.ToString(), *GetNameSafe(Character), *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole());
	}
	else UE_LOG(LogCatCharacter, Warning, TEXT("Event=fish_throw_presentation_unavailable FishInstanceId=%s Target=%s World=%s NetMode=%d Reason=MontageRejected"),
		*SourceFishInstanceId.ToString(), *GetNameSafe(Character), *GetNameSafe(GetWorld()), GetNetMode());
}

void ACatFishThrowEffectActor::HandleBarrierHit(UPrimitiveComponent*, AActor* Other,
	UPrimitiveComponent*, FVector, const FHitResult&)
{
	ACatCharacter* Character = Cast<ACatCharacter>(Other);
	if (HasAuthority() && Character && !ReactedCharacters.Contains(Character))
	{
		ReactedCharacters.Add(Character);
		OnRep_Effect();
		ForceNetUpdate();
	}
}

void ACatFishThrowEffectActor::EndPlay(const EEndPlayReason::Type Reason)
{
	Barrier->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	for (const auto& Character : LocalReactedCharacters)
	{
		if (Character.IsValid()) Character->StopAnimMontage(ActiveEffect.ReactionMontage.Get());
	}
	UE_LOG(LogCatCharacter, Log, TEXT("Event=fish_throw_effect_ended FishInstanceId=%s World=%s NetMode=%d Authority=%d LocalRole=%d"),
		*SourceFishInstanceId.ToString(), *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole());
	Super::EndPlay(Reason);
}

void ACatFishThrowEffectActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, ActiveEffect);
	DOREPLIFETIME(ThisClass, HitTarget);
	DOREPLIFETIME(ThisClass, SourceFishInstanceId);
	DOREPLIFETIME(ThisClass, EndsServerTime);
	DOREPLIFETIME(ThisClass, ReactedCharacters);
}
