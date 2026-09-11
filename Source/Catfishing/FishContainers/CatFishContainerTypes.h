#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "CatFishContainerTypes.generated.h"

class AActor;

/** 鱼容器的宿主类别；每个类别都有明确的服务器 Actor 宿主，客户端只把它当显示和请求复核上下文。 */
UENUM(BlueprintType)
enum class ECatContainerKind : uint8
{
	/** 容器尚未注册或种类未裁。 */
	Unknown = 0,
	/** 关卡中共享鱼缸 Actor 承载的团队容器。 */
	SharedFishTank = 2,
	/** 关卡中可交互鱼护箱子承载的鱼容器；它不绑定玩家身份，也不套用鱼缸展示资格。 */
	FishGuard = 3
};

/** 一条局内实物鱼；与图鉴/印记 Grant 的永久事实分离，容器删除不能回滚捕获记录。 */
USTRUCT(BlueprintType)
struct FCatFishInstance
{
	GENERATED_BODY()

	/** 捕获提交前由服务器会话分配、并在成功时只写入一次的局内鱼实例 ID；转移、印记和鱼去向结算始终引用它。 */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FGuid FishInstanceId;

	/** 真实鱼表资产中的稳定定义 ID；没有定义时不创建实例。 */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FName FishDefinitionId = NAME_None;

	/** 鱼实例的服务器私有捕获者 StableNetId；仅权威领域与存档读写，RepSkip 排除 FastArray 嵌套复制，不向客户端暴露身份。 */
	UPROPERTY(SaveGame, NotReplicated)
	FString OwnerStableNetId;

	/** 产生该实例的 FishingSession ID；用于捕获幂等审计，不用于恢复已经结束的会话。 */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FGuid SourceFishingSessionId;

	/** 捕获时由服务器鱼运行态给出的真实重量，单位千克；非有限或非正值不得创建实例。 */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	double WeightKilograms = 0.0;
};

/** 一个鱼容器的复制读模型；服务端事务整体发布，网络出口只复制正式鱼槽事实。 */
USTRUCT(BlueprintType)
struct FCatContainerSnapshot
{
	GENERATED_BODY()

	/** 容器的一局稳定 ID；命令终态缓存、复制快照和持久化映射都用它确认同一个鱼护或鱼缸。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid ContainerId;

	/** 容器所有权类别；客户端只用它渲染，不据此自行授权。 */
	UPROPERTY(BlueprintReadOnly)
	ECatContainerKind Kind = ECatContainerKind::Unknown;

	/** 容器快照序号；每次鱼槽事实成功变化后递增，供复制和恢复流程判断快照新旧。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;

	/** 容器当前被正式玩法裁定的格子容量；UI 用它决定 WrapBox 创建多少格，不把它当成扩容写口。 */
	UPROPERTY(BlueprintReadOnly)
	int32 Capacity = 0;

	/** 当前容器的鱼槽数组；数组下标就是容器格子，FishInstanceId 无效的条目表示中间空格占位。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatFishInstance> Fish;
};

/** 已提交世界鱼容器的持久化快照；保存层只读写这份数据，不依赖鱼容器写入口类。 */
USTRUCT()
struct FCatPersistentContainerSnapshot
{
	GENERATED_BODY()

	/** 关卡宿主路径或鱼容器服务分配的实体键；恢复用它重新关联宿主，不使用运行期 ContainerId 或随机 GUID 定位。 */
	UPROPERTY(SaveGame)
	FString PersistentKey;

	/** 宿主是否由运行时生成；true 时恢复会按已保存类和位置重建，false 时必须找到原关卡对象。 */
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

	/** 容器的鱼容器领域种类；恢复时必须与当前宿主完全一致，不能把鱼护内容迁到鱼缸。 */
	UPROPERTY(SaveGame)
	ECatContainerKind Kind = ECatContainerKind::Unknown;

	/** 容器中已提交的鱼；偷鱼窗口、请求缓存和 FastArray 派生投影均被排除。 */
	UPROPERTY(SaveGame)
	TArray<FCatFishInstance> Fish;
};

/** FishingSession 提交给鱼容器服务的捕获命令；StableNetId 与重量均由服务器会话填写，客户端不能直接调用写口。 */
USTRUCT()
struct FCatCaptureCommitCommand
{
	GENERATED_BODY()

	/** 捕获请求的 RequestId 与首抄者服务器身份；目标容器由服务器当前状态重读。 */
	FCatDomainCommandContext Context;

	/** 当前唯一 FishingSession ID；鱼容器服务用它防止跨会话复用捕获请求。 */
	FGuid FishingSessionId;

	/** FishingSession 在提交前为捕获事实分配的稳定鱼实例 ID；鱼容器服务只验证并原样提交，使印记预检与实物使用同一主体。 */
	FGuid FishInstanceId;

	/** 服务器已解析并通过 runtime gate 的鱼种稳定 ID。 */
	FName FishDefinitionId = NAME_None;

	/** 嘴叼鱼要写入的当前交互地面鱼护 ID；共享鱼缸必须走后续 Transfer。 */
	FGuid TargetContainerId;

	/** 从服务器鱼运行态冻结的真实重量，单位千克。 */
	double WeightKilograms = 0.0;

};

/** 单条鱼原子转移命令；鱼护和共享鱼缸拖拽只提交这份鱼领域意图。 */
USTRUCT()
struct FCatFishTransferCommand
{
	GENERATED_BODY()

