#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatAbilitySettings.generated.h"

class UCatAbilityInputConfig;
class UCatAbilitySet;
class UCatCharacterDefinition;

/** Character-owned ASC 的已验证复制策略；Mixed 仍需 V3 证据，因此没有可选枚举值。 */
UENUM(BlueprintType)
enum class ECatAbilityReplicationPolicy : uint8
{
	/** 复制策略尚未明确启用，Character 保持组件存在但清除 ActorInfo。 */
	Undecided,
	/** 使用安全的 Full GameplayEffect 复制；双客户端证据形成后仍可继续验证是否需要 Mixed。 */
	Full
};

/** Character-owned ASC 的集中设置；正式运行 gate、初始身体数值和 Fishing Ability 资产必须一起 fail-closed。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Character Abilities"))
class CATFISHING_API UCatAbilitySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 判断 Character ASC 正式运行链是否显式启用；当前只接受已有安全语义的 Full 策略。 */
	bool IsRuntimeEnabled() const;

	/** 读取新 Character 的三项局内初始属性；任一非法值都会清空输出并保持 fail-closed。 */
	bool TryGetInitialAttributes(float& OutPoison, float& OutFishingStrength, float& OutFightStamina) const;

	/** 正式 Fishing GAS 资产必须同时存在且 InputConfig 完整，缺任一项都不授予或绑定输入。 */
	bool IsFishingRuntimeReady() const;

	/** 只读取已验证的正 FightStamina 基线，供 SetByCaller 初始化/恢复 GE 使用。 */
	bool TryGetInitialFightStamina(float& OutFightStamina) const;

	/** 同步解析猫种类清单并只接受唯一就绪匹配；重复或未就绪返回空，防止两端选到不同数值。 */
	const UCatCharacterDefinition* FindRuntimeCharacterDefinition(FName CatDefinitionId) const;

	/**
	 * 按猫种类读取三项初始属性：Id 为 None 时回退全局初值；
	 * Id 已指定但定义缺失/未就绪时 fail-closed 返回 false，不悄悄换成全局值。
	 */
	bool TryGetInitialAttributesForCharacter(FName CatDefinitionId, float& OutPoison,
		float& OutFishingStrength, float& OutFightStamina) const;

	/** 按猫种类读取搏斗体力基线；解析语义与上项一致，供 ASC 会话初始化与搏斗装配共用。 */
	bool TryGetFightStaminaBaselineForCharacter(FName CatDefinitionId, float& OutFightStamina) const;

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

	/** 正式猫种类清单；默认空且不扫描资产目录，Character 通过 CatDefinitionId 选择其中一项。 */
	UPROPERTY(Config, EditAnywhere, Category = "Characters")
	TArray<TSoftObjectPtr<UCatCharacterDefinition>> CharacterDefinitions;

	/** Character authority 的正式默认 AbilitySet；代码不创建或猜测资产路径。 */
	UPROPERTY(Config, EditAnywhere, Category = "Fishing", meta = (AllowedClasses = "/Script/Catfishing.CatAbilitySet"))
	TSoftObjectPtr<UCatAbilitySet> DefaultAbilitySet;

	/** PlayerController 的正式 Ability 输入映射；复用现有 /Game/Input Action。 */
	UPROPERTY(Config, EditAnywhere, Category = "Fishing", meta = (AllowedClasses = "/Script/Catfishing.CatAbilityInputConfig"))
	TSoftObjectPtr<UCatAbilityInputConfig> AbilityInputConfig;
};
