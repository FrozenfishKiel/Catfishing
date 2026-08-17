#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Environment/CatWaterTypes.h"
#include "Framework/Core/CatRunContracts.h"
#include "CatEnvironmentSettings.generated.h"

/** Environment 的局内时段、天气与公共事件配置；默认全部 Unset，不读取系统时钟或真实天气。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Environment"))
class CATFISHING_API UCatEnvironmentSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 裁决环境提供者能否生成正式快照；缺 gate、天气或晨昏边界时返回 false，Run 因此保持 fail-closed 而不造中性天气。 */
	bool IsRuntimeReady() const;

	/** 根据 Run 的白天开始/截止和当前服务器世界时间计算时段；夜晚或参数无效返回 Unknown。 */
	ECatEnvironmentTimeOfDay ResolveTimeOfDay(const FCatRunPhaseSnapshot& RunSnapshot, double ServerNowSeconds) const;

	/** 读取当前公共自然事件对共享 WaterRegion 的显式聚鱼输入；任一事件、区域或三轴 Unset 都返回 false。 */
	bool TryGetNaturalChumField(FName& OutChumDefinitionId, FName& OutAnchorId) const;

	/** 正式环境运行 gate；默认关闭，配置提供者保持 Unknown 并拒绝发布伪造天气。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bEnableEnvironmentRuntime = false;

	/** 当前一局使用的显式天气；概率/转移资产未接入前只允许数据人员选择，不提供晴天默认。 */
	UPROPERTY(Config, EditAnywhere, Category = "Weather")
	ECatEnvironmentWeather ConfiguredWeather = ECatEnvironmentWeather::Unknown;

	/** 白天进度小于该比例为 Morning；必须在 0 到 DuskStartFraction 之间。 */
	UPROPERTY(Config, EditAnywhere, Category = "TimeOfDay", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double MorningEndFraction = 0.0;

	/** 白天进度不小于该比例为 Dusk；必须大于 MorningEndFraction 且小于 1。 */
	UPROPERTY(Config, EditAnywhere, Category = "TimeOfDay", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double DuskStartFraction = 0.0;

	/** 当前显式公共自然事件 ID；None 表示无事件，不会创建占位事件。 */
	UPROPERTY(Config, EditAnywhere, Category = "Event")
	FName ActiveEventId = NAME_None;

	/** 自然事件使用的唯一 ready ChumDefinition；None 表示不创建空间窝点。 */
	UPROPERTY(Config, EditAnywhere, Category = "Event")
	FName NaturalChumDefinitionId = NAME_None;

	/** 自然事件落点的唯一 ChumFieldAnchor；锚点冻结 exact WaterHandle 与世界位置。 */
	UPROPERTY(Config, EditAnywhere, Category = "Event")
	FName NaturalChumAnchorId = NAME_None;
};
