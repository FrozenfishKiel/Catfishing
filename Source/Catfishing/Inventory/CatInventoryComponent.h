#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "Inventory/CatInventoryInterface.h"
#include "Inventory/CatInventoryStatics.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "CatInventoryComponent.generated.h"

class APawn;
class UActorChannel;
class UCatInventoryItemDefinition;
class UCatInventoryItemInstance;
class FOutBunch;
struct FReplicationFlags;

/** 一个库存格的复制条目；格子只记录实例指针和堆叠数量，不持有 GAS、Fishing 或 UI 的下游状态。 */
USTRUCT(BlueprintType)
struct FCatInventoryEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** 构造一个空格；SlotOwnerComponent 会在库存组件初始化、复制或跨组件移动时补齐。 */
	FCatInventoryEntry() = default;

	/** 构造一个归属于指定库存组件的空格；用于清空格子后仍保留明确所有者。 */
	explicit FCatInventoryEntry(UCatInventoryComponent* InSlotOwnerComponent);

	/** 格子身份只由实例指针决定；这样堆叠数量变化不会触发错误的换物判断。 */
	bool operator==(const FCatInventoryEntry& Other) const;

	/** 空格不能和空实例互相匹配；避免清理路径把两个空指针当作同一件物品。 */
	bool operator==(const UCatInventoryItemInstance* InInstance) const;

	/** 反向实例匹配复用同一套空值规则；避免移动和删除分支出现第二套判断口径。 */
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

	/** 复制删除回调；客户端用它刷新观察数量并通知 UI 或适配层重读库存。 */
	void PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize);

	/** 复制新增回调；客户端在这里补齐本地格子 owner 并广播库存变化。 */
	void PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize);

	/** 复制变更回调；客户端在这里更新观察数量并广播库存变化。 */
	void PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize);

	/** FastArray delta 序列化入口；只复制 Entries 的变化，不额外复制派生缓存。 */
	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms);

	/** 当前库存组件持有的全部格子；数组下标就是交互和 UI 显示的槽位。 */
	UPROPERTY()
	TArray<FCatInventoryEntry> Entries;

	/** 拥有这份列表的库存组件；复制回调依靠它发出本地变化通知。 */
	UPROPERTY(NotReplicated)
	TObjectPtr<UCatInventoryComponent> OwnerComponent = nullptr;
};

template<>
struct TStructOpsTypeTraits<FCatInventoryList> : public TStructOpsTypeTraitsBase2<FCatInventoryList>
{
	enum { WithNetDeltaSerializer = true };
};

/** Aegis 风格库存组件的 Catfishing 适配版；它负责格子、实例、统一收货、使用扣量和跨库存交换。 */
UCLASS(ClassGroup = (Catfishing), meta = (BlueprintSpawnableComponent))
class CATFISHING_API UCatInventoryComponent : public UActorComponent, public ICatInventoryInterface
{
	GENERATED_BODY()

public:
	DECLARE_MULTICAST_DELEGATE(FOnInventoryObservedChanged);

	/** 构造库存组件并开启复制；格子数量由 NumSlots 在初始化时补齐。 */
	UCatInventoryComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** 组件初始化要补齐 owner 和空槽；只扩容不截断，避免蓝图改小容量时运行期丢物品。 */
	virtual void InitializeComponent() override;

	/** BeginPlay 时再次刷新槽位；处理蓝图默认值或运行期构造顺序导致的延迟配置。 */
	virtual void BeginPlay() override;

	/** 复制声明流程：注册 FastArray 库存列表和内容版本；实例对象走 registered subobject list，终态缓存和本地通知不复制。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 复制就绪后登记当前所有物品实例；客户端才能从 FastArray 指针解析到具体实例。 */
	virtual void ReadyForReplication() override;

	/** 复制子对象入口；使用 registered subobject list，函数保留给引擎复制管线调用。 */
	virtual bool ReplicateSubobjects(UActorChannel* Channel, FOutBunch* Bunch, FReplicationFlags* RepFlags) override;

	/** 由库存接口暴露自身；统一收货入口据此发现组件。 */
	virtual UCatInventoryComponent* GetInventoryComponent() override;

	/** 按定义资产把数量写入服务器正式库存；返回第一份被接收的实例，InOutCount 和 bOutFullyAdded 交回剩余数量，调用方可选择是否立即提交版本。 */
	UCatInventoryItemInstance* AddEntry(UCatInventoryItemDefinition* ItemDefinition,
		int32& InOutCount, bool& bOutFullyAdded,
		TSubclassOf<UCatInventoryItemInstance> ItemInstanceClass = nullptr,
		bool bAdvanceRevision = true);

	/** 按现有实例把数量写入服务器正式库存；首个新格保留传入实例，剩余数量和版本提交时机由调用方通过输出参数和 bAdvanceRevision 接收。 */
	void AddEntry(UCatInventoryItemInstance* ItemInstance, int32& InOutCount, bool& bOutFullyAdded,
		bool bAdvanceRevision = true);

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

