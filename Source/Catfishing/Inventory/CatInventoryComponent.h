#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "Inventory/CatInventoryStatics.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Templates/Function.h"
#include "CatInventoryComponent.generated.h"

class APawn;
class ACatCharacter;
class UActorChannel;
class UCatInventoryItemDefinition;
class UCatInventoryItemInstance;
class UCatInventoryModel;
class FOutBunch;
struct FReplicationFlags;
struct FCatInventoryItemUseContext;
enum class ECatInventoryCarryCategory : uint8;

/**
 * 这份库存在团队装备库里扮演的角色。
 *
 * 商店册 §3.1.2：「购买物进入团队装备库：竿和漂放入公共架，其他备装自取，消耗品从公库领取」——
 * 也就是团队装备库有两个去处，不是一个。这个枚举就是那两个去处的运行标识，挂在库存组件上而不是
 * 营地 Actor 上，好让关卡里摆两个营地容器时各自声明自己是哪一个。
 *
 * Unspecified 是默认值，语义是「这份库存没声明角色」。关卡里一个角色都没声明时，商店交付回退到
 * 「全部进同一个公共仓库」的旧行为并记一条 Warning —— 不是把购买判死。
 */
UENUM(BlueprintType)
enum class ECatTeamStorageRole : uint8
{
	/** 未声明角色；参与旧的单一公共仓库回退路径。 */
	Unspecified = 0,
	/** 公共架：竿、漂这类非消耗功能装备的去处。 */
	EquipmentRack = 1,
	/** 公库：饵、窝料这类本局消耗品的去处。 */
	SupplyStore = 2
};

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

	/** 按定义资产接收服务器库存可容纳的数量；返回首个发生增加的格子实例，InOutCount 返回余数，bOutFullyAdded 表示是否全收，通知可延迟。 */
	UCatInventoryItemInstance* AddEntry(UCatInventoryItemDefinition* ItemDefinition,
		int32& InOutCount, bool& bOutFullyAdded,
		TSubclassOf<UCatInventoryItemInstance> ItemInstanceClass = nullptr,
		bool bBroadcastChange = true);

	/** 按现有实例把数量写入服务器正式库存；首个新格保留传入实例，剩余数量和变化通知时机由调用方通过输出参数和 bBroadcastChange 接收。 */
	void AddEntry(UCatInventoryItemInstance* ItemInstance, int32& InOutCount, bool& bOutFullyAdded,
		bool bBroadcastChange = true);

	/** 查找当前最适合接收指定实例的槽位；优先可堆叠格，其次空格，找不到返回 INDEX_NONE。 */
	int32 FindAvailableSlot(UCatInventoryItemInstance* ItemInstance, int32 Count) const;

	/** 按 NumSlots 补齐库存格，不裁掉已有格子；默认在新增格子后广播，关闭通知时由调用方在关联提交完成后发布。 */
	virtual void InitializeOrRefreshInventorySlots(bool bBroadcastChange = true);

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

	/**
	 * authority 整批接收定义或现有实例；空批次成功，容量不足或实例准备失败时不提交本批次，不恢复实例初始化期间其他调用产生的变更。
	 * 全部分配和实例准备完成后才迁移载体并写入正式条目；本入口不执行业务回调，也不缓存请求终态。
	 * 非空批次成功后默认广播一次，拒绝不广播；关闭通知时由调用方在关联资源提交完整后统一发布。
	 */
	bool TryAddInventoryBatch(const FCatInventoryReceiveBatch& ReceiveBatch, bool bBroadcastChange = true);

	/** 只读预检已经解析出的物品定义能否进入当前正式库存；调用方用它把容量和堆叠裁决交回 Inventory。 */
	ECatDomainCommandError ValidateResolvedInventoryDefinitionGrantFromAuthority(
		FGuid RequestId, UCatInventoryItemDefinition* ItemDefinition, int32 Count) const;

	/** authority 按已经解析出的物品定义发货；调用方只提供业务载荷，库存按当前条目、容量和幂等缓存裁决。 */
	FCatDomainCommandResult GrantResolvedInventoryDefinitionFromAuthority(FGuid RequestId,
		UCatInventoryItemDefinition* ItemDefinition, int32 Count);

	/** Actor 级收货用这个开关区分公共入口和专用容器；避免外部系统误把所有库存都当默认背包。 */
	bool CanReceiveUnifiedInventoryIntake() const;

	/** 读取 Actor 级统一收货优先级；数值越大越先尝试接收整批物品。 */
	int32 GetUnifiedInventoryIntakePriority() const;

	/** authority 将容量配置限制为非负值并补齐空槽，不裁掉已有格子；默认新增格子后广播，关闭通知时由调用方统一发布。 */
	void SetInventorySlotCountFromAuthority(int32 NewSlotCount, bool bBroadcastChange = true);

	/** authority 原子替换完整槽位并保持格位顺序；售鱼可暂缓广播，调用方必须在经济提交成功后显式通知或失败时恢复原快照。 */
	bool ReplaceInventoryEntriesFromAuthority(const TArray<FCatInventoryEntry>& NewEntries, int32 MinimumSlotCount,
		bool bBroadcastChange = true);

	/** 按实例移除物品；实例完全离开当前库存后会解除复制子对象登记。 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Catfishing|Inventory")
	void RemoveItemInstance(UCatInventoryItemInstance* ItemInstance);

	/** 按槽位清空物品；调用方必须已确认这是允许丢弃或转移的库存实例命令。 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Catfishing|Inventory")
	void RemoveItemInstanceFromIndex(int32 TargetIndex);

	/** authority 从指定槽位移出完整 entry；部署、跨容器转移等需要保留实例身份的流程用它接走正式库存事实。 */
	bool RemoveInventoryEntryAtSlotFromAuthority(int32 TargetIndex, FCatInventoryEntry& OutRemovedEntry);

	/** authority 按实例身份借出部署型物品；正式库存负责服务器校验、槽位解析和 held entry，返回值交给部署/回滚调用方串联同一实例。 */
	FCatDomainCommandResult HoldInventoryItemInstanceFromAuthority(FGuid RequestId, FGuid ItemInstanceId, FCatInventoryEntry& OutHeldEntry);

	/** authority 按实例身份把部署型物品从活动区归还可见库存；收杆和 Use 回滚只消费结构化结果，库存负责同一实例、容量、复制和变化通知，且不缓存终态以便外层失败后重新借回。 */
	FCatDomainCommandResult ReturnHeldInventoryItemInstanceFromAuthority(FGuid RequestId, FGuid ItemInstanceId,
		int32 MinimumSlotCount, FCatInventoryEntry& OutReturnedEntry);

	/** authority 退役活动区里的同一实例；存档已接管部署物时用它清掉本库存活动区保管记录。 */
	bool RetireHeldInventoryEntryFromAuthority(FGuid ItemInstanceId);
	/** Transfer exact deployed instances during owner teardown; callers rebind consumers before publishing. */
	bool MoveHeldInventoryEntriesToCustodianFromAuthority(UCatInventoryComponent* Target, const TArray<FGuid>& ItemInstanceIds);



	/** authority 读取活动区里某个实例的只读 entry；导出或预检只需要观察时用它避免暴露可写库存格。 */
	const FCatInventoryEntry* FindHeldInventoryEntryFromAuthority(FGuid ItemInstanceId) const;

	/** authority 把当前活动区里的 held entry 追加到输出快照；存档导出用它读取库存正在保管的部署型实例。 */
	void AppendHeldInventoryEntriesFromAuthority(TArray<FCatInventoryEntry>& OutEntries) const;

	/** authority 查询库存活动区是否仍有部署型实例；恢复和失败预算用它判断库存是否处于可写空闲态。 */
	bool HasActiveHeldInventoryEntriesFromAuthority() const;

	/** authority 从指定格扣除正数数量，不足时拒绝；归零清格，并在无其他格引用时解除复制登记。默认广播，业务可延迟到关联提交完成后发布。 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Catfishing|Inventory")
	bool ConsumeItemAtSlot(int32 SlotIndex, int32 ConsumeCount, bool bBroadcastChange = true);
	/** 能力成本消费精确实例并记录终态；默认发布数量变化，关闭通知时调用方须在同步提交完成后发布，重放不再扣量或调用外部效果。 */
	FCatDomainCommandResult ConsumeAbilityItemFromAuthority(FGuid RequestId, FGuid ItemId, int32 Quantity, bool bPublishChange = true);

	/** 菜单操作的唯一库存命令入口；复核载荷、槽位、访问与定义声明，预占请求后交给 Statics 执行并缓存结果。世界操作成功后通知库存，出售通知沿商店事务发布。 */
	FCatDomainCommandResult ExecuteItemActionFromAuthority(const FCatInventoryItemUseContext& Context,
		FGuid ItemInstanceId, FGameplayTag Action, int32 Quantity);

	/** 将丢弃、放置或 Carry 的枚举入口转换为菜单动作标签和上下文，复用 ExecuteItemActionFromAuthority 的校验、请求缓存与执行链。 */
	FCatDomainCommandResult ReleaseItemToWorldFromAuthority(ACatCharacter* Character, FGuid RequestId,
		int32 SlotIndex, FGuid ItemInstanceId, int32 Quantity, ECatInventoryWorldAction Action);

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
	int32 FindFirstInventorySlotIndexByItemId(int32  ItemId) const;

	/** 当前可见库存数量表示玩家背包格里仍可整理、可选择的同定义总数；库存可用性判断读取它，不包含 held 活动区、Fishing 会话冻结或其他已离开可见槽位的实例。 */
	int32 CountVisibleInventoryQuantityByItemId(int32  ItemId) const;

	/** 读取当前库存槽位数量；用于 UI 创建格子和交换操作校验下标。 */
	int32 GetInventorySlotCount() const;

	/** 只读取得某个槽位条目；越界时返回空指针，调用方不得保存为可写引用。 */
	const FCatInventoryEntry* GetInventoryEntryAtSlot(int32 SlotIndex) const;

	/** Use 预检交给实例语义决定；默认使用拥有者 Pawn，库存核心不认识 GAS、装备或窝料目标。 */
	bool CanUseItemAtSlot(int32 SlotIndex, APawn* UserPawn = nullptr) const;


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

	/**
	 * 声明：这份库存还能再收几份这类物品，才不突破随身携带总量上限（道具册：普通饵 8 份、窝料 5 份）。
	 * 实现：只有 EnforcesCarryLimits() 为真的库存才真的算；其余一律返回 MAX_int32。
	 * 边界：它和格数、单格堆叠上限是三件不同的事，三道都要过；未配置上限时等价于不设限。
	 */
	int32 GetRemainingCarryAllowanceForDefinition(const UCatInventoryItemDefinition& ItemDefinition) const;

	/**
	 * 这份库存是否受「随身携带总量」约束。
	 * 只有猫身上的背包受约束——「携带上限」讲的是一只猫身上能带多少，营地公库、鱼护和商店货架不在其内。
	 */
	virtual bool EnforcesCarryLimits() const { return false; }
	int32 GetEffectiveCarryLimit(ECatInventoryCarryCategory Category) const;

	/** 读取这份库存在团队装备库里声明的角色；商店交付按它把竿漂与消耗品分到两个去处。 */
	ECatTeamStorageRole GetTeamStorageRole() const { return TeamStorageRole; }
	/** 仅权威恢复空仓的角色；已有库存不能在运行时改投递规则。 */
	bool RestoreTeamStorageRoleFromAuthority(ECatTeamStorageRole Role);

