#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatFishingSettings.generated.h"

class UStateTree;
class UCatBitePersonalityDefinition;
class UCatFightPersonalityDefinition;

/** Fishing 长流程与未裁数值的 fail-closed 配置；默认不启动会话且不制造响应窗口或公式。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Fishing"))
class CATFISHING_API UCatFishingSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 判断正式运行配置是否具备显式总 gate、StateTree 资产、正响应/终态复制窗口与近岸几何；任一未裁字段都返回 false。 */
	bool IsRuntimeReady() const;

	/** 读取服务器近岸目标到抢抄者的正 reach；未配置或 runtime gate 关闭时清零并返回 false。 */
	bool TryGetScoopReach(double& OutReachCentimeters) const;

	/** 读取终态快照的有界复制留存秒数；未裁或 runtime gate 关闭时清零并返回 false。 */
	bool TryGetTerminalReplicationWindow(double& OutWindowSeconds) const;
	const UCatBitePersonalityDefinition* FindBitePersonality(FName PersonalityId) const;
	const UCatFightPersonalityDefinition* FindFightPersonality(FName PersonalityId) const;

	/** 钓鱼正式运行总 gate；默认关闭，由产品配置显式开启，Shipping 不做隐式改写。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bEnableFishingRuntime = false;

	/** 唯一 ST_FishingSession 软引用；空时不回退 C++ FSM。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	TSoftObjectPtr<UStateTree> FishingSessionStateTree;

	/** 真咬响应窗口秒数；0 表示 Unset，资产 Task 不应启动计时。 */
	UPROPERTY(Config, EditAnywhere, Category = "Tuning", meta = (ClampMin = "0"))
	double TrueBiteWindowSeconds = 0.0;
	UPROPERTY(Config, EditAnywhere, Category="Bite", meta=(ClampMin="0")) double BaseBiteRatePerSecond = 0.0;
	UPROPERTY(Config, EditAnywhere, Category="Bite", meta=(ClampMin="0")) double MinimumBiteDelaySeconds = 0.0;
	UPROPERTY(Config, EditAnywhere, Category="Bite", meta=(ClampMin="0")) double MaximumBiteDelaySeconds = 0.0;

	/** NearShore 合法几何策略 gate；默认 false，未接真实岸线验证时不允许测试命令伪造捕获。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bEnableNearShoreValidation = false;

	/** 服务器权威近岸目标允许抢抄的最大距离，单位厘米；0 表示 Unset，不从客户端命中位置推导。 */
	UPROPERTY(Config, EditAnywhere, Category = "Tuning", meta = (ClampMin = "0"))
	double ScoopReachCentimeters = 0.0;

	/** Resolved/Terminated 快照发布后 Actor 继续复制的有界秒数；0 表示 Unset 并阻止新会话，避免泄漏或丢最后终态。 */
	UPROPERTY(Config, EditAnywhere, Category = "Tuning", meta = (ClampMin = "0"))
	double TerminalReplicationWindowSeconds = 0.0;

	UPROPERTY(Config, EditAnywhere, Category="Personality")
	TArray<TSoftObjectPtr<UCatBitePersonalityDefinition>> BitePersonalities;
	UPROPERTY(Config, EditAnywhere, Category="Personality")
	TArray<TSoftObjectPtr<UCatFightPersonalityDefinition>> FightPersonalities;
};
