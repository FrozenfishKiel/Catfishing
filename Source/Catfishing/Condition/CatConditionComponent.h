#pragma once

#include "CoreMinimal.h"
#include "Condition/CatConditionTypes.h"
#include "Environment/CatWaterTypes.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "CatConditionComponent.generated.h"

class AController;
class UCatAbilitySystemComponent;
class UCatFishDefinition;

/** Condition 完整快照发生提交或复制变化的本机通知；订阅者必须重新读取 GetSnapshot，不使用增量载荷拼状态。 */
DECLARE_MULTICAST_DELEGATE(FCatConditionSnapshotChanged);

/**
 * Character 局内离散身体状态组件；ASC 拥有数值，本组件只裁决 Wet/Downed/疲惫档/恢复生命周期。
 *
 * 倒地口径（猫册 §3.1.5，09-12 收口）：来源唯一＝吃下重毒鱼，按鱼各配、无渐进升级；
 * 解除有四条——队友搬运回营地、休息（营地或野外）、倒地满一段时间自愈、翻天自动救起。
 * 倒地不是「全面交互禁用」：本册只写了两条禁令（钓鱼中倒地要中断钓鱼、倒地不能自己把鱼叼上祭坛），
 * 其余禁用属工程自造、尚未入册，不要在这里继续加宽。
 */
UCLASS(ClassGroup = (Catfishing), meta = (BlueprintSpawnableComponent))
class CATFISHING_API UCatConditionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 开启组件复制并关闭 Tick；状态由显式 authority 命令推进，自愈另用一次性计时器。 */
	UCatConditionComponent();

	/** 注册唯一 Snapshot 复制；终态缓存和 Controller/身份不会进入网络。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Owner 结束时收掉自愈计时器；组件销毁后不得再有定时回调改身体状态。 */
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

	/** 提供身体条件的服务器最终值或客户端复制值；外部只据此判断交互资格，不能借返回值改写 Wet/Downed。 */
	const FCatConditionSnapshot& GetSnapshot() const;

	/** 落水、天气等非技能反馈在 authority 设置纯表现 Wet；重复相同值不增加 Revision，也不修改任何 Attribute。 */
	void SetWetFromAuthority(bool bNewWet);

	/** 由钓鱼固定步提交采样时长；组件自行查询脚点水深并维护滞回/确认。 */
	ECatWaterExposureUpdate UpdateWaterExposureFromAuthority(const FCatWaterRegionHandle& WaterRegion,
		double DeltaSeconds, double& OutImmersionDepthCentimeters);

	/** 纯演出的疲惫档写口；只改快照供表现层读取，不写任何数值、不触发玩法后果。 */
	void SetFatigueTierFromAuthority(ECatFatigueTier NewTier);

	/** 在实物鱼被不可逆移除前只读校验食用定义、本条鱼的实际重量、ASC 与运行 gate；返回 None 才允许上层提交库存事务。 */
	ECatDomainCommandError ValidateFishConsumption(const UCatFishDefinition* FishDefinition,
		double WeightKilograms) const;

	/**
	 * 实物鱼消费提交后读取 FishDefinition 食用字段：按鱼种结论直接裁决倒地（最重一档＝吃下即倒地）、
	 * 授予该鱼配置的黄色体力护盾、推进成长经验。跨鱼累加与阈值模型已删除，不存在「连吃两条轻毒鱼倒地」。
	 */
	FCatDomainCommandResult ConsumeCommittedFish(FGuid RequestId, const UCatFishDefinition* FishDefinition,
		double WeightKilograms);

	/**
	 * 重毒鱼的身体后果入口：吃下即倒地。ConsumeCommittedFish 确认这条鱼是最重一档之后调用它，
	 * 它是全项目唯一把猫打倒的入口（倒地来源全游戏唯一＝吃到重毒鱼）。已经倒地时返回 false。
	 */
	bool ApplySevereToxicityFromAuthority();

	/**
	 * 当前是否正周身臭气（臭臭鱼的「请勿靠近」）。社交权限谓词与搬运救援都读它。
	 * 它只挡「被恶作剧选中」和「被搬运」两件事；扑倒反制不读它（扑倒不算恶作剧，2026-08-21 裁定）。
	 */
	bool IsStench() const { return Snapshot.bStench; }

	/**
	 * 吃下发臭的鱼之后开启 90 秒臭气（时长与名册都在 CatConditionSettings）。
	 * 重复吃只把结束时间整体后移，不叠层——设计只写了一个持续时长，没有强度或层数。
	 */
	bool ApplyStenchFromAuthority(double DurationSeconds);

	/** 单人可用的野外休息入口；不要求其他玩家在场，成功即解除倒地。 */
	FCatDomainCommandResult RequestFieldSelfRecovery(AController* RequestingController, FGuid RequestId);

	/** 固定营地休息入口；只允许 Camp actor 传入已到达事实，成功即解除倒地。 */
	FCatDomainCommandResult RequestCampRest(AController* RequestingController, FGuid RequestId, bool bAtCamp);

	/**
	 * 搬运完成入口；要求真实救援者、目标仍倒地且服务器已把 Character 放到固定营地救援点。到点即解除倒地。
	 * 正臭着的猫搬不动：搬运算「帮助」，被臭气屏蔽（2026-08-21 裁定，段子本身是有意保留的）——
	 * 他只能自己爬、等自愈，或者等翻天自动救起。
	 */
	FCatDomainCommandResult CompleteCarryToCamp(AController* HelpingController, FGuid RequestId, bool bAtCampRescuePoint);

	/**
	 * 翻天自动救起入口；由 GameMode 在进入新一天时对仍在倒地的猫调用（服务器已先把它传送回营地）。
	 * 它不要求救援者，也不走幂等缓存——翻天每天最多发生一次，重复调用对已站起来的猫是空操作。
	 */
	bool CompleteDayBreakRescueFromAuthority();

	/** 本机完整快照变化通知；LocalPlayer UI 成对订阅，领域写入者不依赖该通知推进。 */
	FCatConditionSnapshotChanged OnSnapshotChanged;

