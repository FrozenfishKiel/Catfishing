#include "AbilitySystem/BodyAction/CatBodyActionPresentationSettings.h"

#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "Animation/AnimMontage.h"

namespace
{
	/** 追加一个保留 BodyAction 的默认表现记录；不指定 Cue 或猜测 Montage 资产。 */
	void AddDefaultBodyActionPresentationConfig(TArray<FCatBodyActionPresentationConfig>& Configs, const FGameplayTag BodyActionEventTag, const float LeadInSeconds)
	{
		FCatBodyActionPresentationConfig Config;
		Config.BodyActionEventTag = BodyActionEventTag;
		Config.LeadInSeconds = LeadInSeconds;
		Configs.Add(Config);
	}
}

UCatBodyActionPresentationSettings::UCatBodyActionPresentationSettings()
{
	// 构造流程：为四个仍由 Ability 承担前摇和表现生命周期的 Camp/Social 动作建立标签配置；库存和供品结算走各自业务服务或表现层。
	ActionPresentationConfigs.Reserve(4);
	const FGameplayTag BodyActionEventTags[] = {
		CatFishingAbilityTags::AbilityEvent_Body_CampfirePlayback.GetTag(),
		CatFishingAbilityTags::AbilityEvent_Body_RequestManualHelp.GetTag(),
		CatFishingAbilityTags::AbilityEvent_Body_RequestMischief.GetTag(),
		CatFishingAbilityTags::AbilityEvent_Body_PlaceProtectionSign.GetTag()
	};
	for (const FGameplayTag EventTag : BodyActionEventTags)
	{
		AddDefaultBodyActionPresentationConfig(ActionPresentationConfigs, EventTag, DefaultLeadInSeconds);
	}
}

const FCatBodyActionPresentationConfig* UCatBodyActionPresentationSettings::FindPresentationConfig(const FGameplayTag BodyActionEventTag) const
{
	// 查找流程：空标签立即拒绝；有效标签从后向前扫描，使 ini 追加项可以覆盖构造期默认项。
	if (!BodyActionEventTag.IsValid()) return nullptr;
	for (int32 Index = ActionPresentationConfigs.Num() - 1; Index >= 0; --Index)
	{
		if (ActionPresentationConfigs[Index].BodyActionEventTag == BodyActionEventTag) return &ActionPresentationConfigs[Index];
	}
	return nullptr;
}

float UCatBodyActionPresentationSettings::GetLeadInSeconds(const FGameplayTag BodyActionEventTag) const
{
	// 前摇读取流程：优先返回动作级配置；缺失时使用非负默认值，避免配置缺失把 Cancel 窗口变成负时长。
	const FCatBodyActionPresentationConfig* Config = FindPresentationConfig(BodyActionEventTag);
	return FMath::Max(0.0f, Config ? Config->LeadInSeconds : DefaultLeadInSeconds);
}

UAnimMontage* UCatBodyActionPresentationSettings::LoadMontage(const FGameplayTag BodyActionEventTag) const
{
	// Montage 读取流程：只有命中配置且软引用有效时同步加载；缺资源返回空，由 GA 保留原有前摇和提交时点。
	const FCatBodyActionPresentationConfig* Config = FindPresentationConfig(BodyActionEventTag);
	return Config && !Config->Montage.IsNull() ? Config->Montage.LoadSynchronous() : nullptr;
}
