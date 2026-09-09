#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatSacrificeContracts.h"
#include "Subsystems/WorldSubsystem.h"
#include "Items/CatItemTypes.h"
#include "CatItemsService.generated.h"

class UCatContainerReplicationComponent;
class UCatSocialService;

/** Items 向持久化边界导出的已提交世界鱼容器；它属于 Items 协议，不携带磁盘格式、槽位或 SaveGame 对象。 */
USTRUCT()
struct FCatPersistentContainerSnapshot
{
	GENERATED_BODY()

	/** 关卡宿主路径或 Items 分配并随快照延续的实体键；恢复保留原键，不使用运行期 ContainerId 或随机 GUID 定位。 */
	UPROPERTY(SaveGame)
	FString PersistentKey;

	/** 宿主是否由运行时生成；true 时 Items 按已保存类和位置重建，false 时必须找到原关卡对象。 */
	UPROPERTY(SaveGame)
	bool bRuntimeCreated = false;

	/** 原始鱼护或鱼缸的实际宿主类；恢复仅允许对应领域 Actor 派生类，不能从磁盘任意生成其他 Actor。 */
	UPROPERTY(SaveGame)
	TSoftClassPtr<AActor> HostClass;

	/** 宿主在保存时的世界位置、旋转和比例；动态容器恢复时原样使用，位置单位为厘米。 */
	UPROPERTY(SaveGame)
	FTransform HostTransform = FTransform::Identity;

	/** 宿主中注册容器的组件名；生成后按它核对注册记录，避免把内容写进同 Actor 的其他容器。 */
	UPROPERTY(SaveGame)
	FName ComponentName = NAME_None;

	/** 容器的 Items 领域种类；恢复时必须与当前宿主完全一致，不能把鱼护内容迁到鱼缸。 */
	UPROPERTY(SaveGame)
	ECatContainerKind Kind = ECatContainerKind::Unknown;

	/** 容器中已提交的鱼；预留、escrow、请求缓存和 FastArray 派生投影均被排除。 */
	UPROPERTY(SaveGame)
	TArray<FCatFishInstance> Fish;
};

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

	/** 注册真实宿主并建立网络 ID 与持久键，发布初始容量；恢复窗口只允许当前预期宿主登记，意外重入使本次恢复失败。 */
	bool RegisterContainer(UCatContainerReplicationComponent* ReplicationComponent, FGuid ContainerId,
		ECatContainerKind Kind, int32 Capacity);

	/** 宿主离开时按精确组件解除登记，包含正在销毁的组件；恢复中非预期注销会封锁提交，已有终态不会迁移到其他容器。 */
	void UnregisterContainer(UCatContainerReplicationComponent* ReplicationComponent);

	/** 复制指定容器已提交的公开事实供上层校验 Revision；不存在时整体失败，预留事实始终留在服务端记录。 */
	bool TryGetContainerSnapshot(FGuid ContainerId, FCatContainerSnapshot& OutSnapshot) const;

	/** 返回容器的服务器种类与真实 Actor 宿主；供空间权限校验使用，授权身份仍从鱼实例或调用方上下文读取。 */
	bool TryGetContainerHost(FGuid ContainerId, ECatContainerKind& OutKind, AActor*& OutAuthorityActor) const;

	/** 为上层售鱼链准备容器里的鱼事实；Items 裁决容器类型、版本、锁定状态和鱼护归属，Shop 只能读取重量和实例 ID。 */
	bool TryPrepareFishForSaleFromContainer(FGuid FishInstanceId, FGuid ContainerId,
		int64 ExpectedContainerRevision, const FString& SellerStableNetId, FCatFishInstance& OutFish,
		int64& OutContainerRevision, ECatDomainCommandError& OutError) const;

	/** 导出地图与动态世界鱼容器的完整状态，包含空箱；动态宿主保留实体键、类和位置，未收口的事务使导出明确失败。 */
	bool ExportPersistedWorldFishContainers(TArray<FCatPersistentContainerSnapshot>& OutContainers, FText& OutFailure) const;

	/** 只读预检保存的世界鱼容器能否映射到当前地图宿主；检查稳定键、种类、容量、定义、鱼实例唯一性和短生命周期事务。 */
	bool CanRestorePersistedWorldFishContainers(const TArray<FCatPersistentContainerSnapshot>& SavedContainers,
		FText& OutFailure) const;

	/** 预检后创建全部动态宿主并核对登记，确认旧动态宿主销毁后才提交鱼数组；销毁或回调异常会关闭本 World 写口并返回失败，调用方不得继续进局。 */
	bool RestorePersistedWorldFishContainers(const TArray<FCatPersistentContainerSnapshot>& SavedContainers);

	/** 嘴叼世界鱼对具体鱼护入箱时的唯一提交入口；恢复期间拒绝新提交，同 RequestId/会话重放只返回首次 Committed DTO。 */
	FCatCaptureCommitResult CommitCapture(const FCatCaptureCommitCommand& Command);

	/** 在源/目标容器版本同时匹配时原子移动一个容器物体；当前只有鱼策略会提交，其他类别先显式拒绝，调用方不能直接写数组。 */
	FCatDomainCommandResult TransferContainedObject(const FCatContainerObjectTransferCommand& Command);

	/** 在源/目标版本匹配且不处于恢复窗口时原子移动一条鱼；UI/RPC 应优先走通用容器物体入口。 */
	FCatDomainCommandResult TransferOwnedFish(const FCatFishTransferCommand& Command);

	/** 非恢复窗口中从鱼护或共享鱼缸移除未预留鱼；成功后上层才可应用食用效果。 */
	FCatFishConsumeResult ConsumeFish(const FCatFishConsumeCommand& Command);

	/** 只读查询直接吃鱼请求是否已有 Items 终态；命中前校验鱼实例和版本签名，不命中时不读取或修改容器。 */
	bool TryReplayFishConsumeTerminal(const FCatFishConsumeCommand& Command, FCatFishConsumeResult& OutResult) const;

	/** 非恢复窗口中由献祭协调器锁定一条鱼；预留增加 Revision 但不从复制数组删除。 */
	FCatFishReservationResult ReserveFish(const FCatSacrificeCommand& Command);

	/** Items commit 前按服务器身份、容器与 RequestId 取消精确预留；已提交或不匹配时不恢复鱼。 */
	FCatDomainCommandResult CancelFishReservation(const FString& StableNetId, FGuid RequestId, FGuid ContainerId);

	/** 按服务器身份、容器与 RequestId 不可逆移除已预留鱼；成功和重复提交返回同一贡献与 Revision。 */
	FCatFishReservationCommitResult CommitFishReservation(const FString& StableNetId, FGuid RequestId, FGuid ContainerId);

	/** Host teardown 关闭新写口并取消所有尚未提交的预留；已提交记录只供协调器补 Run。 */
	void CloseCommandsAndCancelReservations();

