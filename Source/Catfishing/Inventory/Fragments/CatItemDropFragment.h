#pragma once
#include "Inventory/CatInventoryItemDefinition.h"
#include "CatItemDropFragment.generated.h"

/** 捕获完成时的一条额外掉落配置；物品及数量完全由内容资产决定。 */
USTRUCT(BlueprintType)
struct FCatItemExtraDrop
{
	GENERATED_BODY()
	/** 要发放的物品定义；结算加载它并通过统一收货入口入库或落地。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UCatInventoryItemDefinition> Item;
	/** 基础掉落概率，0 到 1；执行者的 GAS 加成在此基础上相加并限制到合法概率。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0", ClampMax="1")) float Probability = 0.f;
	/** 成功时最少发放的件数；与上限共同定义包含端点的整数随机区间。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1")) int32 MinimumCount = 1;
	/** 成功时最多发放的件数；不得小于下限。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1")) int32 MaximumCount = 1;
};

/** 挂在捕获对象定义上的额外掉落；不参与普通拾取、转移或出售。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class CATFISHING_API UCatItemDropFragment : public UCatInventoryItemFragment
{
	GENERATED_BODY()
public:
	/** 各项独立判定的奖励表；空表表示本对象没有额外掉落。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="掉落") TArray<FCatItemExtraDrop> ExtraDrops;
	/** 校验定义引用、概率及数量区间，不在静态检查时产生奖励。 */
	virtual bool IsRuntimeReady() const override;
	/** 捕获权威入口调用一次；按事件 ID 固定随机流，用实际执行者的属性结算并发货，返回交付结果。 */
	bool AwardCaptureDropsFromAuthority(AActor* Executor, const FGuid& CaptureId) const;
};
