#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Environment/CatWaterTypes.h"
#include "Framework/Core/CatRunContracts.h"
#include "CatEnvironmentSettings.generated.h"

/**
 * 飞书「环境与氛围」册已定义的六类自然事件的稳定事件 ID。
 *
 * 它们是事件这一概念的对外名字，快照 ActiveEventId、印记候选 EventType 和日志都引用同一份字面量，
 * 不允许各处再写一次字符串。这里只登记名字与"这类事件会不会抛印记"，出场条件由 UCatEnvironmentSettings 裁决。
 */
namespace CatEnvironmentEvents
{
	/** 彩虹：雨后放晴时出场。 */
	CATFISHING_API extern const FName Rainbow;
	/** 萤火虫：夜晚出场（飞书原文还写了"草木边"，那是摆放位置，不是出场时机）。 */
	CATFISHING_API extern const FName Fireflies;
	/** 晚霞：黄昏时段出场。 */
	CATFISHING_API extern const FName SunsetGlow;
	/** 月光湖面：夜晚且天气为晴时出场。 */
	CATFISHING_API extern const FName MoonlitLake;
	/** 鸟群/蝴蝶：白天的环境底噪，任何白天时段都可能出场。 */
	CATFISHING_API extern const FName BirdsAndButterflies;
	/** 森林湖鱼群：聚鱼时刻的自然触发源，出场时向森林湖投一次窝料。 */
	CATFISHING_API extern const FName ForestLakeSchool;

	/**
	 * 该事件按飞书环境册是否属于"抛印记"的那几类。
	 * 六类里只有鸟群/蝴蝶不单独抛，其余五类都抛。
	 * 返回 true 只表示"这类事件够格拍一张"，能不能真的成立仍由印记册的准入名单和单局上限裁决。
	 */
	CATFISHING_API bool DoesEventProduceImprintCandidate(FName EventId);
}

