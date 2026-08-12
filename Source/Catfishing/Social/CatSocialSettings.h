#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "Social/CatSocialTypes.h"
#include "CatSocialSettings.generated.h"

/** Social 的权限、窗口与信号配置；频率/范围未裁时对应路径全部 fail-closed。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Social"))
class CATFISHING_API UCatSocialSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 裁决偷鱼入口、Timer 与追回空间验证能否共同运行；任一权限、窗口或距离未配置都返回 false，避免半协议。 */
	bool IsTheftReady() const;

	/** 裁决普通恶作剧是否具备权限、冷却和权威交互范围；缺项时所有请求保持 fail-closed。 */
	bool IsMischiefReady() const;

	/** 裁决附近求助信号能否发布；范围或冷却未调时返回 false，普通求助不会被提升成全局 Giant 提示。 */
	bool IsManualHelpReady() const;

	/** Social 总运行 gate；默认关闭。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bEnableSocialRuntime = false;

	/** 房主裁决的偷鱼权限；Unset 时所有偷鱼命令拒绝。 */
	UPROPERTY(Config, EditAnywhere, Category = "Theft")
	ECatDomainPolicy TheftPermission = ECatDomainPolicy::Unset;

	/** 偷鱼到吃完的唯一追回窗口秒数；0 表示未调。 */
	UPROPERTY(Config, EditAnywhere, Category = "Theft", meta = (ClampMin = "0.0"))
	double TheftEatingWindowSeconds = 0.0;

	/** 发起偷鱼时角色到真实容器宿主的最大距离，单位厘米；0 表示权威交互边界未裁。 */
	UPROPERTY(Config, EditAnywhere, Category = "Theft", meta = (ClampMin = "0.0"))
	double TheftInteractionRangeCentimeters = 0.0;

	/** 追回时合法捕手到当前小偷角色的最大距离，单位厘米；0 表示追逐命中边界未裁。 */
	UPROPERTY(Config, EditAnywhere, Category = "Theft", meta = (ClampMin = "0.0"))
	double TheftCatchRangeCentimeters = 0.0;

	/** 共享鱼缸的追回权限；Undecided 时共享缸偷鱼拒绝，个人鱼护仍按原主人。 */
	UPROPERTY(Config, EditAnywhere, Category = "Theft")
	ECatSharedTankRecoveryPolicy SharedTankRecoveryPolicy = ECatSharedTankRecoveryPolicy::Undecided;

	/** 偷鱼被抓的正式印记事件 ID；None 时不生成占位候选。 */
	UPROPERTY(Config, EditAnywhere, Category = "Theft")
	FName TheftCaughtImprintEventId = NAME_None;

	/** 普通恶作剧权限；ProtectionSign 只在 Enabled 时继续裁决。 */
	UPROPERTY(Config, EditAnywhere, Category = "Mischief")
	ECatDomainPolicy MischiefPermission = ECatDomainPolicy::Unset;

	/** 同一玩家普通恶作剧最小间隔秒数；0 表示频率上限未裁。 */
	UPROPERTY(Config, EditAnywhere, Category = "Mischief", meta = (ClampMin = "0.0"))
	double MischiefCooldownSeconds = 0.0;

	/** 普通恶作剧时双方权威 Pawn 的最大距离，单位厘米；0 表示交互范围未裁。 */
	UPROPERTY(Config, EditAnywhere, Category = "Mischief", meta = (ClampMin = "0.0"))
	double MischiefInteractionRangeCentimeters = 0.0;

	/** 防骚扰牌子的保护半径，单位厘米；0 表示范围未裁，放牌命令拒绝。 */
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
