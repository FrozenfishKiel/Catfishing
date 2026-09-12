#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "Social/CatSocialTypes.h"
#include "CatSocialSettings.generated.h"

/**
 * Social 的范围与信号配置；范围未裁时对应路径 fail-closed。
 *
 * 这里没有拿鱼的开关：拿鱼是客观的库存移动，只受距离、鱼护落地和防骚扰牌约束，不另设权限档（2026-09-11 拍）。
 *
 * 墓碑（2026-09-12）：原有 MischiefPermission（ECatDomainPolicy 三态）与 MischiefCooldownSeconds 两项，全部删除。
 * 前者是「恶作剧权限开关」这层概念的唯一代码载体，09-12 裁决③把这层概念整个去掉——它从 08-16 起唯一的载体
 * 就是防骚扰立牌，没有第二种形态，好友局／生人局的默认档位分档一并退役；立牌本身保留、待重新设计。
 * 后者是系统级频率上限，联机社交 §3.1.4 明写「不设系统级频率上限与时机限制（熟人自治）」，
 * 护栏只有立牌＋房主踢人两样。两项同批删，还顺手拆掉了一处耦合：旧 IsMischiefReady() 要求冷却 > 0，
 * 于是「把冷却配成 0」＝按设计取消频率上限，却会连带把放牌命令一起关死（放牌读的是同一个 gate）。
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Social"))
class CATFISHING_API UCatSocialSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 裁决普通恶作剧能否发起：只看总运行 gate 与权威交互范围。没有频率上限，也没有时机限制（熟人自治）。 */
	bool IsMischiefReady() const;

	/** 裁决放置防骚扰立牌能否受理：只看总运行 gate 与立牌自己的两个半径。它与恶作剧 gate 互不牵连。 */
	bool IsProtectionSignReady() const;

	/** 裁决附近求助信号能否发布；范围或冷却未调时返回 false，普通求助不会被提升成全局 Giant 提示。 */
	bool IsManualHelpReady() const;

	/** Social 总运行 gate；默认关闭。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bEnableSocialRuntime = false;

	/** 普通恶作剧时双方权威 Pawn 的最大距离，单位厘米；0 表示交互范围未裁。这是几何够不够得着，不是频率闸。 */
	UPROPERTY(Config, EditAnywhere, Category = "Mischief", meta = (ClampMin = "0.0"))
	double MischiefInteractionRangeCentimeters = 0.0;

	/** 防骚扰牌子的保护半径，单位厘米；0 表示范围未裁，放牌命令拒绝。牌子只挡恶作剧、不挡拿鱼（09-12 裁决③）。 */
	UPROPERTY(Config, EditAnywhere, Category = "Mischief", meta = (ClampMin = "0.0"))
	double ProtectionSignRadiusCentimeters = 0.0;

	/** 玩家从自身位置放牌的最大距离，单位厘米；0 表示交互边界未裁。 */
	UPROPERTY(Config, EditAnywhere, Category = "Mischief", meta = (ClampMin = "0.0"))
	double ProtectionSignPlacementRangeCentimeters = 0.0;

	/** 手动求助的附近感知半径，单位厘米；0 表示范围未裁。 */
	UPROPERTY(Config, EditAnywhere, Category = "Help", meta = (ClampMin = "0.0"))
	double ManualHelpRadiusCentimeters = 0.0;

	/** 同一玩家手动求助最小间隔秒数；0 表示冷却未裁。 */
	UPROPERTY(Config, EditAnywhere, Category = "Help", meta = (ClampMin = "0.0"))
	double ManualHelpCooldownSeconds = 0.0;
};
