#pragma once

#include "AbilitySystem/Items/Abilities/CatEquipmentItemAbility.h"
#include "CatGA_UseScoopNet.generated.h"

/** 抄网目标行为；本地采样视线，服务器沿原抄取规则重算目标、距离与冷却。 */
UCLASS()
class CATFISHING_API UCatGA_UseScoopNet : public UCatEquipmentItemAbility
{
	GENERATED_BODY()
public:
	/** 搏斗中仍可抄鱼；命中、距离、收鱼窗口继续由抄取领域复核。 */
	virtual bool AllowsUseDuringActiveFishing() const override { return true; }
	/** 冻结按下时的准星目标，随标准 TargetData 传输给服务器复核。 */
	virtual void CaptureTarget(APlayerController* Controller, FCatItemAbilityTargetData& Target) const override;
	/** 取消时先撤销原请求的待裁决捕获，再让共同能力释放目标和表现。 */
	virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
protected:
	/** 提交原抄网实例及视线；捕获完成通过同一能力的结束回调收口。 */
	virtual FCatDomainCommandResult ExecuteEquipmentUse(const FCatInventoryItemUseContext& Context, const UCatEquipmentItemDefinition& Definition) override;
};
