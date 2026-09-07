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
	/** 构造一份空物品实例；定义类会在正式入库前由库存组件写入。 */
	UCatInventoryItemInstance(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** 复制声明流程：同步定义类和运行宿主，客户端据此还原展示和只读语义。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 实例需要支持网络引用；库存组件把它登记为子对象，避免客户端 FastArray 只拿到空指针。 */
	virtual bool IsSupportedForNetworking() const override;

	/** 定义类一旦绑定就驱动片段初始化；这样实例状态从静态配置派生，避免显示名或标签反推身份。 */
	void SetItemDefinitionClass(TSubclassOf<UCatInventoryItemDefinition> InDefinitionClass);

	/** 读取这份实例的静态定义类；存档、日志或适配层需要稳定类型时使用它。 */
	TSubclassOf<UCatInventoryItemDefinition> GetItemDefinitionClass() const;

	/** 读取定义类默认对象；调用方只能把它当静态配置，不应在运行期修改。 */
	const UCatInventoryItemDefinition* GetItemDefinition() const;

	/** 运行宿主记录当前拥有者；跨库存移动会刷新它，避免实例行为继续认为自己属于旧 Actor。 */
	void SetRuntimeOwnerActor(AActor* InRuntimeOwnerActor);

	/** 读取当前运行宿主；没有显式宿主时回退到 Outer Actor，方便刚创建的实例立即可用。 */
	AActor* GetRuntimeOwnerActor() const;

	/** Use 能力由具体实例子类声明；通用实例默认拒绝，避免任意物品被库存层扣掉。 */
	virtual bool CanUseFromInventory(const FCatInventoryEntry& InventoryEntry, APawn* UserPawn) const;

	/** 执行这份物品的库存使用裁决；成功时返回应从格子扣除的数量，通用实例默认不产生使用结果。 */
	virtual bool TryUseFromInventory(FCatInventoryEntry& InventoryEntry, APawn* UserPawn, int32& OutConsumeCount);

protected:
	/** 这份实例对应的静态定义类；复制它比复制一份可变定义对象更符合 Catfishing 让库存脱离 GAS 的目标。 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Inventory")
	TSubclassOf<UCatInventoryItemDefinition> ItemDefinitionClass;

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
