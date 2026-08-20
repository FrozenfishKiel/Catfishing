#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatAbilitySettings.generated.h"

class UInputAction;
class UInputMappingContext;

/** Character-owned ASC 的已验证复制策略；Mixed 仍需 V3 证据，因此没有可选枚举值。 */
UENUM(BlueprintType)
enum class ECatAbilityReplicationPolicy : uint8
{
	/** 复制策略尚未明确启用，Character 保持组件存在但清除 ActorInfo。 */
	Undecided,
	/** 使用安全的 Full GameplayEffect 复制；双客户端证据形成后仍可继续验证是否需要 Mixed。 */
	Full
};

/** Character-owned ASC 与开发诊断输入的集中设置；正式运行 gate 和非 Shipping 诊断 gate 相互独立。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Character Abilities"))
class CATFISHING_API UCatAbilitySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 判断 Character ASC 正式运行链是否显式启用；当前只接受已有安全语义的 Full 策略。 */
	bool IsRuntimeEnabled() const;

	/** 读取非 Shipping 诊断 Ability 的 Poison 改变量；诊断关闭、非有限或零值时清输出并返回 false。 */
	bool TryGetDiagnosticPoisonDelta(float& OutDelta) const;

	/**
	 * 读取新 Character 的三项局内初始属性；任一负值或非有限都清空输出并保持 fail-closed。饥饿与疲惫数值都已按飞书删
	 * 除，不再输出 Hunger 或 Fatigue。
	 */
	bool TryGetInitialAttributes(float& OutPoison, float& OutFishingStrength, float& OutFightStamina) const;

	/** Character-owned ASC 正式运行总 gate；默认关闭，项目接线后可在所有构建配置显式启用。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bEnableCharacterAbilityRuntime = false;

	/** 当前 GameplayEffect 复制策略；Undecided 不会隐式升级为 Mixed。 */
	UPROPERTY(Config, EditAnywhere, Category = "Replication")
	ECatAbilityReplicationPolicy ReplicationPolicy = ECatAbilityReplicationPolicy::Undecided;

	/** 新 Character 初始属性总 gate；默认关闭，数值必须由项目配置显式提供且只应用一次。 */
	UPROPERTY(Config, EditAnywhere, Category = "Attributes")
	bool bEnableInitialAttributeTuning = false;

	/** 新 Character 初始 Poison；负值表示 Unset。 */
	UPROPERTY(Config, EditAnywhere, Category = "Attributes", meta = (ClampMin = "-1.0"))
	float InitialPoison = -1.0f;

	/** 新 Character 初始 FishingStrength；必须为正才能支持正式搏斗。 */
	UPROPERTY(Config, EditAnywhere, Category = "Attributes", meta = (ClampMin = "-1.0"))
	float InitialFishingStrength = -1.0f;

	/** 新 Character 初始 FightStamina；必须为正才能支持正式搏斗。 */
	UPROPERTY(Config, EditAnywhere, Category = "Attributes", meta = (ClampMin = "-1.0"))
	float InitialFightStamina = -1.0f;

	/** 开发诊断 Ability/Input 总 gate；Shipping 构建始终忽略它，正式身体链不依赖该开关。 */
	UPROPERTY(Config, EditAnywhere, Category = "Diagnostics")
	bool bEnableDiagnosticAbility = false;

	/**
	 * 诊断 Ability 对 Poison 的加法改变量；0 表示未配置，不属于正式身体公式。疲惫数值删除后诊断改写仍在生产目录里的
	 * Poison，让开发者能用它手动把猫推到倒地阈值。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Diagnostics")
	float DiagnosticPoisonDelta = 0.0f;

	/** 开发诊断 InputAction 软引用；正式玩法输入资产未接线时保持空。 */
	UPROPERTY(Config, EditAnywhere, Category = "Diagnostics", meta = (AllowedClasses = "/Script/EnhancedInput.InputAction"))
	TSoftObjectPtr<UInputAction> DiagnosticInputAction;

	/** 开发诊断 MappingContext 软引用；Character 只移除自己添加的实例。 */
	UPROPERTY(Config, EditAnywhere, Category = "Diagnostics", meta = (AllowedClasses = "/Script/EnhancedInput.InputMappingContext"))
	TSoftObjectPtr<UInputMappingContext> DiagnosticMappingContext;
};