/**
 * Environment 的局内时段、天气调度、窝料与自然事件配置。
 *
 * 时间源只有一个：Run 写下的局内服务器时钟锚点与白天截止点。本类任何一条规则都不读系统时钟、不读真实天气，
 * 也不持有自己的计时器——它只提供纯函数，由 GameMode 在阶段进入和时段分界时调用。
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Environment"))
class CATFISHING_API UCatEnvironmentSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 裁决环境提供者能否生成正式快照；缺 gate、天气或晨昏边界时返回 false，Run 因此保持 fail-closed 而不造中性天气。 */
	bool IsRuntimeReady() const;

	/** 根据 Run 的白天开始/截止和当前服务器世界时间计算时段；夜晚或参数无效返回 Unknown。 */
	ECatEnvironmentTimeOfDay ResolveTimeOfDay(const FCatRunPhaseSnapshot& RunSnapshot, double ServerNowSeconds) const;

	/**
	 * 把"现在是第几天的哪一段"折算成一个单调递增的调度槽序号，并同时给出该槽的白天时段。
	 *
	 * 槽是天气与自然事件唯一的切换粒度：一天四段（晨、昼、暮、夜），槽号 = DayIndex×4 + 段序号。
	 * 之所以复用晨/暮分界而不另开一套天气分界，是因为 GameMode 已经在这两个时刻排了重算计时器，
	 * 再加一套边界就要多一个计时器和多一份"现在到底该用哪一段"的真相。
	 *
	 * 返回 false 表示当前 Phase 根本不拥有环境时段（未开局、结算夜、收口），调用方必须 fail-closed 而不是补一个中性槽。
	 * 返回 true 且 OutTimeOfDay 为 Unknown 时表示这一槽是夜晚——夜晚不钓鱼，所以时段轴里没有它的取值。
	 */
	bool TryResolveScheduleSlot(const FCatRunPhaseSnapshot& RunSnapshot, double ServerNowSeconds,
		int64& OutSlotIndex, ECatEnvironmentTimeOfDay& OutTimeOfDay) const;

	/**
	 * 求某一个调度槽的天气。
	 *
	 * ConfiguredWeather 非 Unknown 时它是显式覆盖，直接返回，用于 PIE 人工验收和测试；
	 * 否则按 晴/雨/雾 三个权重做一次确定性抽样，随机流由 (RunSeed, 槽号) 唯一确定，
	 * 因此同一局同一槽反复求值结果恒等，测试推进时钟就能复现整条天气序列，不需要真随机。
	 * 权重全为零或非有限时返回 Unknown，让调用方保持 fail-closed，不编造晴天。
	 */
	ECatEnvironmentWeather ResolveWeatherForSlot(int64 SlotIndex, int32 RunSeed) const;

	/**
	 * 求某一个调度槽出场的自然事件 ID；NAME_None 表示这一槽没有事件。
	 *
	 * 出场条件照抄飞书环境册：彩虹要雨后放晴、萤火虫要夜晚、晚霞要黄昏、月光湖面要夜晚且晴、鸟群蝴蝶要白天、
	 * 森林湖鱼群走聚鱼时刻。同一槽可能同时满足多条（例如夜晚且晴既是萤火虫也是月光湖面），
	 * 而快照只装得下一个事件，所以按"越有条件的越优先"的固定顺序取第一条，顺序本身是工程暂定。
	 *
	 * PreviousWeather 是上一槽的天气，只有彩虹读它；传 Unknown 表示上一槽不存在（开局第一槽），彩虹因此不会在开局凭空出现。
	 * bAggregationAvailable 由调用方从窝点账本取，false 表示聚鱼时刻还在冷却，森林湖鱼群这一槽不出场。
	 */
	FName ResolveNaturalEventId(int64 SlotIndex, int32 RunSeed, ECatEnvironmentTimeOfDay TimeOfDay,
		ECatEnvironmentWeather Weather, ECatEnvironmentWeather PreviousWeather, bool bAggregationAvailable) const;

	/** 把 Run 身份折成天气/事件抽样用的种子；WeatherScheduleSeed 非零时它是显式固定种子，否则由 RunId 派生，使每局的天气序列不同但可复现。 */
	int32 ResolveScheduleSeed(const FCatRunPhaseSnapshot& RunSnapshot) const;

	/** 求出当前时段还能维持到哪个服务器时刻，供 Run 宿主安排一次性重新发布环境快照；
	 *  只有白天且还存在尚未越过的晨/暮分界时才返回 true，因此结构上不可能被用来给夜晚开计时。 */
	bool TryGetNextTimeOfDayBoundarySeconds(const FCatRunPhaseSnapshot& RunSnapshot, double ServerNowSeconds,
		double& OutBoundaryServerTimeSeconds) const;

	/** 裁决窝点子系统能否受理投窝与衰减；要求环境总 gate 打开且半径、衰减、安全夹全部是有效正值。 */
	bool IsChumRuntimeReady() const;

	/** 按窝料 Total 求本次咬钩基准间隔；配置未就绪或 Total 非法时返回 false，让调用方 fail-closed 而不是拿到一个编造的秒数。 */
	bool TryResolveBiteIntervalSeconds(double ChumTotal, double& OutIntervalSeconds) const;

	/** 读取森林湖鱼群出场时要投进窝点的三轴增量；环境配置未就绪或三轴非法时清空输出并返回 false，不投一个编造的量。 */
	bool TryGetNaturalAggregationContribution(FCatChumVector& OutContribution) const;

	/** 正式环境运行 gate；默认关闭，配置提供者保持 Unknown 并拒绝发布伪造天气。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bEnableEnvironmentRuntime = false;

	/**
	 * 强制覆盖整局天气的显式开关；Unknown 表示不覆盖，天气交给按槽调度。
	 * 它存在的唯一理由是人工验收和测试要能钉死一种天气（例如站在雨里看猫变湿），正式游玩应保持 Unknown。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Weather")
	ECatEnvironmentWeather ConfiguredWeather = ECatEnvironmentWeather::Unknown;

	/**
	 * 晴天在一次天气抽样里的相对权重。三个权重之间只比相对大小，不要求和为 1。
	 * 飞书只裁了"有晴/雨/雾，按局内时钟切换"，没有给分布，所以这组数是工程暂定（决策记录 D-24）。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Weather", meta = (ClampMin = "0.0"))
	double WeatherClearWeight = 0.6;

	/** 雨天在一次天气抽样里的相对权重；同 WeatherClearWeight 一样是工程暂定值。 */
	UPROPERTY(Config, EditAnywhere, Category = "Weather", meta = (ClampMin = "0.0"))
	double WeatherRainWeight = 0.25;

	/** 雾天在一次天气抽样里的相对权重；同 WeatherClearWeight 一样是工程暂定值。 */
	UPROPERTY(Config, EditAnywhere, Category = "Weather", meta = (ClampMin = "0.0"))
	double WeatherFogWeight = 0.15;

	/**
	 * 天气与自然事件抽样的固定种子；0 表示按 RunId 派生，让每一局的天气序列不同。
	 * 配成非零值会让所有局跑出同一条天气序列，只用于复现问题和人工验收。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Weather")
	int32 WeatherScheduleSeed = 0;

	/** 白天进度小于该比例为 Morning；必须在 0 到 DuskStartFraction 之间。 */
	UPROPERTY(Config, EditAnywhere, Category = "TimeOfDay", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double MorningEndFraction = 0.0;

	/** 白天进度不小于该比例为 Dusk；必须大于 MorningEndFraction 且小于 1。 */
	UPROPERTY(Config, EditAnywhere, Category = "TimeOfDay", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double DuskStartFraction = 0.0;

	/** 新建窝点时使用的圆半径，单位是世界单位（厘米）；默认 500 即策划口径的 5 米，改它只影响之后新建的窝。 */
	UPROPERTY(Config, EditAnywhere, Category = "Chum", meta = (ClampMin = "0.0"))
	double ChumSpotRadius = 500.0;

	/** 全局衰减的周期秒数；默认 30 秒执行一次，是所有窝点共用的同一个节拍，不按窝点各自计时。 */
	UPROPERTY(Config, EditAnywhere, Category = "Chum", meta = (ClampMin = "0.0"))
	double ChumDecayIntervalSeconds = 30.0;

	/** 每次衰减保留的比例；默认 0.9，即新池子 = 旧池子 乘 0.9。必须落在 0 到 1 之间才有收敛意义。 */
	UPROPERTY(Config, EditAnywhere, Category = "Chum", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double ChumDecayFactor = 0.9;

	/** 窝点消散的 Total 地板；衰减后 Total 低于它就整体归零并移除该窝点，默认 1 即策划口径的"Total 小于 1 强制归零"。 */
	UPROPERTY(Config, EditAnywhere, Category = "Chum", meta = (ClampMin = "0.0"))
	double ChumDecayFloorTotal = 1.0;

	/** 纯粹的数值异常保险丝：单个窝点 Total 的工程上限，默认 1e9，正常游玩不可能碰到。
	 *  它不是游戏规则——策划明确窝料没有设计上限，稳态由衰减收敛；这里只是防止脏配置或溢出把池推成 inf 之后污染整局判定。 */
	UPROPERTY(Config, EditAnywhere, Category = "Chum", meta = (ClampMin = "0.0"))
	double ChumTotalSafetyCap = 1.0e9;

	/** 窝点存在时的咬钩基准秒数；默认 15，是 T_actual 公式里 Total 大于 0 那一档的 T_base。 */
	UPROPERTY(Config, EditAnywhere, Category = "Chum", meta = (ClampMin = "0.0"))
	double ChumBiteBaseSecondsWithChum = 15.0;

	/** 没有窝点时的咬钩基准秒数；默认 120，是 T_actual 公式里 Total 等于 0 那一档的 T_base。 */
	UPROPERTY(Config, EditAnywhere, Category = "Chum", meta = (ClampMin = "0.0"))
	double ChumBiteBaseSecondsWithoutChum = 120.0;

	/** 咬钩间隔公式 T_actual = T_base / (1 + Total / K) 里的 K；默认 100，决定窝料总量对提速的边际效果。 */
	UPROPERTY(Config, EditAnywhere, Category = "Chum", meta = (ClampMin = "0.0"))
	double ChumBiteTotalScaleK = 100.0;

	/**
	 * 森林湖鱼群自发涌现的落点所在水域的稳定 RegionId；None 表示没接线，这一类事件因此永远不出场。
	 * 用 RegionId 而不是世界坐标，是因为坐标会随关卡改动失效，而 RegionId 与鱼表「出没地点」列同源。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Event")
	FName NaturalAggregationRegionId = NAME_None;

	/**
	 * 森林湖鱼群在一个白天调度槽里自发涌现的概率，0 到 1。
	 * 飞书把聚鱼时刻的稀缺预算留给了数值阶段，所以这个数是工程暂定（决策记录 D-25）。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Event", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double NaturalSchoolEmergenceChance = 0.25;

	/**
	 * 聚鱼时刻这一个机制的共享冷却秒数：距离上一次投窝（无论来自玩家还是自然事件）不足这么久，自然涌现就不出场。
	 * 它让两种触发源共用一本账，自然事件不会叠在玩家刚投的窝上再送一份收益。玩家自己投窝不受它限制——
	 * 给玩家投窝加 cd 是产品改动，飞书没裁过。数值同样是工程暂定（决策记录 D-25）。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Event", meta = (ClampMin = "0.0"))
	double AggregationMomentCooldownSeconds = 300.0;

	/** 森林湖鱼群出场时投进窝点的三轴增量；默认全零，此时自然涌现拿不到合法投放量，不产生自然聚鱼。 */
	UPROPERTY(Config, EditAnywhere, Category = "Event")
	FCatChumVector NaturalAggregationContribution;
};
