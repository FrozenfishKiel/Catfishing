#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Equipment/CatEquipmentTypes.h"
#include "CatEquipmentComponent.generated.h"

class UCatEquipmentDefinition;
class ACatCampInventoryActor;

/** Equipment 随身库存与钓鱼选择快照发生提交或复制变化的本机通知；UI 只把它当重读信号。 */
DECLARE_MULTICAST_DELEGATE(FCatEquipmentSnapshotChanged);

/** Character 的一局随身库存聚合；复制钓鱼选择、物品数量和鱼竿耐久，不持有永久解锁且不提供任何偷取接口。 */
UCLASS(ClassGroup = (Catfishing), meta = (BlueprintSpawnableComponent))
class CATFISHING_API UCatEquipmentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 开启默认复制并关闭 Tick；所有写入由 authority 命令提交。 */
	UCatEquipmentComponent();

	/** 注册单一随身库存快照；终态缓存不复制。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 提供服务器最终随身库存与钓鱼选择读模型；调用方只能据此显示/校验 Revision，不能通过引用补耐久或改库存。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Equipment")
	const FCatEquipmentLoadoutSnapshot& GetSnapshot() const;

	/** 根据服务器目录、可信解锁证明和随身库存持有量设置当前钓鱼选择；当前已部署鱼竿可作为原选择继续沿用，但不能借此切换到另一根鱼竿。 */
	FCatDomainCommandResult ConfigureLoadoutFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		FName RodDefinitionId, FName BaitDefinitionId, FName FloatDefinitionId,
		FName ScoopNetDefinitionId = NAME_None, FName RodSkinDefinitionId = NAME_None,
		FGuid RodItemInstanceId = FGuid(), FGuid BaitItemInstanceId = FGuid(),
		FGuid FloatItemInstanceId = FGuid(), FGuid ScoopNetItemInstanceId = FGuid());

	/** 只读预检数量型物品能否进入随身库存；商店用它保证扣款前已经确认角色确实收得下这组数量。 */
	ECatDomainCommandError ValidateInventoryQuantityGrant(FGuid RequestId, FName DefinitionId,
		int32 Quantity) const;

	/** 只读预检商店非数量物品能否进入本人随身库存；商店用它在扣款前确认买家 Pawn 和定义都能接收。 */
	ECatDomainCommandError ValidateEquipmentGrantFromAuthority(FGuid RequestId, FName DefinitionId) const;

	/** 一局拾取、商店或奖励上层提交数量型库存物品；成功后只写统一库存格数组，并会在需要时更新钓鱼选择。 */
	FCatDomainCommandResult GrantInventoryQuantityFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		FName DefinitionId, int32 Quantity);

	/** 商店或其他服务器权威来源授予非数量物品；成功后写入库存数组，并在空选择、旧选择缺货或同定义已选竿不可用时修正当前选择。 */
	FCatDomainCommandResult GrantEquipmentFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		FName DefinitionId);

	/** 背包点击或玩法入口共用的物品使用入口；它按实例调用定义侧 Use 裁决，Equipment 只执行移出实例、扣指定数量或 no-op 的库存事务。 */
	FCatInventoryItemUseResult Use(FGuid RequestId, int64 ExpectedRevision, FGuid ItemInstanceId,
		int32 Quantity = 1);

	/** 部署型物品收口时共用的停止使用入口；它按实例调用定义侧 UnUse 裁决，成功才把活动记录里的同一物品放回随身库存。 */
	FCatInventoryItemUseResult UnUse(FGuid RequestId, FGuid ItemInstanceId);

	/** 整理两个随身库存格；服务器按数组下标移动、合并或交换物品，成功后发布同一份库存快照。 */
	FCatDomainCommandResult MoveInventorySlotFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		int32 SourceSlotIndex, int32 TargetSlotIndex);

	/** 提交一次钓鱼失败预算；一个 RequestId 只能选择 None/丢特殊饵/伤竿之一，绝不双罚。 */
	FCatFishingFailureResult CommitFishingFailure(FGuid RequestId, int64 ExpectedRevision,
		ECatFishingFailurePenalty Penalty);

	/** Fishing 会话开始前按 SessionId 申请当前钓鱼选择使用权；Begin 会从随身库存暂存一份选中鱼饵，后续由本会话消耗或归还。 */
	FCatFishingUseReservationResult BeginFishingUse(FGuid FishingSessionId, FGuid RodItemInstanceId,
		FGuid BaitItemInstanceId, FGuid FloatItemInstanceId, FName RodDefinitionId,
	FName BaitDefinitionId, FName FloatDefinitionId, int64 ExpectedRevision);
	/** 确认消耗 Begin 已暂存的鱼饵；库存数量已经在 Begin 发布，本函数只收口会话内的饵料事务。 */
	FCatFishingUseOperationResult CommitFishingBaitDeferred(FGuid FishingSessionId);
	/** 按递增累计磨损的差额立即扣减 Begin 绑定的鱼竿实例；重复序号不重扣，Release 不回滚。 */
	FCatFishingUseOperationResult ApplyFishingRodWear(FGuid FishingSessionId, int64 WearSequence,
		double AbsoluteTotal);
	/** 从该会话绑定的库存或活动 Use 实例读取跨场保留的耐久，不读取当前选择的另一根竿。 */
	bool GetFishingRodDurability(FGuid FishingSessionId, double& OutDurability, bool& OutBroken) const;
	/** 结束 Fishing 使用记录；未消耗的暂存饵会回到随身库存，已消耗的记录只关闭自身。 */
	FCatFishingUseOperationResult ReleaseFishingUse(FGuid FishingSessionId);
	/** 当前是否有仍未结束的 Fishing 使用记录；维修和失败预算用它避开进行中的钓鱼结算。 */
	bool HasActiveFishingUse() const;
	/** 指定 Fishing 会话是否仍处于活动状态；Commit/Release 用它防止旧会话重复改写。 */
	bool IsFishingUseActive(FGuid FishingSessionId) const;

	/** 固定营地修竿点提交维修；只消费一份显式浮木并恢复到当前 Rod 定义最大耐久。 */
	FCatDomainCommandResult RepairRodAtCamp(FGuid RequestId, int64 ExpectedRevision, bool bAtCamp);

	/** 本机随身库存或钓鱼选择变化通知；不携带可写指针或客户端授权。 */
	FCatEquipmentSnapshotChanged OnSnapshotChanged;

