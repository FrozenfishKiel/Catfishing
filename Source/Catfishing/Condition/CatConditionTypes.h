#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "CatConditionTypes.generated.h"

/**
 * 倒地后的恢复方式；它描述服务器已接受的路径，不包含动画或数值公式。
 *
 * 墓碑（2026-09-12）：这里原有一项 Herb（自己或伙伴消耗草药）。草药机制 2026-08-13 已由设计删除
 * （猫册 v1.3「倒地解除改为救援与休息」），09-09「代码超前项逐个过」又明确裁「删代码一条——草药恢复链」。
 * 本次连同 UCatHerbRecoveryItemFragment、ServerUseHerbOnCharacter RPC、
 * HerbPoisonRelief/HerbUseRangeCentimeters 两行 ini 一起删除。倒地解除现在只有救援与休息两条路。
 */
UENUM(BlueprintType)
enum class ECatRecoveryMode : uint8
{
	/** 当前没有恢复动作。 */
	None,
	/** 单人可用的野外休息/爬行自救路径。 */
	FieldSelfRecovery,
	/** 固定营地的快速休息路径。 */
	CampRest,
	/** 伙伴搬运到固定营地救援点；到点即解除倒地。 */
	CarriedToCamp,
	/** 倒地满一段时间后自己站起来（猫册 §3.1.5，时长占位 60 秒）。 */
	SelfHealTimeout,
	/** 翻天时仍在倒地的被自动救起，清晨在营地醒来。 */
	DayBreakAutoRescue
};

UENUM(BlueprintType)
enum class ECatWaterExposureState : uint8
{
	Dry,
	Shallow,
	Dangerous
};

enum class ECatWaterExposureUpdate : uint8
{
	Unavailable,
	Unchanged,
	Changed,
	DangerousEntered
};

/**
 * 疲惫演出档位（猫册 §3.1.3）。疲惫数值制 2026-08-15 已废除：这一档**只驱动演出**——
 * 轻→打哈欠、中→走路拖沓尾巴耷拉、重→坐下就瞌睡点头。
 * 它不进任何数值公式、不影响搏斗体力、不带玩法惩罚；回营或原地趴一会儿只是把档位归零的猫味动作。
 */
UENUM(BlueprintType)
enum class ECatFatigueTier : uint8
{
	None,
	Light,
	Moderate,
	Heavy
};

/** Character 局内身体离散状态的复制读模型；搏斗数值只在 ASC AttributeSet，吃鱼成长只在 Growth。 */
USTRUCT(BlueprintType)
struct FCatConditionSnapshot
{
	GENERATED_BODY()

	/** 每次 Wet、Downed、疲惫档或恢复方式提交后递增。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;

	/** 毛发当前是否淋湿；它只驱动表现，不带移动、数值或钓鱼惩罚。 */
	UPROPERTY(BlueprintReadOnly)
	bool bWet = false;

	/** 水深阈值的唯一离散结果；危险只在服务器持续确认后进入。 */
	UPROPERTY(BlueprintReadOnly)
	ECatWaterExposureState WaterExposure = ECatWaterExposureState::Dry;

	/** 猫是否因吃下重毒鱼进入可恢复倒地；倒地来源全游戏唯一，项目不存在死亡终态。 */
	UPROPERTY(BlueprintReadOnly)
	bool bDowned = false;

	/** 最近一次服务器接受的恢复方式；无动作时为 None。 */
	UPROPERTY(BlueprintReadOnly)
	ECatRecoveryMode RecoveryMode = ECatRecoveryMode::None;

	/** 当前疲惫演出档；纯表现，ABP 与表现组件读它选动作，不参与任何数值裁决。 */
	UPROPERTY(BlueprintReadOnly)
	ECatFatigueTier FatigueTier = ECatFatigueTier::None;

	/**
	 * 周身臭气：吃下臭臭鱼后的「请勿靠近」（联机社交 §3.1.4、猫册子页「吃鱼效果」）。
	 * 它不是纯演出——臭着期间无法被恶作剧选中、无法被队友搬运回营（搬运算「帮助」，2026-08-21 裁定）。
	 * **扑倒反制不受它影响**（扑倒不算恶作剧，同日裁定），否则臭臭鱼就成了完美作案 buff。
	 */
	UPROPERTY(BlueprintReadOnly)
	bool bStench = false;

	/** 臭气结束的服务器世界时间（秒）；给表现做倒计时用，权威解除仍由服务器一次性计时器写 bStench。 */
	UPROPERTY(BlueprintReadOnly)
	double StenchEndsServerTimeSeconds = 0.0;
};
