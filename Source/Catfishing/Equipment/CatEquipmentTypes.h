#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "CatEquipmentTypes.generated.h"

/** 功能型装备/道具类别；不存在品质、等级、随机词条或通用战力轴。 */
UENUM(BlueprintType)
enum class ECatEquipmentKind : uint8
{
	/** 数据尚未声明功能类别；运行目录必须拒绝该定义，不能把它当通用道具。 */
	Unknown,
	/** 有耐久的鱼竿。 */
	Rod,
	/** 鱼饵装配类别；普通饵和特殊饵都作为数量型库存物品进入统一库存，特殊标记只影响偏好、表现或额外失败惩罚语义。 */
	Bait,
	/** 四种正式玩法路线之一的鱼漂。 */
	Float,
	/** 已上钩鱼的范围抄取工具；不读取鱼的剩余体力。 */
	ScoopNet,
	/** 共享聚鱼池的窝料。 */
	Chum,
	/** 身体恢复上层先提交的草药耗材。 */
	Herb,
	/** 修竿使用的浮木。 */
	Driftwood,
	/** 其他正式一次性特效道具。 */
	Utility
};

/** 旧装备资产保存的 Use 库存影响配置；运行时会映射成 ECatInventoryItemUseEffect，保留它是为了不破坏现有 DataAsset 和测试字段。 */
UENUM(BlueprintType)
enum class ECatEquipmentUseInventoryEffect : uint8
{
	/** 兼容旧数据的自动策略；定义解析会把配置了 UseActorClass 的旧部署型资产视为实例占用，未配置的视为 no-op。 */
	Auto,
	/** 这类物品没有通用 Use 库存事务；统一入口返回 AlreadyResolved，不移动、不扣量，也不创建活动记录。 */
	None,
	/** Use 成功后整份实例离开背包，由活动使用记录持有到 UnUse 再放回；部署表现只引用这份实例身份。 */
	HoldInstanceUntilUnUse,
	/** Use 成功后从同一 ItemInstanceId 的数量栈扣指定份数；调用方必须先完成目标、距离和效果前置裁决。 */
	ConsumeQuantity
};

/** 一次钓鱼失败预算允许的唯一惩罚；None 与两个正式结果之外没有第二刀。 */
UENUM(BlueprintType)
enum class ECatFishingFailurePenalty : uint8
{
	/** 失败发生但本次不提交物资惩罚。 */
	None,
	/** 只追加损失一份已选特殊鱼饵；普通饵的基础使用扣减由 Fishing 提交链处理，不进入额外失败惩罚。 */
	LoseSpecialBait,
	/** 只降低当前鱼竿耐久；降至零即断竿，不再同时丢饵。 */
	DamageRod
};

/** 一局随身库存的单个格子；数组下标就是玩家看到和操作的格子位置，空格保持默认值。 */
USTRUCT(BlueprintType)
struct FCatRunInventorySlot
{
	GENERATED_BODY()

	/** 这个格子里物品对应的 EquipmentDefinition 稳定 ID；为空表示格子没有内容，商店、使用和 UI 都按同一个库存数组读取它。 */
	UPROPERTY(BlueprintReadOnly)
	FName DefinitionId = NAME_None;

	/** 这个格子里这份运行期物品或堆栈的实例身份；放置 Actor、仓库转移和收回都会用它确认自己处理的是同一份物品。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid ItemInstanceId;

	/** 这个格子里的堆叠数量；装备型物品固定为 1，数量型物品按配置上限在同一个格子内堆叠。 */
	UPROPERTY(BlueprintReadOnly)
	int32 Quantity = 0;

	/** 这份实例当前携带的鱼竿耐久；只有 Rod 会读写它，其他物品保持 0，避免把工具状态藏在选择快照里。 */
	UPROPERTY(BlueprintReadOnly)
	double RodDurability = 0.0;

	/** 这份实例是否已经断竿；只有 Rod 使用它，部署 Actor 和库存 UI 都从同一实例状态同步。 */
	UPROPERTY(BlueprintReadOnly)
	bool bRodBroken = false;
};
/** Character 当前钓鱼选择和迁移期库存投影的复制读模型；正式物品实例由 InventoryComponent 承载，旧消费者暂时继续读取这里。 */
USTRUCT(BlueprintType)
struct FCatEquipmentLoadoutSnapshot
{
	GENERATED_BODY()

