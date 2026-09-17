#include "AbilitySystem/BodyAction/Social/CatGA_BodyActionRequestManualHelp.h"
#include "AbilitySystem/Tags/CatStateTags.h"
#include "Logging/CatLog.h"

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

UCatGA_BodyActionRequestManualHelp::UCatGA_BodyActionRequestManualHelp()
{
	// 构造流程：求助动作由拥有者本地预测启动、服务器接收 GAS 事件并验证，按 Actor 保存自己的前摇状态，并暴露共同资产标签供 Fishing Cancel 命中。
	// 倒地玩家仍可求救；仅撤销默认准入阻挡，不授予倒地中断标签。
	ActivationBlockedTags.RemoveTag(CatStateTags::Downed);
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	SetAssetTags(FGameplayTagContainer(CatFishingAbilityTags::Ability_Body_Action.GetTag()));
	FAbilityTriggerData Trigger;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	Trigger.TriggerTag = CatFishingAbilityTags::AbilityEvent_Body_RequestManualHelp;
	AbilityTriggers.Add(Trigger);
}

void UCatGA_BodyActionRequestManualHelp::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 激活流程：
	// 1. 先校验求助事件、精确载荷类型和 owning Controller，错配直接取消，不进入 Social 写口。
	// 2. 按值冻结请求，拥有者立即预测蒙太奇与可选 Cue；服务器验证激活后执行对应表现，只有服务器创建提交前摇任务。
	// 3. 零秒前摇也走本类 CommitManualHelpAfterWindow，保证求助没有第二条即时提交路径。
	const FGameplayTag BodyActionEventTag = CatFishingAbilityTags::AbilityEvent_Body_RequestManualHelp.GetTag();
	// 参数随 GameplayEvent 的 TargetData 进入能力；精确验证结构后按值冻结，前摇期间不会被后续请求改写。
	const FGameplayAbilityTargetData* Data = TriggerEventData && TriggerEventData->TargetData.Num() == 1 ? TriggerEventData->TargetData.Get(0) : nullptr;
	const auto* Request = Data && Data->GetScriptStruct() == FCatBodyActionRequestManualHelpTargetData::StaticStruct() ? static_cast<const FCatBodyActionRequestManualHelpTargetData*>(Data) : nullptr;
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
		// 求助不依赖动作姿势完成；倒地动画可接管同一槽，不能因此取消救援请求。取消由 GAS 生命周期处理。
		Task->ReadyForActivation();
		if (!IsActive()) return;
	}
	// 本地事件立即播放任务，无需等待服务器批准；GAS 使用激活预测键传输同一事件。领域结果仍只在服务器产生。
	if (const auto* Config = Presentation->FindPresentationConfig(BodyActionEventTag); Config && Config->GameplayCue.IsValid())
		K2_AddGameplayCue(Config->GameplayCue, MakeEffectContext(Handle, ActorInfo), true);
	if (!ActorInfo->IsNetAuthority()) return;
	const float LeadInSeconds = Presentation->GetLeadInSeconds(BodyActionEventTag);
	if (LeadInSeconds <= 0.0f)
	{
		CommitManualHelpAfterWindow();
		return;
	}

	UAbilityTask_WaitDelay* CommitDelay = UAbilityTask_WaitDelay::WaitDelay(this, LeadInSeconds);
	if (!CommitDelay)
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
		return;
	}
	// 委托生命周期由 AbilityTask 挂在当前求助 Ability 实例上；取消时 EndAbility 清空 ActiveRequest，迟到回调只能取消收口。
	CommitDelay->OnFinish.AddDynamic(this, &ThisClass::CommitManualHelpAfterWindow);
	CommitDelay->ReadyForActivation();
}

void UCatGA_BodyActionRequestManualHelp::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility, const bool bWasCancelled)
{
	// 结束流程：忽略失效或重复的结束请求，再记录本次结果；成功提交后动画自然播完，取消则停止蒙太奇。
	// 再清空冻结参数并交给 GAS 销毁任务、回收随能力添加的 Cue；不回滚已经确认的领域结果。
	if (!IsEndAbilityValid(Handle, ActorInfo)) return;
	UE_LOG(LogCatCharacter, Log, TEXT("Event=body_action_ended Ability=%s RequestId=%s Actor=%s World=%s NetMode=%d Authority=%d Cancelled=%d"),
		*GetClass()->GetName(), *ActiveRequest.RequestId.ToString(), *GetNameSafe(GetAvatarActorFromActorInfo()),
		*GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), ActorInfo && ActorInfo->IsNetAuthority(), bWasCancelled);
	if (bWasCancelled) MontageStop();
	ActiveRequest = {};
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UCatGA_BodyActionRequestManualHelp::CommitManualHelpAfterWindow()
{
	// 提交流程：前摇结束后复核激活、Controller、命令窗口与服务依赖，再提交 GAS 成本和冷却，最后由领域服务裁决共享结果。
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
		// 前摇结束后再次检查并提交 GAS 成本和冷却；失败取消本次能力，不进入领域写口。
		if (!CommitAbility(CurrentSpecHandle, ActorInfo, GetCurrentActivationInfo()))
		{
			CancelAbility(CurrentSpecHandle, ActorInfo, GetCurrentActivationInfo(), true);
			return;
		}
		Result = Social->RequestManualHelp(Controller, ActiveRequest.RequestId, ActiveRequest.HelpKind);
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

// 目标数据传输流程：序列化请求身份与本动作参数；加载时还原字段，流错误标记失败，服务器据此校验请求，客户端预测不获得领域提交权。
bool FCatBodyActionRequestManualHelpTargetData::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Ar << RequestId;
	uint8 Kind = uint8(HelpKind);
	Ar << Kind;
	if (Ar.IsLoading()) HelpKind = ECatHelpSignalKind(Kind);
	bOutSuccess = !Ar.IsError();
	return bOutSuccess;
}
