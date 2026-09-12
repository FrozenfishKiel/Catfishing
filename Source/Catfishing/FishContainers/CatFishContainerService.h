#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FishContainers/CatFishContainerTypes.h"
#include "CatFishContainerService.generated.h"

class UCatContainerReplicationComponent;
class AController;
class ACatCharacter;

/** 一局服务器鱼容器模块；它是鱼容器数组、捕获创建、转移与消费的唯一写入口。
 *  没有「偷取」这条支线：拿鱼就是转移，机制层不问动机也不问归属（2026-09-11 拍）。 */
UCLASS()
class CATFISHING_API UCatFishContainerService : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 仅在 authority World 创建服务；客户端只消费容器复制组件。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** World 销毁时关闭新命令并清空仅属本局的容器与终态缓存。 */
	virtual void Deinitialize() override;

	/** 注册真实宿主并建立网络 ID 与持久键，发布初始容量；恢复窗口只允许当前预期宿主登记，意外重入使本次恢复失败。 */
	bool RegisterContainer(UCatContainerReplicationComponent* ReplicationComponent, FGuid ContainerId,
		ECatContainerKind Kind, int32 Capacity);

	/** 宿主离开时按精确组件解除登记，包含正在销毁的组件；恢复中非预期注销会封锁提交，已有终态不会自动改挂到其他容器。 */
	void UnregisterContainer(UCatContainerReplicationComponent* ReplicationComponent);

	/**
	 * 复制指定容器已提交的公开事实供上层读取；不存在时整体失败。
	 * 开成 BlueprintCallable 只是为了让服务器侧表现与验收蓝图能读到这份事实；它没有写口，
	 * 而且本服务只在 authority World 创建，客户端调用只会拿到 false。
	 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|FishContainers")
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

	/** Host teardown 关闭容器写口；容器里没有可逆的中间态事务，关门之后没有要收口的东西。 */
	void CloseCommandsFromAuthority();

private:
	/** 单容器服务器记录；公开快照含容量和槽位事实。 */
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

	/** 为容器发布新快照；组件失效不回滚服务器事务。 */
	void PublishContainer(FContainerRecord& Record);

	/** 组合身份、操作、聚合 ID 与 RequestId 的稳定私有终态键；原始身份不进入日志或复制。 */
	static FString MakeTerminalKey(const FString& StableNetId, const TCHAR* Operation, const FGuid& AggregateId,
		const FGuid& RequestId);

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
