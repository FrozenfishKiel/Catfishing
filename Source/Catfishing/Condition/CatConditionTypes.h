#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "CatConditionTypes.generated.h"

/** 钓鱼旧接口的水域读值，由 ASC 水域 Tag 投影；保留至钓鱼负责人迁移读取，不独立保存权威状态。 */
UENUM(BlueprintType)
enum class ECatWaterExposureState : uint8
{
	Dry,
	Shallow,
	Dangerous
};

/** 单次环境采样的结果；调用方据此区分不可采样、无变化、变化和首次进入危险水域，不代表角色持续状态。 */
enum class ECatWaterExposureUpdate : uint8
{
	Unavailable,
	Unchanged,
	Changed,
	DangerousEntered
};

/** ASC 状态的兼容读模型；供尚未迁移的钓鱼接口和 UI 读取，不独立复制或接受写入。 */
USTRUCT(BlueprintType)
struct FCatConditionSnapshot
{
	GENERATED_BODY()

	/** 本机观察到的状态变化次数；Condition 收到 ASC Tag 通知时递增，不能用来比较服务器与客户端版本。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;

	/** 毛发当前是否淋湿；它只驱动表现，不带移动、数值或钓鱼惩罚。 */
	UPROPERTY(BlueprintReadOnly)
	bool bWet = false;

	/** 由 ASC 水域标签投影的旧接口值；危险水域由服务器采样并确认，枚举不持有权威状态。 */
	UPROPERTY(BlueprintReadOnly)
	ECatWaterExposureState WaterExposure = ECatWaterExposureState::Dry;

	/** ASC 当前是否拥有倒地标签；旧接口消费者只读，写入归对应来源 GE。 */
	UPROPERTY(BlueprintReadOnly)
	bool bDowned = false;
};
