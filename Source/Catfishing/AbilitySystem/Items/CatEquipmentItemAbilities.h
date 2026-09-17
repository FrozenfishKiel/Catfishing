#pragma once
#include "CoreMinimal.h"
#include "AbilitySystem/Items/CatItemGameplayAbility.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "CatEquipmentItemAbilities.generated.h"

class UCatEquipmentItemDefinition;

/** 非消耗装备的使用生命周期；来源仍是随身物品，拿出后的操作能力由装备实例另行授予。 */
UCLASS(Abstract)
class CATFISHING_API UCatEquipmentItemAbility : public UCatItemGameplayAbility
{
	GENERATED_BODY()
protected:
	/** 以冻结来源构造权威上下文后调用领域行为；异步抄取继续持有能力直至收到完成结果。 */
	virtual void CommitUse() override;
	/** 具体装备行为由 C++ 实现；上下文已固定原始来源，禁止使用当前快捷格替代。 */
	virtual FCatDomainCommandResult ExecuteEquipmentUse(const FCatInventoryItemUseContext& Context, const UCatEquipmentItemDefinition& Definition) PURE_VIRTUAL(UCatEquipmentItemAbility::ExecuteEquipmentUse, return {};);
private:
	/** 接收同步或异步领域终态；只完成仍属于本次请求的能力，迟到结果不影响下一次激活。 */
	void CompleteEquipmentUse(const FCatDomainCommandResult& Result);
};

/** 鱼竿拿出行为；复用既有部署与真实鱼竿物理，库存实例不再编排部署。 */
UCLASS()
class CATFISHING_API UCatGA_DeployFishingRod : public UCatEquipmentItemAbility
{
	GENERATED_BODY()
protected:
	/** 将原竿身份交给部署命令；选择格子本身不会触发本能力。 */
	virtual FCatDomainCommandResult ExecuteEquipmentUse(const FCatInventoryItemUseContext& Context, const UCatEquipmentItemDefinition& Definition) override;
};

/** 抄网目标行为；本地采样视线，服务器沿原抄取规则重算目标、距离与冷却。 */
UCLASS()
class CATFISHING_API UCatGA_UseScoopNet : public UCatEquipmentItemAbility
{
	GENERATED_BODY()
public:
	/** 冻结按下时的准星目标，随标准 TargetData 传输给服务器复核。 */
	virtual void CaptureTarget(APlayerController* Controller, FCatItemAbilityTargetData& Target) const override;
protected:
	/** 提交原抄网实例及视线；捕获完成通过同一能力的结束回调收口。 */
	virtual FCatDomainCommandResult ExecuteEquipmentUse(const FCatInventoryItemUseContext& Context, const UCatEquipmentItemDefinition& Definition) override;
};

/** 鱼饵和鱼漂装配行为；仅更改选择，鱼饵仍在既定真咬时点消耗。 */
UCLASS()
class CATFISHING_API UCatGA_SelectFishingLoadout : public UCatEquipmentItemAbility
{
	GENERATED_BODY()
protected:
	/** 按定义的明确目标槽装配原实例，保留其余槽选择和现有消耗规则。 */
	virtual FCatDomainCommandResult ExecuteEquipmentUse(const FCatInventoryItemUseContext& Context, const UCatEquipmentItemDefinition& Definition) override;
};
