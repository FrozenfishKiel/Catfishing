#include "AbilitySystem/CatAbilitySettings.h"

// ASC runtime gate 流程：要求显式总开关和当前唯一支持的 Full 策略；不按构建配置关闭正式 Character 身体链，也不猜测 Mixed。
bool UCatAbilitySettings::IsRuntimeEnabled() const
{
	return bEnableCharacterAbilityRuntime && ReplicationPolicy == ECatAbilityReplicationPolicy::Full;
}

// 诊断参数读取流程：先清输出；Shipping 永久拒绝，其他构建还需正式 runtime、诊断 gate 与有限非零值同时成立。
bool UCatAbilitySettings::TryGetDiagnosticPoisonDelta(float& OutDelta) const
{
	OutDelta = 0.0f;
#if UE_BUILD_SHIPPING
	return false;
#else
	if (!IsRuntimeEnabled() || !bEnableDiagnosticAbility || !FMath::IsFinite(DiagnosticPoisonDelta)
		|| FMath::IsNearlyZero(DiagnosticPoisonDelta))
	{
		return false;
	}
	OutDelta = DiagnosticPoisonDelta;
	return true;
#endif
}

// 初始属性读取流程：先清三项输出；只有正式 runtime、显式 tuning、非负 Poison 和两项正搏斗值全部有限时才整体返回，避免半套初值进入 ASC。
bool UCatAbilitySettings::TryGetInitialAttributes(float& OutPoison, float& OutFishingStrength, float& OutFightStamina) const
{
	OutPoison = 0.0f;
	OutFishingStrength = 0.0f;
	OutFightStamina = 0.0f;
	if (!IsRuntimeEnabled() || !bEnableInitialAttributeTuning
		|| !FMath::IsFinite(InitialPoison)
		|| !FMath::IsFinite(InitialFishingStrength) || !FMath::IsFinite(InitialFightStamina)
		|| InitialPoison < 0.0f
		|| InitialFishingStrength <= 0.0f || InitialFightStamina <= 0.0f)
	{
		return false;
	}
	OutPoison = InitialPoison;
	OutFishingStrength = InitialFishingStrength;
	OutFightStamina = InitialFightStamina;
	return true;
}
