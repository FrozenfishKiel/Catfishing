#include "AbilitySystem/BodyAction/CatBodyActionPresentationSettings.h"

#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "Animation/AnimMontage.h"

namespace
{
	/** 追加一个保留 BodyAction 的默认表现记录；默认将动作事件本身作为表现键，不猜测 Montage 资产。 */
	void AddDefaultBodyActionPresentationConfig(TArray<FCatBodyActionPresentationConfig>& Configs, const FGameplayTag BodyActionEventTag, const float LeadInSeconds)
	{
		FCatBodyActionPresentationConfig Config;
		Config.BodyActionEventTag = BodyActionEventTag;
		Config.LeadInSeconds = LeadInSeconds;
		Config.PresentationEventTag = BodyActionEventTag;
		Configs.Add(Config);
	}
}

UCatBodyActionPresentationSettings::UCatBodyActionPresentationSettings()
{
	// 构造流程：为六个仍由 Ability 承担前摇和表现生命周期的 Camp/Social 动作建立标签配置；库存、献祭、偷鱼和 Wet 反馈走各自业务服务或表现层。
	ActionPresentationConfigs.Reserve(6);
	const FGameplayTag BodyActionEventTags[] = {
		CatFishingAbilityTags::AbilityEvent_Body_CampRest.GetTag(),
		CatFishingAbilityTags::AbilityEvent_Body_CampfirePlayback.GetTag(),
		CatFishingAbilityTags::AbilityEvent_Body_RescueCharacterToCamp.GetTag(),
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

FGameplayTag UCatBodyActionPresentationSettings::GetPresentationEventTag(const FGameplayTag BodyActionEventTag) const
{
	// 表现键读取流程：显式键有效时使用它；否则返回动作事件标签，让开始和停止表现始终共享同一个可追踪键。
	if (const FCatBodyActionPresentationConfig* Config = FindPresentationConfig(BodyActionEventTag); Config && Config->PresentationEventTag.IsValid()) return Config->PresentationEventTag;
	return BodyActionEventTag;
}

UAnimMontage* UCatBodyActionPresentationSettings::LoadMontage(const FGameplayTag BodyActionEventTag) const
{
	// Montage 读取流程：只有命中配置且软引用有效时同步加载；缺资源返回空让蓝图表现继续承接。
	const FCatBodyActionPresentationConfig* Config = FindPresentationConfig(BodyActionEventTag);
	return Config && !Config->Montage.IsNull() ? Config->Montage.LoadSynchronous() : nullptr;
}
