#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Fishing/CatFishingGameplayAbility.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "CatFishingChumAbility.generated.h"

class UCatChumEquipmentItemInstance;

/** 来源窝料实例专属的服务器 Ability；G 的连续 Use 激活它，并由 AbilityTask 持有服务器蓄力和松开生命周期。 */
UCLASS()
class CATFISHING_API UCatGA_FishingChum : public UCatFishingGameplayAbility
{
	GENERATED_BODY()

public:
	/** 绑定打窝 Ability Tag 并改为 ServerOnly；能力只能由来源物品实例的 G Use 精确激活，不能绑定全局输入 Tag。 */
	UCatGA_FishingChum();

	/** 从来源 AbilitySpec 读取并冻结当前 UseContext，随后创建 WaitInputRelease 任务开始服务器蓄力。 */
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	/** 命令适配层用请求 ID 定位精确活动 Ability，避免一个 Release 触及其他来源实例。 */
	bool MatchesActiveUseRequest(FGuid RequestId) const;
	/** 查询本实例是否仍由 WaitInputRelease 持有；测试和诊断据此观察真实 Task 生命周期，不读取组件影子时间。 */
	bool IsWaitingForInputRelease() const;
	/** Ability 结束时撤销 Task 等待标记；取消、失焦与正常松开都不能让来源实例留下活动假象。 */
	virtual void EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	/** AbilityTask 收到服务器确认的松开事件后，用服务器时长提交原有窝料事务并结束本次能力。 */
	UFUNCTION()
	void HandleInputReleased(float ServerHeldSeconds);

	/** 本次连续 Use 冻结的库存上下文；激活时从来源实例读取，Task 结束前不再扫描当前选中格。 */
	FCatInventoryItemUseContext ActiveUseContext;

	/** 本次 Use 对应的运行物品实例；提交时用其稳定实例和定义身份阻止换物后松开。 */
	TWeakObjectPtr<UCatChumEquipmentItemInstance> ActiveSourceItem;

	/** 当前 AbilityTask 是否仍在等待本 Spec 的 Release；创建任务后写入，任务回调或任意 End 路径清除。 */
	bool bWaitingForInputRelease = false;
};
