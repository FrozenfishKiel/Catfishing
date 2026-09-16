#pragma once

#include "CoreMinimal.h"
#include "Equipment/CatEquipmentInventoryItemInstance.h"
#include "AbilitySystem/Config/CatAbilitySet.h"
#include "CatEquipmentUseItemInstances.generated.h"

/** 鱼竿库存实例；Use 只把这件精确实例交给既有部署事务，耐久和 held-entry 身份由父类保存。 */
UCLASS(BlueprintType)
class CATFISHING_API UCatFishingRodEquipmentItemInstance final : public UCatEquipmentInventoryItemInstance
{
	GENERATED_BODY()
public:
	/** 鱼竿 Use 的正式入口；成功后由库存事务把本实例移入 held entry，并由 Fishing 部署同一件世界竿。 */
	virtual FCatDomainCommandResult UseFromInventorySlotFromAuthority(const FCatInventoryEntry& InventoryEntry, const FCatInventoryItemUseContext& UseContext) override;
	/** 鱼竿必须保留实例才能让部署 Actor、耐久和收竿回包持续指向同一件物品。 */
	virtual bool KeepsInventoryInstanceWhileUsed() const override;
};

/** 抄网库存实例；Use 是一次既有抢抄事务，不建立持续输入或装备选择状态。 */
UCLASS(BlueprintType)
class CATFISHING_API UCatScoopNetEquipmentItemInstance final : public UCatEquipmentInventoryItemInstance
{
	GENERATED_BODY()
public:
	/** 抄网 Use 的正式入口；将精确实例交给既有 Scoop 命令，由命令保持范围和结算规则。 */
	virtual FCatDomainCommandResult UseFromInventorySlotFromAuthority(const FCatInventoryEntry& InventoryEntry, const FCatInventoryItemUseContext& UseContext) override;
};

/** 装配型库存实例；Use 按定义显式 TargetSlot 更新既有 Bait 或 Float 读模型。 */
UCLASS(BlueprintType)
class CATFISHING_API UCatLoadoutEquipmentItemInstance final : public UCatEquipmentInventoryItemInstance
{
	GENERATED_BODY()
public:
	/** 装配 Use 的正式入口；只按 TargetSlot 写入精确实例，鱼饵消耗与鱼漂抛投仍由既有链路处理。 */
	virtual FCatDomainCommandResult UseFromInventorySlotFromAuthority(const FCatInventoryEntry& InventoryEntry, const FCatInventoryItemUseContext& UseContext) override;
};

/** 窝料库存实例；Use 保存唯一连续输入上下文，并沿用 Commands 的 Begin/End 事务。 */
UCLASS(BlueprintType)
class CATFISHING_API UCatChumEquipmentItemInstance final : public UCatEquipmentInventoryItemInstance
{
	GENERATED_BODY()
public:
	/** 窝料 Begin Use；记录本实例唯一请求上下文并开启既有蓄力事务。 */
	virtual FCatDomainCommandResult UseFromInventorySlotFromAuthority(const FCatInventoryEntry& InventoryEntry, const FCatInventoryItemUseContext& UseContext) override;
	/** 窝料声明连续输入；输入 Ability 用它固定 Begin 的实例和请求，Release 不会读取新选中格。 */
	virtual bool UsesContinuousInput() const override;
	/** 窝料提交由既有 End 事务扣除一份数量；库存 Use 预检通过此声明保持数量物契约。 */
	virtual bool ConsumesInventoryQuantityOnUse() const override;
	/** 窝料 Release/Cancel；调用既有 End API 后清理保存的上下文，重复结束保持无可用上下文。 */
	virtual FCatDomainCommandResult EndUseFromInventorySlotFromAuthority(const FCatInventoryItemUseContext& UseContext, bool bCancelled) override;
	/** 读取当前连续 Use 上下文；来源 Ability 在激活首帧固定副本，后续不再依赖实例的可变状态。 */
	bool TryGetActiveUseContext(FCatInventoryItemUseContext& OutUseContext) const;
	/** 能力结束时幂等清理匹配请求；本次句柄转交原 ASC 的延迟回收，不依赖库存实例继续存活。 */
	void AbortActiveUseFromAbility(FGuid RequestId, UCatAbilitySystemComponent* SourceAbilitySystem);

private:
	/** 当前窝料实例尚未结束的唯一 Use 上下文；服务器 Begin 写入、来源 Ability 读取、End 无论结果都清空。 */
	FCatInventoryItemUseContext ActiveUseContext;
	/** ActiveUseContext 是否对应尚未结束的权威请求；防止默认构造的空 Request 被 Ability 当成有效命令。 */
	bool bHasActiveUseContext = false;
	/** 本次窝料使用拥有的能力与效果句柄；结束时移出并按原 ASC 回收，下一次使用不会共享这些句柄。 */
	FCatGrantedAbilitySetHandles ActiveUseAbilityHandles;
};
