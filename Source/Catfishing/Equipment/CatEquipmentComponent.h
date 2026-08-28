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

	/** 根据服务器目录、可信解锁证明和随身库存持有量设置当前钓鱼选择；它不发放物品，也不能借重复请求修复耐久。 */
	FCatDomainCommandResult ConfigureLoadoutFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		FName RodDefinitionId, FName BaitDefinitionId, FName FloatDefinitionId,
		FName ScoopNetDefinitionId = NAME_None, FName RodSkinDefinitionId = NAME_None);

	/** 只读预检数量型物品能否进入随身库存；商店用它保证扣款前已经确认角色确实收得下这组数量。 */
	ECatDomainCommandError ValidateInventoryQuantityGrant(FGuid RequestId, FName DefinitionId,
		int32 Quantity) const;

	/** 只读预检商店装备型物品能否进入本人随身库存；商店用它在扣款前确认买家 Pawn 和定义都能接收。 */
	ECatDomainCommandError ValidateEquipmentGrantFromAuthority(FGuid RequestId, FName DefinitionId) const;

	/** 一局拾取、商店或奖励上层提交数量型库存物品；成功后只写统一库存格数组，并会在需要时更新钓鱼选择。 */
	FCatDomainCommandResult GrantInventoryQuantityFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		FName DefinitionId, int32 Quantity);

	/** 商店或其他服务器权威来源授予装备型物品；成功后写入库存数组，并在空选择、旧选择缺货或同定义已选竿不可用时修正当前选择。 */
	FCatDomainCommandResult GrantEquipmentFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		FName DefinitionId);

	/** 整理两个随身库存格；服务器按数组下标移动、合并或交换物品，成功后发布同一份库存快照。 */
	FCatDomainCommandResult MoveInventorySlotFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		int32 SourceSlotIndex, int32 TargetSlotIndex);

	/** 从统一库存里扣一份指定数量型物品；草药这类上层效果必须等本结果成功后才能发生。 */
	FCatDomainCommandResult ConsumeInventoryQuantityFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		FName DefinitionId);

	/** 提交一次钓鱼失败预算；一个 RequestId 只能选择 None/丢特殊饵/伤竿之一，绝不双罚。 */
	FCatFishingFailureResult CommitFishingFailure(FGuid RequestId, int64 ExpectedRevision,
		ECatFishingFailurePenalty Penalty);

	/** Fishing 会话开始前按 SessionId 申请当前钓鱼选择使用权；不同鱼竿会话可并行，各自保护一份鱼饵库存。 */
	FCatFishingUseReservationResult BeginFishingUse(FGuid FishingSessionId, FName RodDefinitionId,
		FName BaitDefinitionId, FName FloatDefinitionId, int64 ExpectedRevision);
	/** 立即提交 Fishing 已保护的鱼饵数量，并在成功时发布新的 Equipment 快照。 */
	FCatFishingUseOperationResult CommitFishingBait(FGuid FishingSessionId);
	/** 只提交 Fishing 已保护的鱼饵数量但暂不发布快照；FishingSession 用它把结算和表现事件排成确定顺序。 */
	FCatFishingUseOperationResult CommitFishingBaitDeferred(FGuid FishingSessionId);
	/** 发布此前延迟提交的 Fishing 鱼饵扣减结果；重复调用只返回既有终态，不会再次广播。 */
	void PublishDeferredFishingBait(FGuid FishingSessionId);
	FCatFishingUseOperationResult SetAccumulatedFishingRodWear(FGuid FishingSessionId, int64 WearSequence,
		double AbsoluteTotal);
	FCatFishingUseOperationResult CommitFishingRodWear(FGuid FishingSessionId);
	FCatFishingUseOperationResult CommitFishingRodBreak(FGuid FishingSessionId);
	FCatFishingUseOperationResult ReleaseFishingUse(FGuid FishingSessionId);
	bool HasActiveFishingUse() const;
	bool IsFishingUseActive(FGuid FishingSessionId) const;

	/** 草药、窝料和通用道具使用前申请一局耗材预留；会把 Fishing 已保护的鱼饵数量排除在可用库存之外。 */
	FCatRunConsumableUseResult BeginRunConsumableUse(FGuid OperationId,
		FName DefinitionId, int32 Quantity, int64 ExpectedRevision);
	FCatRunConsumableUseResult CommitRunConsumableUse(FGuid OperationId);
	FCatRunConsumableUseResult CommitRunConsumableUseDeferred(FGuid OperationId);
	void PublishDeferredRunConsumableUse(FGuid OperationId);
	FCatRunConsumableUseResult ReleaseRunConsumableUse(FGuid OperationId);
	bool HasActiveRunConsumableUse() const;

	/** 固定营地修竿点提交维修；只消费一份显式浮木并恢复到当前 Rod 定义最大耐久。 */
	FCatDomainCommandResult RepairRodAtCamp(FGuid RequestId, int64 ExpectedRevision, bool bAtCamp);

	/** 本机随身库存或钓鱼选择变化通知；不携带可写指针或客户端授权。 */
	FCatEquipmentSnapshotChanged OnSnapshotChanged;

