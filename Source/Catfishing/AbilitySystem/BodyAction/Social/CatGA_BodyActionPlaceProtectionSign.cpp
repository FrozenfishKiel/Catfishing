#include "AbilitySystem/Tags/CatStateTags.h"
#include "Logging/CatLog.h"
#include "AbilitySystem/BodyAction/Social/CatGA_BodyActionPlaceProtectionSign.h"

#include "AbilitySystem/BodyAction/CatBodyActionPresentationSettings.h"
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Engine/PackageMapClient.h"
#include "GameFramework/PlayerState.h"
#include "Character/CatCharacter.h"
#include "Engine/World.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Social/CatSocialService.h"

UCatGA_BodyActionPlaceProtectionSign::UCatGA_BodyActionPlaceProtectionSign()
{
	// 构造流程：放牌动作由服务器启动、拥有者客户端同步表现，按 Actor 保存自己的前摇状态，并暴露共同资产标签供 Fishing Cancel 命中。
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	FGameplayTagContainer BodyActionTags(CatFishingAbilityTags::Ability_Body_Action);
	BodyActionTags.AddTag(CatStateTags::AbilityInterruptOnDowned);
	SetAssetTags(BodyActionTags);
	FAbilityTriggerData Trigger;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	Trigger.TriggerTag = CatFishingAbilityTags::AbilityEvent_Body_PlaceProtectionSign;
	AbilityTriggers.Add(Trigger);
}

void UCatGA_BodyActionPlaceProtectionSign::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 激活流程：
	// 1. 先校验保护牌事件、精确载荷类型和 owning Controller，错配直接取消，不进入 Social 写口。
	// 2. 按值冻结请求，用能力任务播放可选蒙太奇；拥有者客户端到此只做表现，服务器继续授予可选 Cue 并等待前摇。
	// 3. 零秒前摇也走本类 CommitPlaceProtectionSignAfterWindow，保证放牌没有第二条即时提交路径。
	const FGameplayTag BodyActionEventTag = CatFishingAbilityTags::AbilityEvent_Body_PlaceProtectionSign.GetTag();
	// 参数随 GameplayEvent 的 TargetData 进入能力；精确验证结构后按值冻结，前摇期间不会被后续请求改写。
	const FGameplayAbilityTargetData* Data = TriggerEventData && TriggerEventData->TargetData.Num() == 1 ? TriggerEventData->TargetData.Get(0) : nullptr;
	const auto* Request = Data && Data->GetScriptStruct() == FCatBodyActionRequestPlaceProtectionSignTargetData::StaticStruct() ? static_cast<const FCatBodyActionRequestPlaceProtectionSignTargetData*>(Data) : nullptr;
	ACatfishingPlayerController* Controller = GetCatPlayerControllerFromActorInfo();
	if (!TriggerEventData || TriggerEventData->EventTag != BodyActionEventTag
		|| !Request || !Request->RequestId.IsValid() || !Controller)
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
		return;
	}

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	ActiveRequest = *Request;
	UE_LOG(LogCatCharacter, Log, TEXT("Event=body_action_started Ability=%s RequestId=%s Actor=%s World=%s NetMode=%d Authority=%d"),
		*GetClass()->GetName(), *ActiveRequest.RequestId.ToString(), *GetNameSafe(GetAvatarActorFromActorInfo()),
		*GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), ActorInfo->IsNetAuthority());
	const auto* Presentation = GetDefault<UCatBodyActionPresentationSettings>();
	if (UAnimMontage* Montage = Presentation->LoadMontage(BodyActionEventTag))
	{
		auto* Task = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, Montage, 1.f, NAME_None, false);
		Task->OnInterrupted.AddDynamic(this, &ThisClass::CancelAction);
		Task->OnCancelled.AddDynamic(this, &ThisClass::CancelAction);
		Task->ReadyForActivation();
		if (!IsActive()) return;
	}
	// 拥有者客户端只播放任务，领域提交和 Cue 授予由服务器执行；旁观者使用 ASC 蒙太奇复制。
	if (!ActorInfo->IsNetAuthority()) return;
	if (const auto* Config = Presentation->FindPresentationConfig(BodyActionEventTag); Config && Config->GameplayCue.IsValid())
		K2_AddGameplayCue(Config->GameplayCue, MakeEffectContext(Handle, ActorInfo), true);
	const float LeadInSeconds = Presentation->GetLeadInSeconds(BodyActionEventTag);
	if (LeadInSeconds <= 0.0f)
	{
		CommitPlaceProtectionSignAfterWindow();
		return;
	}

	UAbilityTask_WaitDelay* CommitDelay = UAbilityTask_WaitDelay::WaitDelay(this, LeadInSeconds);
	if (!CommitDelay)
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
		return;
	}
	// 委托生命周期由 AbilityTask 挂在当前放牌 Ability 实例上；取消时 EndAbility 清空 ActiveRequest，迟到回调只能取消收口。
	CommitDelay->OnFinish.AddDynamic(this, &ThisClass::CommitPlaceProtectionSignAfterWindow);
	CommitDelay->ReadyForActivation();
}

