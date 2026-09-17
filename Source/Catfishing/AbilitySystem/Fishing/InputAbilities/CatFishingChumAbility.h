#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Items/Abilities/CatItemGameplayAbility.h"
#include "CatFishingChumAbility.generated.h"

/** 窝料使用能力；预测蓄力表现并以标准松开事件提交原物品，实例只保存持久数据。 */
UCLASS()
class CATFISHING_API UCatGA_FishingChum : public UCatItemGameplayAbility
{
	GENERATED_BODY()
public:
	/** 窝料必须消费正整数件数；世界窝点配置属于窝料片段，不接收自用 GE 配置。 */
	virtual bool ValidateUseConfiguration(const UCatItemUseFragment& Configuration, FText& OutError) const override;
	/** 窝料由按住和松开组成；菜单调用则在同一能力中立即提交零蓄力。 */
	virtual bool UsesContinuousInput() const override { return true; }
	/** 窝点与数量在同一提交中确认，环境服务记录终态后发布库存变化。 */
	virtual bool DefersInventoryCostNotification() const override { return true; }
	/** 查询真实任务等待状态，供本地预览和运行诊断观察。 */
	bool IsWaitingForInputRelease() const { return bWaitingForInputRelease; }
	/** 查询本地预测蓄力秒数；非本地或已结束时不展示预览。 */
	bool TryGetLocalChargePreview(APlayerController* Controller, float& OutHeldSeconds) const;
	/** 清除本次预测预览；提交锁尚未退出时按 GAS 规则延迟清理。 */
	virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
protected:
	/** 目标校验完成后开始等待松开；这里不支付物品，落点和水域校验完成后才消费。 */
	virtual void CommitUse() override;
private:
	/** 当前能力是否仍等待释放；任务建立后写入，松开和结束立即清除。 */
	bool bWaitingForInputRelease = false;
	/** 本地预览起始世界秒数；仅供表现读取，权威蓄力由服务器任务时钟计算。 */
	double PreviewStartSeconds = 0.0;
	/** 标准输入释放任务给出的持续秒数；服务器复核来源并调用原有投放计算与消耗提交。 */
	UFUNCTION() void HandleInputReleased(float HeldSeconds);
};
