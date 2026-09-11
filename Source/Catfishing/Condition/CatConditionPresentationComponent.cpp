#include "Condition/CatConditionPresentationComponent.h"

#include "Condition/CatConditionComponent.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Logging/CatLog.h"
#include "UObject/ConstructorHelpers.h"

UCatConditionPresentationComponent::UCatConditionPresentationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Sit(TEXT("/Game/Animalia/Cat/Animations/InPlace/Trans_Stand_To_Sitting-IP.Trans_Stand_To_Sitting-IP"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Lie(TEXT("/Game/Animalia/Cat/Animations/InPlace/Trans_Sitting_To_Lying-IP.Trans_Sitting_To_Lying-IP"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Rest(TEXT("/Game/Animalia/Cat/Animations/InPlace/Lying_00-IP.Lying_00-IP"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> SitUp(TEXT("/Game/Animalia/Cat/Animations/InPlace/Trans_Lying_To_Sitting-IP.Trans_Lying_To_Sitting-IP"));
	static ConstructorHelpers::FObjectFinder<UAnimSequence> Stand(TEXT("/Game/Animalia/Cat/Animations/InPlace/Trans_Sitting_To_Stand-IP.Trans_Sitting_To_Stand-IP"));
	PoseClips = {Sit.Object, Lie.Object, Rest.Object, SitUp.Object, Stand.Object};
}

void UCatConditionPresentationComponent::BeginPlay()
{
	Super::BeginPlay();
	Condition = GetOwner()->FindComponentByClass<UCatConditionComponent>();
	if (Condition) Condition->OnSnapshotChanged.AddUObject(this, &ThisClass::RefreshCondition);
	RefreshCondition();
}

FName UCatConditionPresentationComponent::GetObservedPosePhase() const
{
	static const FName Names[] = {TEXT("SittingDown"), TEXT("LyingDown"), TEXT("DownedPose"), TEXT("SittingUp"), TEXT("StandingUp")};
	return Phase >= 0 && Phase < UE_ARRAY_COUNT(Names) ? Names[Phase] : FName(TEXT("Locomotion"));
}

void UCatConditionPresentationComponent::RefreshCondition()
{
	if (!Condition) return;
	const bool bDowned = Condition->GetSnapshot().bDowned;
	if (bInitialized && bDowned == bObservedDowned) return;
	const bool bWasDowned = bObservedDowned;
	bObservedDowned = bDowned;
	bInitialized = true;
	if (bDowned) PlayPhase(0);
	else if (bWasDowned) PlayPhase(3);
}

void UCatConditionPresentationComponent::PlayPhase(int32 NewPhase)
{
	Phase = NewPhase;
	const auto* Cat = Cast<ACatCharacter>(GetOwner());
	UAnimInstance* Anim = Cat && Cat->GetMesh() ? Cat->GetMesh()->GetAnimInstance() : nullptr;
	if (Anim && ActiveMontage) Anim->Montage_Stop(.10f, ActiveMontage);
	ActiveMontage = nullptr;
	UAnimSequence* Clip = PoseClips.IsValidIndex(Phase) ? PoseClips[Phase].Get() : nullptr;
	PhaseEndsAt = GetWorld()->GetTimeSeconds() + (Clip ? Clip->GetPlayLength() : .1);
	if (Anim && Clip && GetWorld()->GetNetMode() != NM_DedicatedServer)
		ActiveMontage = Anim->PlaySlotAnimationAsDynamicMontage(Clip, TEXT("DefaultSlot"), .1f, .1f, 1.0f, Phase == 2 ? MAX_int32 : 1);
	UE_LOG(LogCatCharacter, Log,
		TEXT("Event=condition_pose_observed World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s BodyId=%s Revision=%lld Downed=%d Phase=%s Clip=%s Result=%s"),
		*GetNameSafe(GetWorld()), int32(GetOwner()->GetNetMode()), GetOwner()->HasAuthority(), int32(GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()),
		Cat ? *Cat->GetPhysicalBodyComponent()->GetBodyId().ToString() : TEXT("None"), Condition ? Condition->GetSnapshot().Revision : 0,
		bObservedDowned, *GetObservedPosePhase().ToString(), *GetNameSafe(Clip), Phase == INDEX_NONE ? TEXT("LocomotionRestored") : ActiveMontage ? TEXT("MontagePlaying") : TEXT("NoLocalAnimation"));
}

void UCatConditionPresentationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	if (Phase == INDEX_NONE || Phase == 2 || GetWorld()->GetTimeSeconds() < PhaseEndsAt) return;
	PlayPhase(Phase == 4 ? INDEX_NONE : Phase + 1);
}

void UCatConditionPresentationComponent::EndPlay(EEndPlayReason::Type Reason)
{
	if (Condition) Condition->OnSnapshotChanged.RemoveAll(this);
	if (const auto* Cat = Cast<ACatCharacter>(GetOwner()))
		if (auto* Anim = Cat->GetMesh()->GetAnimInstance())
			if (ActiveMontage) Anim->Montage_Stop(0, ActiveMontage);
	Super::EndPlay(Reason);
}