	/** 每次选择、库存物品、耐久或维修提交后递增；前端和钓鱼命令用它做并发前提。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;

	/** 当前选中的鱼竿稳定 ID；它是钓鱼选择，不代表一个独立装备栏，是否已获得由随身库存证明。 */
	UPROPERTY(BlueprintReadOnly)
	FName RodDefinitionId = NAME_None;

	/** 当前选中的鱼竿实例 ID；放杆时 Use 会按它移出库存，鱼竿 Actor 也复制它来阻止同实例重复出现。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid RodItemInstanceId;

	/** 当前选中的鱼饵稳定 ID；这份饵还能不能用于 Fishing 由同 ID 的库存数量证明。 */
	UPROPERTY(BlueprintReadOnly)
	FName BaitDefinitionId = NAME_None;

	/** 当前选中的鱼饵堆栈实例 ID；Fishing 提交扣饵时按它锁定具体数量栈，不会再误扣同定义的另一格。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid BaitItemInstanceId;

	/** 当前选中的鱼漂稳定 ID；它是钓鱼选择，不再被库存 UI 展示成单独装备槽。 */
	UPROPERTY(BlueprintReadOnly)
	FName FloatDefinitionId = NAME_None;

	/** 当前选中的鱼漂实例 ID；后续鱼漂也需要 Use/UnUse 时不用再扩展第二套选择身份。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid FloatItemInstanceId;

	/** 当前选中的抄网稳定 ID；它跟随钓鱼/抢抄能力读取，不代表一个独立装备栏。 */
	UPROPERTY(BlueprintReadOnly)
	FName ScoopNetDefinitionId = NAME_None;

	/** 当前选中的抄网实例 ID；当前只用于选择追踪，未来部署或耐久可直接沿用同一实例身份。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid ScoopNetItemInstanceId;

	/** 当前选中的鱼竿外观 ID；外观选择不进入库存物品数量。 */
	UPROPERTY(BlueprintReadOnly)
	FName RodSkinDefinitionId = NAME_None;

	/** 当前鱼竿耐久；没有合法鱼竿时为 0。 */
	UPROPERTY(BlueprintReadOnly)
	double RodDurability = 0.0;

	/** 当前鱼竿实例是否已损坏；耐久耗尽后必须维修才可继续使用。 */
	UPROPERTY(BlueprintReadOnly)
	bool bRodBroken = false;

	/** 一局随身库存的迁移期格子投影；正式物品事实已经同步到 InventoryComponent，旧 UI、存档和钓鱼链路暂时读取这份数组。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatRunInventorySlot> InventorySlots;
};

/** 一次运行期物品 Use/UnUse 的结果；调用方拿到的是实例副本和库存版本，不需要自己改库存数组。 */
USTRUCT(BlueprintType)
struct FCatInventoryItemUseResult
{
	GENERATED_BODY()

	/** 本次使用或收回请求的关联 ID；日志、回执和上层命令用它把库存变化与世界 Actor 变化串起来。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid RequestId;

	/** 本次被移出、扣减或放回库存的运行期实例副本；部署回滚和收回归还都必须沿用同一 ItemInstanceId。 */
	UPROPERTY(BlueprintReadOnly)
	FCatRunInventorySlot Item;

	/** Use/UnUse 的领域结果；成功只表示库存事务成立，不代表调用方后续 Actor 生成一定成功。 */
	UPROPERTY(BlueprintReadOnly)
	ECatDomainCommandError Error = ECatDomainCommandError::InvalidPayload;

	/** 库存事务结束后的正式库存内容版本；Use/UnUse 成功、失败或回放时都尽量写入，调用方用它确认背包事实推进到哪一版。 */
	UPROPERTY(BlueprintReadOnly)
	int64 InventoryRevision = 0;

	/** 库存 Use/UnUse 同步旧 Equipment 读模型后的版本；迁移期保留给旧前端和旧钓鱼命令回执，新调用方应优先读取 InventoryRevision。 */
	UPROPERTY(BlueprintReadOnly)
	int64 EquipmentRevision = 0;

	/** 本次调用是否实际改变了库存或活动使用记录；重放、无实现或已收口路径会保持 false。 */
	UPROPERTY(BlueprintReadOnly)
	bool bCommitted = false;

	/** 本结果是否来自同一 Use/UnUse 请求的终态缓存；协调器用它判断是否需要补放后续领域提交。 */
	UPROPERTY(BlueprintReadOnly)
	bool bTerminalReplay = false;

