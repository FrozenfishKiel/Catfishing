#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "CatFishingUseResults.generated.h"

/** 一次钓鱼失败预算允许的唯一惩罚结果；它只描述 Fishing 会话的失败结算，不是物品或装备的中心分类入口。 */
UENUM(BlueprintType)
enum class ECatFishingFailurePenalty : uint8
{
	/** 失败发生但本次不提交物资惩罚。 */
	None,
	/** 只追加损失一份已选特殊鱼饵；普通饵的基础使用扣减由 Fishing 提交链处理，不进入额外失败惩罚。 */
	LoseSpecialBait,
	/** 只降低当前鱼竿耐久；降至零即断竿，断竿和丢饵互斥。 */
	DamageRod
};

/** 一次失败预算提交结果；明确记录唯一选择的惩罚。 */
USTRUCT(BlueprintType)
struct FCatFishingFailureResult
{
	GENERATED_BODY()

	/** 公共幂等终态；Revision 对应 Equipment 聚合。 */
	UPROPERTY(BlueprintReadOnly)
	FCatDomainCommandResult Command;

	/** 首次提交的唯一失败结算结果；StateTree 写入它，Session 和 Equipment 只按该结果收口一次预算。 */
	UPROPERTY(BlueprintReadOnly)
	ECatFishingFailurePenalty Penalty = ECatFishingFailurePenalty::None;

	/** 惩罚后的鱼竿耐久；丢饵时保持原值。 */
	UPROPERTY(BlueprintReadOnly)
	double RemainingRodDurability = 0.0;
};

/** Fishing 使用冻结的 Begin 结果；bBaitFrozen 表示本次 Fishing 已从随身库存暂存一份鱼饵，直到 Commit 消耗或 Release 归还。 */
USTRUCT(BlueprintType)
struct FCatFishingUseFreezeResult
{
	GENERATED_BODY()

	/** 本次 Fishing 使用记录的会话 ID；后续 Commit/Release 必须带同一个 ID。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid SessionId;

	/** Begin 阶段装备冻结的领域结果；None 表示鱼竿、鱼饵和当前装备版本都被接受。 */
	UPROPERTY(BlueprintReadOnly)
	ECatDomainCommandError Error = ECatDomainCommandError::InvalidPayload;

	/** Begin 完成后的 Equipment 读模型版本；调用方用它确认钓具选择没有被并发替换。 */
	UPROPERTY(BlueprintReadOnly)
	int64 EquipmentRevision = 0;

	/** 当前会话已经接受的最新鱼竿磨损序号；Begin 时为 0。 */
	UPROPERTY(BlueprintReadOnly)
	int64 WearSequence = 0;

	/** 当前会话已经累计提交的鱼竿绝对磨损；Begin 时为 0。 */
	UPROPERTY(BlueprintReadOnly)
	double AbsoluteRodWear = 0.0;

	/** Begin 后绑定鱼竿实例的剩余耐久；没有有效记录时为 0。 */
	UPROPERTY(BlueprintReadOnly)
	double RemainingRodDurability = 0.0;

	/** 本次 Begin 是否真的从正式库存暂存了一份鱼饵；失败和重放不会重复扣量。 */
	UPROPERTY(BlueprintReadOnly)
	bool bBaitFrozen = false;

	/** Begin 后绑定鱼竿实例是否已经损坏；调用方用它阻止继续进入正常钓鱼流程。 */
	UPROPERTY(BlueprintReadOnly)
	bool bRodBroken = false;
};

/** Fishing use 后续操作结果；bApplied 只在首次改变 private record 或公开 Equipment 事实时为 true。 */
USTRUCT(BlueprintType)
struct FCatFishingUseOperationResult
{
	GENERATED_BODY()

	/** 本次后续操作对应的 Fishing 会话 ID；日志和幂等缓存用它对齐 Begin 记录。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid SessionId;

	/** Commit、Wear 或 Release 的领域结果；AlreadyResolved 表示同会话终态已经收口。 */
	UPROPERTY(BlueprintReadOnly)
	ECatDomainCommandError Error = ECatDomainCommandError::InvalidPayload;

	/** 操作完成后的 Equipment 读模型版本；没有公开状态变化时保持当前版本。 */
	UPROPERTY(BlueprintReadOnly)
	int64 EquipmentRevision = 0;

	/** 当前会话已接受的最新磨损序号；非磨损操作返回记录里的当前值。 */
	UPROPERTY(BlueprintReadOnly)
	int64 WearSequence = 0;

	/** 当前会话已累计提交的绝对磨损；重复序号不会增加它。 */
	UPROPERTY(BlueprintReadOnly)
	double AbsoluteRodWear = 0.0;

	/** 操作后绑定鱼竿实例的剩余耐久；丢饵或单纯 Release 时返回记录里的当前耐久。 */
	UPROPERTY(BlueprintReadOnly)
	double RemainingRodDurability = 0.0;

	/** 本次调用是否首次改变了会话记录、库存数量或鱼竿实例状态。 */
	UPROPERTY(BlueprintReadOnly)
	bool bApplied = false;

	/** 操作后绑定鱼竿实例是否断裂；调用方用它停止继续提交正常磨损。 */
	UPROPERTY(BlueprintReadOnly)
	bool bRodBroken = false;
};
