#include "AbilitySystem/CatAbilitySettings.h"

#include "AbilitySystem/CatAbilityInputConfig.h"
#include "AbilitySystem/CatAbilitySet.h"
#include "Character/CatCharacterDefinition.h"

// ASC runtime gate 流程：要求显式总开关和当前唯一支持的 Full 策略；不按构建配置关闭正式 Character 身体链，也不猜测 Mixed。
bool UCatAbilitySettings::IsRuntimeEnabled() const
{
	return bEnableCharacterAbilityRuntime && ReplicationPolicy == ECatAbilityReplicationPolicy::Full;
}

// 初始属性读取流程：先清三项输出；只有正式 runtime、显式 tuning、非负 Poison 和两项正搏斗值全部有限时才整体返回，避免半套初值进入 ASC。
bool UCatAbilitySettings::TryGetInitialAttributes(float& OutPoison, float& OutFishingStrength,
	float& OutFightStamina) const
{
	OutPoison = 0.0f;
	OutFishingStrength = 0.0f;
	OutFightStamina = 0.0f;
	if (!IsRuntimeEnabled() || !bEnableInitialAttributeTuning || !FMath::IsFinite(InitialPoison)
		|| !FMath::IsFinite(InitialFishingStrength) || !FMath::IsFinite(InitialFightStamina)
		|| InitialPoison < 0.0f || InitialFishingStrength <= 0.0f || InitialFightStamina <= 0.0f)
	{
		return false;
	}
	OutPoison = InitialPoison;
	OutFishingStrength = InitialFishingStrength;
	OutFightStamina = InitialFightStamina;
	return true;
}

bool UCatAbilitySettings::IsFishingRuntimeReady() const
{
	// 运行就绪检查流程：先要求 Ability runtime gate 与两份软引用存在，再同步加载 AbilitySet/InputConfig，
	// 最后复用三项初始身体属性校验；任何一环缺失都保持 fail-closed，避免输入或 ASC 单独就绪。
	if (!IsRuntimeEnabled() || DefaultAbilitySet.IsNull() || AbilityInputConfig.IsNull())
	{
		return false;
	}
	const UCatAbilitySet* AbilitySet = DefaultAbilitySet.LoadSynchronous();
	const UCatAbilityInputConfig* InputConfig = AbilityInputConfig.LoadSynchronous();
	float Poison = 0.0f;
	float FishingStrength = 0.0f;
	float FightStamina = 0.0f;
	return AbilitySet && AbilitySet->IsRuntimeReady() && InputConfig && InputConfig->IsRuntimeReady()
		&& TryGetInitialAttributes(Poison, FishingStrength, FightStamina);
}

bool UCatAbilitySettings::TryGetInitialFightStamina(float& OutFightStamina) const
{
	// 搏斗体力基线读取流程：仍走完整初始属性校验，只把 FightStamina 输出给钓鱼消耗方，避免绕过 Poison/FishingStrength 的同源配置检查。
	OutFightStamina = 0.0f;
	float Poison = 0.0f;
	float FishingStrength = 0.0f;
	return TryGetInitialAttributes(Poison, FishingStrength, OutFightStamina);
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

// 按种类初始属性流程：全局 runtime/tuning gate 仍先成立；Id 指定即必须解析到唯一就绪定义（fail-closed），None 走全局路径。
bool UCatAbilitySettings::TryGetInitialAttributesForCharacter(const FName CatDefinitionId, float& OutPoison,
	float& OutFishingStrength, float& OutFightStamina) const
{
	OutPoison = 0.0f;
	OutFishingStrength = 0.0f;
	OutFightStamina = 0.0f;
	if (CatDefinitionId.IsNone())
	{
		return TryGetInitialAttributes(OutPoison, OutFishingStrength, OutFightStamina);
	}
	if (!IsRuntimeEnabled() || !bEnableInitialAttributeTuning)
	{
		return false;
	}
	const UCatCharacterDefinition* Definition = FindRuntimeCharacterDefinition(CatDefinitionId);
	if (!Definition)
	{
		return false;
	}
	OutPoison = Definition->InitialPoison;
	OutFishingStrength = Definition->FishingStrength;
	OutFightStamina = Definition->FightStaminaMaximum;
	return true;
}

// 体力基线流程：复用按种类初始属性的完整解析，保证"当前体力初始化"与"搏斗上限装配"读到同一个值。
bool UCatAbilitySettings::TryGetFightStaminaBaselineForCharacter(const FName CatDefinitionId, float& OutFightStamina) const
{
	float Poison = 0.0f;
	float FishingStrength = 0.0f;
	return TryGetInitialAttributesForCharacter(CatDefinitionId, Poison, FishingStrength, OutFightStamina);
}
