#include "AbilitySystem/Fishing/CatFishingGameplayAbility.h"

#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Framework/Game/CatGameplayTypes.h"

UCatFishingGameplayAbility::UCatFishingGameplayAbility()
{
	// 构造流程：
	// 1. 先允许客户端本地预测执行，让按键表现不用等服务器往返。
	// 2. 再按 Owner Actor 保留实例，确保按住型输入的生命周期不会在多玩家或多技能之间串状态。
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

bool UCatFishingGameplayAbility::ShouldWaitForRemoteClient(const bool bIsNetAuthority,
	const bool bIsLocallyControlled)
{
	// 判断流程：只有服务器上的远程客户端镜像返回 true；这种镜像会通过 RPC 收到客户端命令，不能在 Ability 激活点再提交一次。
	return bIsNetAuthority && !bIsLocallyControlled;
}

UCatFishingCommandComponent* UCatFishingGameplayAbility::ResolveCommandComponent(
	const FGameplayAbilityActorInfo* ActorInfo) const
{
	// 解析流程：从 ActorInfo 读取 owning PlayerController 并转为项目控制器；缺少任何一层时返回 nullptr，让具体输入类取消激活。
	const ACatfishingPlayerController* Controller = ActorInfo
		? Cast<ACatfishingPlayerController>(ActorInfo->PlayerController.Get()) : nullptr;
	return Controller ? Controller->GetFishingCommandComponent() : nullptr;
}

bool UCatFishingGameplayAbility::CanSubmitLocalCommand(const FGameplayAbilityActorInfo* ActorInfo) const
{
	// 判断流程：先确认实例属于本地控制端，再确认能解析出命令组件；服务器镜像和异常控制器都不能直接发起玩家输入命令。
	return ActorInfo && ActorInfo->IsLocallyControlled() && ResolveCommandComponent(ActorInfo);
}

bool UCatFishingGameplayAbility::IsRemoteAuthorityMirror(const FGameplayAbilityActorInfo* ActorInfo) const
{
	// 判断流程：复用 authority/local control 组合判定，让所有 Fishing 输入类共享同一条防重复提交规则。
	return ActorInfo && ShouldWaitForRemoteClient(ActorInfo->IsNetAuthority(), ActorInfo->IsLocallyControlled());
}

void UCatFishingGameplayAbility::FinishOneShot(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bSubmitted)
{
	// 收尾流程：一次性命令拿到有效提交结果时结束 Ability；本地校验失败或命令组件缺失时取消，避免留下悬空激活实例。
	if (bSubmitted)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
	}
	else
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
	}
}
