#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "CatItemTypes.generated.h"

/** 阶段 E 正式容器边界；容器都由各自独立宿主接入 Items 事务。 */
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

	/** 捕获提交前由服务器会话分配、并在成功时只写入一次的局内鱼实例 ID；转移、预留、印记和献祭始终引用它。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid FishInstanceId;

	/** 真实鱼表资产中的稳定定义 ID；没有定义时不创建实例。 */
	UPROPERTY(BlueprintReadOnly)
	FName FishDefinitionId = NAME_None;

	/** 鱼实例的服务器私有捕获者 StableNetId；用于吃鱼、售鱼、偷取与归档权限，不代表角色额外拥有容器库存。 */
	FString OwnerStableNetId;

	/** 产生该实例的 FishingSession ID；用于捕获幂等审计，不用于恢复旧会话。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid SourceFishingSessionId;

	/** 从真实鱼定义冻结的献祭贡献；后续资产改值不改变已捕获实例的事务价值。 */
	UPROPERTY(BlueprintReadOnly)
	int32 SacrificeContribution = 0;

	/** 捕获时由服务器鱼运行态给出的真实重量，单位千克；非有限或非正值不得创建实例。 */
	UPROPERTY(BlueprintReadOnly)
	double WeightKilograms = 0.0;
};

/** 容器内物体的领域类别；它用于路由容器规则，不表示所有道具必须继承同一个万能 Item 类型。 */
UENUM(BlueprintType)
enum class ECatContainedObjectKind : uint8
{
	/** 未能识别的容器物体；UI 不允许拖拽，服务器事务也会 fail-closed。 */
	Unknown,
	/** 一条局内实物鱼；鱼的吃、卖、献祭等行为仍由鱼领域结构承载。 */
	Fish,
	/** 一件有实例身份的装备；后续鱼竿、鱼漂等进入容器时使用同一容器移动语义。 */
	Equipment,
	/** 一组可堆叠耗材；后续鱼饵、窝料、草药等进入容器时按数量扩展具体载荷。 */
	Consumable
};

/** 容器读模型中的通用物体投影；当前由权威鱼槽派生，具体行为仍回到各自领域。 */
USTRUCT(BlueprintType)
struct FCatContainedObjectInstance
{
	GENERATED_BODY()

	/** 容器物体在当前局内的稳定实例 ID；UI 拖拽和服务器事务会把它与槽位下标一起复核。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid ObjectInstanceId;

	/** 该对象属于哪个领域类别；PageController 只转发，Items 服务按类别选择正式容器策略。 */
	UPROPERTY(BlueprintReadOnly)
	ECatContainedObjectKind ObjectKind = ECatContainedObjectKind::Unknown;

	/** 对象对应的稳定定义 ID；蓝图展示和服务器策略可读取它，但定义字段仍由各领域资产决定。 */
	UPROPERTY(BlueprintReadOnly)
	FName DefinitionId = NAME_None;

	/** 该对象当前在容器中占用的堆叠数量；非堆叠实例保持 1，后续耗材容器可用它表达栈数量。 */
	UPROPERTY(BlueprintReadOnly)
	int32 StackQuantity = 1;

	/** 当 ObjectKind 为 Fish 时携带的鱼领域副本；其他类别在正式接入统一容器槽位前不会由 Items 持久化。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishInstance Fish;
};

/** 一个 Items 聚合的复制读模型；服务端事务整体发布，网络出口先复制正式鱼事实，再派生通用容器物体投影。 */
USTRUCT(BlueprintType)
struct FCatContainerSnapshot
{
	GENERATED_BODY()

	/** 容器的一局稳定 ID；Container 命令的幂等与 Revision 作用域都绑定该值。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid ContainerId;

	/** 容器所有权类别；客户端只用它渲染，不据此自行授权。 */
	UPROPERTY(BlueprintReadOnly)
	ECatContainerKind Kind = ECatContainerKind::Unknown;

	/** 每次成功捕获、转移、预留状态变化或消费后递增的聚合版本。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;

	/** 容器当前被正式玩法裁定的格子容量；UI 用它决定 WrapBox 创建多少格，不把它当成扩容写口。 */
	UPROPERTY(BlueprintReadOnly)
	int32 Capacity = 0;

	/** 当前容器的鱼槽数组；数组下标就是容器格子，FishInstanceId 无效的条目表示中间空格占位。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatFishInstance> Fish;

	/** 当前容器的通用物体槽位投影；它由权威鱼槽派生给 UI 读，空槽保持默认对象且 UI 不直接写回它。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatContainedObjectInstance> Objects;
};

namespace CatItems
{
	/** 由鱼领域实例构造容器通用投影；调用方只得到可移动对象身份，不获得新的鱼写口。 */
	FCatContainedObjectInstance MakeContainedObjectFromFish(const FCatFishInstance& Fish);

	/** 按当前鱼数组刷新通用容器物体投影中的鱼对象；服务发布和客户端复制重建都用它保持 UI 读模型一致。 */
	void RebuildContainedObjectsFromFish(FCatContainerSnapshot& Snapshot);

