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

	/** 这个格子里的堆叠数量；装备型物品固定为 1，数量型物品按配置上限在同一个格子内堆叠。 */
	UPROPERTY(BlueprintReadOnly)
	int32 Quantity = 0;
};

/** Character 当前随身库存与钓鱼选择的复制读模型；解锁仍在本地 Profile，局内持有量随 Character/World 清空。 */
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

	/** 当前选中的鱼饵稳定 ID；这份饵还能不能用于 Fishing 由同 ID 的库存数量证明。 */
	UPROPERTY(BlueprintReadOnly)
	FName BaitDefinitionId = NAME_None;

	/** 当前选中的鱼漂稳定 ID；它是钓鱼选择，不再被库存 UI 展示成单独装备槽。 */
	UPROPERTY(BlueprintReadOnly)
	FName FloatDefinitionId = NAME_None;

	/** 当前选中的抄网稳定 ID；它跟随钓鱼/抢抄能力读取，不代表一个独立装备栏。 */
	UPROPERTY(BlueprintReadOnly)
	FName ScoopNetDefinitionId = NAME_None;

	/** 当前选中的鱼竿外观 ID；外观选择不进入库存物品数量。 */
	UPROPERTY(BlueprintReadOnly)
	FName RodSkinDefinitionId = NAME_None;

	/** 当前鱼竿耐久；没有合法鱼竿时为 0。 */
	UPROPERTY(BlueprintReadOnly)
	double RodDurability = 0.0;

	/** 当前鱼竿是否已断；只有 DamageRod 把耐久降至零时为 true。 */
	UPROPERTY(BlueprintReadOnly)
	bool bRodBroken = false;

	/** 一局随身库存格子数组；这是库存事实源，鱼饵、窝料、鱼竿和鱼漂都在这里占格，不再另建装备栏库存。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatRunInventorySlot> InventorySlots;
};

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

/** Fishing use reservation 的 Begin 结果；bReserved 表示本次 Fishing 已保护一份鱼饵数量，直到 Commit 或 Release 收口。 */
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

USTRUCT(BlueprintType)
struct FCatRunConsumableUseResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FGuid OperationId;
	UPROPERTY(BlueprintReadOnly) ECatDomainCommandError Error = ECatDomainCommandError::InvalidPayload;
	UPROPERTY(BlueprintReadOnly) FName DefinitionId = NAME_None;
	UPROPERTY(BlueprintReadOnly) int32 Quantity = 0;
	UPROPERTY(BlueprintReadOnly) int64 EquipmentRevision = 0;
	UPROPERTY(BlueprintReadOnly) bool bReserved = false;
	UPROPERTY(BlueprintReadOnly) bool bCommitted = false;
	UPROPERTY(BlueprintReadOnly) bool bReleased = false;
};
