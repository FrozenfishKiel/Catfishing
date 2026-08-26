#include "AbilitySystem/CatFishingAbilities.h"

#include "AbilitySystem/CatFishingAbilityTags.h"
#include "AbilitySystem/CatFishingScoopCooldownEffect.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Framework/Game/CatGameplayTypes.h"

UCatFishingGameplayAbility::UCatFishingGameplayAbility()
{
	// 本地预测执行：客户端立即播放表现并本地跑一遍激活流程，服务器再权威裁决，避免等一个 RTT 才有反馈。
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	// 每个 Owner Actor 各自持有一份实例，保证多个技能/多个玩家之间的状态（如本类的按住计时）互不干扰。
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

bool UCatFishingGameplayAbility::ShouldWaitForRemoteClient(const bool bIsNetAuthority,
	const bool bIsLocallyControlled)
{
	// 只有"服务器且非本机操控"（即服务器上代表远程客户端的镜像实例）才需要等待——
	// 这种情况下服务器已经会通过 RPC 收到客户端的命令，不应该自己在这里重复触发一次。
	return bIsNetAuthority && !bIsLocallyControlled;
}

UCatFishingCommandComponent* UCatFishingGameplayAbility::ResolveCommandComponent(
	const FGameplayAbilityActorInfo* ActorInfo) const
{
	// 从 ActorInfo 拿到玩家控制器，并转型为本项目的 PlayerController 子类；ActorInfo 为空或转型失败都返回 nullptr。
	const ACatfishingPlayerController* Controller = ActorInfo
		? Cast<ACatfishingPlayerController>(ActorInfo->PlayerController.Get()) : nullptr;
	// 命令组件挂在 PlayerController 上，统一作为技能与服务器之间提交/接收命令结果的出入口。
	return Controller ? Controller->GetFishingCommandComponent() : nullptr;
}

bool UCatFishingGameplayAbility::CanSubmitLocalCommand(const FGameplayAbilityActorInfo* ActorInfo) const
{
	// 只有本机操控的 Ability 实例、且能解析出命令组件时，才允许在本地发起一次命令提交
	// （避免服务器上的远程镜像或没有命令组件的异常情况误发命令）。
	return ActorInfo && ActorInfo->IsLocallyControlled() && ResolveCommandComponent(ActorInfo);
}

bool UCatFishingGameplayAbility::IsRemoteAuthorityMirror(const FGameplayAbilityActorInfo* ActorInfo) const
{
	// 复用 ShouldWaitForRemoteClient 的判定：当前实例是否是服务器上代表其他机器玩家的镜像。
	return ActorInfo && ShouldWaitForRemoteClient(ActorInfo->IsNetAuthority(), ActorInfo->IsLocallyControlled());
}

void UCatFishingGameplayAbility::FinishOneShot(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bSubmitted)
{
	// 一次性（非按住型）技能的统一收尾：命令提交成功就正常结束，
	// 否则（本地校验失败/命令组件缺失/RequestId 无效）取消本次激活，避免留下悬空的 Ability 实例。
	if (bSubmitted)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
	}
	else
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
	}
}

UCatGA_FishingRodInteract::UCatGA_FishingRodInteract()
{
	// 绑定该技能对应的 GameplayTag，供输入映射与技能系统按 Tag 查找/激活。
	SetAssetTags(FGameplayTagContainer(CatFishingAbilityTags::Ability_Fishing_RodInteract));
}
void UCatGA_FishingRodInteract::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	(void)TriggerEventData; // 本技能不消费触发事件负载。
	if (IsRemoteAuthorityMirror(ActorInfo))
	{
		// 服务器上的远程玩家镜像不重复触发——真正的命令会通过客户端 RPC 到达。
		return;
	}
	// 立即播放本地表现（如摸杆/互动动画），不携带任何玩法结果。
	BP_OnLocalInputActivated();
	UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo);
	// 与鱼竿互动是一次性命令：本地校验通过并且提交后拿到合法 RequestId 才算成功，否则取消本次激活。
	FinishOneShot(Handle, ActorInfo, ActivationInfo,
		CanSubmitLocalCommand(ActorInfo) && Commands->SubmitRodInteract().RequestId.IsValid());
}

UCatGA_FishingPrimaryAction::UCatGA_FishingPrimaryAction()
{
	SetAssetTags(FGameplayTagContainer(CatFishingAbilityTags::Ability_Fishing_Primary));
}
void UCatGA_FishingPrimaryAction::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	(void)TriggerEventData;
	if (IsRemoteAuthorityMirror(ActorInfo))
	{
		return;
	}
	// Primary 按下有“开始瞄准 / 提竿 / 开始收线”三种语义，不能在尚未裁决时统一播投杆 Montage。
	// 真正抛竿成功后由复制的 Hook CastFlight 状态驱动所有客户端播放；提竿由服务器语义分支广播专用事件。
	UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo);
	// 左键是"按住型"技能：按下只登记一次 Pressed 边沿并保持 Ability 存活，真正的动作在 InputReleased 松开时结算。
	if (!CanSubmitLocalCommand(ActorInfo) || !Commands->SubmitPrimaryPressed().RequestId.IsValid())
	{
		// 按下即失败（无法提交命令）时直接取消，不等待松开。
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
	}
}
void UCatGA_FishingPrimaryAction::InputReleased(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)
{
	// 松开时的本地表现钩子（收势），无论服务器最终判定提竿/抛竿成功与否都要播放。
	BP_OnLocalInputReleased();
	if (UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo); CanSubmitLocalCommand(ActorInfo) && Commands)
	{
		// 提交松开边沿；服务器据此换算按住时长/判定提竿时机，具体结果由 Snapshot/命令结果异步回传。
		Commands->SubmitPrimaryReleased();
	}
	// 按住型技能在松开后即完成自身生命周期，不等待服务器结果。
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