private:
	friend class UCatSocialService;

	/** 单容器服务器记录；公开快照含容量和槽位事实，预留只通过服务私有表按鱼实例单独管理。 */
	struct FContainerRecord
	{
		/** 当前提交后的公开鱼槽数组、容量与 Revision。 */
		FCatContainerSnapshot Snapshot;
		/** 显式产品容量；0 表示未裁。 */
		int32 Capacity = 0;
		/** 接收复制快照的真实组件弱引用。 */
		TWeakObjectPtr<UCatContainerReplicationComponent> ReplicationComponent;
		/** 随该容器持久化的实体键；注册时建立、恢复时继承保存值，与网络 ContainerId 分离。 */
		FString PersistentKey;
		/** 宿主是否需要由持久化重建；关卡原生宿主必须原位映射，动态宿主按快照类和位置再生。 */
		bool bRuntimeCreated = false;
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

	/** 非恢复窗口中只允许 friend Social 建立鱼的可追回 escrow；成功时源容器原子移除并预留返还槽位。 */
	FCatFishTheftResult BeginFishTheft(const FCatFishTheftCommand& Command);

	/** 只允许 friend Social 在追回窗口内把 escrow 鱼原位归还；预留槽确保不会因容量产生第二次丢失。 */
	FCatFishTheftResult ReturnStolenFish(FGuid TheftProtocolId);

	/** 只允许 friend Social 在进食窗口结束后不可逆消费 escrow；返回鱼定义供 Character 应用食用效果。 */
	FCatFishTheftResult CommitStolenFishConsumption(FGuid TheftProtocolId);

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
	/** 当前 World 尚未分配的动态持久实体序号；注册递增，恢复按保存键继续，绝不采用随机网络 GUID。 */
	int64 NextPersistentContainerNumber = 1;
	/** 恢复正在创建动态宿主的短同步窗口；普通 Items 命令暂停，宿主的 RegisterContainer 仍可完成配对。 */
	bool bRestoringPersistentContainers = false;

	/** 恢复当前正在创建或销毁的唯一宿主；注册与注销只接受这条生命周期配对，其他宿主的重入会封锁恢复。 */
	TWeakObjectPtr<AActor> ExpectedRestoreHost;
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
	/** 直接吃鱼终态的请求载荷签名；防止同身份同容器同 RequestId 改鱼实例或版本前提后重放旧终态。 */
	TMap<FString, FString> ConsumeTerminalPayloadByKey;
	/** teardown 后永久关闭本 World 的新 Items 命令。 */
	bool bCommandsOpen = true;
};
