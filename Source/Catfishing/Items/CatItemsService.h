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

	/** 注册一个真实容器宿主并发布初始 Revision 和正式容量；个人容器身份只保存在服务端记录。 */
	bool RegisterContainer(UCatContainerReplicationComponent* ReplicationComponent, FGuid ContainerId,
		ECatContainerKind Kind, const FString& OwnerStableNetId, int32 Capacity);

	/** 宿主离开 World 时按精确组件解除发布目标；已有终态不会迁移到其他容器。 */
	void UnregisterContainer(UCatContainerReplicationComponent* ReplicationComponent);

	/** 复制指定容器已提交的公开事实供上层校验 Revision；不存在时整体失败，预留和主人身份始终留在服务端记录。 */
	bool TryGetContainerSnapshot(FGuid ContainerId, FCatContainerSnapshot& OutSnapshot) const;

	/** 返回容器的服务器种类与真实 Actor 宿主；供空间权限校验使用，不暴露私有主人身份。 */
	bool TryGetContainerHost(FGuid ContainerId, ECatContainerKind& OutKind, AActor*& OutAuthorityActor) const;

	/** 首个合法抢抄终态调用的唯一鱼实例创建入口；提交会话预分配 ID，同 RequestId/会话重放只返回首次 Committed DTO。 */
	FCatCaptureCommitResult CommitCapture(const FCatCaptureCommitCommand& Command);

	/** 在源/目标容器版本同时匹配时原子移动一个容器物体；当前只有鱼策略会提交，其他类别先显式拒绝，调用方不能直接写数组。 */
	FCatDomainCommandResult TransferContainedObject(const FCatContainerObjectTransferCommand& Command);

	/** 在源/目标容器版本同时匹配时原子移动一条鱼；这是鱼领域策略适配层，UI/RPC 应优先走通用容器物体入口。 */
	FCatDomainCommandResult TransferOwnedFish(const FCatFishTransferCommand& Command);

	/** 从本人鱼护或共享鱼缸不可逆移除一条未预留鱼；成功后上层才可应用食用效果。 */
	FCatFishConsumeResult ConsumeFish(const FCatFishConsumeCommand& Command);

	/** 献祭协调器在不可逆点前锁定一条鱼；预留增加 Revision 但不从复制数组删除。 */
	FCatFishReservationResult ReserveFish(const FCatSacrificeCommand& Command);

	/** Items commit 前按服务器身份、容器与 RequestId 取消精确预留；已提交或不匹配时不恢复鱼。 */
	FCatDomainCommandResult CancelFishReservation(const FString& StableNetId, FGuid RequestId, FGuid ContainerId);

	/** 按服务器身份、容器与 RequestId 不可逆移除已预留鱼；成功和重复提交返回同一贡献与 Revision。 */
	FCatFishReservationCommitResult CommitFishReservation(const FString& StableNetId, FGuid RequestId, FGuid ContainerId);

	/** Host teardown 关闭新写口并取消所有尚未提交的预留；已提交记录只供协调器补 Run。 */
	void CloseCommandsAndCancelReservations();

