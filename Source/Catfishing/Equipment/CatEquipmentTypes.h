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
	/** 普通无限或特殊消耗型鱼饵。 */
	Bait,
	/** 四种正式玩法路线之一的鱼漂。 */
	Float,
	/** 近岸抢抄工具；它不改变首个合法抢抄规则。 */
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
	/** 只损失一份已选特殊鱼饵；普通饵无限，不能进入该结果。 */
	LoseSpecialBait,
	/** 只降低当前鱼竿耐久；降至零即断竿，不再同时丢饵。 */
	DamageRod
};

/** 一局耗材堆叠；数量随 World/Character 清空，不进入 Profile。 */
USTRUCT(BlueprintType)
struct FCatRunConsumableStack
{
	GENERATED_BODY()

	/** EquipmentDefinition 稳定 ID。 */
	UPROPERTY(BlueprintReadOnly)
	FName DefinitionId = NAME_None;

	/** 当前一局数量；只由 authority EquipmentComponent 写入。 */
	UPROPERTY(BlueprintReadOnly)
	int32 Quantity = 0;
};

/** Character 当前功能型装配的复制读模型；解锁与跨局选择仍在本地 Profile。 */
USTRUCT(BlueprintType)
struct FCatEquipmentLoadoutSnapshot
{
	GENERATED_BODY()

	/** 每次装配、耗材、耐久或维修提交后递增。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;

	/** 当前鱼竿稳定 ID。 */
	UPROPERTY(BlueprintReadOnly)
	FName RodDefinitionId = NAME_None;

	/** 当前鱼饵稳定 ID；普通饵不会出现在耗材栈。 */
	UPROPERTY(BlueprintReadOnly)
	FName BaitDefinitionId = NAME_None;

	/** 当前鱼漂稳定 ID。 */
	UPROPERTY(BlueprintReadOnly)
	FName FloatDefinitionId = NAME_None;

	/** 当前鱼竿耐久；没有合法鱼竿时为 0。 */
	UPROPERTY(BlueprintReadOnly)
	double RodDurability = 0.0;

	/** 当前鱼竿是否已断；只有 DamageRod 把耐久降至零时为 true。 */
	UPROPERTY(BlueprintReadOnly)
	bool bRodBroken = false;

	/** 一局消耗品数量；不包含无限普通鱼饵或跨局解锁。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatRunConsumableStack> Consumables;
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

/** Fishing use reservation 的 Begin 结果；bReserved 仅表示 special bait 的未提交保护额度。 */
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