	/** 当前库存内容版本；玩家整理、收货和扣量都会推进它，钓鱼选择变化不会污染这份背包版本。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Inventory")
	int64 GetInventoryRevision() const;

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

	/** authority 归还已经从本库存预留出去的批次；普通入库放不下时可补少量返还格，避免收口流程吞掉预留物。 */
	bool TryReturnReservedInventoryBatchFromAuthority(
		const FCatInventoryReceiveBatch& ReceiveBatch, int32 OverflowSlotCount);

	/** 服务器整理本库存里的两个格子；RequestId 和库存 Revision 在正式库存层裁决，返回提交状态、错误和最新库存版本。 */
	FCatDomainCommandResult MoveInventorySlotFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		int32 SourceSlotIndex, int32 TargetSlotIndex);

	/** Actor 级收货用这个开关区分公共入口和专用容器；避免外部系统误把所有库存都当默认背包。 */
	bool CanReceiveUnifiedInventoryIntake() const;

	/** 读取 Actor 级统一收货优先级；数值越大越先尝试接收整批物品。 */
	int32 GetUnifiedInventoryIntakePriority() const;

	/** authority 按外部配置刷新槽位容量；只补齐新增空槽，不因容量变小删除已有物品。 */
	void SetInventorySlotCountFromAuthority(int32 NewSlotCount);

	/** authority 用一份完整槽位快照替换当前库存；存档恢复和旧结构迁移靠它保留格子顺序。 */
	bool ReplaceInventoryEntriesFromAuthority(const TArray<FCatInventoryEntry>& NewEntries, int32 MinimumSlotCount);

	/** 按实例移除物品；实例完全离开当前库存后会解除复制子对象登记。 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Catfishing|Inventory")
	void RemoveItemInstance(UCatInventoryItemInstance* ItemInstance);

	/** 按槽位清空物品；调用方必须已确认这是允许丢弃或迁移的库存事务。 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Catfishing|Inventory")
	void RemoveItemInstanceFromIndex(int32 TargetIndex);

	/** authority 从指定槽位移出完整 entry；部署、跨容器转移等需要保留实例身份的流程用它接走正式库存事实。 */
	bool RemoveInventoryEntryAtSlotFromAuthority(int32 TargetIndex, FCatInventoryEntry& OutRemovedEntry);

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

	/** 按稳定实例 ID 查找所在槽位；旧 Equipment 投影和网络命令只拿到 ID 时用它回到正式库存格。 */
	int32 FindInventorySlotIndexFromInstanceId(FGuid ItemInstanceId) const;

	/** 读取当前库存槽位数量；用于 UI 创建格子和交换操作校验下标。 */
	int32 GetInventorySlotCount() const;

	/** 只读取得某个槽位条目；越界时返回空指针，调用方不得保存为可写引用。 */
	const FCatInventoryEntry* GetInventoryEntryAtSlot(int32 SlotIndex) const;

	/** Use 预检交给实例语义决定；默认使用拥有者 Pawn，避免库存核心知道具体玩法系统。 */
	bool CanUseItemAtSlot(int32 SlotIndex, APawn* UserPawn = nullptr) const;

	/** 从指定槽位发起库存 Use；客户端请求会转到服务器，服务器按实例语义扣量。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Inventory")
	bool TryUseItemAtSlot(int32 SlotIndex, APawn* UserPawn = nullptr);

	/** 客户端请求服务器执行跨库存交换；真正写入仍由服务器再次校验。 */
	UFUNCTION(Server, Reliable)
	void ServerExchangeInventorySlot(UCatInventoryComponent* DropInventory, int32 DraggedSlotIndex,
		int32 DropSlotIndex);

	/** 拖放交换先做无副作用校验；避免客户端请求或 UI 预检直接改写库存事实。 */
	static bool CanExecuteExchangeRequest(UCatInventoryComponent* DraggedInventory, int32 DraggedSlotIndex,
		UCatInventoryComponent* DropInventory, int32 DropSlotIndex);

	/** 在服务器上执行一次拖放交换或堆叠合并；调用方必须已经位于 authority 路径。 */
	static bool ExecuteExchangeRequestOnAuthority(UCatInventoryComponent* DraggedInventory, int32 DraggedSlotIndex,
		UCatInventoryComponent* DropInventory, int32 DropSlotIndex);

	/** 发起一次拖放交换；客户端走 RPC，服务器或单机直接执行 authority 写入。 */
	static bool ExecuteExchangeRequest(UCatInventoryComponent* DraggedInventory, int32 DraggedSlotIndex,
		UCatInventoryComponent* DropInventory, int32 DropSlotIndex);

	/** 按实例返回一份格子快照；找不到时返回空格，不给调用方可写引用。 */
	FCatInventoryEntry FindInventoryEntryFromInstance(UCatInventoryItemInstance* ItemInstance) const;

	/** 当库存复制或服务器提交发生变化时触发；UI 和适配层收到后重新读取完整库存。 */
	FOnInventoryObservedChanged OnInventoryObservedChanged;

	/** 客户端删除格子的表现扩展点；默认只依赖统一变化通知，子类可补 UI 特效。 */
	virtual void BroadcastInventoryRemoveOnClient(const TArrayView<int32> RemovedIndices, int32 FinalSize);

	/** 客户端新增格子的表现扩展点；默认只依赖统一变化通知，子类可补获取提示。 */
	virtual void BroadcastInventoryAddOnClient(const TArrayView<int32> AddedIndices, int32 FinalSize,
		const TArray<FCatInventoryEntry>& TargetList);

	/** 客户端变更格子的表现扩展点；默认只依赖统一变化通知，子类可补高亮刷新。 */
	virtual void BroadcastInventoryChangeOnClient(const TArrayView<int32> ChangedIndices, int32 FinalSize);

	/** 广播本地库存变化；服务器提交和客户端复制最终都收敛到这一个通知。 */
	virtual void BroadcastInventoryChange(int32 ChangedIndex = INDEX_NONE);

	/** 槽位接收规则默认保持通用背包语义；装备栏子类可按标签收窄，避免库存核心硬编码装备类别。 */
	virtual bool CanAcceptInventoryEntryAtSlot(const FCatInventoryEntry& IncomingEntry, int32 TargetSlotIndex) const;

