#include "Condition/CatConditionPresentationComponent.h"
#include "AbilitySystem/Tags/CatStateTags.h"

#include "AbilitySystemComponent.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Logging/CatLog.h"
#include "UObject/ConstructorHelpers.h"

// 构造流程：在物理和移动更新后推进姿势，按固定顺序加载默认骨架的过渡与循环片段。
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

// 绑定流程：找到身体 ASC 后监听倒地标签的出现或消失，立即查询已有状态；没有 ASC 时不创建本地状态替代物。
void UCatConditionPresentationComponent::BeginPlay()
{
	Super::BeginPlay();
	AbilitySystem = GetOwner()->FindComponentByClass<UAbilitySystemComponent>();
	if (AbilitySystem) AbilitySystem->RegisterGameplayTagEvent(CatStateTags::Downed, EGameplayTagEventType::NewOrRemoved).AddUObject(this, &ThisClass::RefreshCondition);
	RefreshCondition();
}

// 读取流程：有效索引映射为诊断阶段名，空闲或越界索引统一返回正常移动，不推进动画。
FName UCatConditionPresentationComponent::GetObservedPosePhase() const
{
	static const FName Names[] = {TEXT("SittingDown"), TEXT("LyingDown"), TEXT("DownedPose"), TEXT("SittingUp"), TEXT("StandingUp")};
	return Phase >= 0 && Phase < UE_ARRAY_COUNT(Names) ? Names[Phase] : FName(TEXT("Locomotion"));
}

// 状态观察流程：重读 ASC 并忽略已经展示的同值；进入倒地从坐下开始，离开倒地从起坐开始，首次正常状态保留移动姿势。
void UCatConditionPresentationComponent::RefreshCondition(FGameplayTag Tag, int32 Count)
{
	if (!AbilitySystem) return;
	const bool bDowned = AbilitySystem->HasMatchingGameplayTag(CatStateTags::Downed);
	if (bInitialized && bDowned == bObservedDowned) return;
	const bool bWasDowned = bObservedDowned;
	bObservedDowned = bDowned;
	bInitialized = true;
	if (bDowned) PlayPhase(0);
	else if (bWasDowned) PlayPhase(3);
}

// 姿势切换流程：记录新阶段并停止旧动态蒙太奇，按片段长度设置期限；资源缺失时以短间隔推进，专服不播放。
// 躺卧阶段循环，其余阶段播放一次；最后记录本机实际播放结果，任何分支都不写回 ASC 状态。
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
		TEXT("Event=condition_pose_observed World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s BodyId=%s Downed=%d Phase=%s Clip=%s Result=%s"),
		*GetNameSafe(GetWorld()), int32(GetOwner()->GetNetMode()), GetOwner()->HasAuthority(), int32(GetOwner()->GetLocalRole()), *GetNameSafe(GetOwner()),
		Cat ? *Cat->GetPhysicalBodyComponent()->GetBodyId().ToString() : TEXT("None"),
		bObservedDowned, *GetObservedPosePhase().ToString(), *GetNameSafe(Clip), Phase == INDEX_NONE ? TEXT("LocomotionRestored") : ActiveMontage ? TEXT("MontagePlaying") : TEXT("NoLocalAnimation"));
}

// 帧推进流程：保留父类更新；空闲、躺卧循环或尚未到期时不切换，到期后推进一段，站起结束回到正常移动。
void UCatConditionPresentationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	if (Phase == INDEX_NONE || Phase == 2 || GetWorld()->GetTimeSeconds() < PhaseEndsAt) return;
	PlayPhase(Phase == 4 ? INDEX_NONE : Phase + 1);
}

// 退出流程：先从原 ASC 解除本对象的标签订阅，再停止仍可访问的姿势蒙太奇，最后执行父类清理。
void UCatConditionPresentationComponent::EndPlay(EEndPlayReason::Type Reason)
{
	if (AbilitySystem) AbilitySystem->RegisterGameplayTagEvent(CatStateTags::Downed, EGameplayTagEventType::NewOrRemoved).RemoveAll(this);
	if (const auto* Cat = Cast<ACatCharacter>(GetOwner()))
		if (auto* Anim = Cat->GetMesh()->GetAnimInstance())
			if (ActiveMontage) Anim->Montage_Stop(0, ActiveMontage);
	Super::EndPlay(Reason);
}
