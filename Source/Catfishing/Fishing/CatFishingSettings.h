#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatFishingSettings.generated.h"

class UStateTree;
class UCatBitePersonalityDefinition;
class UCatFishingFightBalanceDefinition;
class UCatFightPersonalityDefinition;

/** Fishing 长流程与未裁数值的 fail-closed 配置；默认不启动会话且不制造响应窗口或公式。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Fishing"))
class CATFISHING_API UCatFishingSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 判断正式运行配置是否具备显式总 gate、StateTree 资产、正响应/终态复制窗口与近岸几何；任一未裁字段都返回 false。 */
	bool IsRuntimeReady() const;

	/** 读取服务器抄网射线的正 reach；未配置或 runtime gate 关闭时清零并返回 false。 */
	bool TryGetScoopReach(double& OutReachCentimeters) const;
	/** 读取有限正抄网冷却；非法配置清零并返回 false，服务器据此 fail-closed。 */
	bool TryGetScoopCooldown(double& OutCooldownSeconds) const;
	/** 读取真咬前有限正预警时长；调度器保证预警完整播放后才允许进入真咬。 */
	bool TryGetBiteWarning(double& OutWarningSeconds) const;
	/** 读取有界操作位数量与左右间距；槽位 0 从右侧开始，后续左右交替向外扩展。 */
	bool TryGetRodOperatorLayout(int32& OutMaximumSlots, double& OutSlotSpacingCentimeters) const;

	/** 读取终态快照的有界复制留存秒数；未裁或 runtime gate 关闭时清零并返回 false。 */
	bool TryGetTerminalReplicationWindow(double& OutWindowSeconds) const;
	/** 同步加载唯一正式搏斗平衡资产；缺失、关闭或字段非法时返回空，不回退到 C++/ini 第二套数值。 */
	const UCatFishingFightBalanceDefinition* LoadFightBalanceDefinition() const;
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

	/** 全局力量、运动、体力和鱼线裁决的唯一策划调参入口。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime", meta = (DisplayName = "搏斗平衡数据资产"))
	TSoftObjectPtr<UCatFishingFightBalanceDefinition> FightBalanceDefinition;

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

	/** 服务器权威固定模拟步长，属于运行时技术配置，不进入策划平衡资产。 */
	UPROPERTY(Config, EditAnywhere, Category="Fight", meta=(ClampMin="0.001")) double FixedFightStepSeconds = 0.05;

	/** 一根部署鱼竿最多可占用的操作位；当前产品使用左右两位，数组/站位算法预留到更多协作者。 */
	UPROPERTY(Config, EditAnywhere, Category="Rod|Operators", meta=(ClampMin="1", ClampMax="8"))
	int32 MaximumRodOperatorSlots = 2;
	/** 左右第一对站位中心之间的距离；0 表示所有槽位暂时共用原 Stand 锚点。 */
	UPROPERTY(Config, EditAnywhere, Category="Rod|Operators", meta=(ClampMin="0", Units="cm"))
	double RodOperatorSlotSpacingCentimeters = 140.0;

	/** 手持鱼竿的服务器规范握把偏移：X=角色前方、Y=角色右侧、Z=角色中心向上。 */
	UPROPERTY(Config, EditAnywhere, Category="Rod|HeldPose", meta=(Units="cm"))
	FVector HeldRodGripOffsetCentimeters = FVector(35.0, 24.0, 24.0);
	/** 服务器只接受该范围内的控制器 Pitch 来驱动鱼竿，避免异常视角翻转权威竿尖。 */
	UPROPERTY(Config, EditAnywhere, Category="Rod|HeldPose", meta=(ClampMin="-89", ClampMax="89", Units="deg"))
	double HeldRodMinimumPitchDegrees = -35.0;
	UPROPERTY(Config, EditAnywhere, Category="Rod|HeldPose", meta=(ClampMin="-89", ClampMax="89", Units="deg"))
	double HeldRodMaximumPitchDegrees = 70.0;
	/** 搏斗中无负载时实际鱼竿追随玩家瞄准意图的最大角速度；鱼线转矩在此基础上连续降速。 */
	UPROPERTY(Config, EditAnywhere, Category="Fight|HeldRod", meta=(ClampMin="1", Units="deg/s"))
	double HeldRodMaximumAngularSpeedDegreesPerSecond = 360.0;
	/** 旋转速度倍率追随20Hz转矩结果的平滑时间；满负载最终仍会收敛到完全停转。 */
	UPROPERTY(Config, EditAnywhere, Category="Fight|HeldRod", meta=(ClampMin="0.01", Units="s"))
	double HeldRodAngularResistanceResponseSeconds = 0.08;

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
