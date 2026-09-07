#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CatInventoryItemInstance.generated.h"

class APawn;
class UCatInventoryItemDefinition;
struct FCatInventoryEntry;

/** 运行期的一份物品身份；库存格保存数量，实例保存这件物品跨移动、使用和复制时不该丢的身份与行为入口。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatInventoryItemInstance : public UObject
{
	GENERATED_BODY()

public:
	/** 构造一份空物品实例；定义资产会在正式入库前由库存组件写入。 */
	UCatInventoryItemInstance(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** 复制声明流程：同步定义资产和运行宿主，客户端据此还原展示和只读语义。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 实例需要支持网络引用；库存组件把它登记为子对象，避免客户端 FastArray 只拿到空指针。 */
	virtual bool IsSupportedForNetworking() const override;

	/** 定义资产一旦绑定就驱动片段初始化；这样实例状态从静态配置派生，避免显示名或标签反推身份。 */
	void SetItemDefinition(UCatInventoryItemDefinition* InDefinition);

	/** 读取这份实例的静态定义资产；存档、日志或适配层需要稳定目录入口时使用它。 */
	UCatInventoryItemDefinition* GetItemDefinition() const;

	/** 读取这份实例的稳定定义 ID；日志、旧快照投影和商店回执用它对齐同一种物品。 */
	FName GetItemDefinitionId() const;

	/** 读取这份运行实例自己的稳定 ID；堆叠格共享一个实例 ID，非堆叠物每件各自拥有一个。 */
	FGuid GetItemInstanceId() const;

	/** 运行宿主记录当前拥有者；跨库存移动会刷新它，避免实例行为继续认为自己属于旧 Actor。 */
	void SetRuntimeOwnerActor(AActor* InRuntimeOwnerActor);

	/** 读取当前运行宿主；没有显式宿主时回退到 Outer Actor，方便刚创建的实例立即可用。 */
	AActor* GetRuntimeOwnerActor() const;

	/** Use 能力由具体实例子类声明；通用实例默认拒绝，避免任意物品被库存层扣掉。 */
	virtual bool CanUseFromInventory(const FCatInventoryEntry& InventoryEntry, APawn* UserPawn) const;

	/** 执行这份物品的库存使用裁决；成功时返回应从格子扣除的数量，通用实例默认不产生使用结果。 */
	virtual bool TryUseFromInventory(FCatInventoryEntry& InventoryEntry, APawn* UserPawn, int32& OutConsumeCount);

protected:
	/** 定义绑定后的实例状态扩展点；父类片段已完成初始化后调用它，子类只能补齐自己拥有的运行状态。 */
	virtual void HandleItemDefinitionAssigned();

	/** 这份运行物品的稳定实例 ID；服务器创建时写入，客户端和旧快照投影只读取它。 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Inventory")
	FGuid ItemInstanceId;

	/** 这份实例对应的静态定义资产；复制资产引用比复制可变定义对象更符合 Catfishing 让库存脱离 GAS 的目标。 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Inventory")
	TObjectPtr<UCatInventoryItemDefinition> ItemDefinition;

	/** 当前运行宿主 Actor；服务器移动实例后刷新它，客户端只用它判断本地归属和调试来源。 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Inventory")
	TObjectPtr<AActor> RuntimeOwnerActor = nullptr;
};

/** 库存层面的简单消耗品实例；它只完成扣量裁决，实际治疗、窝料或钓鱼效果由调用方在提交后处理。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatInventoryConsumableItemInstance : public UCatInventoryItemInstance
{
	GENERATED_BODY()

public:
	/** 消耗品 Use 需要片段和足够数量同时成立；避免没有消耗语义的物品被右键扣除。 */
	virtual bool CanUseFromInventory(const FCatInventoryEntry& InventoryEntry, APawn* UserPawn) const override;

	/** 返回本次消耗品 Use 应扣的数量；不在这里触发任何 GAS、Condition 或场景副作用。 */
	virtual bool TryUseFromInventory(FCatInventoryEntry& InventoryEntry, APawn* UserPawn, int32& OutConsumeCount) override;
};
