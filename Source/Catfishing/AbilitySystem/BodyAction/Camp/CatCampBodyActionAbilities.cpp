#include "AbilitySystem/BodyAction/Camp/CatCampBodyActionAbilities.h"

#include "AbilitySystem/BodyAction/CatBodyActionPresentationSettings.h"
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Camp/CatCampHubActor.h"
#include "Character/CatCharacter.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "GameFramework/Pawn.h"

namespace
{
	/** 从当前 ActorInfo 找 owning Controller；PlayerController 缺失时只回退 Avatar Pawn 的 Controller。 */
	ACatfishingPlayerController* ResolveBodyActionController(const FGameplayAbilityActorInfo* ActorInfo)
	{
		if (!ActorInfo)
		{
			return nullptr;
		}
		ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(ActorInfo->PlayerController.Get());
		if (!Controller)
		{
			const APawn* AvatarPawn = Cast<APawn>(ActorInfo->AvatarActor.Get());
			Controller = AvatarPawn ? Cast<ACatfishingPlayerController>(AvatarPawn->GetController()) : nullptr;
		}
		return Controller;
	}

	/** 从当前 ActorInfo 找身体 Avatar；身体动作表现永远由 Character 承接，不把动画状态搬到 Controller。 */
	ACatCharacter* ResolveBodyActionCharacter(const FGameplayAbilityActorInfo* ActorInfo)
	{
		return ActorInfo ? Cast<ACatCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	}

	/** 解析动作表现标签；配置缺失时回退到动作事件本身，保证开始和停止表现拥有同一身份。 */
	FGameplayTag ResolvePresentationEventTag(const FGameplayTag BodyActionEventTag)
	{
		const UCatBodyActionPresentationSettings* Settings = GetDefault<UCatBodyActionPresentationSettings>();
		return Settings ? Settings->GetPresentationEventTag(BodyActionEventTag) : BodyActionEventTag;
	}

	/** 解析可取消前摇时长；具体 Ability 只读表现配置，不从 Camp 领域服务借第二套时长规则。 */
	float ResolveLeadInSeconds(const FGameplayTag BodyActionEventTag)
	{
		const UCatBodyActionPresentationSettings* Settings = GetDefault<UCatBodyActionPresentationSettings>();
		return Settings ? Settings->GetLeadInSeconds(BodyActionEventTag) : 0.0f;
	}
}

UCatGA_BodyActionCampRest::UCatGA_BodyActionCampRest()
{
	// 构造流程：休息动作只在服务器执行，按 Actor 保存自己的前摇状态，并暴露共同资产标签供 Fishing Cancel 命中。
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	SetAssetTags(FGameplayTagContainer(CatFishingAbilityTags::Ability_Body_Action.GetTag()));
	FAbilityTriggerData Trigger;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	Trigger.TriggerTag = CatFishingAbilityTags::AbilityEvent_Body_CampRest;
	AbilityTriggers.Add(Trigger);
}

void UCatGA_BodyActionCampRest::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 激活流程：
	// 1. 先校验休息事件、精确载荷类型和 owning Controller，错配直接取消，不进入 Camp 写口。
	// 2. 再冻结请求和表现标签，启动 Character 表现，随后打开可取消的前摇窗口。
	// 3. 零秒前摇也走本类 CommitCampRestAfterWindow，保证休息没有第二条即时提交路径。
	const FGameplayTag BodyActionEventTag = CatFishingAbilityTags::AbilityEvent_Body_CampRest.GetTag();
	// GameplayEventData 只给 Ability 一个只读视图；请求 UObject 由命令组件创建，这里只借非 const 指针保留 GC 引用，不改写载荷内容。
	UCatBodyActionRequestCampRest* Request = TriggerEventData
		? const_cast<UCatBodyActionRequestCampRest*>(Cast<UCatBodyActionRequestCampRest>(TriggerEventData->OptionalObject.Get())) : nullptr;
	ACatfishingPlayerController* Controller = ResolveBodyActionController(ActorInfo);
	if (!TriggerEventData || TriggerEventData->EventTag != BodyActionEventTag
		|| !Request || Request->GetClass() != UCatBodyActionRequestCampRest::StaticClass() || !Controller)
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
		return;
	}

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	ActiveRequest = Request;
	ActivePresentationEventTag = ResolvePresentationEventTag(BodyActionEventTag);
	if (ACatCharacter* Character = ResolveBodyActionCharacter(ActorInfo); Character && ActivePresentationEventTag.IsValid())
	{
		Character->Multicast_PlayBodyActionPresentation(BodyActionEventTag, ActivePresentationEventTag);
	}

	const float LeadInSeconds = ResolveLeadInSeconds(BodyActionEventTag);
	if (LeadInSeconds <= 0.0f)
	{
		CommitCampRestAfterWindow();
		return;
	}

	UAbilityTask_WaitDelay* CommitDelay = UAbilityTask_WaitDelay::WaitDelay(this, LeadInSeconds);
	if (!CommitDelay)
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
		return;
	}
	// 委托生命周期由 AbilityTask 挂在当前休息 Ability 实例上；取消时 EndAbility 清空 ActiveRequest，迟到回调只能取消收口。
	CommitDelay->OnFinish.AddDynamic(this, &ThisClass::CommitCampRestAfterWindow);
	CommitDelay->ReadyForActivation();
}

