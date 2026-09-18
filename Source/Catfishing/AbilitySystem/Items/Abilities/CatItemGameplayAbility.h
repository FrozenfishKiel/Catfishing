#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/CatGameplayAbility.h"
#include "AbilitySystem/Items/CatItemAbilityTargetData.h"
#include "CatItemGameplayAbility.generated.h"

class UCatInventoryItemInstance;
class UCatItemUseFragment;

/** 物品动作的共同生命周期；TargetData 固定来源，AbilityTask 管前摇，成本与 GE 在服务器提交。 */
UCLASS(Abstract)
class CATFISHING_API UCatItemGameplayAbility : public UCatGameplayAbility
{
	GENERATED_BODY()
public:
	/** 安装精确来源物品成本，并让所有物品动作遵守同一互斥标签。 */
	UCatItemGameplayAbility();
	/** 建立目标数据监听，本地提交输入意图；服务器只使用验证后的目标启动动作。 */
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	/** 移除目标监听、停止未完成动作并报告服务器结果；重复取消不再次消费。 */
	virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
	/** 读取本次冻结的来源；成本不得改用玩家后来选中的格子。 */
	const FCatItemAbilityTargetData& GetUseTarget() const { return UseTarget; }
	/** 从库存重读同一实例；来源已被转移或删除时返回空。 */
	UCatInventoryItemInstance* ResolveSourceItem() const;
	/** 本次来源的只读使用配置；库存按实例定义取值，嘴叼鱼按正式鱼目录取值，来源消失后不得继续提交。 */
	const UCatItemUseFragment* GetUseConfiguration() const;
	/** 预检目标访问、动作许可与来源绑定；派生能力只补充自己的领域规则。 */
	virtual bool ValidateUse() const;
	/** 检查配置能否被本行为完整执行，防止策划填入不会生效的字段；失败原因同时用于编辑器提示和运行拒绝。 */
	virtual bool ValidateUseConfiguration(const UCatItemUseFragment& Configuration, FText& OutError) const;
	/** 是否允许在自己的搏斗阶段使用；默认禁止，抄网行为按既定玩法显式放行。 */
	virtual bool AllowsUseDuringActiveFishing() const { return false; }
	/** 生成来源相关效果参数；普通道具只读配置，食物按真实重量计算。 */
	virtual void GatherEffectParameters(TMap<FGameplayTag, float>& Parameters) const;
	/** 成功后通知领域消费者；默认无额外副作用，不能从这里再次扣除物品。 */
	virtual void OnUseCommitted(UCatInventoryItemInstance* ConsumedItem);
	/** 成本写回实际支付结果；只记录本次能力状态，不是跨系统可变回执对象。 */
	void SetResourceCommitted(bool bCommitted) const { bResourceCommitted = bCommitted; }
	/** 按下时由本地输入冻结视线；自用物品可忽略射线，目标型能力可覆写采样但不能在这里执行玩法。 */
	virtual void CaptureTarget(APlayerController* Controller, FCatItemAbilityTargetData& Target) const;
	/** 重查来源视点与方向是否合法；目标型使用只接受角色附近的视线，具体命中由各行为在服务器重算。 */
	bool ResolveUseRay(FVector& Origin, FVector& Direction) const;
	/** 行为是否需要等待同一来源的松开事件；瞬时动作不会因按钮松开而被取消。 */
	virtual bool UsesContinuousInput() const { return false; }
	/** 是否提供右键副操作；输入路由据此决定使用物品还是保留原右手抓握。 */
	virtual bool SupportsSecondaryUse() const { return false; }
	/** 是否需先输入一句话；本地确认后才发起能力，取消不产生服务器成本。 */
	virtual bool RequiresMessageInput() const { return false; }
	/** 世界产物与资源需要一同公开时延迟库存通知；领域提交方必须在终态写入后发布。 */
	virtual bool DefersInventoryCostNotification() const { return false; }
protected:
	/** 当前激活的不可变来源意图；只在合法 TargetData 到达时写一次，结束清理。 */
	UPROPERTY() FCatItemAbilityTargetData UseTarget;
	/** 已冻结的原物品；最后一件扣除后仍用于效果上下文与成功事件，能力结束时释放。 */
	UPROPERTY() TObjectPtr<UCatInventoryItemInstance> CommittedSource;
	/** 本次能力是否已成功支付资源；成本写入、提交流程读取，最终效果防重还依赖 bUseCommitted 和库存请求记录。 */
	mutable bool bResourceCommitted = false;
	/** 是否已经接受本次目标；同一激活的重复 TargetData 不会启动第二个前摇。 */
	bool bTargetAccepted = false;
	/** 本次服务器动作是否成功；EndAbility 用它输出唯一回执。 */
	bool bUseCommitted = false;
	/** 前摇完成后的提交点；效果型道具提交成本与 GE，领域行为可覆写执行已有权威命令。 */
	UFUNCTION() virtual void CommitUse();
private:
	/** TargetData 监听句柄；结束能力时从同一预测键移除。 */
	FDelegateHandle TargetDelegate;
	/** 目标数据等待期限；服务器未收到目标时释放动作占用，结束时清除定时器。 */
	FTimerHandle TargetTimeout;
	/** 前摇被打断或目标数据超时后取消；已经提交的收益保持权威结果。 */
	UFUNCTION() void CancelPendingUse();
	/** 接受同一激活的来源数据并校验；伪造类型、来源或重复提交被拒绝。 */
	void ReceiveTargetData(const FGameplayAbilityTargetDataHandle& Data, FGameplayTag ApplicationTag);
};
