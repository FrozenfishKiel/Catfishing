#include "AbilitySystem/Fishing/InputAbilities/CatFishingScoopAbility.h"

#include "AbilitySystemGlobals.h"

#include "AbilitySystem/Effects/CatFishingScoopCooldownEffect.h"
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"

UCatGA_FishingScoop::UCatGA_FishingScoop()
{
	// 构造流程：只写入抄鱼技能 Tag，让独立的挥网输入能被 AbilitySystem 精确授予和激活。
	// 冷却 GE 类保留但本技能自己不再施加它：CheckCooldown 仍靠这个类的授予标签判断「爪子还在麻」，
	// 施加时点已移到服务器判定失败之后（UCatFishingCommandComponent 的 RequestScoop 权威分支），所以本类不再覆写 ApplyCooldown。
	SetAssetTags(FGameplayTagContainer(CatFishingAbilityTags::Ability_Fishing_Scoop));
	CooldownGameplayEffectClass = UCatGE_FishingScoopCooldown::StaticClass();
}

void UCatGA_FishingScoop::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 激活流程：先复核硬直未在身上并提交消耗，再过滤服务器远端镜像；本地端播放挥网表现并提交一次抢抄命令。
	// 硬直只罚挥空、成功抄到不吃硬直（钓鱼规则 §5.2），而成败只有服务器判得出来：
	// 所以这里改成 CommitAbilityCost（不含冷却），冷却 GE 由 UCatFishingCommandComponent 在判定失败后对本人施加。
	// 本地预测施加会让每一次成功抄网都先白锁 3 秒，再靠回滚抹掉，表现上等于成功也吃了硬直。
	// 往返窗口内的连点由服务器的 ScoopCooldownGate 兜住，客户端少一层预测不会多出一次有效出手。
	(void)TriggerEventData;
	// 冷却复核与引擎 CanActivateAbility 同口径：全局忽略冷却的调试开关照旧优先。
	const bool bCooldownReady = UAbilitySystemGlobals::Get().ShouldIgnoreCooldowns()
		|| CheckCooldown(Handle, ActorInfo);
	if (!bCooldownReady || !CommitAbilityCost(Handle, ActorInfo, ActivationInfo))
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
		return;
	}
	if (IsRemoteAuthorityMirror(ActorInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}
	BP_OnLocalInputActivated();
	UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo);
	FinishOneShot(Handle, ActorInfo, ActivationInfo,
		CanSubmitLocalCommand(ActorInfo) && Commands->SubmitScoop().RequestId.IsValid());
}
