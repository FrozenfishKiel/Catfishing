#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "Inventory/CatInventoryStatics.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Templates/Function.h"
#include "CatInventoryComponent.generated.h"

class APawn;
class UActorChannel;
class UCatInventoryItemDefinition;
class UCatInventoryItemInstance;
class UCatInventoryModel;
class FOutBunch;
struct FReplicationFlags;
struct FCatInventoryItemUseContext;

/** 一个库存格的复制条目；格子只记录实例指针和堆叠数量，不持有 GAS、Fishing 或 UI 的下游状态。 */
USTRUCT(BlueprintType)
struct FCatInventoryEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** 构造一个空格；SlotOwnerComponent 会在库存组件初始化、复制或跨组件移动时补齐。 */
	FCatInventoryEntry() = default;

	/** 构造一个归属于指定库存组件的空格；用于清空格子后仍保留明确所有者。 */
	explicit FCatInventoryEntry(UCatInventoryComponent* InSlotOwnerComponent);

	/** 用另一格的物品内容替换本格；保留本格 FastArray 身份与客户端观察历史，让客户端把移动识别为原位置的内容变化。 */
	FCatInventoryEntry& operator=(const FCatInventoryEntry& Other)
	{
		// 只搬运物品和归属；基类赋值会重置复制身份，使清空/移动变成删格再加格，客户端数组下标随之错位。
		// LastObservedCount 属于目标格的本地观察历史，留给复制回调更新；自赋值不改变任何状态。
		if (this != &Other)
		{
			Instance = Other.Instance;
			StackCount = Other.StackCount;
			SlotOwnerComponent = Other.SlotOwnerComponent;
		}
		return *this;
	}

	/** 格子身份只由实例指针决定；这样堆叠数量变化不会触发错误的换物判断。 */
	bool operator==(const FCatInventoryEntry& Other) const;

	/** 空格不能和空实例互相匹配；避免清理路径把两个空指针当作同一件物品。 */
	bool operator==(const UCatInventoryItemInstance* InInstance) const;

	/** 反向实例匹配复用同一套空值规则；避免移动和删除分支出现额外判断口径。 */
	bool operator!=(const UCatInventoryItemInstance* InInstance) const;

public:
	/** 当前格子持有的物品实例；空指针表示这个格子没有内容。 */
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UCatInventoryItemInstance> Instance = nullptr;

	/** 当前格子的堆叠数量；不可堆叠物品固定为 1，空格固定为 0。 */
	UPROPERTY(BlueprintReadOnly)
	int32 StackCount = 0;

	/** 客户端上一次观察到的数量；FastArray 回调用它判断复制变化，不参与服务器玩法裁决。 */
	UPROPERTY(NotReplicated)
	int32 LastObservedCount = 0;

	/** 这个格子归属的库存组件；它只用于本地回调和调试，不作为网络事实复制。 */
	UPROPERTY(BlueprintReadOnly, NotReplicated)
	TObjectPtr<UCatInventoryComponent> SlotOwnerComponent = nullptr;
};

/** 一次库存 Use 或 UnUse 的事务结果；回执直接携带真实 entry，调用方必须从实例读取鱼竿或鱼的专属状态。 */
USTRUCT(BlueprintType)
struct FCatInventoryItemUseResult
{
	GENERATED_BODY()

	/** 本次命令的稳定关联 ID；服务器幂等缓存和上层回执用它识别同一意图。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid RequestId;

	/** 本次事务读取或移动的真实库存 entry；其中的实例是鱼竿、鱼等专属运行状态的唯一来源。 */
	UPROPERTY(BlueprintReadOnly)
	FCatInventoryEntry Item;

	/** 本次库存裁决的领域错误；成功只表示库存事务成立，外层仍需提交自己的世界效果。 */
	UPROPERTY(BlueprintReadOnly)
	ECatDomainCommandError Error = ECatDomainCommandError::InvalidPayload;

	/** 本次请求是否实际改变库存或活动实例所有权。 */
	UPROPERTY(BlueprintReadOnly)
	bool bCommitted = false;

