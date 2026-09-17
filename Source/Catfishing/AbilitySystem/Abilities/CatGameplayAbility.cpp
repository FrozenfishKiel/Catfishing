#include "AbilitySystem/Abilities/CatGameplayAbility.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "GameFramework/Pawn.h"
#include "AbilitySystem/Costs/CatAbilityCost.h"
#include "AbilitySystem/Tags/CatStateTags.h"

// 默认执行策略：倒地标签阻止新激活，每个授予 Spec 使用独立能力实例，客户端可预测启动；子类可改网络策略，资源写入由成本的权威分支负责。
UCatGameplayAbility::UCatGameplayAbility()
{
	ActivationBlockedTags.AddTag(CatStateTags::Downed);
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
}

// 预检流程：先遵守 GAS 的属性资源约束，再检查所有附加成本；任意配置缺失或资源不足都拒绝激活。
bool UCatGameplayAbility::CheckCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!ActorInfo || !Super::CheckCost(Handle, ActorInfo, OptionalRelevantTags)) return false;
	for (const UCatAbilityCost* Cost : AdditionalCosts)
		if (!Cost || !Cost->CheckCost(this, ActorInfo, OptionalRelevantTags)) return false;
	return true;
}

// 支付流程：GAS 处理属性成本及其预测，服务器处理实物成本；成本实现不得重新激活能力或自行执行玩法效果。
void UCatGameplayAbility::ApplyCost(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayAbilityActivationInfo ActivationInfo) const
{
	Super::ApplyCost(Handle, ActorInfo, ActivationInfo);
	if (ActorInfo && ActorInfo->IsNetAuthority())
		for (const UCatAbilityCost* Cost : AdditionalCosts)
			if (Cost) Cost->ApplyCost(this, ActorInfo);
}

// 控制器读取流程：优先读取 GAS 的当前 ActorInfo；无控制器缓存时只查询本次 Avatar 的 Pawn，不保存跨占有引用。
ACatfishingPlayerController* UCatGameplayAbility::GetCatPlayerControllerFromActorInfo() const
{
	if (!CurrentActorInfo) return nullptr;
	if (auto* Controller = Cast<ACatfishingPlayerController>(CurrentActorInfo->PlayerController.Get())) return Controller;
	const auto* Pawn = Cast<APawn>(CurrentActorInfo->AvatarActor.Get());
	return Pawn ? Cast<ACatfishingPlayerController>(Pawn->GetController()) : nullptr;
}