	/** 转移请求的身份与 RequestId。 */
	FCatDomainCommandContext Context;

	/** 源容器中要移动的唯一实物鱼；它必须仍位于 SourceContainerSlotIndex 指向的槽位。 */
	FGuid FishInstanceId;

	/** 当前源容器 ID。 */
	FGuid SourceContainerId;

	/** Drop 源格在源容器内的下标；数组槽位是本容器权威位置，不由鱼实例自己保存。 */
	int32 SourceContainerSlotIndex = INDEX_NONE;

	/** 目标容器 ID；目标容量在同一提交前检查。 */
	FGuid TargetContainerId;

	/** Drop 目标在目标容器内的下标；空槽接收移动鱼，已占用槽位在允许时与源槽交换。 */
	int32 TargetContainerSlotIndex = INDEX_NONE;


};

/** Social 提交给鱼容器服务的单条偷鱼开始命令；身份由服务器重建，客户端不能直接访问写口。 */
USTRUCT()
struct FCatFishTheftCommand
{
	GENERATED_BODY()

	/** 偷取者身份与 RequestId。 */
	FCatDomainCommandContext Context;

	/** Social 为首次合法 Begin 分配的服务器唯一协议 ID；鱼容器 escrow 只按此键索引，绝不信任客户端 RequestId 的全局唯一性。 */
	FGuid TheftProtocolId;

	/** 被偷的唯一实物鱼。 */
	FGuid FishInstanceId;

	/** 目标地面鱼护箱子或共享鱼缸容器 ID。 */
	FGuid SourceContainerId;
};

/** 鱼容器服务建立单条偷鱼 escrow 的不可变结果；鱼已离开容器但尚未吃掉，可在窗口内原位归还。 */
USTRUCT()
struct FCatFishTheftResult
{
	GENERATED_BODY()

	/** 公共命令终态；Revision 只回传源容器移除后的快照序号。 */
	FCatDomainCommandResult Command;

	/** 鱼容器服务实际使用的服务器协议 ID；Social 的计时、追回和消费必须复用它。 */
	FGuid TheftProtocolId;

	/** 进入 escrow 的唯一鱼实例；Social 只用定义 ID/原主人裁决追逐与进食。 */
	FCatFishInstance Fish;

	/** 待归还槽位所属的源容器 ID。 */
	FGuid SourceContainerId;
};

/** 直接吃鱼的命令；地面鱼护要求捕获者本人，共享鱼缸允许当前 Active 玩家但仍由服务器身份写入。 */
USTRUCT(BlueprintType)
struct FCatFishConsumeCommand
{
	GENERATED_BODY()

	/** RequestId 与服务器身份。 */
	UPROPERTY(BlueprintReadWrite)
	FCatDomainCommandContext Context;

	/** 要直接吃掉的一条实物鱼。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid FishInstanceId;

	/** 鱼当前所在的地面鱼护或共享鱼缸。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid SourceContainerId;
};

/** 直接进食的不可变鱼容器结果；成功后鱼已从容器移除，Character 才能消费定义效果。 */
USTRUCT(BlueprintType)
struct FCatFishConsumeResult
{
	GENERATED_BODY()

	/** 公共命令终态；Revision 只回传容器移除后的快照序号。 */
	UPROPERTY(BlueprintReadOnly)
	FCatDomainCommandResult Command;

	/** 实物鱼移除后提交到 Condition/Growth 的身体终态；用于区分容器成功和身体效果失败。 */
	UPROPERTY(BlueprintReadOnly)
	FCatDomainCommandResult Body;

	/** 已被不可逆吃掉的鱼事实；拒绝时保持默认。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishInstance Fish;
};

/** 捕获 Compare-and-Commit 的唯一不可变结果；Fishing、Collection 与 Imprint 只消费这份已提交事实。 */
USTRUCT(BlueprintType)
struct FCatCaptureCommittedResult
{
	GENERATED_BODY()

	/** 嘴叼世界鱼首次成功入箱的请求 ID；同一会话后续请求只能读取该终态。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid CaptureRequestId;

	/** 已原子关闭入箱竞争的 FishingSession ID。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid FishingSessionId;

	/** 唯一创建的鱼实例；其 OwnerStableNetId 必须等于提交这条嘴叼鱼的玩家。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishInstance FishInstance;

	/** 实例最终写入的容器 ID；嘴叼鱼首次入箱只允许当前交互的地面鱼护。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid ContainerId;

	/** 捕获提交后的容器快照序号；复制组件最终应收敛到不小于该值。 */
	UPROPERTY(BlueprintReadOnly)
	int64 ContainerRevision = 0;
};

/** 鱼容器捕获事务结果；首次成功包含不可变提交 DTO，缓存重放返回首次结果。 */
USTRUCT(BlueprintType)
struct FCatCaptureCommitResult
{
	GENERATED_BODY()

	/** 公共命令终态头；Revision 与捕获结果里的容器快照序号保持一致。 */
	UPROPERTY(BlueprintReadOnly)
	FCatDomainCommandResult Command;

	/** 仅首次成功或 AlreadyResolved 重放时有效的捕获事实。 */
	UPROPERTY(BlueprintReadOnly)
	FCatCaptureCommittedResult Committed;
};