	/** 回执是否来自同一请求的终态缓存；重放不再次改变库存。 */
	UPROPERTY(BlueprintReadOnly)
	bool bTerminalReplay = false;

	/** 重放的首次事务是否实际提交；下游只在此为真时继续自己的提交。 */
	UPROPERTY(BlueprintReadOnly)
	bool bReplayedTerminalCommitted = false;

	/** 重放前首次事务的原始错误；失败重放继续暴露首次拒绝原因。 */
	UPROPERTY(BlueprintReadOnly)
	ECatDomainCommandError ReplayedTerminalError = ECatDomainCommandError::InvalidPayload;
};

/** 把首次 Use/UnUse 终态改写为只读重放，避免相同 RequestId 再次改变库存。 */
inline void MarkInventoryItemUseReplayed(FCatInventoryItemUseResult& Result)
{
	const bool bOriginalCommitted = Result.bCommitted;
	const ECatDomainCommandError OriginalError = Result.Error;
	Result.bCommitted = false;
	Result.bTerminalReplay = true;
	Result.bReplayedTerminalCommitted = bOriginalCommitted;
	Result.Error = bOriginalCommitted && OriginalError == ECatDomainCommandError::None
		? ECatDomainCommandError::AlreadyResolved : OriginalError;
	Result.ReplayedTerminalError = OriginalError;
}

/** 下游世界效果以首次提交事实为依据；成功重放允许续做尚未落地的效果，失败重放必须停止，不能只看当前错误码。 */
inline bool CatIsAcceptedInventoryItemUseResult(const FCatInventoryItemUseResult& Result)
{
	return (Result.bCommitted && Result.Error == ECatDomainCommandError::None)
		|| (Result.bTerminalReplay && Result.bReplayedTerminalCommitted
			&& Result.ReplayedTerminalError == ECatDomainCommandError::None);
}

/** 库存格 FastArray；负责把格子增删改复制给客户端，并把变化通知交回拥有的库存组件。 */
USTRUCT(BlueprintType)
struct FCatInventoryList : public FFastArraySerializer
{
	GENERATED_BODY()

	/** 构造一份暂未绑定组件的库存列表；组件构造后会补充 OwnerComponent。 */
	FCatInventoryList() = default;

	/** 构造一份已绑定组件的库存列表；服务器和客户端复制回调都通过这个组件广播变化。 */
	explicit FCatInventoryList(UCatInventoryComponent* InOwnerComponent);

	/** 收集当前所有非空物品实例；调用方拿到的是快照数组，不能通过它修改库存。 */
	TArray<UCatInventoryItemInstance*> GetAllItems() const;

	/** 复制删除前更新观察数量；完整列表通知延后到删除真正完成后。 */
	void PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize);

	/** 复制新增回调；客户端在这里补齐本地格子 owner 和物品运行宿主。 */
	void PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize);

	/** 复制变更回调；客户端在这里更新观察数量和物品归属。 */
	void PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize);

	/** 一批复制及延迟对象映射结束后通知组件；此时数组已经完成增删，Model 和 UI 可以读取最终槽位列表。 */
	void PostReplicatedReceive(const FFastArraySerializer::FPostReplicatedReceiveParameters& Parameters);

	/** FastArray delta 序列化入口；只复制 Entries 的变化，不额外复制派生缓存。 */
	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms);

	/** 当前库存组件持有的全部格子；数组下标就是交互和 UI 显示的槽位。 */
	UPROPERTY()
	TArray<FCatInventoryEntry> Entries;

	/** 拥有这份列表的库存组件；复制回调依靠它发出本地变化通知。 */
	UPROPERTY(NotReplicated)
	TObjectPtr<UCatInventoryComponent> OwnerComponent = nullptr;
};

/** Unreal 结构体 traits 声明这份库存列表走 FastArray delta 序列化；没有它时 Entries 会失去按条目增删改复制的语义。 */
template<>
struct TStructOpsTypeTraits<FCatInventoryList> : public TStructOpsTypeTraitsBase2<FCatInventoryList>
{
	enum { WithNetDeltaSerializer = true };
};

