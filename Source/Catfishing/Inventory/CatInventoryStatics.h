#pragma once

#include "CoreMinimal.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CatInventoryStatics.generated.h"

class UCatInventoryComponent;
class UCatInventoryItemInstance;

/** 按定义发货的一项库存载荷；拾取、商店、奖励等来源只需要描述物品类型和数量。 */
USTRUCT(BlueprintType)
struct FCatInventoryDefinitionEntry
{
	GENERATED_BODY()

	/** 本项要发放的数量；统一收货入口要求它大于 0，避免出现空事务。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	int32 Count = 0;

	/** 要生成的物品定义资产；库存组件会按定义解析默认实例类型。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	TObjectPtr<UCatInventoryItemDefinition> ItemDefinition = nullptr;

	/** 可选的实例类覆盖；特殊来源需要指定运行实例子类时使用，普通来源留空。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	TSubclassOf<UCatInventoryItemInstance> ItemInstanceClass = nullptr;
};

/** 按现有实例发货的一项库存载荷；非堆叠物可保留原实例身份，堆叠物只合并同定义数量。 */
USTRUCT(BlueprintType)
struct FCatInventoryInstanceEntry
{
	GENERATED_BODY()

	/** 本项要放入库存的数量；堆叠物按定义合并，非堆叠物会按实例类补齐独立实例。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	int32 Count = 0;

	/** 要接收的运行期物品实例；非堆叠接收成功后会把运行宿主刷新为接收库存的拥有者。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	TObjectPtr<UCatInventoryItemInstance> ItemInstance = nullptr;
};

/** 一次统一收货事务的完整批次；组件会先确认整批能放下，再正式写入，避免半批成功。 */
USTRUCT(BlueprintType)
struct FCatInventoryReceiveBatch
{
	GENERATED_BODY()

	/** 需要按定义生成的物品列表；适合商店、奖励和拾取这种没有既有实例的来源。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	TArray<FCatInventoryDefinitionEntry> DefinitionEntries;

	/** 需要保留实例身份入库的物品列表；适合收回、转移和后续世界掉落回包。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	TArray<FCatInventoryInstanceEntry> InstanceEntries;

	/** 空批次按幂等成功处理；避免奖励或商店空载荷把上层流程误判成失败。 */
	bool IsEmpty() const
	{
		return DefinitionEntries.IsEmpty() && InstanceEntries.IsEmpty();
	}
};

/** Actor 级统一库存入口；外部系统只向目标 Actor 发货，不直接了解背包、快捷栏或仓库的内部格子。 */
UCLASS()
class CATFISHING_API UCatInventoryStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 判断目标 Actor 身上的某个库存组件能否完整接收这一批物品；它只做预检，不改变库存状态。 */
	static bool CanActorFullyAcceptInventoryBatch(const AActor* TargetActor,
		const FCatInventoryReceiveBatch& ReceiveBatch);

	/** 按库存组件优先级寻找第一个能完整接收整批物品的组件，并把批次正式写入那里。 */
	static bool TryAddInventoryBatchToActor(AActor* TargetActor, const FCatInventoryReceiveBatch& ReceiveBatch);

	/** 收集目标 Actor 公开的库存组件并按统一收货优先级排序；适配层用它显式选择目标库存。 */
	static void AppendInventoryComponentsFromActor(const AActor* TargetActor,
		TArray<UCatInventoryComponent*>& OutInventoryComponents);

private:
	/** 从 Actor 组件上的库存接口收集候选库存；函数只负责发现和排序，不执行容量或写入判断。 */
	static void CollectInventoryComponentsFromActor(const AActor* TargetActor,
		TArray<UCatInventoryComponent*>& OutInventoryComponents);
};