private:
	/** 营地公共仓库负责背包和公共仓库之间的服务器拖放事务；只允许它在同一提交里同时改双方快照并发布广播。 */
	friend class ACatCampInventoryActor;

	struct FCatFishingUseRecord
	{
		FGuid SessionId;
		FName RodDefinitionId = NAME_None;
		FName BaitDefinitionId = NAME_None;
		FName FloatDefinitionId = NAME_None;
		int64 ReservationRevision = 0;
		int64 LastWearSequence = 0;
		double AbsoluteRodWear = 0.0;
		/** Fishing 会话已保护的一份鱼饵数量；Begin 写入，Commit 扣除，Release 放弃，防止普通饵和特殊饵被其他耗材入口双花。 */
		bool bBaitQuantityReserved = false;
		bool bBaitCommitted = false;
		bool bBaitCommitPublished = false;
		bool bWearCommitted = false;
		bool bBreakCommitted = false;
		bool bReleased = false;
	};

	struct FCatRunConsumableUseRecord
	{
		FGuid OperationId;
		FName DefinitionId = NAME_None;
		int32 Quantity = 0;
		int64 ReservationRevision = 0;
		int64 ResultRevision = 0;
		bool bCommitted = false;
		bool bReleased = false;
		bool bCommitPublished = false;
	};

	FCatFishingUseRecord* FindFishingUseRecord(FGuid FishingSessionId);
	const FCatFishingUseRecord* FindFishingUseRecord(FGuid FishingSessionId) const;
	/** 某种鱼饵正在被 Fishing 会话保护但尚未提交的数量；授予、直接扣除和使用事务都会读它避免同一份饵被双花。 */
	int32 GetPendingReservedFishingBaitCount(FName DefinitionId) const;
	/** 读取某个定义在库存格数组中的可见数量；选择自动切换和商店预检用它判断旧选择是否仍有实物。 */
	int32 GetInventoryItemQuantity(FName DefinitionId) const;
	/** 根据新入库定义修正空选择、无库存旧选择或已断/耐久非法的同定义已选竿；它只改钓鱼选择，不把库存物品移出数组。 */
	void AutoSelectGrantedInventoryItem(const UCatEquipmentDefinition& Definition, FName DefinitionId);
	int32 GetPendingReservedRunConsumableCount(FName DefinitionId) const;
	FCatRunConsumableUseResult MakeRunConsumableUseResult(FGuid OperationId,
		ECatDomainCommandError Error, const FCatRunConsumableUseRecord* Record = nullptr) const;
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

	/** 把指定数量放入库存格数组；调用前必须已通过 CanStoreInventoryItem，成功后只修改这份库存事实。 */
	bool AddInventoryItemQuantity(const UCatEquipmentDefinition& Definition, FName DefinitionId, int32 Quantity);

	/** 从库存格数组扣除指定数量；数量归零时清空格子，成功后保留数组位置供 UI 稳定显示。 */
	bool RemoveInventoryItemQuantity(FName DefinitionId, int32 Quantity);

	/** 构造操作+RequestId 幂等键；只在当前 Character 生命周期使用。 */
	static FString MakeTerminalKey(const TCHAR* Operation, FGuid RequestId);

	/** authority 提交后请求复制并广播，客户端 RepNotify 只广播；所有 HUD 刷新因此走同一完整快照信号。 */
	void PublishSnapshot();

	/** 随身库存、钓鱼选择与鱼竿耐久的唯一复制读模型。 */
	UPROPERTY(ReplicatedUsing = OnRep_Snapshot)
	FCatEquipmentLoadoutSnapshot Snapshot;

	/** 普通随身库存命令首次终态缓存。 */
	TMap<FString, FCatDomainCommandResult> TerminalCache;

	/** 新接入的跨聚合命令载荷签名；防止同一 RequestId 被换定义或数量后再次利用。 */
	TMap<FString, FString> TerminalPayloadByKey;

	/** 失败预算命令首次完整终态缓存；重放不会再次扣饵或耐久。 */
	TMap<FGuid, FCatFishingFailureResult> FailureTerminalCache;

	/** 当前 Character 生命周期内按 SessionId 隔离的 fishing reservation/tombstone；不复制也不持久化。 */
	TMap<FGuid, FCatFishingUseRecord> FishingUseRecords;
	TMap<FGuid, FCatRunConsumableUseRecord> RunConsumableUseRecords;
	FGuid ActiveRunConsumableUseOperationId;
};