void UCatGA_BodyActionCampRest::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility, const bool bWasCancelled)
{
	// 收尾流程：只有取消才停止前摇表现；正常提交成功后由表现系统自己进入完成态，本 Ability 只清空本次请求。
	if (bWasCancelled && ActiveRequest && ActivePresentationEventTag.IsValid())
	{
		if (ACatCharacter* Character = ResolveBodyActionCharacter(ActorInfo))
		{
			Character->Multicast_StopBodyActionPresentation(CatFishingAbilityTags::AbilityEvent_Body_CampRest,
				ActivePresentationEventTag);
		}
	}
	ActiveRequest = nullptr;
	ActivePresentationEventTag = FGameplayTag();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UCatGA_BodyActionCampRest::CommitCampRestAfterWindow()
{
	// 提交流程：前摇结束后重读 Controller 和 Camp，先过统一玩法 gate，再把休息裁决交回 Camp/Condition。
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	ACatfishingPlayerController* Controller = ResolveBodyActionController(ActorInfo);
	if (!ActiveRequest || !Controller)
	{
		CancelAbility(CurrentSpecHandle, ActorInfo, GetCurrentActivationInfo(), true);
		return;
	}

	FCatDomainCommandResult Result;
	Result.RequestId = ActiveRequest->RequestId;
	if (!Controller->CanSubmitBodyActionCommand())
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (!ActiveRequest->Camp || ActiveRequest->Camp->GetWorld() != Controller->GetWorld())
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else
	{
		Result = ActiveRequest->Camp->RequestRest(Controller, ActiveRequest->RequestId);
	}

	Controller->DeliverBodyActionCommandResultToOwningClient(Result);
	if (!CatIsAcceptedDomainCommandResult(Result))
	{
		CancelAbility(CurrentSpecHandle, ActorInfo, GetCurrentActivationInfo(), true);
		return;
	}
	EndAbility(CurrentSpecHandle, ActorInfo, GetCurrentActivationInfo(), true, false);
}

UCatGA_BodyActionCampfirePlayback::UCatGA_BodyActionCampfirePlayback()
{
	// 构造流程：回看动作只在服务器执行，按 Actor 保存自己的前摇状态，并暴露共同资产标签供 Fishing Cancel 命中。
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	SetAssetTags(FGameplayTagContainer(CatFishingAbilityTags::Ability_Body_Action.GetTag()));
	FAbilityTriggerData Trigger;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	Trigger.TriggerTag = CatFishingAbilityTags::AbilityEvent_Body_CampfirePlayback;
	AbilityTriggers.Add(Trigger);
}

void UCatGA_BodyActionCampfirePlayback::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 激活流程：
	// 1. 先校验回看事件、精确载荷类型和 owning Controller，错配直接取消，不进入 Camp 写口。
	// 2. 再冻结请求和表现标签，启动 Character 表现，随后打开可取消的前摇窗口。
	// 3. 零秒前摇也走本类 CommitCampfirePlaybackAfterWindow，保证回看没有第二条即时提交路径。
	const FGameplayTag BodyActionEventTag = CatFishingAbilityTags::AbilityEvent_Body_CampfirePlayback.GetTag();
	// GameplayEventData 只给 Ability 一个只读视图；请求 UObject 由命令组件创建，这里只借非 const 指针保留 GC 引用，不改写载荷内容。
	UCatBodyActionRequestCampfirePlayback* Request = TriggerEventData
		? const_cast<UCatBodyActionRequestCampfirePlayback*>(Cast<UCatBodyActionRequestCampfirePlayback>(TriggerEventData->OptionalObject.Get())) : nullptr;
	ACatfishingPlayerController* Controller = ResolveBodyActionController(ActorInfo);
	if (!TriggerEventData || TriggerEventData->EventTag != BodyActionEventTag
		|| !Request || Request->GetClass() != UCatBodyActionRequestCampfirePlayback::StaticClass() || !Controller)
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
		return;
	}

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	ActiveRequest = Request;
	ActivePresentationEventTag = ResolvePresentationEventTag(BodyActionEventTag);
	if (ACatCharacter* Character = ResolveBodyActionCharacter(ActorInfo); Character && ActivePresentationEventTag.IsValid())
	{
		Character->Multicast_PlayBodyActionPresentation(BodyActionEventTag, ActivePresentationEventTag);
	}

	const float LeadInSeconds = ResolveLeadInSeconds(BodyActionEventTag);
	if (LeadInSeconds <= 0.0f)
	{
		CommitCampfirePlaybackAfterWindow();
		return;
	}

	UAbilityTask_WaitDelay* CommitDelay = UAbilityTask_WaitDelay::WaitDelay(this, LeadInSeconds);
	if (!CommitDelay)
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
		return;
	}
	// 委托生命周期由 AbilityTask 挂在当前回看 Ability 实例上；取消时 EndAbility 清空 ActiveRequest，迟到回调只能取消收口。
	CommitDelay->OnFinish.AddDynamic(this, &ThisClass::CommitCampfirePlaybackAfterWindow);
	CommitDelay->ReadyForActivation();
}

