#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatSacrificeContracts.h"
#include "Subsystems/WorldSubsystem.h"
#include "Items/CatItemTypes.h"
#include "CatItemsService.generated.h"

class UCatContainerReplicationComponent;
class UCatSocialService;

/** 一局服务器 Items 深模块；它是容器数组、预留、捕获创建、转移与消费的唯一写入口。 */
UCLASS()
class CATFISHING_API UCatItemsService : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 仅在 authority World 创建服务；客户端只消费容器复制组件。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** World 销毁时关闭新命令并清空仅属本局的容器、预留与终态缓存。 */
	virtual void Deinitialize() override;

	/** 注册一个真实容器宿主并发布初始 Revision；个人容器身份只保存在服务端记录。 */
	bool RegisterContainer(UCatContainerReplicationComponent* ReplicationComponent, FGuid ContainerId,
		ECatContainerKind Kind, const FString& OwnerStableNetId, int32 Capacity);

	/** 宿主离开 World 时按精确组件解除发布目标；已有终态不会迁移到其他容器。 */
	void UnregisterContainer(UCatContainerReplicationComponent* ReplicationComponent);

	/** 复制指定容器已提交的公开事实供上层校验 Revision；不存在时整体失败，预留、容量和主人身份始终留在服务端记录。 */
	bool TryGetContainerSnapshot(FGuid ContainerId, FCatContainerSnapshot& OutSnapshot) const;

	/** 返回容器的服务器种类与真实 Actor 宿主；供空间权限校验使用，不暴露私有主人身份。 */
	bool TryGetContainerHost(FGuid ContainerId, ECatContainerKind& OutKind, AActor*& OutAuthorityActor) const;

	/** 首个合法抢抄终态调用的唯一鱼实例创建入口；提交会话预分配 ID，同 RequestId/会话重放只返回首次 Committed DTO。 */
	FCatCaptureCommitResult CommitCapture(const FCatCaptureCommitCommand& Command);

	/** 在两个容器版本同时匹配时原子移动一条鱼；任何失败都保持源和目标原样。 */
	FCatDomainCommandResult TransferOwnedFish(const FCatFishTransferCommand& Command);

	/** 从本人鱼护或共享鱼缸不可逆移除一条未预留鱼；成功后上层才可应用食用效果。 */
	FCatFishConsumeResult ConsumeFish(const FCatFishConsumeCommand& Command);

	/** 献祭协调器在不可逆点前锁定一条鱼；预留增加 Revision 但不从复制数组删除。 */
	FCatFishReservationResult ReserveFish(const FCatSacrificeCommand& Command);

	/** Items commit 前按服务器身份、容器与 RequestId 取消精确预留；已提交或不匹配时不恢复鱼。 */
	FCatDomainCommandResult CancelFishReservation(const FString& StableNetId, FGuid RequestId, FGuid ContainerId);

	/** 按服务器身份、容器与 RequestId 不可逆移除已预留鱼；成功和重复提交返回同一贡献与 Revision。 */
	FCatFishReservationCommitResult CommitFishReservation(const FString& StableNetId, FGuid RequestId, FGuid ContainerId);

	/** 商店售卖第一阶段：按容器权限与 Revision 冻结个人鱼护或共用鱼缸里的一条鱼；只上锁，不删鱼、不算价、不碰钱包。 */
	FCatFishSaleHoldResult PrepareFishSale(const FCatFishSaleHoldCommand& Command);

	/** 商店售卖回退：钱包没入账或入账失败时释放同一售卖请求的冻结，鱼原位恢复可转移、可吃、可献祭、可被偷。 */
	FCatFishSaleHoldResult CancelPreparedFishSale(const FString& StableNetId, FGuid ContainerId, FGuid SaleRequestId);

	/** 商店售卖第二阶段：钱包已入账后不可逆移除同一售卖请求冻结的鱼；重放返回首次的 Revision 与同一条鱼。 */
	FCatFishSaleHoldResult CommitPreparedFishSale(const FString& StableNetId, FGuid ContainerId, FGuid SaleRequestId);

	/** Host teardown 关闭新写口并取消所有尚未提交的持有（含献祭预留与商店售卖冻结）；已提交记录只供协调器补 Run。 */
	void CloseCommandsAndCancelReservations();