private:
	/** 营地公共仓库负责背包和公共仓库之间的服务器拖放事务；只允许它在同一提交里同时改双方快照并发布广播。 */
	friend class ACatCampInventoryActor;

	struct FCatFishingUseRecord
	{
		/** 本场 Fishing 会话身份；后续扣饵、耐久结算和释放都按它找到同一条短生命周期记录。 */
		FGuid SessionId;
		/** Begin 冻结的鱼竿实例与定义；后续磨损不得按当前选择重新选竿。 */
		FGuid RodItemInstanceId;
		FName RodDefinitionId = NAME_None;
		/** Begin 从随身库存移出的一份鱼饵定义；数量型物品脱离原堆栈后不再复用原 ItemInstanceId。 */
		FName ReservedBaitDefinitionId = NAME_None;
		/** 已接收的竿磨损序号；磨损事件按递增序号提交，重复或跳号不会改耐久。 */
		int64 LastWearSequence = 0;
		/** 已按差额写入绑定实例的累计磨损；仅用于序号去重，不是另一份剩余耐久。 */
		double AbsoluteRodWear = 0.0;
		/** 当前记录是否仍持有 Begin 移出的那份鱼饵；Commit 消耗或 Release 归还后清掉，防止同一份饵重复收口。 */
		bool bBaitQuantityReserved = false;
		/** 鱼饵是否已经被本会话确认消耗；它让重复结算只返回终态，不再次处理暂存物。 */
		bool bBaitCommitted = false;
		/** 本会话是否已经结束；结束后的记录只作为重放终态，不再保护鱼饵或接受耐久事件。 */
		bool bReleased = false;
	};

	struct FCatInventoryItemUseRecord
	{
		/** 正在使用的物品实例身份；同一实例只能存在一条活动记录，防止背包和场景同时持有它。 */
		FGuid ItemInstanceId;
		/** 从库存移出的物品副本；UnUse 放回库存时必须沿用它，不能按 DefinitionId 重新生成一件。 */
		FCatRunInventorySlot Item;
		/** Use 成功时的 Equipment 版本；诊断用它串联库存移出和后续世界 Actor 生成。 */
		int64 UseRevision = 0;
		/** 活动记录是否已经收口；收口后的记录不再参与可用性判断。 */
		bool bReleased = false;
	};

	FCatFishingUseRecord* FindFishingUseRecord(FGuid FishingSessionId);
	const FCatFishingUseRecord* FindFishingUseRecord(FGuid FishingSessionId) const;
	FCatInventoryItemUseRecord* FindInventoryItemUseRecord(FGuid ItemInstanceId);
	const FCatInventoryItemUseRecord* FindInventoryItemUseRecord(FGuid ItemInstanceId) const;
	FCatRunInventorySlot* FindFishingRodInstance(const FCatFishingUseRecord& Record);
	const FCatRunInventorySlot* FindFishingRodInstance(const FCatFishingUseRecord& Record) const;
	/** 是否存在尚未收口的物品 Use 记录；维修和失败预算用它避免改写正在由场景持有的物品状态。 */
	bool HasActiveInventoryItemUse() const;
	/** 读取某个定义在库存格数组中的可见数量；选择自动切换和商店预检用它判断旧选择是否仍有实物。 */
	int32 GetInventoryItemQuantity(FName DefinitionId) const;
	/** 根据新入库定义修正空选择、无库存旧选择或已断/耐久非法的同定义已选竿；它只改钓鱼选择，不把库存物品移出数组。 */
	void AutoSelectGrantedInventoryItem(const UCatEquipmentDefinition& Definition, FName DefinitionId);
	/** 把当前选择中的鱼竿状态同步到库存格或活动 Use 记录；耐久、断竿和收杆归还都读这份实例副本。 */
	void SyncSelectedRodStateToSelectedInstance();
	FCatFishingUseReservationResult MakeFishingUseReservationResult(FGuid FishingSessionId,
		ECatDomainCommandError Error, bool bReserved, const FCatFishingUseRecord* Record = nullptr) const;
	FCatFishingUseOperationResult MakeFishingUseOperationResult(FGuid FishingSessionId,
		ECatDomainCommandError Error, bool bApplied, const FCatFishingUseRecord* Record = nullptr) const;
	/** 客户端收到完整随身库存事实后只供 UI/玩法只读消费；不反向请求自动选择。 */
	UFUNCTION()
	void OnRep_Snapshot();

	/** 读取随身库存配置容量；0 表示本局没有可用格子，写入路径必须拒绝新物品。 */
	int32 GetConfiguredInventorySlotCapacity() const;

	/** 读取一个定义在单格里的最大堆叠数；装备型物品固定为 1，数量型物品使用项目配置。 */
	int32 GetInventoryStackLimit(const UCatEquipmentDefinition& Definition) const;

	/** 只读判断指定数量是否能放进库存格数组；商店扣款前和授予入口共用它避免半写入。 */
	bool CanStoreInventoryItem(const UCatEquipmentDefinition& Definition, FName DefinitionId, int32 Quantity) const;

	/** 让复制快照至少拥有配置声明的格子数；只追加空格，不截断已有物品。 */
	void EnsureInventorySlotArray();

	/** 补齐现有库存格的实例身份和工具状态；返回值表示本次是否修正了旧数据。 */
	bool NormalizeInventorySlots();

	/** 按实例身份查找随身库存格；Use、选择和诊断用它避免只按 DefinitionId 误伤同类物品。 */
	FCatRunInventorySlot* FindInventorySlotByInstanceId(FGuid ItemInstanceId);
	const FCatRunInventorySlot* FindInventorySlotByInstanceId(FGuid ItemInstanceId) const;

	/** 读取某个定义当前可用的第一份实例；旧 UI 仍按定义选择时用它落到具体物品实例，鱼竿会优先返回未断且有耐久的那份。 */
	const FCatRunInventorySlot* FindFirstInventorySlotByDefinition(FName DefinitionId) const;

	/** 判断一份完整实例副本能否原样放回随身库存；普通入库会拒绝重复非消耗品实例，UnUse 另行收口已有残留。 */
	bool CanStoreInventorySlot(const UCatEquipmentDefinition& Definition, const FCatRunInventorySlot& Item) const;

	/** 把指定数量放入库存格数组；调用前必须已通过 CanStoreInventoryItem，成功后只修改这份库存事实。 */
	bool AddInventoryItemQuantity(const UCatEquipmentDefinition& Definition, FName DefinitionId, int32 Quantity);

	/** 把一份完整实例副本放入库存格数组；它保留 ItemInstanceId 和工具状态，不按 DefinitionId 重新生成物品。 */
	bool AddInventoryItemSlot(const UCatEquipmentDefinition& Definition, const FCatRunInventorySlot& Item);

	/** 服务器内部授予一份完整库存实例；营地取用用它避免按定义重新生成同类装备。 */
	FCatDomainCommandResult GrantInventorySlotFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		const FCatRunInventorySlot& Item);

	/** 从指定实例所在数量栈扣除数量；Use、钓鱼用饵和维修消耗都用它保证扣的是明确实例，并把本次扣减副本交回调用方。 */
	bool RemoveInventoryItemQuantityFromInstance(FGuid ItemInstanceId, int32 Quantity,
		FCatRunInventorySlot& OutConsumedItem);

	/** 从库存格数组移出指定实例并返回副本；Use 用它保证场景 Actor 和背包不会同时持有同一物品。 */
	bool RemoveInventoryItemInstance(FGuid ItemInstanceId, FCatRunInventorySlot& OutItem);

	/** 构造操作+RequestId 幂等键；只在当前 Character 生命周期使用。 */
	static FString MakeTerminalKey(const TCHAR* Operation, FGuid RequestId);

	/** authority 提交后请求复制并广播，客户端 RepNotify 只广播；所有 HUD 刷新因此走同一完整快照信号。 */
	void PublishSnapshot();

	/** 随身库存、钓鱼选择与鱼竿耐久的唯一复制读模型。 */
	UPROPERTY(ReplicatedUsing = OnRep_Snapshot)
	FCatEquipmentLoadoutSnapshot Snapshot;

	/** 普通随身库存命令首次终态缓存。 */
	TMap<FString, FCatDomainCommandResult> TerminalCache;

	/** 库存命令载荷签名；普通入库和 Use/UnUse 共用它防止同一 RequestId 被换定义、数量或实例后再次利用。 */
	TMap<FString, FString> TerminalPayloadByKey;

	/** 失败预算命令首次完整终态缓存；重放不会再次扣饵或耐久。 */
	TMap<FGuid, FCatFishingFailureResult> FailureTerminalCache;

	/** 当前 Character 生命周期内按 SessionId 隔离的 fishing reservation/tombstone；不复制也不持久化。 */
	TMap<FGuid, FCatFishingUseRecord> FishingUseRecords;
	/** 当前 Character 生命周期内被部署型 Use 暂时持有的物品实例；简单消耗品不进入这里，场景 Actor 收口前实例不会回到随身库存。 */
	TMap<FGuid, FCatInventoryItemUseRecord> InventoryItemUseRecords;
	/** 物品 Use/UnUse 首次终态缓存；简单消耗品重试会读它而不是再次扣量，部署/收回重试也不会重复移动同一实例。 */
	TMap<FString, FCatInventoryItemUseResult> InventoryItemUseTerminalCache;
	/** 仅用于客户端复制诊断限频，不参与耐久或玩法裁决。 */
	FGuid LastLoggedRodInstanceId;
	int32 LastLoggedRodDurabilityBand = INDEX_NONE;
	bool bLastLoggedRodBroken = false;
};
