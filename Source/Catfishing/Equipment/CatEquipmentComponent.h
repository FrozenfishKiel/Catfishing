#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Equipment/CatEquipmentTypes.h"
#include "CatEquipmentComponent.generated.h"

/** Equipment 完整装配快照发生提交或复制变化的本机通知；UI 只把它当重读信号。 */
DECLARE_MULTICAST_DELEGATE(FCatEquipmentSnapshotChanged);

/** Character 的一局功能型装配聚合；复制装配/耗材/耐久，不持有永久解锁且不提供任何偷取接口。 */
UCLASS(ClassGroup = (Catfishing), meta = (BlueprintSpawnableComponent))
class CATFISHING_API UCatEquipmentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 开启默认复制并关闭 Tick；所有写入由 authority 命令提交。 */
	UCatEquipmentComponent();

	/** 注册单一 Loadout Snapshot；终态缓存不复制。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 提供服务器最终装配或客户端复制读模型；调用方只能据此显示/校验 Revision，不能通过引用补耐久或改库存。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Equipment")
	const FCatEquipmentLoadoutSnapshot& GetSnapshot() const;

	/** 根据服务器目录与可信解锁证明验证三个稳定 ID 后首次原子装配；客户端 Profile 选择不授予权限，也不能借重复请求修复耐久。 */
	FCatDomainCommandResult ConfigureLoadoutFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		FName RodDefinitionId, FName BaitDefinitionId, FName FloatDefinitionId,
		FName ScoopNetDefinitionId = NAME_None, FName RodSkinDefinitionId = NAME_None);

	/** 只读预检耗材能否授予；商店用它保证扣款前已经确认角色确实收得下货。 */
	ECatDomainCommandError ValidateRunConsumableGrant(FGuid RequestId, FName DefinitionId,
		int32 Quantity) const;

	/** 一局拾取/奖励上层提交耗材；只接受定义为 run consumable 的正式条目。 */
	FCatDomainCommandResult GrantRunConsumableFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		FName DefinitionId, int32 Quantity);

	/** 只读检查团队库实例能否装入本人现有三件套；用于跨库取用链在删除公库实例前确认个人 Equipment 收得下。 */
	ECatDomainCommandError ValidateTeamLibraryEquipFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		const FCatTeamEquipmentInstance& Instance) const;

	/**
	 * 把团队装备库中已购买的实例装入本人现有三件套；只允许 Rod/Bait/Float。
	 * 调用方必须先完成团队库取用预检，再在公库实例成功取走后调用它，避免个人已装备但公库仍保留同一实例。
	 */
	FCatDomainCommandResult EquipFromTeamLibraryFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		const FCatTeamEquipmentInstance& Instance);

	/** 消费一份指定一局耗材；草药、窝料和道具上层必须先成功提交本结果再产生领域效果。 */
	FCatDomainCommandResult ConsumeRunConsumableFromAuthority(FGuid RequestId, int64 ExpectedRevision,
		FName DefinitionId);

	/** 提交一次钓鱼失败预算；一个 RequestId 只能选择 None/丢特殊饵/伤竿之一，绝不双罚。 */
	FCatFishingFailureResult CommitFishingFailure(FGuid RequestId, int64 ExpectedRevision,
		ECatFishingFailurePenalty Penalty);

	FCatFishingUseReservationResult BeginFishingUse(FGuid FishingSessionId, FName RodDefinitionId,
		FName BaitDefinitionId, FName FloatDefinitionId, int64 ExpectedRevision);
	FCatFishingUseOperationResult CommitFishingBait(FGuid FishingSessionId);
	FCatFishingUseOperationResult CommitFishingBaitDeferred(FGuid FishingSessionId);
	void PublishDeferredFishingBait(FGuid FishingSessionId);
	FCatFishingUseOperationResult SetAccumulatedFishingRodWear(FGuid FishingSessionId, int64 WearSequence,
		double AbsoluteTotal);
	FCatFishingUseOperationResult CommitFishingRodWear(FGuid FishingSessionId);
	FCatFishingUseOperationResult CommitFishingRodBreak(FGuid FishingSessionId);
	FCatFishingUseOperationResult ReleaseFishingUse(FGuid FishingSessionId);
	bool HasActiveFishingUse() const;
	bool IsFishingUseActive(FGuid FishingSessionId) const;

	FCatRunConsumableUseResult BeginRunConsumableUse(FGuid OperationId,
		FName DefinitionId, int32 Quantity, int64 ExpectedRevision);
	FCatRunConsumableUseResult CommitRunConsumableUse(FGuid OperationId);
	FCatRunConsumableUseResult CommitRunConsumableUseDeferred(FGuid OperationId);
	void PublishDeferredRunConsumableUse(FGuid OperationId);
	FCatRunConsumableUseResult ReleaseRunConsumableUse(FGuid OperationId);
	bool HasActiveRunConsumableUse() const;

	/** 固定营地修竿点提交维修；只消费一份显式浮木并恢复到当前 Rod 定义最大耐久。 */
	FCatDomainCommandResult RepairRodAtCamp(FGuid RequestId, int64 ExpectedRevision, bool bAtCamp);

	/** 本机完整装配变化通知；不携带可写指针或客户端授权。 */
	FCatEquipmentSnapshotChanged OnSnapshotChanged;