private:
	friend class UCatSocialService;

	/** 单容器服务器记录；公开快照含容量和槽位事实，但预留与 OwnerStableNetId 只留在服务器私有记录。 */
	struct FContainerRecord
	{
		/** 当前提交后的公开鱼槽数组、容量与 Revision。 */
		FCatContainerSnapshot Snapshot;
		/** 个人鱼护的服务器私有 StableNetId；共享鱼缸为空。 */
		FString OwnerStableNetId;
		/** 显式产品容量；0 表示未裁。 */
		int32 Capacity = 0;
		/** 接收复制快照的真实组件弱引用。 */
		TWeakObjectPtr<UCatContainerReplicationComponent> ReplicationComponent;
	};

	/** 一条献祭预留的服务器私有事实；Fish 副本用于不可逆提交后的幂等重放。 */
	struct FReservationRecord
	{
		/** 外部献祭 RequestId，也是唯一 ReservationId。 */
		FGuid RequestId;
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
		/** Social 分配的服务器唯一协议 ID；它是 escrow 主键，不与客户端 RequestId 混用。 */
		FGuid TheftProtocolId;
		/** 最初客户端意图的 RequestId；只用于返回关联和 Begin 终态重放。 */
		FGuid ClientRequestId;
		/** 鱼被拿走前的源容器。 */
		FGuid SourceContainerId;
		/** 鱼被拿走前的源容器槽位；追回时优先放回这个位置，避免数组压缩改变 UI 格子语义。 */
		int32 SourceContainerSlotIndex = INDEX_NONE;
		/** 被拿走的完整实物鱼。 */
		FCatFishInstance Fish;
		/** 偷取者服务器私有 StableNetId。 */
		FString ThiefStableNetId;
	};

	/** 只允许 friend Social 建立一条鱼的可追回 escrow；成功时源容器原子移除并预留返还槽位。 */
	FCatFishTheftResult BeginFishTheft(const FCatFishTheftCommand& Command);

	/** 只允许 friend Social 在追回窗口内把 escrow 鱼原位归还；预留槽确保不会因容量产生第二次丢失。 */
	FCatFishTheftResult ReturnStolenFish(FGuid TheftProtocolId);

	/** 只允许 friend Social 在进食窗口结束后不可逆消费 escrow；返回鱼定义供 Character 应用食用效果。 */
	FCatFishTheftResult CommitStolenFishConsumption(FGuid TheftProtocolId);

	/** 向 friend Social 返回容器服务器宿主、种类与私有主人；用于权威空间/主人校验，不进入复制 DTO。 */
	bool TryGetContainerAuthorityContext(FGuid ContainerId, ECatContainerKind& OutKind,
		FString& OutOwnerStableNetId, AActor*& OutAuthorityActor) const;

	/** 为容器发布新快照；组件失效不回滚服务器事务。 */
	void PublishContainer(FContainerRecord& Record);

	/** 组合身份、操作、聚合 ID 与 RequestId 的稳定私有终态键；原始身份不进入日志或复制。 */
	static FString MakeTerminalKey(const FString& StableNetId, const TCHAR* Operation, const FGuid& AggregateId,
		const FGuid& RequestId);
	/** 组合服务器身份、容器聚合与献祭 RequestId 的预留键；另一玩家不能重放或解锁该记录。 */
	static FString MakeReservationKey(const FString& StableNetId, const FGuid& ContainerId, const FGuid& RequestId);

	/** 统计某容器仍在偷鱼 escrow 中的返还槽位；新增捕获/转移必须把它计入容量。 */
	int32 CountReservedReturnSlots(FGuid ContainerId) const;

	/** 当前 World 的所有容器真相。 */
	TMap<FGuid, FContainerRecord> Containers;
	/** FishInstanceId 到容器+RequestId 预留键的当前锁；防止转移/双预留。 */
	TMap<FGuid, FString> ReservationByFish;
	/** 容器+RequestId 到预留/提交记录；Items commit 后保留到 World 销毁供重试。 */
	TMap<FString, FReservationRecord> Reservations;
	/** 服务器 TheftProtocolId 到当前偷鱼 escrow；追回或吃掉后移除。 */
	TMap<FGuid, FTheftEscrowRecord> TheftEscrows;
	/** 偷鱼开始命令的首次完整终态缓存；重放不重复移除鱼。 */
	TMap<FString, FCatFishTheftResult> TheftTerminalCache;
	/** 捕获命令的首次完整终态缓存。 */
	TMap<FString, FCatCaptureCommitResult> CaptureTerminalCache;
	/** FishingSessionId 到唯一捕获提交事实；即使换身份或 RequestId，也不能为同一会话创建第二条鱼。 */
	TMap<FGuid, FCatCaptureCommittedResult> CaptureByFishingSession;
	/** 转移命令的首次完整终态缓存。 */
	TMap<FString, FCatDomainCommandResult> TransferTerminalCache;
	/** 直接吃鱼命令的首次完整终态缓存。 */
	TMap<FString, FCatFishConsumeResult> ConsumeTerminalCache;
	/** teardown 后永久关闭本 World 的新 Items 命令。 */
	bool bCommandsOpen = true;
};
