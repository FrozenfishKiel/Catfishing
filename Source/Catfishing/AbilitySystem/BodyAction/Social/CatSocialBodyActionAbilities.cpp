#include "AbilitySystem/BodyAction/Social/CatSocialBodyActionAbilities.h"

#include "AbilitySystem/BodyAction/CatBodyActionPresentationSettings.h"
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Character/CatCharacter.h"
#include "Engine/World.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Social/CatSocialService.h"

namespace
{
	/** 从当前 ActorInfo 找 owning Controller；PlayerController 缺失时只回退 Avatar Pawn 的 Controller。 */
	ACatfishingPlayerController* ResolveSocialBodyActionController(const FGameplayAbilityActorInfo* ActorInfo)
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

	/** 从当前 ActorInfo 找身体 Avatar；Social 身体动作表现永远由发起者 Character 承接。 */
	ACatCharacter* ResolveSocialBodyActionCharacter(const FGameplayAbilityActorInfo* ActorInfo)
	{
		return ActorInfo ? Cast<ACatCharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	}

	/** 解析动作表现标签；配置缺失时回退到动作事件本身，保证开始和停止表现拥有同一身份。 */
	FGameplayTag ResolveSocialPresentationEventTag(const FGameplayTag BodyActionEventTag)
	{
		const UCatBodyActionPresentationSettings* Settings = GetDefault<UCatBodyActionPresentationSettings>();
		return Settings ? Settings->GetPresentationEventTag(BodyActionEventTag) : BodyActionEventTag;
	}

	/** 解析可取消前摇时长；具体 Ability 只读表现配置，不从 Social 服务借第二套时长规则。 */
	float ResolveSocialLeadInSeconds(const FGameplayTag BodyActionEventTag)
	{
		const UCatBodyActionPresentationSettings* Settings = GetDefault<UCatBodyActionPresentationSettings>();
		return Settings ? Settings->GetLeadInSeconds(BodyActionEventTag) : 0.0f;
	}
}

UCatGA_BodyActionRequestManualHelp::UCatGA_BodyActionRequestManualHelp()
{
	// 构造流程：求助动作只在服务器执行，按 Actor 保存自己的前摇状态，并暴露共同资产标签供 Fishing Cancel 命中。
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
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
	// 2. 再冻结请求和表现标签，启动 Character 表现，随后打开可取消的前摇窗口。
	// 3. 零秒前摇也走本类 CommitManualHelpAfterWindow，保证求助没有第二条即时提交路径。
	const FGameplayTag BodyActionEventTag = CatFishingAbilityTags::AbilityEvent_Body_RequestManualHelp.GetTag();
	// GameplayEventData 只给 Ability 一个只读视图；请求 UObject 由命令组件创建，这里只借非 const 指针保留 GC 引用，不改写载荷内容。
	UCatBodyActionRequestManualHelp* Request = TriggerEventData
		? const_cast<UCatBodyActionRequestManualHelp*>(Cast<UCatBodyActionRequestManualHelp>(TriggerEventData->OptionalObject.Get())) : nullptr;
	ACatfishingPlayerController* Controller = ResolveSocialBodyActionController(ActorInfo);
	if (!TriggerEventData || TriggerEventData->EventTag != BodyActionEventTag
		|| !Request || Request->GetClass() != UCatBodyActionRequestManualHelp::StaticClass() || !Controller)
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
		return;
	}

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	ActiveRequest = Request;
	ActivePresentationEventTag = ResolveSocialPresentationEventTag(BodyActionEventTag);
	if (ACatCharacter* Character = ResolveSocialBodyActionCharacter(ActorInfo); Character && ActivePresentationEventTag.IsValid())
	{
		Character->Multicast_PlayBodyActionPresentation(BodyActionEventTag, ActivePresentationEventTag);
	}

	const float LeadInSeconds = ResolveSocialLeadInSeconds(BodyActionEventTag);
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
	// 收尾流程：只有取消才停止前摇表现；正常提交成功后由 Social 求助结果驱动后续 UI 和世界状态。
	if (bWasCancelled && ActiveRequest && ActivePresentationEventTag.IsValid())
	{
		if (ACatCharacter* Character = ResolveSocialBodyActionCharacter(ActorInfo))
		{
			Character->Multicast_StopBodyActionPresentation(
				CatFishingAbilityTags::AbilityEvent_Body_RequestManualHelp, ActivePresentationEventTag);
		}
	}
	ActiveRequest = nullptr;
	ActivePresentationEventTag = FGameplayTag();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UCatGA_BodyActionRequestManualHelp::CommitManualHelpAfterWindow()
{
	// 提交流程：前摇结束后重读 Controller 和 Social 服务，先过统一玩法 gate，再发布普通玩家求助信号。
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	ACatfishingPlayerController* Controller = ResolveSocialBodyActionController(ActorInfo);
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
	else if (UCatSocialService* Social = Controller->GetWorld()
		? Controller->GetWorld()->GetSubsystem<UCatSocialService>() : nullptr)
	{
		Result = Social->RequestManualHelp(Controller, ActiveRequest->RequestId, ActiveRequest->HelpKind);
	}
	else
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}

	Controller->DeliverBodyActionCommandResultToOwningClient(Result);
	if (!CatIsAcceptedDomainCommandResult(Result))
	{
		CancelAbility(CurrentSpecHandle, ActorInfo, GetCurrentActivationInfo(), true);
		return;
	}
	EndAbility(CurrentSpecHandle, ActorInfo, GetCurrentActivationInfo(), true, false);
}

