#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FishContainers/CatFishContainerTypes.h"
#include "CatFishContainerService.generated.h"

class UCatContainerReplicationComponent;
class UCatSocialService;
class AController;
class ACatCharacter;

/** 一局服务器鱼容器模块；它是鱼容器数组、捕获创建、转移、偷取与消费的唯一写入口。 */
UCLASS()
class CATFISHING_API UCatFishContainerService : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 仅在 authority World 创建服务；客户端只消费容器复制组件。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** World 销毁时关闭新命令并清空仅属本局的容器、偷鱼窗口与终态缓存。 */
	virtual void Deinitialize() override;

	/** 注册真实宿主并建立网络 ID 与持久键，发布初始容量；恢复窗口只允许当前预期宿主登记，意外重入使本次恢复失败。 */
	bool RegisterContainer(UCatContainerReplicationComponent* ReplicationComponent, FGuid ContainerId,
		ECatContainerKind Kind, int32 Capacity);

	/** 宿主离开时按精确组件解除登记，包含正在销毁的组件；恢复中非预期注销会封锁提交，已有终态不会自动改挂到其他容器。 */
	void UnregisterContainer(UCatContainerReplicationComponent* ReplicationComponent);

	/** 复制指定容器已提交的公开事实供上层读取；不存在时整体失败，偷鱼窗口始终留在服务端记录。 */
	bool TryGetContainerSnapshot(FGuid ContainerId, FCatContainerSnapshot& OutSnapshot) const;

	/** 返回容器的服务器种类与真实 Actor 宿主；供空间权限校验使用，授权身份仍从鱼实例或调用方上下文读取。 */
	bool TryGetContainerHost(FGuid ContainerId, ECatContainerKind& OutKind, AActor*& OutAuthorityActor) const;

	/** 导出地图与动态世界鱼容器的完整状态，包含空箱；动态宿主保留实体键、类和位置，未收口的事务使导出明确失败。 */
	bool ExportPersistedWorldFishContainers(TArray<FCatPersistentContainerSnapshot>& OutContainers, FText& OutFailure) const;

	/** 恢复世界鱼容器；内部先校验宿主、容量、定义和实例唯一性，再创建动态宿主并提交鱼数组，失败时关闭本 World 写口。 */
	bool RestorePersistedWorldFishContainers(const TArray<FCatPersistentContainerSnapshot>& SavedContainers);

	/** 嘴叼世界鱼对具体鱼护入箱时的唯一提交入口；恢复期间拒绝新提交，同 RequestId/会话重放只返回首次 Committed DTO。 */
	FCatCaptureCommitResult CommitCapture(const FCatCaptureCommitCommand& Command);

	/** 不处于恢复窗口时原子移动一条鱼；UI/RPC 只能提交鱼实例与槽位。 */
	FCatDomainCommandResult TransferOwnedFish(const FCatFishTransferCommand& Command);

	/** Controller 在服务器上发起直接吃鱼时调用；本服务会用服务器身份重读可触达的鱼护或共享鱼缸，并在容器移除成功或终态重放成功后才把食用效果交给目标 Character。 */
	FCatFishConsumeResult ConsumeReachableFish(AController* RequestingController,
		ACatCharacter* EatingCharacter, FCatFishConsumeCommand Command);

	/** 非恢复窗口中从鱼护或共享鱼缸移除目标鱼；成功后上层才可应用食用效果。 */
	FCatFishConsumeResult ConsumeFish(const FCatFishConsumeCommand& Command);

	/** 只读查询直接吃鱼请求是否已有鱼容器终态；命中前校验鱼实例签名，不命中时不读取或修改容器。 */
	bool TryReplayFishConsumeTerminal(const FCatFishConsumeCommand& Command, FCatFishConsumeResult& OutResult) const;

	/** Host teardown 关闭容器写口，并让仍在追回窗口里的偷鱼记录优先回到源容器。 */
	void CloseCommandsFromAuthority();

private:
	friend class UCatSocialService;

	/** 单容器服务器记录；公开快照含容量和槽位事实，偷鱼窗口只保存待归还的源槽。 */
	struct FContainerRecord
	{
		/** 当前提交后的公开鱼槽数组、容量与快照序号。 */
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

	/** 一条进行中的偷鱼 escrow；鱼从源数组移除并记住源槽，直到追回或吃掉。 */
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

	/** 非恢复窗口中只允许 friend Social 建立鱼的可追回 escrow；成功时源容器原子移除并记录返还槽位。 */
	FCatFishTheftResult BeginFishTheft(const FCatFishTheftCommand& Command);

	/** 只允许 friend Social 在追回窗口内把 escrow 鱼原位归还；源槽空位能减少容量变化造成的二次丢失。 */
	FCatFishTheftResult ReturnStolenFish(FGuid TheftProtocolId);

	/** 只允许 friend Social 在进食窗口结束后不可逆消费 escrow；返回鱼定义供 Character 应用食用效果。 */
	FCatFishTheftResult CommitStolenFishConsumption(FGuid TheftProtocolId);

	/** 为容器发布新快照；组件失效不回滚服务器事务。 */
	void PublishContainer(FContainerRecord& Record);

	/** 组合身份、操作、聚合 ID 与 RequestId 的稳定私有终态键；原始身份不进入日志或复制。 */
	static FString MakeTerminalKey(const FString& StableNetId, const TCHAR* Operation, const FGuid& AggregateId,
		const FGuid& RequestId);

	/** 统计某容器仍在偷鱼 escrow 中的待归还槽位；新增捕获/转移必须把它计入容量。 */
	int32 CountPendingReturnSlots(FGuid ContainerId) const;

	/** 校验保存的世界鱼容器能否映射到当前地图宿主；它是服务内部步骤，只服务导出自检和恢复入口，不对 Save 暴露第二条流程。 */
	bool ValidatePersistedWorldFishContainersForRestore(
		const TArray<FCatPersistentContainerSnapshot>& SavedContainers, FText& OutFailure) const;

	/** 当前 World 的所有容器真相。 */
	TMap<FGuid, FContainerRecord> Containers;
	/** 当前 World 尚未分配的动态持久实体序号；注册递增，恢复按保存键继续，绝不采用随机网络 GUID。 */
	int64 NextPersistentContainerNumber = 1;
	/** 恢复正在创建动态宿主的短同步窗口；普通鱼容器命令暂停，宿主的 RegisterContainer 仍可完成配对。 */
	bool bRestoringPersistentContainers = false;

	/** 恢复当前正在创建或销毁的唯一宿主；注册与注销只接受这条生命周期配对，其他宿主的重入会封锁恢复。 */
	TWeakObjectPtr<AActor> ExpectedRestoreHost;
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
	/** 直接吃鱼终态的请求载荷签名；防止同身份同容器同 RequestId 改鱼实例后重放已记录终态。 */
	TMap<FString, FString> ConsumeTerminalPayloadByKey;
	/** teardown 后永久关闭本 World 的新鱼容器命令。 */
	bool bCommandsOpen = true;
};
