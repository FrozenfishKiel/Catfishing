#pragma once

#include "CoreMinimal.h"
#include "CatGrowthTypes.generated.h"

/** 猫本局吃鱼成长的复制读模型；它只保存经验槽进度和待选次数，不提前编造 Buff 选项。 */
USTRUCT(BlueprintType)
struct FCatGrowthSnapshot
{
	GENERATED_BODY()

	/** 每次吃鱼成长提交后递增；UI 和测试用它判断整份快照是否更新。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;

	/** 本局累计获得的吃鱼经验；只随当前 Character 生命周期存在，局末丢弃。 */
	UPROPERTY(BlueprintReadOnly)
	int32 TotalExperience = 0;

	/** 当前经验槽内已经累积但尚未触发下一次三选一的经验。 */
	UPROPERTY(BlueprintReadOnly)
	int32 ExperienceInCurrentSlot = 0;

	/** 已满槽但尚未由正式三选一 UI 消费的选择次数；Buff 池未裁时停在这里 fail-closed。 */
	UPROPERTY(BlueprintReadOnly)
	int32 PendingChoiceCount = 0;
};