void UCatGA_BodyActionCampfirePlayback::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility, const bool bWasCancelled)
{
	// 收尾流程：只有取消才停止前摇表现；正常提交成功后由 Camp 的 CapturePlan 结果驱动后续表现。
	if (bWasCancelled && ActiveRequest && ActivePresentationEventTag.IsValid())
	{
		if (ACatCharacter* Character = ResolveBodyActionCharacter(ActorInfo))
		{
			Character->Multicast_StopBodyActionPresentation(CatFishingAbilityTags::AbilityEvent_Body_CampfirePlayback,
				ActivePresentationEventTag);
		}
	}
	ActiveRequest = nullptr;
	ActivePresentationEventTag = FGameplayTag();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UCatGA_BodyActionCampfirePlayback::CommitCampfirePlaybackAfterWindow()
{
	// 提交流程：前摇结束后重读 Controller 和 Camp，先过统一玩法 gate，再把回看裁决交回 Camp。
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	ACatfishingPlayerController* Controller = ResolveBodyActionController(ActorInfo);
	if (!ActiveRequest || !Controller)
	{
		CancelAbility(CurrentSpecHandle, ActorInfo, GetCurrentActivationInfo(), true);
		return;
	}

	FCatDomainCommandResult Result;
	Result.RequestId = ActiveRequest->RequestId;
	if (!Controller->CanSubmitBodyActionCommand())
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (!ActiveRequest->Camp || ActiveRequest->Camp->GetWorld() != Controller->GetWorld())
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else
	{
		Result = ActiveRequest->Camp->RequestCampfirePlayback(Controller, ActiveRequest->RequestId);
	}

	Controller->DeliverBodyActionCommandResultToOwningClient(Result);
	if (!CatIsAcceptedDomainCommandResult(Result))
	{
		CancelAbility(CurrentSpecHandle, ActorInfo, GetCurrentActivationInfo(), true);
		return;
	}
	EndAbility(CurrentSpecHandle, ActorInfo, GetCurrentActivationInfo(), true, false);
}

UCatGA_BodyActionRescueCharacterToCamp::UCatGA_BodyActionRescueCharacterToCamp()
{
	// 构造流程：救援动作只在服务器执行，按 Actor 保存自己的前摇状态，并暴露共同资产标签供 Fishing Cancel 命中。
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	SetAssetTags(FGameplayTagContainer(CatFishingAbilityTags::Ability_Body_Action.GetTag()));
	FAbilityTriggerData Trigger;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	Trigger.TriggerTag = CatFishingAbilityTags::AbilityEvent_Body_RescueCharacterToCamp;
	AbilityTriggers.Add(Trigger);
}

void UCatGA_BodyActionRescueCharacterToCamp::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 激活流程：
	// 1. 先校验救援事件、精确载荷类型和 owning Controller，错配直接取消，不进入 Camp 写口。
	// 2. 再冻结请求和表现标签，启动 Character 表现，随后打开可取消的前摇窗口。
	// 3. 零秒前摇也走本类 CommitRescueCharacterToCampAfterWindow，保证救援没有第二条即时提交路径。
	const FGameplayTag BodyActionEventTag = CatFishingAbilityTags::AbilityEvent_Body_RescueCharacterToCamp.GetTag();
	// GameplayEventData 只给 Ability 一个只读视图；请求 UObject 由命令组件创建，这里只借非 const 指针保留 GC 引用，不改写载荷内容。
	UCatBodyActionRequestRescueCharacterToCamp* Request = TriggerEventData
		? const_cast<UCatBodyActionRequestRescueCharacterToCamp*>(Cast<UCatBodyActionRequestRescueCharacterToCamp>(TriggerEventData->OptionalObject.Get())) : nullptr;
	ACatfishingPlayerController* Controller = ResolveBodyActionController(ActorInfo);
	if (!TriggerEventData || TriggerEventData->EventTag != BodyActionEventTag
		|| !Request || Request->GetClass() != UCatBodyActionRequestRescueCharacterToCamp::StaticClass() || !Controller)
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
		return;
	}

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	ActiveRequest = Request;
	ActivePresentationEventTag = ResolvePresentationEventTag(BodyActionEventTag);
	if (ACatCharacter* Character = ResolveBodyActionCharacter(ActorInfo); Character && ActivePresentationEventTag.IsValid())
	{
		Character->Multicast_PlayBodyActionPresentation(BodyActionEventTag, ActivePresentationEventTag);
	}

	const float LeadInSeconds = ResolveLeadInSeconds(BodyActionEventTag);
	if (LeadInSeconds <= 0.0f)
	{
		CommitRescueCharacterToCampAfterWindow();
		return;
	}

	UAbilityTask_WaitDelay* CommitDelay = UAbilityTask_WaitDelay::WaitDelay(this, LeadInSeconds);
	if (!CommitDelay)
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
		return;
	}
	// 委托生命周期由 AbilityTask 挂在当前救援 Ability 实例上；取消时 EndAbility 清空 ActiveRequest，迟到回调只能取消收口。
	CommitDelay->OnFinish.AddDynamic(this, &ThisClass::CommitRescueCharacterToCampAfterWindow);
	CommitDelay->ReadyForActivation();
}