UCatGA_BodyActionRequestMischief::UCatGA_BodyActionRequestMischief()
{
	// 构造流程：恶作剧动作只在服务器执行，按 Actor 保存自己的前摇状态，并暴露共同资产标签供 Fishing Cancel 命中。
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	SetAssetTags(FGameplayTagContainer(CatFishingAbilityTags::Ability_Body_Action.GetTag()));
	FAbilityTriggerData Trigger;
	Trigger.TriggerSource = EGameplayAbilityTriggerSource::GameplayEvent;
	Trigger.TriggerTag = CatFishingAbilityTags::AbilityEvent_Body_RequestMischief;
	AbilityTriggers.Add(Trigger);
}

void UCatGA_BodyActionRequestMischief::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 激活流程：
	// 1. 先校验恶作剧事件、精确载荷类型和 owning Controller，错配直接取消，不进入 Social 写口。
	// 2. 再冻结请求和表现标签，启动 Character 表现，随后打开可取消的前摇窗口。
	// 3. 零秒前摇也走本类 CommitMischiefAfterWindow，保证恶作剧没有第二条即时提交路径。
	const FGameplayTag BodyActionEventTag = CatFishingAbilityTags::AbilityEvent_Body_RequestMischief.GetTag();
	// GameplayEventData 只给 Ability 一个只读视图；请求 UObject 由命令组件创建，这里只借非 const 指针保留 GC 引用，不改写载荷内容。
	UCatBodyActionRequestMischief* Request = TriggerEventData
		? const_cast<UCatBodyActionRequestMischief*>(Cast<UCatBodyActionRequestMischief>(TriggerEventData->OptionalObject.Get())) : nullptr;
	ACatfishingPlayerController* Controller = ResolveSocialBodyActionController(ActorInfo);
	if (!TriggerEventData || TriggerEventData->EventTag != BodyActionEventTag
		|| !Request || Request->GetClass() != UCatBodyActionRequestMischief::StaticClass() || !Controller)
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
		return;
	}

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	ActiveRequest = Request;
	ActivePresentationEventTag = ResolveSocialPresentationEventTag(BodyActionEventTag);
	if (ACatCharacter* Character = ResolveSocialBodyActionCharacter(ActorInfo); Character && ActivePresentationEventTag.IsValid())
	{
		Character->Multicast_PlayBodyActionPresentation(BodyActionEventTag, ActivePresentationEventTag);
	}

	const float LeadInSeconds = ResolveSocialLeadInSeconds(BodyActionEventTag);
	if (LeadInSeconds <= 0.0f)
	{
		CommitMischiefAfterWindow();
		return;
	}

	UAbilityTask_WaitDelay* CommitDelay = UAbilityTask_WaitDelay::WaitDelay(this, LeadInSeconds);
	if (!CommitDelay)
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
		return;
	}
	// 委托生命周期由 AbilityTask 挂在当前恶作剧 Ability 实例上；取消时 EndAbility 清空 ActiveRequest，迟到回调只能取消收口。
	CommitDelay->OnFinish.AddDynamic(this, &ThisClass::CommitMischiefAfterWindow);
	CommitDelay->ReadyForActivation();
}

void UCatGA_BodyActionRequestMischief::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility, const bool bWasCancelled)
{
	// 收尾流程：只有取消才停止前摇表现；正常提交成功后由 Social 恶作剧结果驱动冷却和保护牌反馈。
	if (bWasCancelled && ActiveRequest && ActivePresentationEventTag.IsValid())
	{
		if (ACatCharacter* Character = ResolveSocialBodyActionCharacter(ActorInfo))
		{
			Character->Multicast_StopBodyActionPresentation(
				CatFishingAbilityTags::AbilityEvent_Body_RequestMischief, ActivePresentationEventTag);
		}
	}
	ActiveRequest = nullptr;
	ActivePresentationEventTag = FGameplayTag();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UCatGA_BodyActionRequestMischief::CommitMischiefAfterWindow()
{
	// 提交流程：前摇结束后重读 World 和目标 Controller，先过统一玩法 gate，再把权限、冷却和保护牌裁决交给 Social。
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	ACatfishingPlayerController* Controller = ResolveSocialBodyActionController(ActorInfo);
	if (!ActiveRequest || !Controller)
	{
		CancelAbility(CurrentSpecHandle, ActorInfo, GetCurrentActivationInfo(), true);
		return;
	}

	FCatDomainCommandResult Result;
	Result.RequestId = ActiveRequest->RequestId;
	UWorld* World = Controller->GetWorld();
	if (!Controller->CanSubmitBodyActionCommand())
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (!World)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else
	{
		APlayerController* TargetController = nullptr;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (APlayerController* Candidate = It->Get(); Candidate && Candidate->PlayerState == ActiveRequest->TargetPlayerState)
			{
				TargetController = Candidate;
				break;
			}
		}
		if (UCatSocialService* Social = World->GetSubsystem<UCatSocialService>())
		{
			Result = Social->RequestMischief(Controller, TargetController, ActiveRequest->RequestId,
				ActiveRequest->InteractionLocation);
		}
		else
		{
			Result.Error = ECatDomainCommandError::DependencyUnavailable;
		}
	}

	Controller->DeliverBodyActionCommandResultToOwningClient(Result);
	if (!CatIsAcceptedDomainCommandResult(Result))
	{
		CancelAbility(CurrentSpecHandle, ActorInfo, GetCurrentActivationInfo(), true);
		return;
	}
	EndAbility(CurrentSpecHandle, ActorInfo, GetCurrentActivationInfo(), true, false);
}