/** Catfishing 的正式库存组件；它负责格子、实例、统一收货、使用扣量和跨库存交换。 */
UCLASS(ClassGroup = (Catfishing), meta = (BlueprintSpawnableComponent))
class CATFISHING_API UCatInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 库存观察变化的无参通知类型；监听者收到后必须重新读取库存快照，而不是从事件载荷拿可写事实。 */
	DECLARE_MULTICAST_DELEGATE(FOnInventoryObservedChanged);

	/** 构造库存组件并开启复制；格子数量由 NumSlots 在初始化时补齐。 */
	UCatInventoryComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** 取得本库存的本地显示 Model；首次打开 UI 时创建并填入当前列表，此后由库存变化通知更新。 */
	UCatInventoryModel* GetInventoryModel();

	/** 组件初始化要补齐 owner 和空槽；只扩容不截断，避免蓝图改小容量时运行期丢物品。 */
	virtual void InitializeComponent() override;

	/** BeginPlay 时再次刷新槽位；处理蓝图默认值或运行期构造顺序导致的延迟配置。 */
	virtual void BeginPlay() override;

	/** 复制声明流程：注册 FastArray 库存列表；实例对象走 registered subobject list，终态缓存和本地通知不复制。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 复制就绪后登记当前所有物品实例；客户端才能从 FastArray 指针解析到具体实例。 */
	virtual void ReadyForReplication() override;

	/** 复制子对象入口；使用 registered subobject list，函数保留给引擎复制管线调用。 */
	virtual bool ReplicateSubobjects(UActorChannel* Channel, FOutBunch* Bunch, FReplicationFlags* RepFlags) override;

	/** 按定义资产把数量写入服务器正式库存；返回第一份被接收的实例，InOutCount 和 bOutFullyAdded 交回剩余数量，调用方可选择是否立即广播变化。 */
	UCatInventoryItemInstance* AddEntry(UCatInventoryItemDefinition* ItemDefinition,
		int32& InOutCount, bool& bOutFullyAdded,
		TSubclassOf<UCatInventoryItemInstance> ItemInstanceClass = nullptr,
		bool bBroadcastChange = true);

	/** 按现有实例把数量写入服务器正式库存；首个新格保留传入实例，剩余数量和变化通知时机由调用方通过输出参数和 bBroadcastChange 接收。 */
	void AddEntry(UCatInventoryItemInstance* ItemInstance, int32& InOutCount, bool& bOutFullyAdded,
		bool bBroadcastChange = true);

	/** 查找当前最适合接收指定实例的槽位；优先可堆叠格，其次空格，找不到返回 INDEX_NONE。 */
	int32 FindAvailableSlot(UCatInventoryItemInstance* ItemInstance, int32 Count) const;

	/** 按 NumSlots 补齐库存格；不会删除超过配置数量的已有格子，避免运行期丢物品。 */
	virtual void InitializeOrRefreshInventorySlots();

	/** 移除所有持有指定实例的格子；清空后会在安全时解除实例复制登记并广播库存变化。 */
	void RemoveEntry(UCatInventoryItemInstance* ItemInstance);

	/** 非空实例快照只给 UI 和调试读取；调用方不能拿这个数组绕过服务器事务。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory", BlueprintPure = false)
	TArray<UCatInventoryItemInstance*> GetAllItems() const;

	/** 格子快照是只读观察面；写入仍走组件事务，避免 UI 直接改复制事实。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory", BlueprintPure = false)
	TArray<FCatInventoryEntry> GetInventoryEntries() const;

	/** authority 把当前库存拥有的可持久化 entry 导出为保存输入；空格只能是空实例与零数量，Save 用它序列化 visible slots 和 held entries。 */
	bool ExportInventorySlotsFromAuthority(TArray<FCatInventoryEntry>& OutSlots,
		int32 MaximumSlotCount, FText& OutFailure) const;

	/** authority 从已恢复实例的 entry 替换当前库存；保存系统用它反序列化可见格、验证容器槽位规则，并拒绝活动区仍有未收口实例的覆盖。 */
	bool RestoreInventorySlotsFromAuthority(const TArray<FCatInventoryEntry>& RestoredSlots,
		int32 MinimumSlotCount, FText& OutFailure);

	/** 服务器按现有实例正式入库；调用前建议用批次预检避免部分写入。 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Catfishing|Inventory")
	bool AddItemInstance(UCatInventoryItemInstance* ItemInstance, int32 Count);

	/** 服务器按定义资产正式生成并入库；调用前建议用批次预检避免部分写入。 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Catfishing|Inventory")
	bool AddItemDefinition(UCatInventoryItemDefinition* ItemDefinition, int32 Count,
		TSubclassOf<UCatInventoryItemInstance> ItemInstanceClass = nullptr);

	/** 整批收货必须先做容量预演；失败时不触碰正式库存，避免半批成功。 */
	bool CanFullyAcceptInventoryBatch(const FCatInventoryReceiveBatch& ReceiveBatch) const;

	/** 先预检整批载荷，再把它写入当前库存组件；成功后只由这一份组件成为事实源。 */
	bool TryAddInventoryBatch(const FCatInventoryReceiveBatch& ReceiveBatch);

	/** 只读预检稳定物品 ID 能否进入当前正式库存；商店、奖励和初始化发货用它在提交前确认目录、authority 和容量。 */
	ECatDomainCommandError ValidateInventoryDefinitionGrantFromAuthority(FGuid RequestId, FName DefinitionId,
		int32 Count) const;

	/** authority 按稳定物品 ID 向当前正式库存发货；库存组件负责目录解析、幂等和整批写入，并返回提交状态与错误码。 */
	FCatDomainCommandResult GrantInventoryDefinitionFromAuthority(FGuid RequestId, FName DefinitionId, int32 Count);

	/** 只读预检已经解析出的物品定义能否进入当前正式库存；调用方用它把容量和堆叠裁决交回 Inventory。 */
	ECatDomainCommandError ValidateResolvedInventoryDefinitionGrantFromAuthority(
		FGuid RequestId, UCatInventoryItemDefinition* ItemDefinition, int32 Count) const;

	/** authority 按已经解析出的物品定义发货；调用方只提供业务载荷，库存按当前条目、容量和幂等缓存裁决。 */
	FCatDomainCommandResult GrantResolvedInventoryDefinitionFromAuthority(FGuid RequestId,
		UCatInventoryItemDefinition* ItemDefinition, int32 Count);

	/** 只读预检一批已解析定义能否完整进入当前正式库存；调用方用它在扣款、奖励结算等不可逆动作前确认容量。 */
	ECatDomainCommandError ValidateInventoryDefinitionBatchGrantFromAuthority(FGuid RequestId,
		const FString& IdempotencyPayloadContext,
		const FCatInventoryReceiveBatch& ReceiveBatch) const;

	/** authority 按一批已解析定义发货；正式库存先物化实例批次，再负责幂等、容量预演、变化广播和成功重放。 */
	FCatDomainCommandResult GrantInventoryDefinitionBatchFromAuthority(FGuid RequestId,
		const FString& IdempotencyPayloadContext,
		const FCatInventoryReceiveBatch& ReceiveBatch);

	/** Actor 级收货用这个开关区分公共入口和专用容器；避免外部系统误把所有库存都当默认背包。 */
	bool CanReceiveUnifiedInventoryIntake() const;

	/** 读取 Actor 级统一收货优先级；数值越大越先尝试接收整批物品。 */
	int32 GetUnifiedInventoryIntakePriority() const;

	/** authority 按外部配置刷新槽位容量；只补齐新增空槽，不因容量变小删除已有物品。 */
	void SetInventorySlotCountFromAuthority(int32 NewSlotCount);

	/** authority 用一份完整槽位快照替换当前库存；存档恢复靠它保留格子顺序。 */
	bool ReplaceInventoryEntriesFromAuthority(const TArray<FCatInventoryEntry>& NewEntries, int32 MinimumSlotCount);

	/** 按实例移除物品；实例完全离开当前库存后会解除复制子对象登记。 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Catfishing|Inventory")
	void RemoveItemInstance(UCatInventoryItemInstance* ItemInstance);

	/** 按槽位清空物品；调用方必须已确认这是允许丢弃或转移的库存实例命令。 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Catfishing|Inventory")
	void RemoveItemInstanceFromIndex(int32 TargetIndex);

	/** authority 从指定槽位移出完整 entry；部署、跨容器转移等需要保留实例身份的流程用它接走正式库存事实。 */
	bool RemoveInventoryEntryAtSlotFromAuthority(int32 TargetIndex, FCatInventoryEntry& OutRemovedEntry);

	/** authority 把指定槽位完整借出到库存内部活动区；部署型物品用它离开可见格子，收回时必须像普通拾取一样重新占用真实空位。 */
	bool HoldInventoryEntryAtSlotFromAuthority(int32 SlotIndex, FCatInventoryEntry& OutHeldEntry);

	/** authority 按实例身份借出部署型物品；正式库存负责服务器校验、槽位解析和 held entry，返回值交给部署/回滚调用方串联同一实例。 */
	FCatDomainCommandResult HoldInventoryItemInstanceFromAuthority(FGuid RequestId, FGuid ItemInstanceId, FCatInventoryEntry& OutHeldEntry);

	/** authority 按实例执行正式库存 Use；库存负责幂等、定义裁决、扣量/借出和回滚，调用方只补自己的提交后刷新。 */
	FCatInventoryItemUseResult UseItemInstanceFromAuthority(FGuid RequestId, FGuid ItemInstanceId, int32 Quantity, const FString& IdempotencyPayloadContext,
		TFunctionRef<ECatDomainCommandError(FCatInventoryItemUseResult&)> ValidateBeforeMutation,
		TFunctionRef<bool(FCatInventoryItemUseResult&)> FinalizeCommittedUse);

	/** 只读查询正式库存 Use 是否已有终态；命中时返回首次结果的可诊断重放，不重新读取当前槽位。 */
	bool TryReplayItemUseTerminalFromAuthority(FGuid RequestId, FGuid ItemInstanceId, int32 Quantity,
		const FString& IdempotencyPayloadContext, FCatInventoryItemUseResult& OutResult) const;

	/** authority 按实例身份把部署型物品从活动区归还可见库存；收杆和 Use 回滚只消费结构化结果，库存负责同一实例、容量、复制和变化通知，且不缓存终态以便外层失败后重新借回。 */
	FCatDomainCommandResult ReturnHeldInventoryItemInstanceFromAuthority(FGuid RequestId, FGuid ItemInstanceId,
		int32 MinimumSlotCount, FCatInventoryEntry& OutReturnedEntry);

	/** authority 按实例执行正式库存 UnUse；库存负责从活动区归还同一实例、终态重放和失败回滚，调用方只补自己的读模型同步。 */
	FCatInventoryItemUseResult UnUseItemInstanceFromAuthority(FGuid RequestId, FGuid ItemInstanceId,
		int32 MinimumSlotCount, const FString& IdempotencyPayloadContext,
		TFunctionRef<bool(FCatInventoryItemUseResult&)> FinalizeCommittedUnUse);

	/** authority 把活动区里同一不可堆叠实例归还当前库存；这是部署型物品收回时复用本组件容量规则的低层拼装点。 */
	bool ReturnHeldInventoryEntryFromAuthority(FGuid ItemInstanceId, int32 MinimumSlotCount,
		FCatInventoryEntry& OutReturnedEntry);

	/** authority 用保存的单实例 entry 重建活动区记录；只供外层归还后失败回滚，成功后可见库存不应再持有该实例。 */
	bool RestoreHeldInventoryEntryForRollbackFromAuthority(const FCatInventoryEntry& HeldEntry);

	/** authority 退役活动区里的同一实例；存档已接管部署物时用它清掉本库存活动区保管记录。 */
	bool RetireHeldInventoryEntryFromAuthority(FGuid ItemInstanceId);
	/** Transfer exact deployed instances during owner teardown; callers rebind consumers before publishing. */
	bool MoveHeldInventoryEntriesToCustodianFromAuthority(UCatInventoryComponent* Target, const TArray<FGuid>& ItemInstanceIds);

	/** authority 读取活动区里某个实例的可写 entry；调用方只能用于同一服务器事务内同步运行状态。 */
	FCatInventoryEntry* FindHeldInventoryEntryFromAuthority(FGuid ItemInstanceId);

	/** authority 读取活动区里某个实例的只读 entry；导出或预检只需要观察时用它避免暴露可写库存格。 */
	const FCatInventoryEntry* FindHeldInventoryEntryFromAuthority(FGuid ItemInstanceId) const;

	/** authority 把当前活动区里的 held entry 追加到输出快照；存档导出用它读取库存正在保管的部署型实例。 */
	void AppendHeldInventoryEntriesFromAuthority(TArray<FCatInventoryEntry>& OutEntries) const;

	/** authority 查询库存活动区是否仍有部署型实例；恢复和失败预算用它判断库存是否处于可写空闲态。 */
	bool HasActiveHeldInventoryEntriesFromAuthority() const;

	/** 从指定格扣除数量；数量归零时清空格子并在安全时解除实例复制登记。 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Catfishing|Inventory")
	bool ConsumeItemAtSlot(int32 SlotIndex, int32 ConsumeCount);

	/** 槽位合法性只代表数组边界成立；空槽也能参与拖放，避免 UI 把空目标格拒掉。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Inventory")
	bool IsValidInventorySlotIndex(int32 SlotIndex) const;

	/** 占用状态要求实例和数量同时有效；避免坏复制状态被当成可用物品。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Inventory")
	bool HasItemAtSlot(int32 SlotIndex) const;

	/** 按实例查找所在槽位；没有找到或实例为空时返回 INDEX_NONE。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Inventory")
	int32 FindInventorySlotIndexFromInstance(const UCatInventoryItemInstance* ItemInstance) const;

	/** 按稳定实例 ID 查找所在槽位；Equipment 读模型和网络命令只拿到 ID 时用它回到正式库存格。 */
	int32 FindInventorySlotIndexFromInstanceId(FGuid ItemInstanceId) const;

	/** 按稳定定义 ID 查找第一格可消费库存；材料扣除和库存可用性判断用它回到正式库存事实。 */
	int32 FindFirstInventorySlotIndexByDefinitionId(FName DefinitionId) const;

	/** 当前可见库存数量表示玩家背包格里仍可整理、可选择的同定义总数；库存可用性判断读取它，不包含 held 活动区、Fishing 会话冻结或其他已离开可见槽位的实例。 */
	int32 CountVisibleInventoryQuantityByDefinitionId(FName DefinitionId) const;

	/** 读取当前库存槽位数量；用于 UI 创建格子和交换操作校验下标。 */
	int32 GetInventorySlotCount() const;

	/** 只读取得某个槽位条目；越界时返回空指针，调用方不得保存为可写引用。 */
	const FCatInventoryEntry* GetInventoryEntryAtSlot(int32 SlotIndex) const;

	/** Use 预检交给实例语义决定；默认使用拥有者 Pawn，库存核心不认识 GAS、装备、草药或窝料目标。 */
	bool CanUseItemAtSlot(int32 SlotIndex, APawn* UserPawn = nullptr) const;

	/** 服务器用结构化上下文使用指定槽位；库存组件先裁决 RequestId、当前槽位和物品实例，再把真实效果交给实例侧流程。 */
	FCatDomainCommandResult UseItemAtSlotFromAuthority(const FCatInventoryItemUseContext& UseContext);

	/** authority 把当前组件的一个槽位移动、合并或交换到另一个正式库存；组件负责幂等、格子写入和变化通知。 */
	FCatDomainCommandResult MoveItemToInventoryFromAuthority(FGuid RequestId,
		int32 SourceSlotIndex,
		UCatInventoryComponent* TargetInventory, int32 TargetSlotIndex, const FString& IdempotencyPayloadContext);

	/** 在服务器上执行一次拖放交换或堆叠合并；调用方必须已经位于 authority 路径。 */
	static bool ExecuteExchangeRequestOnAuthority(UCatInventoryComponent* DraggedInventory, int32 DraggedSlotIndex,
		UCatInventoryComponent* DropInventory, int32 DropSlotIndex);

	/** 按实例返回一份格子快照；找不到时返回空格，不给调用方可写引用。 */
	FCatInventoryEntry FindInventoryEntryFromInstance(UCatInventoryItemInstance* ItemInstance) const;

	/** 当库存复制或服务器提交发生变化时触发；UI 和适配层收到后重新读取完整库存。 */
	FOnInventoryObservedChanged OnInventoryObservedChanged;

	/** 广播本地库存变化；服务器提交和客户端复制最终都收敛到这一个通知。 */
	virtual void BroadcastInventoryChange(int32 ChangedIndex = INDEX_NONE);

	/** 槽位接收规则默认保持通用背包语义；装备栏子类可按标签收窄，避免库存核心硬编码装备类别。 */
	virtual bool CanAcceptInventoryEntryAtSlot(const FCatInventoryEntry& IncomingEntry, int32 TargetSlotIndex) const;