private:
	/** 客户端收到完整 Snapshot 后只供表现读取；不会在 RepNotify 改 Attribute 或发送恢复命令。 */
	UFUNCTION()
	void OnRep_Snapshot();

	/** 统一恢复路径：验证 authority 与运行 gate 后解除倒地并记录恢复方式；同 RequestId 只提交一次。 */
	FCatDomainCommandResult ApplyRecovery(FGuid RequestId, ECatRecoveryMode Mode);

	/** 写入倒地/起身事实：首次倒地释放钓鱼操作位并起自愈计时，起身时收掉计时器。 */
	void SetDownedFromAuthority(bool bNewDowned, ECatRecoveryMode RecoveryMode);

	/** 自愈计时到点回调；倒地满配置时长后自己站起来（猫册 §3.1.5）。 */
	void HandleDownedSelfRecoveryElapsed();

	/** 臭气计时到点回调；到点解除「请勿靠近」，不影响倒地与恢复方式。 */
	void HandleStenchElapsed();

	/** 定位 Owner Character 的项目 ASC 供属性提交；Owner 类型不匹配时返回空，避免创建平行身体属性源。 */
	UCatAbilitySystemComponent* ResolveAbilitySystem() const;

	/** 构造操作+RequestId 的局内幂等键；身份由上层 Controller 权限另行验证。 */
	static FString MakeTerminalKey(const TCHAR* Operation, FGuid RequestId);

	/** authority 提交后请求复制并广播，客户端 RepNotify 只广播；集中保证 UI 不漏掉任何完整快照变化。 */
	void PublishSnapshot();

	/** Wet/Downed/疲惫档/Recovery 的唯一复制事实；Condition 写入、UI 和表现层读取。 */
	UPROPERTY(ReplicatedUsing = OnRep_Snapshot)
	FCatConditionSnapshot Snapshot;

	/** 本组件处理的身体命令的首次完整终态；防止网络重试重复吃鱼或重复恢复。 */
	TMap<FString, FCatDomainCommandResult> TerminalCache;

	/** 当前脚点持续处在危险水深中的确认时长，单位为 World 秒；水域暴露更新写入它，用来给危险水域进入判定提供滞回前的累计证据。 */
	double DangerousWaterBuildUpSeconds = 0.0;

	/** 倒地自愈的一次性计时器；只在 authority 侧存在，起身或组件结束时清掉。 */
	FTimerHandle DownedSelfRecoveryTimer;

	/** 臭气的一次性计时器；只在 authority 侧存在，到点或组件结束时清掉。重复吃臭鱼只重设它。 */
	FTimerHandle StenchTimer;
};
