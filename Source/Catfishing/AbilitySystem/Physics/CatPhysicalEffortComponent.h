#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Physics/Simulation/CatIntentMotionModel.h"
#include "CatPhysicalEffortComponent.generated.h"

struct FCatBodyDriveSample;
struct FOnAttributeChangeData;
class UCatAbilitySystemComponent;

/** 个人物理协助的资源结算边界；读取实际运动与负载，不加入钓鱼会话，也不重复结算鱼竿成本。 */
UCLASS(ClassGroup=(Catfishing), meta=(BlueprintSpawnableComponent))
class CATFISHING_API UCatPhysicalEffortComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	/** 建立身体组件的默认复制配置，资源余额仍只由 ASC 属性复制。 */
	UCatPhysicalEffortComponent();
	/** 按当前力量、体力和钓鱼平衡配置计算可用推力，单位 kg·cm/s²；缺配置或无体力时返回零。 */
	double GetMaximumForceKgCmS2() const;
	/** 服务器每次实际移动步后结算个人消耗或恢复；候选运动预测不得调用，钓鱼主控的账单交回原 Runner。 */
	void SettleMovementFromAuthority(const FCatBodyDriveSample& Drive, const FVector& IntendedDisplacement,
		const FVector& ActualDisplacement, double Seconds, bool bGrounded);
	/** 供诊断读取最近一次实际运动的距离与耗费需求；数据来自已执行的结算步骤，查询不得再次运行计算或扣量。 */
	const FCatIntentMotionResult& GetLastResult() const { return LastResult; }
	/** 供诊断读取最近体力变化，正数表示消耗、负数表示恢复；服务器是整步结算量，客户端只是限频保留的单次属性通知量。 */
	double GetLastPaid() const { return LastPaid; }
	/** 供日志关联本组件的运动结算步骤；序号仅在本机有效，不用于跨端去重或判断服务器结算先后。 */
	uint64 GetSettlementSequence() const { return SettlementSequence; }
	/** 绑定 ASC 体力变化；不要求属性集认识物理用力系统。 */
	virtual void BeginPlay() override;
	/** 从原 ASC 解绑通知并撤销本组件持有的恢复限制。 */
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	/** 客户端属性通知仅更新诊断读数；服务器扣量仍在固定步中结算。 */
	void HandleStaminaChanged(const FOnAttributeChangeData& Change);
	/** 实际订阅的 ASC，退出时按此对象配对解绑。 */
	TWeakObjectPtr<UCatAbilitySystemComponent> ObservedASC;
	/** 绿色体力通知句柄，由 BeginPlay 创建、EndPlay 释放。 */
	FDelegateHandle GreenChangedHandle;
	/** 黄色储备通知句柄，BeginPlay 创建、EndPlay 释放；用于观察绿段耗尽后的扣量。 */
	FDelegateHandle YellowChangedHandle;
	/** 输出本机运动结果、ASC 余额与身体身份；拒绝记警告，其余记普通诊断日志。 */
	void LogState(FName Event, FName Result) const;
	/** 上次观察的协作连接情况，仅用于状态变化时输出日志。 */
	bool bWasConnected = false;
	/** 上次观察的钓鱼独占结算情况，仅用于日志；准入每步重查原钓鱼入口。 */
	bool bWasInFight = false;
	/** 上次观察的承重事实，仅过滤重复日志；实际恢复资格读取 ASC Tag。 */
	bool bRecoveryBlockedByLoad = false;
	/** 下一次允许输出高频结算日志的 World 秒数，服务端结算和客户端属性观察分别维护。 */
	double NextLogSeconds = 0;
	/** 最近诊断的体力增减量；服务器记录整次结算，客户端记录一次被限频保留的属性通知。 */
	double LastPaid = 0;
	/** 当前组件累计进入有效运动计算的次数；日志据此关联单个结算步骤，不参与玩法。 */
	uint64 SettlementSequence = 0;
	/** 最近运动计算得到的位移与耗费需求；每次结算前清空，由运动模型写入，查询和日志只读。 */
	FCatIntentMotionResult LastResult;
};
