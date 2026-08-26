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
	/** 读取有限正抄网冷却；非法配置清零并返回 false，服务器据此 fail-closed。 */
	bool TryGetScoopCooldown(double& OutCooldownSeconds) const;
	/** 读取真咬前有限正预警时长；调度器保证预警完整播放后才允许进入真咬。 */
	bool TryGetBiteWarning(double& OutWarningSeconds) const;
	/** 读取有界操作位数量与左右间距；槽位 0 从右侧开始，后续左右交替向外扩展。 */
	bool TryGetRodOperatorLayout(int32& OutMaximumSlots, double& OutSlotSpacingCentimeters) const;

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

	/**
	 * 上钩鱼的高层行为拓扑；服务器在 FishEncounter 的 StateTreeComponent 上运行。
	 * 它只选择行为状态，固定步移动/鱼线/资源仍由 Runner 和 Simulator 权威结算。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	TSoftObjectPtr<UStateTree> FishBehaviorStateTree;

	/** 真咬响应窗口秒数；0 表示 Unset，资产 Task 不应启动计时。 */
	UPROPERTY(Config, EditAnywhere, Category = "Tuning", meta = (ClampMin = "0"))
	double TrueBiteWindowSeconds = 0.0;
	UPROPERTY(Config, EditAnywhere, Category="Bite", meta=(ClampMin="0")) double BaseBiteRatePerSecond = 0.0;
	/** 抛竿落水后、开始快速抖动前，浮漂至少保持慢浮的秒数。 */
	UPROPERTY(Config, EditAnywhere, Category="Bite", meta=(ClampMin="0", Units="s"))
	double MinimumBiteDelaySeconds = 0.0;
	/** 从落水到真咬下沉的总时间上限，必须容纳慢浮下限与完整预警。 */
	UPROPERTY(Config, EditAnywhere, Category="Bite", meta=(ClampMin="0", Units="s"))
	double MaximumBiteDelaySeconds = 0.0;
	/** 真咬前浮漂快速点动的服务器权威预警时长；当前产品口径为 3 秒。 */
	UPROPERTY(Config, EditAnywhere, Category="Bite", meta=(ClampMin="0", Units="s"))
	double BiteWarningSeconds = 1.5;

	/** Authority fight runner tuning. Defaults are usable without introducing persistent Config changes. */
	UPROPERTY(Config, EditAnywhere, Category="Fight", meta=(ClampMin="0.001")) double FixedFightStepSeconds = 0.05;
	/** 旧对称消耗模型的基础速率；规格判定表启用后不再参与计算，保留以兼容既有配置。 */
	UPROPERTY(Config, EditAnywhere, Category="Fight", meta=(ClampMin="0.001")) double BaseFightDrainPerSecond = 1.0;
	UPROPERTY(Config, EditAnywhere, Category="Fight", meta=(ClampMin="0.001")) double ReelSpeedCentimetersPerSecond = 80.0;
	/** 本步超线多少厘米视为满表现张力；只用于归一化/UI/Cable，不改变权威约束。 */
	UPROPERTY(Config, EditAnywhere, Category="Fight", meta=(ClampMin="0.1"))
	double TensionResponseRangeCentimeters = 10.0;
	/** 剩余鱼体力小于等于此值时统一吸附为 0 并进入侧翻收近；避免 UI 已显示 0、玩法仍残留小数体力。 */
	UPROPERTY(Config, EditAnywhere, Category="Fight", meta=(ClampMin="0", ClampMax="1"))
	double FishExhaustionThreshold = 0.5;
	UPROPERTY(Config, EditAnywhere, Category="Fight", meta=(ClampMin="0")) double EscapeSlackCentimeters = 100.0;
	/** 兼容旧 StateTree/调试资产的近岸线长；正式抢抄只使用射线几何，靠岸不再终止搏斗。 */
	UPROPERTY(Config, EditAnywhere, Category="Fight", meta=(ClampMin="0")) double NearShoreLineLengthCentimeters = 100.0;

	/** 一根部署鱼竿最多可占用的操作位；当前产品使用左右两位，数组/站位算法预留到更多协作者。 */
	UPROPERTY(Config, EditAnywhere, Category="Rod|Operators", meta=(ClampMin="1", ClampMax="8"))
	int32 MaximumRodOperatorSlots = 2;
	/** 左右第一对站位中心之间的距离；0 表示所有槽位暂时共用原 Stand 锚点。 */
	UPROPERTY(Config, EditAnywhere, Category="Rod|Operators", meta=(ClampMin="0", Units="cm"))
	double RodOperatorSlotSpacingCentimeters = 140.0;

	/**
	 * 打窝蓄力（规格 3.1 打窝：蓄力抛掷、抛物线预览）。服务器按按住时长算 ChargeAlpha，客户端预览用同一组参数（UCatFishingAimLibrary）。
	 * 抛出点 = 角色位置 + ThrowOriginOffset；方向 = 视角 Yaw + 仰角；初速 = Lerp(Min, Max, Alpha)。
	 */
	UPROPERTY(Config, EditAnywhere, Category="Chum|Throw", meta=(ClampMin="0.05")) double ChumChargeMaxSeconds = 1.5;
	UPROPERTY(Config, EditAnywhere, Category="Chum|Throw", meta=(ClampMin="1")) double ChumThrowMinSpeed = 600.0;
	UPROPERTY(Config, EditAnywhere, Category="Chum|Throw", meta=(ClampMin="1")) double ChumThrowMaxSpeed = 1400.0;
	UPROPERTY(Config, EditAnywhere, Category="Chum|Throw", meta=(ClampMin="0", ClampMax="80")) double ChumThrowElevationDegrees = 35.0;
	UPROPERTY(Config, EditAnywhere, Category="Chum|Throw", meta=(ClampMin="0.1")) double ChumThrowGravityScale = 1.0;
	UPROPERTY(Config, EditAnywhere, Category="Chum|Throw") FVector ChumThrowOriginOffset = FVector(40.0, 0.0, 60.0);
	/** 服务器每次按 Q 松开投放的份数。 */
	UPROPERTY(Config, EditAnywhere, Category="Chum|Throw", meta=(ClampMin="1")) int32 ChumThrowQuantity = 1;

	/** 规格 4.3/4.4 遛鱼判定系数；默认值即规格快照，数值拍定以「参数与校准记录」为准。 */
	UPROPERTY(Config, EditAnywhere, Category="Fight|Spec", meta=(ClampMin="0")) double InwardPullCatDrainPerFishStrength = 0.15;
	/** 向内游+拖时鱼的体力消耗系数（× 猫力/秒）；拖永远双方消耗，顺从/挣扎只是档位不同。 */
	UPROPERTY(Config, EditAnywhere, Category="Fight|Spec", meta=(ClampMin="0")) double InwardPullFishDrainPerCatStrength = 0.08;
	UPROPERTY(Config, EditAnywhere, Category="Fight|Spec", meta=(ClampMin="0")) double StalemateRodWearPerFishStrength = 0.1;
	UPROPERTY(Config, EditAnywhere, Category="Fight|Spec", meta=(ClampMin="0")) double StalemateFishDrainPerCatStrength = 0.08;
	UPROPERTY(Config, EditAnywhere, Category="Fight|Spec", meta=(ClampMin="0")) double StalemateCatDrainPerFishStrength = 0.12;
	UPROPERTY(Config, EditAnywhere, Category="Fight|Spec", meta=(ClampMin="0")) double SlackStaminaRegenPerSecond = 1.5;
	UPROPERTY(Config, EditAnywhere, Category="Fight|Spec", meta=(ClampMin="1")) double OverpowerStrengthRatio = 2.0;
	/** 鱼体力低于该比例后休息期乘以下面的倍率（规格 4.6 临时口径）。 */
	UPROPERTY(Config, EditAnywhere, Category="Fight|Spec", meta=(ClampMin="0", ClampMax="1")) double LowStaminaRestThreshold = 0.5;
	UPROPERTY(Config, EditAnywhere, Category="Fight|Spec", meta=(ClampMin="1")) double LowStaminaRestMultiplier = 1.5;

	/** NearShore 合法几何策略 gate；默认 false，未接真实岸线验证时不允许测试命令伪造捕获。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bEnableNearShoreValidation = false;

	/** 服务器权威近岸目标允许抢抄的最大距离，单位厘米；0 表示 Unset，不从客户端命中位置推导。 */
	UPROPERTY(Config, EditAnywhere, Category = "Tuning", meta = (ClampMin = "0"))
	double ScoopReachCentimeters = 0.0;
	/** 每次真实挥网尝试的冷却秒数；GAS 做预测表现，服务器命令层用同一个值做最终限流。 */
	UPROPERTY(Config, EditAnywhere, Category="Scoop", meta=(ClampMin="0", Units="s"))
	double ScoopCooldownSeconds = 3.0;
	UPROPERTY(Config, EditAnywhere, Category="Scoop", meta=(ClampMin="0.01"))
	double NearShoreWidthCentimeters = 300.0;
	UPROPERTY(Config, EditAnywhere, Category="Scoop", meta=(ClampMin="0", ClampMax="89"))
	double MaximumScoopGroundSlopeDegrees = 45.0;

	/**
	 * 抄手与鱼的最大垂直高度差（厘米）；抄网判定本身是纯水平的（俯视投影线段∩圆），
	 * 这一项是唯一的垂直约束，用来挡住"站在悬崖/高台上水平方向够得着、实际根本捞不到"的情况。
	 * 鱼的权威位置在水面，所以这个值大致等于"允许比水面高多少"。0 表示不限制高度差。
	 */
	UPROPERTY(Config, EditAnywhere, Category="Scoop", meta=(ClampMin="0", Units="cm"))
	double MaximumScoopVerticalDeltaCentimeters = 250.0;
	UPROPERTY(Config, EditAnywhere, Category="Scoop")
	TEnumAsByte<ECollisionChannel> ScoopTraceChannel = ECC_Visibility;

	/** Resolved/Terminated 快照发布后 Actor 继续复制的有界秒数；0 表示 Unset 并阻止新会话，避免泄漏或丢最后终态。 */
	UPROPERTY(Config, EditAnywhere, Category = "Tuning", meta = (ClampMin = "0"))
	double TerminalReplicationWindowSeconds = 0.0;

	UPROPERTY(Config, EditAnywhere, Category="Personality")
	TArray<TSoftObjectPtr<UCatBitePersonalityDefinition>> BitePersonalities;
	UPROPERTY(Config, EditAnywhere, Category="Personality")
	TArray<TSoftObjectPtr<UCatFightPersonalityDefinition>> FightPersonalities;
};
