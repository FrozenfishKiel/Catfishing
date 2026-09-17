#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatFishingSettings.generated.h"

class UStateTree;
class UCatBitePersonalityDefinition;
class UCatFishingFightBalanceDefinition;
class UCatFightPersonalityDefinition;
class UCatFishDefinition;
struct FCatFishingBiteTimingParameters;
struct FCatFishResolvedBehavior;

/** Fishing 长流程与未裁数值的 fail-closed 配置；默认不启动会话且不制造响应窗口或公式。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Fishing"))
class CATFISHING_API UCatFishingSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 超时脱手的水平/向上初速度，cm/s；架设竿两项均不使用。 */
	UPROPERTY(Config, EditAnywhere, Category="Tuning|RodEscape", meta=(ClampMin="0.01", Units="cm/s"))
	double RodEscapeThrowSpeed = 120.0;
	UPROPERTY(Config, EditAnywhere, Category="Tuning|RodEscape", meta=(ClampMin="0.01", Units="cm/s"))
	double RodEscapeThrowUpSpeed = 100.0;
	UPROPERTY(Config, EditAnywhere, Category="Tuning|RodEscape", meta=(ClampMin="1", Units="cm/s"))
	double RodEscapeDragSpeed = 33.0;
	UPROPERTY(Config, EditAnywhere, Category="Tuning|RodEscape", meta=(ClampMin="1"))
	double RodEscapeDragAcceleration = 66.0; // cm/s²，独立于鱼力和竿质量。
	UPROPERTY(Config, EditAnywhere, Category="Tuning|RodEscape", meta=(ClampMin="0.1", Units="s"))
	double RodEscapeDragSeconds = 2.5;
	/** 从触发位置累计水平路径，包含短抛；停止后不再持续拖行。 */
	UPROPERTY(Config, EditAnywhere, Category="Tuning|RodEscape", meta=(ClampMin="1", Units="cm"))
	double RodEscapeMaximumTravel = 220.0;

	/** 翻肚到苏醒逃跑的秒数；拖动期间继续计时，已上岸不苏醒。 */
	UPROPERTY(Config, EditAnywhere, Category="Tuning|Terminal", meta=(ClampMin="0.01", Units="s"))
	double ExhaustedFishRevivalSeconds = 30.0;
	/** 渔获结束额外扣除的竿磨损点；与搏斗累计磨损独立结算一次。 */
	UPROPERTY(Config, EditAnywhere, Category="Tuning|Terminal", meta=(ClampMin="0"))
	double CatchCompletionRodWearPoints = 1.0;
	/** 每名玩家可部署的鱼竿数量；默认保持两根，不依赖鱼竿资产新增字段。 */
	UPROPERTY(Config, EditAnywhere, Category="Tuning|Rod", meta=(ClampMin="1"))
	int32 MaximumDeployedRodsPerPlayer = 2;

	// 以下读取保留旧配置的可用默认值；非法新值只记一次 Warning，不让整条钓鱼链因迁移而关闭。
	double GetExhaustedFishRevivalSeconds() const;
	double GetCatchCompletionRodWearPoints() const;
	int32 GetMaximumDeployedRodsPerPlayer() const;
	/** 判断正式运行配置是否具备显式总 gate、StateTree 资产、正响应/终态复制窗口与近岸几何；任一未裁字段都返回 false。 */
	bool IsRuntimeReady() const;

	/** 读取服务器抄网射线的正 reach；未配置或 runtime gate 关闭时清零并返回 false。 */
	bool TryGetScoopReach(double& OutReachCentimeters) const;
	/** 读取有限正抄网冷却；非法配置清零并返回 false，服务器据此 fail-closed。 */
	bool TryGetScoopCooldown(double& OutCooldownSeconds) const;
	/** 读取真咬前有限正预警时长；预警不延长等待，高浓度短间隔允许缩短预警。 */
	bool TryGetBiteWarning(double& OutWarningSeconds) const;
	/** 读取浓度等待公式参数，秒数从落水计到试探开始；非法配置拒绝。 */
	bool TryGetBiteTimingParameters(FCatFishingBiteTimingParameters& OutParameters) const;

	/** 读取终态快照的有界复制留存秒数；未裁或 runtime gate 关闭时清零并返回 false。 */
	bool TryGetTerminalReplicationWindow(double& OutWindowSeconds) const;
	/** 同步加载唯一正式搏斗平衡资产；缺失、关闭或字段非法时返回空，不回退到 C++/ini 第二套数值。 */
	const UCatFishingFightBalanceDefinition* LoadFightBalanceDefinition() const;
	// T10：旧 Bite 查询已删除，三窗口由鱼定义/基础完美窗解析。
	const UCatFightPersonalityDefinition* FindFightPersonality(FName PersonalityId) const;

	/**
	 * 解析一条鱼进搏斗时真正生效的行为参数（食性／发力段长／休息段长／游速系数）。
	 *
	 * 正式来源是鱼表格那四列（2026-09-09 晚裁「四套性格模板是测试用，正式口径逐鱼配」）；
	 * 某列还没填时退回该鱼 FightPersonalityId 指向的测试模板。搏斗启动侧只该调这一个入口，
	 * 不要再直接读 UCatFightPersonalityDefinition::AdaptiveSteeringConfig 或
	 * FullEffortMovementSpeedCentimetersPerSecond —— 那样会绕过鱼表、让四列白填。
	 * 返回 false 表示连模板都不足以凑出一份可用参数，调用方按依赖缺失 fail-closed。
	 */
	bool TryResolveFishBehavior(const UCatFishDefinition& FishDefinition,
		FCatFishResolvedBehavior& OutBehavior) const;

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

	/** 全局力量、运动、体力和鱼线裁决的唯一策划调参入口。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime", meta = (DisplayName = "搏斗平衡数据资产"))
	TSoftObjectPtr<UCatFishingFightBalanceDefinition> FightBalanceDefinition;

	/** 仅逐鱼与鱼目录档位默认均缺配时使用的旧值；带 Warning，正式目录不应走到。0 则拒绝。 */
	UPROPERTY(Config, EditAnywhere, Category = "Tuning", meta = (ClampMin = "0"))
	double TrueBiteWindowSeconds = 0.0;

	/**
	 * 咬钩信号「全场可闻」的门槛：本竿鱼漂的 BiteSignalStability 达到它，真咬那一刻就走一次不受距离衰减的全场广播。
	 *
	 * 为什么用阈值而不是给鱼漂加一个新 bool：鱼漂表里只有铃铛漂带「咬钩铃响、全场可闻」这条特效，
	 * 而三款漂已有的差异字段就是信号稳定度。新加一个必填 bool 而资产没人配过，这条玩法会静默死掉——
	 * 本分支已经为同样的错踩过四次。用阈值则不需要动任何资产，铃铛漂只要是稳定度最高的那一款就自然成立。
	 *
	 * > 1.0 表示这条玩法关闭（稳定度本身被夹在 0~1）。真咬时无论过不过门槛都会记一行
	 * fishing_bite_signal 日志，带上本竿鱼漂、稳定度和阈值，一局就能核出铃铛漂的落值对不对得上。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Tuning", meta = (ClampMin = "0.0"))
	double WorldwideBiteSignalStabilityThreshold = 1.0;
	/** 鱼表试探期及鱼目录档位默认均未填（0）时使用的随机兜底区间；不参与普通或完美响应窗。 */
	UPROPERTY(Config, EditAnywhere, Category = "Tuning", meta = (Units = "s"))
	FVector2D ProbeDurationRangeSeconds = FVector2D(2.0, 4.0);
    /** 空窝间隔；有窝间隔＝基准 / (1 + 三轴有效浓度 / K)。均为落水到试探开始。 */
    UPROPERTY(Config, EditAnywhere, Category="Bite|Concentration", meta=(ClampMin="0.001", Units="s"))
    double UnchummedBiteIntervalSeconds = 120.0;
    UPROPERTY(Config, EditAnywhere, Category="Bite|Concentration", meta=(ClampMin="0.001", Units="s"))
    double ChummedBiteBaseIntervalSeconds = 15.0;
    /** K 为浓度单位，不是条数；暂按五份新窝中心 A=8.5 对应 6 秒标定。 */
    UPROPERTY(Config, EditAnywhere, Category="Bite|Concentration", meta=(ClampMin="0.001"))
    double BiteConcentrationScale = 17.0 / 3.0;
    // 仅保留旧资产字段身份，运行时不读取。待完成 Blueprint 属性绑定迁移后删除。
	UPROPERTY(meta=(DeprecatedProperty, DeprecationMessage="Use UnchummedBiteIntervalSeconds / ChummedBiteBaseIntervalSeconds / BiteConcentrationScale"))
	double NoChumMeanBiteDelaySeconds = 0.0;
	UPROPERTY(meta=(DeprecatedProperty, DeprecationMessage="Use UnchummedBiteIntervalSeconds / ChummedBiteBaseIntervalSeconds / BiteConcentrationScale"))
	double SingleChumMeanBiteDelaySeconds = 0.0;
	UPROPERTY(meta=(DeprecatedProperty, DeprecationMessage="Use UnchummedBiteIntervalSeconds / ChummedBiteBaseIntervalSeconds / BiteConcentrationScale"))
	double FullChumMeanBiteDelaySeconds = 0.0;
	UPROPERTY(meta=(DeprecatedProperty, DeprecationMessage="Retired mean-anchor timing"))
	double SingleChumContribution = 0.0;
	UPROPERTY(meta=(DeprecatedProperty, DeprecationMessage="Retired mean-anchor timing"))
	double FullChumContribution = 0.0;
	UPROPERTY(meta=(DeprecatedProperty, DeprecationMessage="Retired timing floor/cap"))
	double MinimumBiteDelaySeconds = 0.0;
	UPROPERTY(meta=(DeprecatedProperty, DeprecationMessage="Retired timing floor/cap"))
	double MaximumBiteDelaySeconds = 0.0;
	/** 进入 Probe 前的浮漂点动预警；Probe 继续轻点逐鱼秒数，随后真咬下沉。 */
	UPROPERTY(Config, EditAnywhere, Category="Bite", meta=(ClampMin="0", Units="s"))
	double BiteWarningSeconds = 1.5;

	/** 服务器权威固定模拟步长，属于运行时技术配置，不进入策划平衡资产。 */
	UPROPERTY(Config, EditAnywhere, Category="Fight", meta=(ClampMin="0.001")) double FixedFightStepSeconds = 0.05;

	/** 手持鱼竿的服务器规范握把偏移：X=角色前方、Y=角色右侧、Z=角色中心向上。 */
	UPROPERTY(Config, EditAnywhere, Category="Rod|HeldPose", meta=(Units="cm"))
	FVector HeldRodGripOffsetCentimeters = FVector(35.0, 24.0, 24.0);
	/** 服务器只接受该范围内的控制器 Pitch 来驱动鱼竿，避免异常视角翻转权威竿尖。 */
	UPROPERTY(Config, EditAnywhere, Category="Rod|HeldPose", meta=(ClampMin="-89", ClampMax="89", Units="deg"))
	double HeldRodMinimumPitchDegrees = -35.0;
	UPROPERTY(Config, EditAnywhere, Category="Rod|HeldPose", meta=(ClampMin="-89", ClampMax="89", Units="deg"))
	double HeldRodMaximumPitchDegrees = 70.0;
	/** 实际鱼竿的全局角速度上限；净转矩先改变角速度，受载时允许连续减速。 */
	UPROPERTY(Config, EditAnywhere, Category="Fight|HeldRod", meta=(ClampMin="1", Units="deg/s"))
	double HeldRodMaximumAngularSpeedDegreesPerSecond = 360.0;
	/** 猫端瞄准转矩的响应尺度；与最大转速的乘积为达到满力所需的目标偏差。 */
	UPROPERTY(Config, EditAnywhere, Category="Fight|HeldRod", meta=(ClampMin="0.01", Units="s"))
	double HeldRodAngularResistanceResponseSeconds = 0.08;
	/** 杆和握杆动作的等效转动惯性时间；保存角速度，卸载或恢复力量时连续加减速。 */
	UPROPERTY(Config, EditAnywhere, Category="Fight|HeldRod", meta=(ClampMin="0.01", Units="s"))
	double HeldRodAngularInertiaSeconds = 0.08;
	/** 鱼游向/松绷线改变时，有向负载的指数插值时间常数；越大越柔和，不改变稳态平衡角。 */
	UPROPERTY(Config, EditAnywhere, Category="Fight|HeldRod", meta=(ClampMin="0.01", Units="s"))
	double HeldRodFishPullSmoothingSeconds = 0.15;
	/** 鱼负载下追加的粘性阻尼倍率；3 对应满载倍率4，实际阻尼还受空载临界阻尼下限约束。 */
	UPROPERTY(Config, EditAnywhere, Category="Fight|HeldRod", meta=(ClampMin="0"))
	double HeldRodLoadedAngularDampingRatio = 3.0;

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

	/** NearShore 合法几何策略 gate；默认 false，未接真实岸线验证时不允许测试命令伪造捕获。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bEnableNearShoreValidation = false;

	/** 服务器权威近岸目标允许抢抄的最大距离，单位厘米；0 表示 Unset，不从客户端命中位置推导。 */
	UPROPERTY(Config, EditAnywhere, Category = "Tuning", meta = (ClampMin = "0"))
	double ScoopReachCentimeters = 0.0;
	// 墓碑（2026-09-14）：HandoffMinimumStaminaFraction 已删除；
	// Knowledge/Design/设计修改记录.md 2026-09-13 裁决④，双方同意的握手不设体力准入。

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
