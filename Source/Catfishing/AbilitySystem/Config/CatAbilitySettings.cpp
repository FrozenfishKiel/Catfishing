#include "AbilitySystem/Config/CatAbilitySettings.h"

#include "AbilitySystem/Config/CatAbilityInputConfig.h"
#include "AbilitySystem/Config/CatAbilitySet.h"
#include "Character/CatCharacterDefinition.h"

// ASC runtime gate 流程：要求显式总开关和当前唯一支持的 Full 策略；不按构建配置关闭正式 Character 身体链，也不猜测 Mixed。
bool UCatAbilitySettings::IsRuntimeEnabled() const
{
	return bEnableCharacterAbilityRuntime && ReplicationPolicy == ECatAbilityReplicationPolicy::Full;
}

// 初始属性读取流程：先清两项输出；只有正式 runtime、显式 tuning、正力量和正体力上限全部有限时才整体返回，避免半套初值进入 ASC。
// 墓碑（2026-09-12）：这里原本还输出一项 InitialPoison，随渐进中毒模型一并删除，黄色体力开局固定为 0、不由配置播种。
bool UCatAbilitySettings::TryGetInitialAttributes(float& OutFishingStrength,
	float& OutMaxFightStamina) const
{
	OutFishingStrength = 0.0f;
	OutMaxFightStamina = 0.0f;
	if (!IsRuntimeEnabled() || !bEnableInitialAttributeTuning
		|| !FMath::IsFinite(InitialFishingStrength) || !FMath::IsFinite(InitialFightStamina)
		|| InitialFishingStrength <= 0.0f || InitialFightStamina <= 0.0f)
	{
		return false;
	}
	OutFishingStrength = InitialFishingStrength;
	OutMaxFightStamina = InitialFightStamina;
	return true;
}

bool UCatAbilitySettings::IsFishingRuntimeReady() const
{
	// 运行就绪检查流程：先要求 Ability runtime gate 与两份软引用存在，再同步加载 AbilitySet/InputConfig，
	// 最后复用默认猫种解析后的两项初始身体属性校验；任何一环缺失都保持 fail-closed。
	if (!IsRuntimeEnabled() || DefaultAbilitySet.IsNull() || AbilityInputConfig.IsNull())
	{
		return false;
	}
	const UCatAbilitySet* AbilitySet = DefaultAbilitySet.LoadSynchronous();
	const UCatAbilityInputConfig* InputConfig = AbilityInputConfig.LoadSynchronous();
	float FishingStrength = 0.0f;
	float MaxFightStamina = 0.0f;
	return AbilitySet && AbilitySet->IsRuntimeReady() && InputConfig && InputConfig->IsRuntimeReady()
		&& TryGetInitialAttributesForCharacter(NAME_None, FishingStrength, MaxFightStamina);
}

// 猫种类查询流程：同步解析显式清单并只接受唯一就绪匹配；与装备定义查询同一套"重复返回空"语义。
const UCatCharacterDefinition* UCatAbilitySettings::FindRuntimeCharacterDefinition(const FName CatDefinitionId) const
{
	if (CatDefinitionId.IsNone())
	{
		return nullptr;
	}
	const UCatCharacterDefinition* Match = nullptr;
	for (const TSoftObjectPtr<UCatCharacterDefinition>& Ref : CharacterDefinitions)
	{
		const UCatCharacterDefinition* Definition = Ref.LoadSynchronous();
		if (!Definition || !Definition->IsRuntimeDefinitionReady() || Definition->CatDefinitionId != CatDefinitionId)
		{
			continue;
		}
		if (Match)
		{
			return nullptr;
		}
		Match = Definition;
	}
	return Match;
}

// 按种类初始属性流程：显式角色 ID 优先，否则使用配置的默认猫种 ID；只有两者都为 None 才使用全局 Initial* 回退，体力数值只作为 MaxFightStamina 播种源。
bool UCatAbilitySettings::TryGetInitialAttributesForCharacter(const FName CatDefinitionId,
	float& OutFishingStrength, float& OutMaxFightStamina) const
{
	OutFishingStrength = 0.0f;
	OutMaxFightStamina = 0.0f;
	const FName ResolvedDefinitionId = CatDefinitionId.IsNone() ? DefaultCharacterDefinitionId : CatDefinitionId;
	if (ResolvedDefinitionId.IsNone())
	{
		return TryGetInitialAttributes(OutFishingStrength, OutMaxFightStamina);
	}
	if (!IsRuntimeEnabled() || !bEnableInitialAttributeTuning)
	{
		return false;
	}
	const UCatCharacterDefinition* Definition = FindRuntimeCharacterDefinition(ResolvedDefinitionId);
	if (!Definition)
	{
		return false;
	}
	OutFishingStrength = Definition->FishingStrength;
	OutMaxFightStamina = Definition->FightStaminaMaximum;
	return true;
}