UCatGA_BodyActionPlaceProtectionSign::UCatGA_BodyActionPlaceProtectionSign()
{
	// 构造流程：放牌动作只在服务器执行，按 Actor 保存自己的前摇状态，并暴露共同资产标签供 Fishing Cancel 命中。
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	SetAssetTags(FGameplayTagContainer(CatFishingAbilityTags::Ability_Body_Action.GetTag()));
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
	// 2. 再冻结请求和表现标签，启动 Character 表现，随后打开可取消的前摇窗口。
	// 3. 零秒前摇也走本类 CommitPlaceProtectionSignAfterWindow，保证放牌没有第二条即时提交路径。
	const FGameplayTag BodyActionEventTag = CatFishingAbilityTags::AbilityEvent_Body_PlaceProtectionSign.GetTag();
	// GameplayEventData 只给 Ability 一个只读视图；请求 UObject 由命令组件创建，这里只借非 const 指针保留 GC 引用，不改写载荷内容。
	UCatBodyActionRequestPlaceProtectionSign* Request = TriggerEventData
		? const_cast<UCatBodyActionRequestPlaceProtectionSign*>(Cast<UCatBodyActionRequestPlaceProtectionSign>(TriggerEventData->OptionalObject.Get())) : nullptr;
	ACatfishingPlayerController* Controller = ResolveSocialBodyActionController(ActorInfo);
	if (!TriggerEventData || TriggerEventData->EventTag != BodyActionEventTag
		|| !Request || Request->GetClass() != UCatBodyActionRequestPlaceProtectionSign::StaticClass() || !Controller)
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
		return;
	}

	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	ActiveRequest = Request;
	ActivePresentationEventTag = ResolveSocialPresentationEventTag(BodyActionEventTag);
	if (ACatCharacter* Character = ResolveSocialBodyActionCharacter(ActorInfo); Character && ActivePresentationEventTag.IsValid())
	{
		Character->Multicast_PlayBodyActionPresentation(BodyActionEventTag, ActivePresentationEventTag);
	}

	const float LeadInSeconds = ResolveSocialLeadInSeconds(BodyActionEventTag);
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
	// 收尾流程：只有取消才停止前摇表现；正常提交成功后由 Social 放牌结果驱动唯一保护牌状态。
	if (bWasCancelled && ActiveRequest && ActivePresentationEventTag.IsValid())
	{
		if (ACatCharacter* Character = ResolveSocialBodyActionCharacter(ActorInfo))
		{
			Character->Multicast_StopBodyActionPresentation(
				CatFishingAbilityTags::AbilityEvent_Body_PlaceProtectionSign, ActivePresentationEventTag);
		}
	}
	ActiveRequest = nullptr;
	ActivePresentationEventTag = FGameplayTag();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UCatGA_BodyActionPlaceProtectionSign::CommitPlaceProtectionSignAfterWindow()
{
	// 提交流程：前摇结束后重读 Controller 和 Social 服务，先过统一玩法 gate，再提交唯一保护牌位置意图。
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	ACatfishingPlayerController* Controller = ResolveSocialBodyActionController(ActorInfo);
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
	else if (UCatSocialService* Social = Controller->GetWorld()
		? Controller->GetWorld()->GetSubsystem<UCatSocialService>() : nullptr)
	{
		Result = Social->PlaceProtectionSign(Controller, ActiveRequest->RequestId, ActiveRequest->SignLocation);
	}
	else
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}

	Controller->DeliverBodyActionCommandResultToOwningClient(Result);
	if (!CatIsAcceptedDomainCommandResult(Result))
	{
		CancelAbility(CurrentSpecHandle, ActorInfo, GetCurrentActivationInfo(), true);
		return;
	}
	EndAbility(CurrentSpecHandle, ActorInfo, GetCurrentActivationInfo(), true, false);
}