	/** 计算容器当前真实鱼占用量；容量检查和 UI 摘要读这个口径，不能把空槽占位的数组长度算进去。 */
	int32 GetContainedObjectCount(const FCatContainerSnapshot& Snapshot);

	/** 按容器槽位下标读取当前支持的通用对象；空槽返回 false，调用方不能把数组长度误当成物体数量。 */
	bool TryGetContainedObjectAt(const FCatContainerSnapshot& Snapshot, int32 ObjectIndex,
		FCatContainedObjectInstance& OutObject);
}

/** FishingSession 提交给 Items 的捕获命令；StableNetId 与重量均由服务器会话填写，客户端不能直接调用服务写口。 */
USTRUCT()
struct FCatCaptureCommitCommand
{
	GENERATED_BODY()

	/** 捕获请求的 RequestId、目标容器 Revision 与首抄者服务器身份。 */
	FCatDomainCommandContext Context;

	/** 当前唯一 FishingSession ID；Items 用它防止跨会话复用捕获请求。 */
	FGuid FishingSessionId;

	/** FishingSession 在提交前为捕获事实分配的稳定鱼实例 ID；Items 只验证并原样提交，使印记预检与实物使用同一主体。 */
	FGuid FishInstanceId;

	/** 服务器已解析并通过 runtime gate 的鱼种稳定 ID。 */
	FName FishDefinitionId = NAME_None;

	/** 嘴叼鱼要写入的当前交互地面鱼护 ID；共享鱼缸必须走后续 Transfer。 */
	FGuid TargetContainerId;

	/** 从服务器鱼运行态冻结的真实重量，单位千克。 */
	double WeightKilograms = 0.0;

	/** 从真实 FishDefinition 冻结的献祭贡献；客户端载荷不提供该字段。 */
	int32 SacrificeContribution = 0;
};

/** 容器内任意正式物体的转移命令；客户端只提交对象身份和源/目标格意图，具体策略由 Items 服务集中裁决。 */
USTRUCT()
struct FCatContainerObjectTransferCommand
{
	GENERATED_BODY()

	/** 转移请求的身份、RequestId 与源容器 ExpectedRevision。 */
	FCatDomainCommandContext Context;

	/** 要移动的容器物体类别；它决定 Items 选择鱼、装备或耗材等领域策略。 */
	ECatContainedObjectKind ObjectKind = ECatContainedObjectKind::Unknown;

	/** 源容器中要移动的唯一物体实例 ID；服务器会和源槽位一起复核，防止拖拽旧引用改错物体。 */
	FGuid ObjectInstanceId;

	/** 当前源容器 ID。 */
	FGuid SourceContainerId;

	/** Drop 源格在源容器内的下标；服务器用它确认该格当前仍是 ObjectInstanceId。 */
	int32 SourceContainerSlotIndex = INDEX_NONE;

	/** 目标容器 ID；目标容量在同一提交前检查。 */
	FGuid TargetContainerId;

	/** Drop 目标在目标容器内的下标；Items 按源/目标槽位执行同容器交换或跨容器移动。 */
	int32 TargetContainerSlotIndex = INDEX_NONE;

	/** 跨容器事务对目标数组的乐观并发前提；服务与源版本一起比较，任一陈旧都保持两边原样。 */
	int64 ExpectedTargetRevision = 0;
};

/** 单条鱼原子转移命令；它是当前鱼领域策略适配层，通用容器入口会先转成该命令再提交。 */
USTRUCT()
struct FCatFishTransferCommand
{
	GENERATED_BODY()

	/** 转移请求的身份、RequestId 与源容器 ExpectedRevision。 */
	FCatDomainCommandContext Context;

	/** 源容器中要移动的唯一实物鱼；它必须仍位于 SourceContainerSlotIndex 指向的槽位。 */
	FGuid FishInstanceId;

	/** 当前源容器 ID。 */
	FGuid SourceContainerId;

	/** Drop 源格在源容器内的下标；数组槽位是本容器权威位置，不由鱼实例自己保存。 */
	int32 SourceContainerSlotIndex = INDEX_NONE;

	/** 目标容器 ID；目标容量在同一提交前检查。 */
	FGuid TargetContainerId;

	/** Drop 目标在目标容器内的下标；空槽接收移动物体，已占用槽位在允许时与源槽交换。 */
	int32 TargetContainerSlotIndex = INDEX_NONE;

	/** 跨容器事务对目标数组的乐观并发前提；服务与源版本一起比较，任一陈旧都保持两边原样，避免只移动一侧。 */
	int64 ExpectedTargetRevision = 0;
};

/** Social 提交给 Items 的单条偷鱼开始命令；身份与 Revision 均由服务器重建，客户端不能直接访问写口。 */
USTRUCT()
struct FCatFishTheftCommand
{
	GENERATED_BODY()

	/** 偷取者身份、RequestId 与源容器 ExpectedRevision。 */
	FCatDomainCommandContext Context;

	/** Social 为首次合法 Begin 分配的服务器唯一协议 ID；Items escrow 只按此键索引，绝不信任客户端 RequestId 的全局唯一性。 */
	FGuid TheftProtocolId;

