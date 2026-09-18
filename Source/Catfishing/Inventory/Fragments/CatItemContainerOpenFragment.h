#pragma once
#include "Inventory/CatInventoryItemDefinition.h"
#include "CatItemContainerOpenFragment.generated.h"
class UGameplayEffect;
class UCatInventoryComponent;

/** 留在容器格子中的一次性开箱效果；假鱼本身就是物品，不另存容器陷阱状态。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class CATFISHING_API UCatItemContainerOpenFragment : public UCatInventoryItemFragment
{
	GENERATED_BODY()
public:
	/** 打开时施加给开箱者的效果；不区分是否为放置者，效果资产负责 GC 表现。 */
	UPROPERTY(EditDefaultsOnly, Category="开箱") TSubclassOf<UGameplayEffect> Effect;
	/** 效果必须配置，物品不得堆叠；无效配置不进入正式目录。 */
	virtual bool IsRuntimeReady() const override;
	/** 鱼护权威开箱入口调用；按槽位只触发第一件，扣掉该件后施加 GE，重放由库存请求记录拒绝。 */
	static void TriggerFirstFromAuthority(UCatInventoryComponent* Inventory, AActor* Opener, FGuid RequestId);
};
