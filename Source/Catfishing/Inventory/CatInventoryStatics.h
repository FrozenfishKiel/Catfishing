#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Inventory/CatInventoryUseTarget.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CatInventoryStatics.generated.h"

struct FCatInventoryItemUseContext;
class ACatCharacter;
class UCatInventoryComponent;
class UCatInventoryItemInstance;
class UCatInventorySettings;

/** 物品离开库存的玩家动作；丢弃开启物理轻抛，放置在检测通过的位置固定，Carry 只允许鱼护或鱼缸的一条鱼静默移出到嘴部，不触发物品 Use 或地面查询。 */
UENUM(BlueprintType)
enum class ECatInventoryWorldAction : uint8
{
	Drop = 0,
	Place = 1,
	Carry = 2
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
	/** 统一菜单请求的服务器入口；解析可访问库存后由库存重读实例并执行，成功同步既有装备读模型。 */
	static FCatDomainCommandResult ExecuteInventoryActionFromAuthority(ACatCharacter* Character, FGuid RequestId,
		AActor* SourceHost, int32 SourceSlot, FGuid ItemInstanceId, FGameplayTag Action, int32 Quantity,
		const FCatInventoryUseTarget& Target = FCatInventoryUseTarget());
	/** 仅供库存已校验的命令调用；Drop 每次只接受一件，准备载体成功后才扣量；出售沿商店事务。本层不另建请求缓存，世界动作通知由库存调用方发布。 */
	static FCatDomainCommandResult ExecuteResolvedInventoryActionFromAuthority(
		const FCatInventoryItemUseContext& Context, FGameplayTag Action, int32 Quantity);
	/** 只读求解单个世界物的丢弃或放置变换；连续丢弃每次都按当时角色位置调用。 */
	static bool FindWorldReleaseTransform(ACatCharacter* Character, AActor* ItemActor,
		ECatInventoryWorldAction Action, const UCatInventorySettings& Settings, FTransform& OutTransform);

	/** 判断目标 Actor 身上的某个库存组件能否完整接收这一批物品；它只做预检，不改变库存状态。 */
	static bool CanActorFullyAcceptInventoryBatch(const AActor* TargetActor,
		const FCatInventoryReceiveBatch& ReceiveBatch);

	/** 按优先级向首个能完整收货的组件写入；可选输出返回实际接收者，供拾取关联原世界物。 */
	static bool TryAddInventoryBatchToActor(AActor* TargetActor, const FCatInventoryReceiveBatch& ReceiveBatch,
		UCatInventoryComponent** OutReceivingInventory = nullptr);

	/** 拾取与奖励收货：按库存优先级尽量入库，余量掉在宿主附近；先准备所有落地载体，准备失败不写库存。交易仍使用整批原子入口。 */
	static bool ReceiveInventoryWithOverflowFromAuthority(AActor* TargetActor, const FCatInventoryReceiveBatch& ReceiveBatch,
		UCatInventoryComponent** OutReceivingInventory = nullptr);

	/** 在两个可触达 Actor 的正式库存之间移动物品；外部只提交宿主和槽位，组件负责正式格子事务。 */
	static FCatDomainCommandResult MoveItemBetweenInventoryHostsFromAuthority(ACatCharacter* ControlledCharacter,
		FGuid RequestId, AActor* SourceInventoryHost, int32 SourceSlotIndex, AActor* TargetInventoryHost, int32 TargetSlotIndex);

	/** 从可触达库存丢弃、放置或 Carry 指定实例；Carry 只接受鱼容器中的数量一，先复核宿主与容器交互资格，具体移格和重放仍由来源库存裁决。 */
	static FCatDomainCommandResult ReleaseItemToWorldFromAuthority(ACatCharacter* ControlledCharacter,
		FGuid RequestId, AActor* SourceInventoryHost, int32 SourceSlotIndex, FGuid ItemInstanceId,
		int32 Quantity, ECatInventoryWorldAction Action);

	/**
	 * 声明：翻天时把地上没人捡的东西一次清掉——装备道具落地物与地上未拾取的鱼，返回销毁数量。
	 * 依据：道具册 §4「物品当天不灭失、跨天不保留（翻天时一并消失）」（2026-09-05 由「次日清晨回收到营地」改）。
	 * 实现：只在服务器跑，逐类遍历世界 Actor，只销毁仍在等人捡的那些。
	 * 边界（这条最要紧）：**已被拾取的载体不清**。落地物被捡走后并不销毁，而是隐藏下来继续替背包里那件
	 *       物品保管世界 Actor 引用；鱼进了鱼护或鱼缸也是同一套隐藏保管。把它们一起清掉等于把玩家背包里的
	 *       物品和鱼护里的鱼也一并删掉。嘴里叼着的鱼同理不清。
	 */
	static int32 PurgeUnclaimedWorldDropsFromAuthority(UWorld* World);

	/** 收集目标 Actor 上的库存组件并按统一收货优先级排序；Actor 级入口用它显式选择目标库存。 */
	static void AppendInventoryComponentsFromActor(const AActor* TargetActor,
		TArray<UCatInventoryComponent*>& OutInventoryComponents);

private:
	/** 从 Actor 组件列表收集候选库存；函数只负责发现和排序，不执行容量或写入判断。 */
	static void CollectInventoryComponentsFromActor(const AActor* TargetActor,
		TArray<UCatInventoryComponent*>& OutInventoryComponents);
};