UCatGA_FishingSlack::UCatGA_FishingSlack()
{
	SetAssetTags(FGameplayTagContainer(CatFishingAbilityTags::Ability_Fishing_Slack));
}
void UCatGA_FishingSlack::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	(void)TriggerEventData;
	if (IsRemoteAuthorityMirror(ActorInfo))
	{
		return;
	}
	BP_OnLocalInputActivated();
	UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo);
	// 右键松线同样是按住型：按下登记开放线杯边沿，服务器只在 HookedFight 阶段处理。
	if (!CanSubmitLocalCommand(ActorInfo) || !Commands->SubmitSlackPressed().RequestId.IsValid())
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
	}
}
void UCatGA_FishingSlack::InputReleased(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)
{
	BP_OnLocalInputReleased();
	if (UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo); CanSubmitLocalCommand(ActorInfo) && Commands)
	{
		// 提交右键松开边沿，服务器据此重新锁住当前已放出线长。
		Commands->SubmitSlackReleased();
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

UCatGA_FishingCancel::UCatGA_FishingCancel()
{
	SetAssetTags(FGameplayTagContainer(CatFishingAbilityTags::Ability_Fishing_Cancel));
}
void UCatGA_FishingCancel::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	(void)TriggerEventData;
	if (IsRemoteAuthorityMirror(ActorInfo))
	{
		return;
	}
	BP_OnLocalInputActivated();
	UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo);
	// 取消是一次性命令：提交成功（拿到合法 RequestId）才算完成，交由服务器权威判定当前阶段是否允许取消。
	FinishOneShot(Handle, ActorInfo, ActivationInfo,
		CanSubmitLocalCommand(ActorInfo) && Commands->SubmitCancel().RequestId.IsValid());
}

UCatGA_FishingScoop::UCatGA_FishingScoop()
{
	SetAssetTags(FGameplayTagContainer(CatFishingAbilityTags::Ability_Fishing_Scoop));
	CooldownGameplayEffectClass = UCatGE_FishingScoopCooldown::StaticClass();
}
void UCatGA_FishingScoop::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	(void)TriggerEventData;
	// Commit 是 GAS 真正应用预测冷却/服务器确认冷却的入口；只配置 Cooldown GE 而不 Commit 不会生效。
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
		return;
	}
	if (IsRemoteAuthorityMirror(ActorInfo))
	{
		// 远端客户端的服务器镜像也要 Commit 才有服务器权威 GE；命令仍由客户端 RPC 提交，不能重复发。
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}
	// 挥网抄鱼的即时表现（挥网动作），不代表抢抄一定成功，成败由服务器 Compare-and-Commit 裁决。
	BP_OnLocalInputActivated();
	UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo);
	// 抄鱼是一次性命令：本地提交拿到合法 RequestId 即视为已发起，真正是否抢到鱼由 Session::RequestScoop 权威裁决。
	FinishOneShot(Handle, ActorInfo, ActivationInfo,
		CanSubmitLocalCommand(ActorInfo) && Commands->SubmitScoop().RequestId.IsValid());
}

void UCatGA_FishingScoop::ApplyCooldown(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const
{
	double CooldownSeconds = 0.0;
	if (!GetDefault<UCatFishingSettings>()->TryGetScoopCooldown(CooldownSeconds))
	{
		return; // 服务器命令层会对非法配置 fail-closed；这里不制造错误时长的预测 GE。
	}
	FGameplayEffectSpecHandle Spec = MakeOutgoingGameplayEffectSpec(Handle, ActorInfo, ActivationInfo,
		CooldownGameplayEffectClass, GetAbilityLevel(Handle, ActorInfo));
	if (Spec.IsValid())
	{
		Spec.Data->SetDuration(static_cast<float>(CooldownSeconds), true);
		ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, Spec);
	}
}

UCatGA_FishingChum::UCatGA_FishingChum()
{
	SetAssetTags(FGameplayTagContainer(CatFishingAbilityTags::Ability_Fishing_Chum));
}
void UCatGA_FishingChum::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	(void)TriggerEventData;
	if (IsRemoteAuthorityMirror(ActorInfo))
	{
		return;
	}
	BP_OnLocalInputActivated();
	// Q 打窝为按住蓄力：按下只发起蓄力边沿，松开（InputReleased）时服务器按时长投放。
	UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo);
	// 按下先登记 ChumPressed 边沿，服务器记录蓄力起始时间（见 CommandComponent::ChumChargeStartServerTime）；
	// 若本地校验或提交失败则直接取消，不进入蓄力等待。
	if (!CanSubmitLocalCommand(ActorInfo) || !Commands->SubmitChumPressed().RequestId.IsValid())
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
	}
}
void UCatGA_FishingChum::InputReleased(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo)
{
	BP_OnLocalInputReleased();
	if (UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo); CanSubmitLocalCommand(ActorInfo) && Commands)
	{
		// 松开触发真正的打窝投掷：服务器按"按下到松开"的时长换算蓄力，决定弹道抛掷距离/窝料量。
		Commands->SubmitChumReleased();
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

UCatGA_FishingOutcomeBase::UCatGA_FishingOutcomeBase()
{
	// 结果类技能（如捕获成功/失败的呈现）只能由服务器发起，客户端不可预测触发，避免误播与权威结果不一致的表现。
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}