	/** 终态重放对应的首次库存请求是否真的扣量、移出或放回；失败重放不会驱动草药恢复等后续效果。 */
	UPROPERTY(BlueprintReadOnly)
	bool bReplayedTerminalCommitted = false;

	/** 终态重放对应的首次库存错误；非重放结果保持默认，成功重放为 None，失败重放继续暴露库存拒绝原因。 */
	UPROPERTY(BlueprintReadOnly)
	ECatDomainCommandError ReplayedTerminalError = ECatDomainCommandError::InvalidPayload;
};

/** 把库存 Use/UnUse 的首次终态改写成可诊断重放；成功重放显示 AlreadyResolved，失败重放继续暴露首次失败。 */
inline void MarkInventoryItemUseReplayed(FCatInventoryItemUseResult& Result)
{
	const bool bOriginalCommitted = Result.bCommitted;
	const ECatDomainCommandError OriginalError = Result.Error;
	Result.bCommitted = false;
	Result.bTerminalReplay = true;
	Result.bReplayedTerminalCommitted = bOriginalCommitted;
	Result.Error = bOriginalCommitted && OriginalError == ECatDomainCommandError::None
		? ECatDomainCommandError::AlreadyResolved : OriginalError;
	Result.ReplayedTerminalError = OriginalError;
}

/** 判断库存 Use/UnUse 是否已经被服务器接受；只允许首次成功或成功终态重放驱动后续领域提交。 */
inline bool CatIsAcceptedInventoryItemUseResult(const FCatInventoryItemUseResult& Result)
{
	return (Result.bCommitted && Result.Error == ECatDomainCommandError::None)
		|| (Result.bTerminalReplay && Result.bReplayedTerminalCommitted
			&& Result.ReplayedTerminalError == ECatDomainCommandError::None);
}

/** 一次失败预算提交结果；明确记录唯一选择的惩罚。 */
USTRUCT(BlueprintType)
struct FCatFishingFailureResult
{
	GENERATED_BODY()

	/** 公共幂等终态；Revision 对应 Equipment 聚合。 */
	UPROPERTY(BlueprintReadOnly)
	FCatDomainCommandResult Command;

	/** 首次提交的唯一惩罚类别。 */
	UPROPERTY(BlueprintReadOnly)
	ECatFishingFailurePenalty Penalty = ECatFishingFailurePenalty::None;

	/** 惩罚后的鱼竿耐久；丢饵时保持原值。 */
	UPROPERTY(BlueprintReadOnly)
	double RemainingRodDurability = 0.0;
};

/** Fishing use reservation 的 Begin 结果；bReserved 表示本次 Fishing 已从随身库存暂存一份鱼饵，直到 Commit 消耗或 Release 归还。 */
USTRUCT(BlueprintType)
struct FCatFishingUseReservationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FGuid SessionId;

	UPROPERTY(BlueprintReadOnly)
	ECatDomainCommandError Error = ECatDomainCommandError::InvalidPayload;

	UPROPERTY(BlueprintReadOnly)
	int64 EquipmentRevision = 0;

	UPROPERTY(BlueprintReadOnly)
	int64 WearSequence = 0;

	UPROPERTY(BlueprintReadOnly)
	double AbsoluteRodWear = 0.0;

	UPROPERTY(BlueprintReadOnly)
	double RemainingRodDurability = 0.0;

	UPROPERTY(BlueprintReadOnly)
	bool bReserved = false;

	UPROPERTY(BlueprintReadOnly)
	bool bRodBroken = false;
};
/** Fishing use 后续操作结果；bApplied 只在首次改变 private record 或公开 Equipment 事实时为 true。 */
USTRUCT(BlueprintType)
struct FCatFishingUseOperationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FGuid SessionId;

	UPROPERTY(BlueprintReadOnly)
	ECatDomainCommandError Error = ECatDomainCommandError::InvalidPayload;

	UPROPERTY(BlueprintReadOnly)
	int64 EquipmentRevision = 0;

	UPROPERTY(BlueprintReadOnly)
	int64 WearSequence = 0;

	UPROPERTY(BlueprintReadOnly)
	double AbsoluteRodWear = 0.0;

	UPROPERTY(BlueprintReadOnly)
	double RemainingRodDurability = 0.0;

	UPROPERTY(BlueprintReadOnly)
	bool bApplied = false;

	UPROPERTY(BlueprintReadOnly)
	bool bRodBroken = false;
};
