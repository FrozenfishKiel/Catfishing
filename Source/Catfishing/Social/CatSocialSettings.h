#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "Social/CatSocialTypes.h"
#include "CatSocialSettings.generated.h"

/** Social 的权限、窗口、范围与信号配置；除恶作剧密度按需求不设系统上限外，其余未裁项都让对应路径 fail-closed。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Social"))
class CATFISHING_API UCatSocialSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/**
	 * 裁决偷鱼入口、Timer 与追回空间验证的数值参数是否齐全；窗口或任一距离未配置都返回 false，避免半协议。这里刻意不
	 * 看偷取权限：权限已经升级为局主运行期可改的策略，由 UCatSocialService 持有，Settings 只提供它的开局默认值。
	 */
	bool AreTheftParametersReady() const;

	/**
	 * 裁决普通恶作剧的权威交互范围是否齐全；飞书已拍定恶作剧不设系统级频率上限，因此这里不含任何冷却判据。与偷鱼同
	 * 理，恶作剧权限本身由运行期策略裁决，不在这里判断。
	 */
	bool AreMischiefParametersReady() const;

	/**
	 * 裁决偷来的鱼能否被卖掉。飞书把"奔店路程"算进追回窗口，所以售出必须有真实的商店位置作为空间前提；商店锚点标签或
	 * 距离任一未登记都返回 false，售出终态直接不可达。
	 */
	bool IsTheftSaleReady() const;

	/**
	 * 裁决偷来的鱼能否被吃掉。飞书把"找地方进食"算进追回窗口，所以进食必须有"已经跑开受害者"的距离前提；
	 * 距离未登记时返回 false，窗口到期只会把鱼原样还回去。
	 */
	bool IsTheftConsumptionReady() const;

	/** 裁决防骚扰牌能否放置；牌子是同时挡恶作剧和偷窃的护栏，只看自身半径与放置距离，不随恶作剧权限一起关闭。 */
	bool IsProtectionSignReady() const;

	/** 裁决附近求助信号能否发布；范围或冷却未调时返回 false，普通求助不会被提升成全局 Giant 提示。 */
	bool IsManualHelpReady() const;

	/** Social 总运行 gate；默认关闭。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bEnableSocialRuntime = false;

	/**
	 * 偷鱼权限的开局默认值；UCatSocialService 初始化时把它抄进运行期策略，之后局主可以随时改，Settings 不再参与裁决。
	 * Unset 时开局即拒绝所有偷鱼，直到局主显式打开。
	 */
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

	/**
	 * 关卡里标记商店位置的 Actor Tag；Social 只按这个标签在世界里找商店锚点，不自己生成商店也不猜测坐标。None 表示锚
	 * 点尚未在关卡登记，偷鱼售出因此不可达。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Theft")
	FName TheftSaleShopAnchorTag = NAME_None;

	/** 售出偷来的鱼时小偷到商店锚点的最大距离，单位厘米；0 表示这个距离尚未裁决，售出终态 fail-closed。 */
	UPROPERTY(Config, EditAnywhere, Category = "Theft", meta = (ClampMin = "0.0"))
	double TheftSaleShopRangeCentimeters = 0.0;

	/** 窗口到期吃掉偷来的鱼时，小偷必须已经离开受害者的最小距离，单位厘米；0 表示这个距离尚未裁决，进食终态 fail-closed。 */
	UPROPERTY(Config, EditAnywhere, Category = "Theft", meta = (ClampMin = "0.0"))
	double TheftConsumeVictimEscapeDistanceCentimeters = 0.0;

	/** 共享鱼缸的追回权限；Undecided 时共享缸偷鱼拒绝，个人鱼护仍按原主人。 */
	UPROPERTY(Config, EditAnywhere, Category = "Theft")
	ECatSharedTankRecoveryPolicy SharedTankRecoveryPolicy = ECatSharedTankRecoveryPolicy::Undecided;

	/** 偷鱼被抓的正式印记事件 ID；None 时不生成占位候选。 */
	UPROPERTY(Config, EditAnywhere, Category = "Theft")
	FName TheftCaughtImprintEventId = NAME_None;

	/**
	 * 普通恶作剧权限的开局默认值；与偷鱼一样，服务初始化后由局主运行期策略接管，Settings 不再参与裁决。它只决定恶作剧
	 * 本身能否发起，不决定防骚扰牌能否放置或是否挡偷窃。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Mischief")
	ECatDomainPolicy MischiefPermission = ECatDomainPolicy::Unset;

	/** 普通恶作剧时双方权威 Pawn 的最大距离，单位厘米；0 表示交互范围未裁。 */
	UPROPERTY(Config, EditAnywhere, Category = "Mischief", meta = (ClampMin = "0.0"))
	double MischiefInteractionRangeCentimeters = 0.0;

	/** 防骚扰牌子的保护半径，单位厘米；牌子挡恶作剧也挡偷窃，0 表示范围未裁，放牌命令拒绝。 */
	UPROPERTY(Config, EditAnywhere, Category = "Mischief", meta = (ClampMin = "0.0"))
	double ProtectionSignRadiusCentimeters = 0.0;

	/** 玩家从自身位置放牌的最大距离，单位厘米；0 表示交互边界未裁，放牌命令拒绝。 */
	UPROPERTY(Config, EditAnywhere, Category = "Mischief", meta = (ClampMin = "0.0"))
	double ProtectionSignPlacementRangeCentimeters = 0.0;

	/** 手动求助的附近感知半径，单位厘米；0 表示范围未裁。 */
	UPROPERTY(Config, EditAnywhere, Category = "Help", meta = (ClampMin = "0.0"))
	double ManualHelpRadiusCentimeters = 0.0;

	/** 同一玩家手动求助最小间隔秒数；0 表示冷却未裁。 */
	UPROPERTY(Config, EditAnywhere, Category = "Help", meta = (ClampMin = "0.0"))
	double ManualHelpCooldownSeconds = 0.0;
};