	/** 被偷的唯一实物鱼。 */
	FGuid FishInstanceId;

	/** 目标地面鱼护箱子或共享鱼缸容器 ID。 */
	FGuid SourceContainerId;
};

/** Items 建立单条偷鱼 escrow 的不可变结果；鱼已离开容器但尚未吃掉，可在窗口内原位归还。 */
USTRUCT()
struct FCatFishTheftResult
{
	GENERATED_BODY()

	/** 公共命令终态；Revision 是源容器移除后的版本。 */
	FCatDomainCommandResult Command;

	/** Items 实际使用的服务器协议 ID；Social 的计时、追回和消费必须复用它。 */
	FGuid TheftProtocolId;

	/** 进入 escrow 的唯一鱼实例；Social 只用定义 ID/原主人裁决追逐与进食。 */
	FCatFishInstance Fish;

	/** 预留返还槽位的源容器 ID。 */
	FGuid SourceContainerId;
};

/** 直接吃鱼的命令；地面鱼护要求捕获者本人，共享鱼缸允许当前 Active 玩家但仍由服务器身份写入。 */
USTRUCT(BlueprintType)
struct FCatFishConsumeCommand
{
	GENERATED_BODY()

	/** RequestId、源容器 ExpectedRevision 与服务器身份。 */
	UPROPERTY(BlueprintReadWrite)
	FCatDomainCommandContext Context;

	/** 要直接吃掉的一条实物鱼。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid FishInstanceId;

	/** 鱼当前所在的地面鱼护或共享鱼缸。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid SourceContainerId;
};

/** 直接进食的不可变 Items 结果；成功后鱼已从容器移除，Character 才能消费定义效果。 */
USTRUCT(BlueprintType)
struct FCatFishConsumeResult
{
	GENERATED_BODY()

	/** 公共命令终态；Revision 是容器移除后的值。 */
	UPROPERTY(BlueprintReadOnly)
	FCatDomainCommandResult Command;

	/** 已被不可逆吃掉的鱼事实；拒绝时保持默认。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishInstance Fish;
};

/** 捕获 Compare-and-Commit 的唯一不可变结果；Fishing、Collection 与 Imprint 以后只消费该 DTO。 */
USTRUCT(BlueprintType)
struct FCatCaptureCommittedResult
{
	GENERATED_BODY()

	/** 首次成功抢抄请求 ID；同一会话后续请求只能读取该终态。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid CaptureRequestId;

	/** 已原子关闭捕获竞争的 FishingSession ID。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid FishingSessionId;

	/** 唯一创建的鱼实例；其 OwnerStableNetId 必须等于首个合法抢抄者。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishInstance FishInstance;

	/** 实例最终写入的容器 ID；嘴叼鱼首次入箱只允许当前交互的地面鱼护。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid ContainerId;

	/** 捕获提交后的容器 Revision；复制组件最终应收敛到不小于该值。 */
	UPROPERTY(BlueprintReadOnly)
	int64 ContainerRevision = 0;
};

/** Items 捕获事务结果；首次成功包含不可变提交 DTO，缓存重放不再创建鱼。 */
USTRUCT(BlueprintType)
struct FCatCaptureCommitResult
{
	GENERATED_BODY()

	/** 公共命令终态头；Revision 与 ContainerRevision 保持一致。 */
	UPROPERTY(BlueprintReadOnly)
	FCatDomainCommandResult Command;

	/** 仅首次成功或 AlreadyResolved 重放时有效的捕获事实。 */
	UPROPERTY(BlueprintReadOnly)
	FCatCaptureCommittedResult Committed;
};

/** 献祭预留结果；Reserved 只锁定鱼，仍允许协调器在 Items commit 前取消。 */
USTRUCT()
struct FCatFishReservationResult
{
	GENERATED_BODY()

	/** 本次是否新建或重放一个有效预留。 */
	bool bReserved = false;

	/** 结构化拒绝原因；成功为 None。 */
	ECatDomainCommandError Error = ECatDomainCommandError::InvalidPayload;

	/** 与 Sacrifice RequestId 一一对应的预留 ID。 */
	FGuid ReservationId;

	/** 预留状态对应的容器 Revision。 */
	int64 ContainerRevision = 0;

	/** 目标鱼冻结的献祭贡献；协调器用它做 RunAccepted 校验。 */
	int32 SacrificeContribution = 0;
};

/** Items 不可逆消费结果；成功后鱼已从容器移除，Cancel 必须拒绝回滚。 */
USTRUCT()
struct FCatFishReservationCommitResult
{
	GENERATED_BODY()

	/** 预留是否已经提交；重复提交返回 true 与同一 Revision。 */
	bool bCommitted = false;

	/** 结构化拒绝原因；成功或幂等重放为 None。 */
	ECatDomainCommandError Error = ECatDomainCommandError::InvalidPayload;

	/** 鱼移除后的容器 Revision；证明不可逆 Items 提交点。 */
	int64 ContainerRevision = 0;

	/** 被消费实例冻结的贡献值；Run apply 只能使用该值。 */
	int32 SacrificeContribution = 0;
};