void UCatGA_BodyActionRescueCharacterToCamp::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility, const bool bWasCancelled)
{
	// 收尾流程：只有取消才停止前摇表现；正常提交成功后由 Camp/Condition 的救援结果驱动角色状态。
	if (bWasCancelled && ActiveRequest && ActivePresentationEventTag.IsValid())
	{
		if (ACatCharacter* Character = ResolveBodyActionCharacter(ActorInfo))
		{
			Character->Multicast_StopBodyActionPresentation(
				CatFishingAbilityTags::AbilityEvent_Body_RescueCharacterToCamp, ActivePresentationEventTag);
		}
	}
	ActiveRequest = nullptr;
	ActivePresentationEventTag = FGameplayTag();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UCatGA_BodyActionRescueCharacterToCamp::CommitRescueCharacterToCampAfterWindow()
{
	// 提交流程：前摇结束后重读 Controller、Camp 和目标 Character，先过统一玩法 gate，再把救援裁决交回 Camp/Condition。
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	ACatfishingPlayerController* Controller = ResolveBodyActionController(ActorInfo);
	if (!ActiveRequest || !Controller)
	{
		CancelAbility(CurrentSpecHandle, ActorInfo, GetCurrentActivationInfo(), true);
		return;
	}

	FCatDomainCommandResult Result;
	Result.RequestId = ActiveRequest->RequestId;
	if (!Controller->CanSubmitBodyActionCommand())
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (!ActiveRequest->Camp || !ActiveRequest->TargetCharacter
		|| ActiveRequest->Camp->GetWorld() != Controller->GetWorld()
		|| ActiveRequest->TargetCharacter->GetWorld() != Controller->GetWorld())
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else
	{
		Result = ActiveRequest->Camp->RescueToCamp(Controller, ActiveRequest->TargetCharacter,
			ActiveRequest->RequestId);
	}

	Controller->DeliverBodyActionCommandResultToOwningClient(Result);
	if (!CatIsAcceptedDomainCommandResult(Result))
	{
		CancelAbility(CurrentSpecHandle, ActorInfo, GetCurrentActivationInfo(), true);
		return;
	}
	EndAbility(CurrentSpecHandle, ActorInfo, GetCurrentActivationInfo(), true, false);
}