void UCatGA_BodyActionPlaceProtectionSign::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility, const bool bWasCancelled)
{
	// 结束流程：忽略失效或重复的结束请求并关闭取消资格，再记录本次结果；成功提交后动画自然播完，取消则停止蒙太奇。
	// 再清空冻结参数并交给 GAS 销毁任务、回收随能力添加的 Cue；不回滚已经确认的领域结果。
	// MontageStop 会触发任务中断回调；先关闭取消资格，避免清理过程中再次进入 EndAbility。
	if (!IsEndAbilityValid(Handle, ActorInfo)) return;
	SetCanBeCanceled(false);
	UE_LOG(LogCatCharacter, Log, TEXT("Event=body_action_ended Ability=%s RequestId=%s Actor=%s World=%s NetMode=%d Authority=%d Cancelled=%d"),
		*GetClass()->GetName(), *ActiveRequest.RequestId.ToString(), *GetNameSafe(GetAvatarActorFromActorInfo()),
		*GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), ActorInfo && ActorInfo->IsNetAuthority(), bWasCancelled);
	if (bWasCancelled) MontageStop();
	ActiveRequest = {};
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UCatGA_BodyActionPlaceProtectionSign::CommitPlaceProtectionSignAfterWindow()
{
	// 提交流程：前摇结束后重读 Controller 和 Social 服务，先过统一玩法 gate，再提交唯一保护牌位置意图。
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	ACatfishingPlayerController* Controller = GetCatPlayerControllerFromActorInfo();
	if (!IsActive() || !ActorInfo || !ActorInfo->IsNetAuthority() || !ActiveRequest.RequestId.IsValid() || !Controller)
	{
		CancelAbility(CurrentSpecHandle, ActorInfo, GetCurrentActivationInfo(), true);
		return;
	}

	FCatDomainCommandResult Result;
	Result.RequestId = ActiveRequest.RequestId;
	if (!Controller->CanSubmitBodyActionCommand())
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (UCatSocialService* Social = Controller->GetWorld()
		? Controller->GetWorld()->GetSubsystem<UCatSocialService>() : nullptr)
	{
		Result = Social->PlaceProtectionSign(Controller, ActiveRequest.RequestId, ActiveRequest.SignLocation);
	}
	else
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}

	// 领域结果只回给拥有者；拒绝会取消表现，接受则正常结束，前摇期间失效的依赖不会继续写入。
	Controller->DeliverBodyActionCommandResultToOwningClient(Result);
	if (!CatIsAcceptedDomainCommandResult(Result))
	{
		CancelAbility(CurrentSpecHandle, ActorInfo, GetCurrentActivationInfo(), true);
		return;
	}
	EndAbility(CurrentSpecHandle, ActorInfo, GetCurrentActivationInfo(), true, false);
}

// 中断流程：动画被其他动作打断时取消当前 GA；领域前摇任务随能力销毁，已提交的结果不会回滚。
void UCatGA_BodyActionPlaceProtectionSign::CancelAction()
{
	if (IsActive()) CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true);
}

// 目标数据传输流程：序列化请求身份与本动作参数；加载时还原字段，流错误标记失败，客户端据此启动表现但不获得领域提交权。
bool FCatBodyActionRequestPlaceProtectionSignTargetData::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Ar << RequestId;
	Ar << SignLocation;
	bOutSuccess = !Ar.IsError();
	return bOutSuccess;
}
