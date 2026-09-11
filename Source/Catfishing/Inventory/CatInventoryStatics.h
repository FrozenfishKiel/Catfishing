#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CatInventoryStatics.generated.h"

class ACatCharacter;
class UCatInventoryComponent;
class UCatInventoryItemInstance;
class UCatInventorySettings;

/** 物品离开库存的两种玩家动作；丢弃开启物理轻抛，放置在检测通过的位置固定，不触发物品 Use。 */
UENUM(BlueprintType)
enum class ECatInventoryWorldAction : uint8
{
	Drop,
	Place
};

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

/** 一次统一收货事务的完整批次；组件会先确认整批能放下，正式命令再决定是否需要更强的失败回滚边界。 */
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
	/** 只读求解已有世界物的丢弃或放置变换；库存落地与嘴叼鱼共用碰撞和空间规则，失败不移动物体或改变所有权。 */
	static bool FindWorldReleaseTransform(ACatCharacter* Character, AActor* ItemActor,
		ECatInventoryWorldAction Action, const UCatInventorySettings& Settings, FTransform& OutTransform);

	/** 判断目标 Actor 身上的某个库存组件能否完整接收这一批物品；它只做预检，不改变库存状态。 */
	static bool CanActorFullyAcceptInventoryBatch(const AActor* TargetActor,
		const FCatInventoryReceiveBatch& ReceiveBatch);

	/** 按库存组件优先级寻找第一个能完整接收整批物品的组件，并把批次正式写入那里。 */
	static bool TryAddInventoryBatchToActor(AActor* TargetActor, const FCatInventoryReceiveBatch& ReceiveBatch);

	/** 在两个可触达 Actor 的正式库存之间移动物品；外部只提交宿主和槽位，组件负责正式格子事务。 */
	static FCatDomainCommandResult MoveItemBetweenInventoryHostsFromAuthority(ACatCharacter* ControlledCharacter,
		FGuid RequestId, AActor* SourceInventoryHost, int32 SourceSlotIndex, AActor* TargetInventoryHost, int32 TargetSlotIndex);

	/** 使用某个可触达 Actor 正式库存中的一格物品；鱼、草药和装备类效果都由物品实例自己裁决。 */
	static FCatDomainCommandResult UseItemFromInventoryHostFromAuthority(ACatCharacter* ControlledCharacter,
		FGuid RequestId, AActor* SourceInventoryHost, int32 SourceSlotIndex);

	/** 从可触达库存丢弃或放置指定实例数量；先复核宿主，具体生成、扣量和重放仍由来源库存统一裁决。 */
	static FCatDomainCommandResult ReleaseItemToWorldFromAuthority(ACatCharacter* ControlledCharacter,
		FGuid RequestId, AActor* SourceInventoryHost, int32 SourceSlotIndex, FGuid ItemInstanceId,
		int32 Quantity, ECatInventoryWorldAction Action);

	/** 收集目标 Actor 上的库存组件并按统一收货优先级排序；Actor 级入口用它显式选择目标库存。 */
	static void AppendInventoryComponentsFromActor(const AActor* TargetActor,
		TArray<UCatInventoryComponent*>& OutInventoryComponents);

private:
	/** 从 Actor 组件列表收集候选库存；函数只负责发现和排序，不执行容量或写入判断。 */
	static void CollectInventoryComponentsFromActor(const AActor* TargetActor,
		TArray<UCatInventoryComponent*>& OutInventoryComponents);
};