private:
	struct FCatFishingUseRecord
	{
		FGuid SessionId;
		FName RodDefinitionId = NAME_None;
		FName BaitDefinitionId = NAME_None;
		FName FloatDefinitionId = NAME_None;
		int64 ReservationRevision = 0;
		int64 LastWearSequence = 0;
		double AbsoluteRodWear = 0.0;
		bool bSpecialBaitReserved = false;
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
	int32 GetPendingReservedSpecialBaitCount(FName DefinitionId) const;
	int32 GetPendingReservedRunConsumableCount(FName DefinitionId) const;
	FCatRunConsumableUseResult MakeRunConsumableUseResult(FGuid OperationId,
		ECatDomainCommandError Error, const FCatRunConsumableUseRecord* Record = nullptr) const;
	FCatFishingUseReservationResult MakeFishingUseReservationResult(FGuid FishingSessionId,
		ECatDomainCommandError Error, bool bReserved, const FCatFishingUseRecord* Record = nullptr) const;
	FCatFishingUseOperationResult MakeFishingUseOperationResult(FGuid FishingSessionId,
		ECatDomainCommandError Error, bool bApplied, const FCatFishingUseRecord* Record = nullptr) const;
	/** 客户端收到完整装配事实后只供 UI/玩法只读消费；不反向请求自动装备。 */
	UFUNCTION()
	void OnRep_Snapshot();

	/** 查找或创建一局耗材栈；调用方必须先验证定义，创建时数量为 0。 */
	FCatRunConsumableStack& FindOrAddConsumable(FName DefinitionId);

	/** 按定义 ID 查现有耗材栈；未找到返回空。 */
	FCatRunConsumableStack* FindConsumable(FName DefinitionId);

	/** 构造操作+RequestId 幂等键；只在当前 Character 生命周期使用。 */
	static FString MakeTerminalKey(const TCHAR* Operation, FGuid RequestId);

	/** authority 提交后请求复制并广播，客户端 RepNotify 只广播；所有 HUD 刷新因此走同一完整快照信号。 */
	void PublishSnapshot();

	/** 装配、耗材与耐久的唯一复制读模型。 */
	UPROPERTY(ReplicatedUsing = OnRep_Snapshot)
	FCatEquipmentLoadoutSnapshot Snapshot;

	/** 普通装备/耗材命令首次终态缓存。 */
	TMap<FString, FCatDomainCommandResult> TerminalCache;

	/** 新接入的跨聚合命令载荷签名；防止同一 RequestId 被换定义或数量后再次利用。 */
	TMap<FString, FString> TerminalPayloadByKey;

	/** 失败预算命令首次完整终态缓存；重放不会再次扣饵或耐久。 */
	TMap<FGuid, FCatFishingFailureResult> FailureTerminalCache;

	/** 当前 Character 生命周期内的私有 fishing reservation/tombstone；不复制也不持久化。 */
	TMap<FGuid, FCatFishingUseRecord> FishingUseRecords;
	FGuid ActiveFishingUseSessionId;
	TMap<FGuid, FCatRunConsumableUseRecord> RunConsumableUseRecords;
	FGuid ActiveRunConsumableUseOperationId;
};