protected:
	/** 容量预演里的轻量格子；它只保存定义和数量，不创建运行实例或触发复制。 */
	struct FSimulatedInventorySlot
	{
		/** 预演格子里当前物品的静态定义；空指针表示这个模拟格为空。 */
		const UCatInventoryItemDefinition* ItemDefinition = nullptr;

		/** 预演格子里当前数量；只服务容量计算，不写回正式库存。 */
		int32 StackCount = 0;
	};

	/** 库存交换的内部结果；公开命令和旧 bool 入口共用它，避免同一套移动规则复制两份。 */
	struct FInventoryExchangeMutation
	{
		/** 本次交换的领域错误；None 只和真实格子变化一起出现。 */
		ECatDomainCommandError Error = ECatDomainCommandError::InvalidPayload;

		/** 格子数组是否已经被修改；调用方据此决定是否推进 Revision 和广播。 */
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

	/** 判断传入整表是否和当前库存内容一致；只比较格位、实例和数量，不让外部同步无故推进版本。 */
	bool AreInventoryEntriesEquivalent(const TArray<FCatInventoryEntry>& NewEntries,
		int32 MinimumSlotCount) const;

	/** authority 库存内容发生变化时推进版本并请求拥有 Actor 复制；客户端不能本地制造新版本。 */
	void AdvanceInventoryRevisionFromAuthority();

	/** 复用正式交换规则但暂不广播；返回结构化错误和变更标记，让命令入口先推进版本再统一通知读者重读。 */
	static FInventoryExchangeMutation ExecuteExchangeRequestOnAuthorityInternal(
		UCatInventoryComponent* DraggedInventory, int32 DraggedSlotIndex,
		UCatInventoryComponent* DropInventory, int32 DropSlotIndex);

	/** 构造库存命令幂等键；缓存只在当前组件生命周期内保护重复 RPC。 */
	static FString MakeTerminalKey(const TCHAR* Operation, FGuid RequestId);

	/** 库存版本复制到客户端时触发一次重读；FastArray 与版本字段到达顺序不稳定，因此刷新必须可重复。 */
	UFUNCTION()
	void OnRep_InventoryRevision();

	/** 客户端请求服务器使用槽位；服务器实现会重新走 TryUseItemAtSlot 的 authority 校验。 */
	UFUNCTION(Server, Reliable)
	void ServerTryUseItemAtSlot(int32 SlotIndex, APawn* UserPawn);

	/** 当前库存的复制格子列表；它是组件内部的唯一库存事实源。 */
	UPROPERTY(Replicated)
	FCatInventoryList InventoryList;

	/** 当前库存内容的乐观并发版本；服务器提交库存内容变化时递增，客户端只拿它回传命令前提。 */
	UPROPERTY(ReplicatedUsing = OnRep_InventoryRevision, BlueprintReadOnly, Category = "InventoryConfig",
		meta = (AllowPrivateAccess = "true"))
	int64 InventoryRevision = 0;

	/** 配置声明的槽位数；初始化只补齐空格，不会因为配置变小而删除已有物品。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "InventoryConfig")
	int32 NumSlots = 0;

	/** 是否允许 Actor 级统一收货入口选择本组件；专用仓库可关闭它只接受显式调用。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "InventoryConfig")
	bool bAllowUnifiedInventoryIntake = true;

	/** Actor 级统一收货优先级；数值越大越先尝试完整接收整批物品。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "InventoryConfig")
	int32 UnifiedInventoryIntakePriority = 0;

	/** 普通库存命令首次终态缓存；重复 RequestId 只返回首次结果，不再次整理格子。 */
	TMap<FString, FCatDomainCommandResult> TerminalCache;

	/** 库存命令载荷签名；同一 RequestId 换槽位或版本会被拒绝，避免客户端复用幂等键改写新意图。 */
	TMap<FString, FString> TerminalPayloadByKey;
};