protected:
	friend class UCatEquipmentComponent;
	/** Internal mutation lets the fishing coordinator establish its record before notifying observers. */
	bool ConsumeItemAtSlotInternal(int32 SlotIndex, int32 ConsumeCount, bool bBroadcastChange);
	bool TryAddInventoryBatchInternal(const FCatInventoryReceiveBatch& ReceiveBatch, bool bBroadcastChange);
	/** 本库存独立的显示 Model；组件按需创建并持有，服务器本地提交或客户端复制后更新，其他库存不会写入它。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatInventoryModel> InventoryModel;

	/** 定义批次接收规则默认保持通用背包语义；不创建实例的容量预演用它和正式入库保持同一槽位口径。 */
	virtual bool CanAcceptInventoryDefinitionAtSlot(const UCatInventoryItemDefinition& IncomingDefinition,
		int32 TargetSlotIndex) const;

	/** 容量预演里的轻量格子；它只保存定义和数量，不创建运行实例或触发复制。 */
	struct FSimulatedInventorySlot
	{
		/** 预演格子里当前物品的静态定义；空指针表示这个模拟格为空。 */
		const UCatInventoryItemDefinition* ItemDefinition = nullptr;

		/** 预演格子里当前数量；只服务容量计算，不写回正式库存。 */
		int32 StackCount = 0;
	};

	/** 库存交换的内部结果；公开命令用它避免同一套移动规则复制两份。 */
	struct FInventoryExchangeMutation
	{
		/** 本次交换的领域错误；None 只和真实格子变化一起出现。 */
		ECatDomainCommandError Error = ECatDomainCommandError::InvalidPayload;

		/** 格子数组是否已经被修改；调用方据此决定是否广播变化。 */
		bool bChanged = false;

	};

	/** 解析定义资产和实例类型并创建真正入库的物品实例；失败时不修改任何格子。 */
	UCatInventoryItemInstance* CreateInventoryItemInstance(
		UCatInventoryItemDefinition* ItemDefinition,
		TSubclassOf<UCatInventoryItemInstance> ItemInstanceClass = nullptr);

	/** 在模拟格子数组里尝试放入指定数量的同类定义；成功时扣减 InOutRemainingCount。 */
	bool SimulateAddItemDefinition(TArray<FSimulatedInventorySlot>& SimulatedSlots,
		const UCatInventoryItemDefinition& ItemDefinition, int32& InOutRemainingCount) const;

	/** 用整批输入预演当前库存能否完整接收；成功时不会创建实例，也不会改变正式库存。 */
	bool SimulateAddInventoryBatch(const FCatInventoryReceiveBatch& ReceiveBatch,
		TArray<FSimulatedInventorySlot>& SimulatedSlots) const;

	/** 稳定物品发货预检的共用裁决；调用方可以来自目录 ID 或已解析定义，但最终都按同一份库存载荷签名回答。 */
	ECatDomainCommandError ValidateInventoryDefinitionGrantFromAuthorityInternal(
		FGuid RequestId, FName DefinitionId, UCatInventoryItemDefinition* ItemDefinition, int32 Count) const;

	/** 稳定物品发货提交的共用写入口；它是正式库存写入的唯一实现，外层系统只负责把自己的业务意图解析成库存定义。 */
	FCatDomainCommandResult GrantInventoryDefinitionFromAuthorityInternal(
		FGuid RequestId, FName DefinitionId,
		UCatInventoryItemDefinition* ItemDefinition, int32 Count);

	/** 读取某个定义的有效堆叠上限；集中处理非法配置，确保预演和正式入库口径一致。 */
	int32 GetMaxStackCountForDefinition(const UCatInventoryItemDefinition& ItemDefinition) const;

	/** 实例复制登记只有在最后一个槽位放手后才能解除；避免同实例多格引用时客户端丢对象。 */
	bool IsItemInstanceReferencedByOtherSlots(const UCatInventoryItemInstance* ItemInstance,
		int32 IgnoredSlotIndex) const;

	/** 更新物品实例在两个库存组件的复制登记；跨组件移动时确保只有当前持有者负责复制。 */
	void UpdateReplicatedItemRegistration(UCatInventoryItemInstance* ItemInstance,
		UCatInventoryComponent* PreviousOwnerComponent, UCatInventoryComponent* NewOwnerComponent);

	/** 把实例的运行宿主同步成当前库存拥有者；交换、收货和复制回调都通过它收束归属。 */
	void SyncInventoryItemRuntimeOwner(UCatInventoryItemInstance* ItemInstance) const;

	/** 判断传入整表是否和当前库存内容一致；只比较格位、实例和数量，避免外部同步制造无意义广播。 */
	bool AreInventoryEntriesEquivalent(const TArray<FCatInventoryEntry>& NewEntries,
		int32 MinimumSlotCount) const;

	/** 复用正式交换规则但暂不广播；返回结构化错误和变更标记，让命令入口统一通知读者重读。 */
	static FInventoryExchangeMutation ExecuteExchangeRequestOnAuthorityInternal(
		UCatInventoryComponent* DraggedInventory, int32 DraggedSlotIndex,
		UCatInventoryComponent* DropInventory, int32 DropSlotIndex);

	/** 构造库存命令幂等键；缓存只在当前组件生命周期内保护重复命令。 */
	static FString MakeTerminalKey(const TCHAR* Operation, FGuid RequestId);

	/** 当前库存的复制格子列表；它是组件内部的唯一库存事实源。 */
	UPROPERTY(Replicated)
	FCatInventoryList InventoryList;

	/** 配置声明的槽位数；初始化只补齐空格，不会因为配置变小而删除已有物品。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "InventoryConfig")
	int32 NumSlots = 0;

	/** 是否允许 Actor 级统一收货入口选择本组件；专用仓库可关闭它只接受显式调用。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "InventoryConfig")
	bool bAllowUnifiedInventoryIntake = true;

	/** Actor 级统一收货优先级；数值越大越先尝试完整接收整批物品。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "InventoryConfig")
	int32 UnifiedInventoryIntakePriority = 0;

	/** 当前从本库存借出但尚未归还或退役的不可堆叠单实例；键是实例身份，值是唯一的完整 entry，部署与收回都只操作这份所有权记录。 */
	UPROPERTY(Transient)
	TMap<FGuid, FCatInventoryEntry> ActiveHeldItemEntries;

	/** 物品 Use/UnUse 的首次终态缓存；Transient 反射引用会保活回包 entry 中的实例，重复 RequestId 只重放原结果，不重新扣量、借出或归还实例。 */
	UPROPERTY(Transient)
	TMap<FString, FCatInventoryItemUseResult> InventoryItemUseTerminalCache;

	/** 普通库存命令首次终态缓存；重复 RequestId 只返回首次结果，避免重复整理格子。 */
	TMap<FString, FCatDomainCommandResult> TerminalCache;

	/** 库存命令载荷签名；同一 RequestId 换槽位、目标或物品载荷会被拒绝，避免客户端复用幂等键改写新意图。 */
	TMap<FString, FString> TerminalPayloadByKey;
};