private:
	friend class UCatSocialService;

	/** 单容器服务器记录；公开快照不含容量、预留或 OwnerStableNetId。 */
	struct FContainerRecord
	{
		/** 当前提交后的公开鱼数组与 Revision。 */
		FCatContainerSnapshot Snapshot;
		/** 个人鱼护的服务器私有 StableNetId；共享鱼缸为空。 */
		FString OwnerStableNetId;
		/** 显式产品容量；0 表示未裁。 */
		int32 Capacity = 0;
		/** 接收复制快照的真实组件弱引用。 */
		TWeakObjectPtr<UCatContainerReplicationComponent> ReplicationComponent;
	};

	/** 一条鱼冻结的服务器私有事实；献祭预留与商店售卖冻结共用该记录，Fish 副本用于不可逆提交后的幂等重放。 */
	struct FReservationRecord
	{
		/** 被锁定鱼所在的容器。 */
		FGuid ContainerId;
		/** 被锁定鱼实例的不可变副本。 */
		FCatFishInstance Fish;
		/** Items 是否已经不可逆删除该鱼。 */
		bool bCommitted = false;
		/** 不可逆提交后的容器 Revision；重复提交原样返回。 */
		int64 CommittedRevision = 0;
	};

	/** 一条进行中的偷鱼 escrow；鱼从源数组移除但槽位仍预留，直到追回或吃掉。 */
	struct FTheftEscrowRecord
	{
		/** 最初客户端意图的 RequestId；只用于返回关联和 Begin 终态重放。 */
		FGuid ClientRequestId;
		/** 鱼被拿走前的源容器。 */
		FGuid SourceContainerId;
		/** 被拿走的完整实物鱼。 */
		FCatFishInstance Fish;
		/** 偷取者服务器私有 StableNetId。 */
		FString ThiefStableNetId;
		/** 售卖是否已经进入可恢复准备态；准备态会阻止追回和吃掉，只允许同一售鱼请求完成或取消。 */
		bool bSalePrepared = false;
		/** 进入售卖准备态的请求 ID；用于重试 drain 和取消时确认仍是同一玩家意图。 */
		FGuid SaleRequestId;
	};

	/** 只允许 friend Social 建立一条鱼的可追回 escrow；成功时源容器原子移除并预留返还槽位。 */
	FCatFishTheftResult BeginFishTheft(const FCatFishTheftCommand& Command);

	/** 只允许 friend Social 在追回窗口内把 escrow 鱼原位归还；预留槽确保不会因容量产生第二次丢失。 */
	FCatFishTheftResult ReturnStolenFish(FGuid TheftProtocolId);

	/** 只允许 friend Social 在进食窗口结束后不可逆消费 escrow；返回鱼定义供 Character 应用食用效果。 */
	FCatFishTheftResult CommitStolenFishConsumption(FGuid TheftProtocolId);

	/** 只允许 friend Social 在写钱包前把 escrow 标记为售卖准备态；准备态阻止追回/吃掉，避免钱鱼双得。 */
	FCatFishTheftResult PrepareStolenFishSale(FGuid TheftProtocolId, FGuid SaleRequestId, const FString& ThiefStableNetId);

	/** 只允许 friend Social 在钱包入账失败时取消售卖准备态；取消后追回/吃掉窗口恢复原语义。 */
	FCatFishTheftResult CancelPreparedStolenFishSale(FGuid TheftProtocolId, FGuid SaleRequestId, const FString& ThiefStableNetId);

	/** 只允许 friend Social 在钱包已经入账后 drain 同一准备态 escrow；不同 RequestId 或身份不能删除鱼。 */
	FCatFishTheftResult CommitPreparedStolenFishSale(FGuid TheftProtocolId, FGuid SaleRequestId, const FString& ThiefStableNetId);

	/** 向 friend Social 返回容器服务器宿主、种类与私有主人；用于权威空间/主人校验，不进入复制 DTO。 */
	bool TryGetContainerAuthorityContext(FGuid ContainerId, ECatContainerKind& OutKind,
		FString& OutOwnerStableNetId, AActor*& OutAuthorityActor) const;

	/**
	 * 声明：献祭预留与商店售卖冻结共用的第一阶段，把容器里一条鱼锁到指定持有目的下；成功不删除鱼，只推进 Revision。
	 * 实现：先按目的+身份+容器+RequestId 用共享模板查终态缓存做幂等重放，命中成功终态时再确认预留记录仍然存在——
	 *       记录已被释放说明那把锁早就没了，此时返回 Cancelled 而不是重放一句过期的"冻结成功"；随后依次校验命令门、
	 *       容器、Revision、鱼是否存在、持有权限与是否已被其他持有占用，最后写锁、推进 Revision 并发布快照。
	 * OutFish：非空时只在锁仍然成立的合法重放和首次成功路径回填被冻结的鱼；载荷漂移、已释放的重放与各类拒绝一律不回填。
	 */
	FCatFishReservationResult HoldFish(const FCatDomainCommandContext& Context, FGuid ContainerId,
		FGuid FishInstanceId, const TCHAR* HoldPurpose, FCatFishInstance* OutFish);

	/**
	 * 声明：释放一条尚未提交的持有，鱼原位恢复可写；已提交的持有不会被回滚。
	 * 实现：按目的键定位记录与容器，移除鱼锁与记录，推进 Revision 并发布；已提交记录只返回 AlreadyResolved 与提交 Revision。
	 * OutFish：非空且找到记录时回填被持有的鱼，便于调用方在回退日志里指明是哪条鱼。
	 */
	FCatDomainCommandResult ReleaseFishHold(const FString& StableNetId, FGuid ContainerId, FGuid RequestId,
		const TCHAR* HoldPurpose, FCatFishInstance* OutFish);

	/**
	 * 声明：把一条已持有的鱼不可逆移出容器，这是献祭与售卖共同的不可逆点。
	 * 实现：按目的键定位记录与容器；已提交记录原样返回首次事实，否则确认鱼锁仍属于该键且鱼仍在数组，
	 *       再删除数组元素、释放鱼锁、推进并记录提交 Revision 并发布。
	 * OutFish：非空且找到记录时回填被消费的鱼；重放同样回填，保证重试方拿到与首次一致的事实。
	 */
	FCatFishReservationCommitResult CommitFishHold(const FString& StableNetId, FGuid ContainerId, FGuid RequestId,
		const TCHAR* HoldPurpose, FCatFishInstance* OutFish);

	/** 为容器发布新快照；组件失效不回滚服务器事务。 */
	void PublishContainer(FContainerRecord& Record);

	/** 组合身份、操作、聚合 ID 与 RequestId 的稳定私有终态键；原始身份不进入日志或复制。 */
	static FString MakeTerminalKey(const FString& StableNetId, const TCHAR* Operation, const FGuid& AggregateId,
		const FGuid& RequestId);
	/** 组合持有目的、服务器身份、容器聚合与外部 RequestId 的持有键；另一玩家不能重放或解锁该记录，另一种持有目的也不能。 */
	static FString MakeReservationKey(const FString& StableNetId, const FGuid& ContainerId, const FGuid& RequestId,
		const TCHAR* HoldPurpose);

	/** 统计某容器仍在偷鱼 escrow 中的返还槽位；新增捕获/转移必须把它计入容量。 */
	int32 CountReservedReturnSlots(FGuid ContainerId) const;

	/** 当前 World 的所有容器真相。 */
	TMap<FGuid, FContainerRecord> Containers;
	/** FishInstanceId 到当前持有键的唯一锁；一条鱼同时只能被一个持有占用，转移、进食、偷取与另一次持有都据此拒绝。 */
	TMap<FGuid, FString> ReservationByFish;
	/** 持有键到预留/提交记录；Items commit 后保留到 World 销毁供重试。 */
	TMap<FString, FReservationRecord> Reservations;
	/**
	 * 每个持有键第一次受理时得到的完整结果，成功与拒绝都记。它是"这个请求当年的答复"，不是"这条鱼现在的状态"：
	 * 条目从不随取消一起删除，正因为要靠它挡住"取消之后拿同一个 RequestId 再锁一次鱼"；
	 * 也正因为它比 Reservations 里那把锁活得久，HoldFish 重放成功终态前必须再查一次锁是否还在。
	 */
	TMap<FString, FCatFishReservationResult> ReservationTerminalCache;
	/** 持有命令首次受理时的业务载荷签名；同 RequestId 只能重放同一条鱼和同一版本前提。 */
	TMap<FString, FString> ReservationPayloadByKey;
	/** 服务器 TheftProtocolId 到当前偷鱼 escrow；追回、吃掉和售出提交这三条终态都会移除它。 */
	TMap<FGuid, FTheftEscrowRecord> TheftEscrows;
	/** 偷鱼开始命令的首次完整终态缓存；重放不重复移除鱼。 */
	TMap<FString, FCatFishTheftResult> TheftTerminalCache;
	/** 偷鱼开始命令首次受理时的业务载荷签名；同 RequestId 不能换协议 ID 或目标鱼。 */
	TMap<FString, FString> TheftPayloadByKey;
	/** 捕获命令的首次完整终态缓存。 */
	TMap<FString, FCatCaptureCommitResult> CaptureTerminalCache;
	/** 捕获命令首次受理时的业务载荷签名；同 RequestId 不能换目标鱼、容器或冻结数值。 */
	TMap<FString, FString> CapturePayloadByKey;
	/** FishingSessionId 到唯一捕获提交事实；即使换身份或 RequestId，也不能为同一会话创建第二条鱼。 */
	TMap<FGuid, FCatCaptureCommittedResult> CaptureByFishingSession;
	/** 转移命令的首次完整终态缓存。 */
	TMap<FString, FCatDomainCommandResult> TransferTerminalCache;
	/** 转移命令首次受理时的业务载荷签名；同 RequestId 不能换鱼、目标容器或双 Revision 前提。 */
	TMap<FString, FString> TransferPayloadByKey;
	/** 直接吃鱼命令的首次完整终态缓存。 */
	TMap<FString, FCatFishConsumeResult> ConsumeTerminalCache;
	/** 直接吃鱼命令首次受理时的业务载荷签名；同 RequestId 不能换鱼或源 Revision 前提。 */
	TMap<FString, FString> ConsumePayloadByKey;
	/** teardown 后永久关闭本 World 的新 Items 命令。 */
	bool bCommandsOpen = true;
};