protected:
	/** 正在准备离库的请求标识；非空表示这份库存被同步事务占用，普通写入口必须拒绝以保护已核对的槽位。 */
	FGuid PreparedRemovalRequest;
	/** 本批将整件移出的原槽位集合；PrepareRemovalFromAuthority 写入，FinishRemovalFromAuthority 读取，取消时只清锁不重建实例。 */
	TArray<int32> PreparedRemovalSlots;

public:
	/** 校验一组精确实例仍在本库存并独占到 Finish；准备阶段不改槽位，失败不改变库存，调用方必须在同一同步事务中完成或取消。 */
	bool PrepareRemovalFromAuthority(FGuid RequestId, const TArray<FGuid>& ItemIds);
	/** 完成已准备的整件离库或取消预留；提交不再执行可失败预检，可延后广播到外层全部来源完成后统一通知。 */
	void FinishRemovalFromAuthority(FGuid RequestId, bool bCommit, bool bBroadcast = true);
	/** 库存是否正在参与尚未完成的同步事务；容器拾取、交换、使用和恢复等写入口读取它来拒绝重入。 */
	bool HasPreparedRemoval() const { return PreparedRemovalRequest.IsValid(); }

protected:
	/** 本库存独立的显示 Model；组件按需创建并持有，服务器本地提交或客户端复制后更新，其他库存不会写入它。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatInventoryModel> InventoryModel;

	/** 定义批次接收规则默认保持通用背包语义；不创建实例的容量预演用它和正式入库保持同一槽位口径。 */
	virtual bool CanAcceptInventoryDefinitionAtSlot(const UCatInventoryItemDefinition& IncomingDefinition,
		int32 TargetSlotIndex) const;

	/** 本次同步入库的槽位分配；预检和写入共用，不保存到组件或跨帧传递。 */
	struct FInventoryIntakeSlot
	{
		/** 分配后的定义；空值表示仍为空格，由分配计算写入。 */
		UCatInventoryItemDefinition* ItemDefinition = nullptr;
		/** 接收格的原实例或待接收实例；空值时提交前按定义创建。 */
		UCatInventoryItemInstance* Instance = nullptr;
		/** 新格采用的实例类型；定义批次可指定，实例批次沿用来源类型。 */
		TSubclassOf<UCatInventoryItemInstance> InstanceClass;
		/** 分配后的份数；只在提交成功后写入正式条目。 */
		int32 StackCount = 0;
		/** 本次为该格分配的新增份数；分配时累加，提交与返回实例时据此跳过未变格。 */
		int32 AddedCount = 0;
		/** 合并数量时需要接过载体的来源实例；提交成功前不清除来源关联。 */
		UCatInventoryItemInstance* WorldActorSource = nullptr;
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

	/** 只读正式库存并输出临时槽位分配；余数指针仅供单项调用允许部分接收，省略时任一项不完整即失败。 */
	bool AllocateInventoryIntake(const FCatInventoryReceiveBatch& Batch,
		TArray<FInventoryIntakeSlot>& Slots, int32* OutRemaining = nullptr) const;

	/** 准备新实例并核对准备期间原条目未变，再迁移载体、写条目与复制登记；拒绝时不提交本批次，也不撤销重入变更，通知由调用方发布。 */
	bool ApplyInventoryIntake(TArray<FInventoryIntakeSlot>& Slots);

	/** 读取某个定义的有效堆叠上限；集中处理非法配置，确保预演和正式入库口径一致。 */
	int32 GetMaxStackCountForDefinition(const UCatInventoryItemDefinition& ItemDefinition) const;

	/** 统计本库存可见格里属于同一携带分类的总份数；只服务随身总量上限，不参与格数或堆叠裁决。 */
	int32 CountVisibleQuantityForCarryCategory(ECatInventoryCarryCategory Category) const;

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

	/**
	 * 这份库存在团队装备库里的角色；关卡里放公共架和公库两个容器时，各自在实例上选一个。
	 * 默认 Unspecified 保持现状：没人声明角色时商店仍旧把整车交给同一个公共仓库。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "InventoryConfig")
	ECatTeamStorageRole TeamStorageRole = ECatTeamStorageRole::Unspecified;

	/** 当前从本库存借出但尚未归还或退役的不可堆叠单实例；键是实例身份，值是唯一的完整 entry，部署与收回都只操作这份所有权记录。 */
	UPROPERTY(Transient)
	TMap<FGuid, FCatInventoryEntry> ActiveHeldItemEntries;

	/** 普通库存命令首次终态缓存；重复 RequestId 只返回首次结果，避免重复整理格子。 */
	TMap<FString, FCatDomainCommandResult> TerminalCache;

	/** 库存命令载荷签名；同一 RequestId 换槽位、目标或物品载荷会被拒绝，避免客户端复用幂等键改写新意图。 */
	TMap<FString, FString> TerminalPayloadByKey;
};
