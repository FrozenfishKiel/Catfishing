#include "Environment/CatEnvironmentSettings.h"

#include "Math/RandomStream.h"

namespace CatEnvironmentEvents
{
	// 六个稳定事件名的唯一定义处；改这里等于改对外事件 ID，会同时影响环境快照、印记准入名单和日志。
	const FName Rainbow(TEXT("Rainbow"));
	const FName Fireflies(TEXT("Fireflies"));
	const FName SunsetGlow(TEXT("SunsetGlow"));
	const FName MoonlitLake(TEXT("MoonlitLake"));
	const FName BirdsAndButterflies(TEXT("BirdsAndButterflies"));
	const FName ForestLakeSchool(TEXT("ForestLakeSchool"));

	// 抛印记判定流程：按飞书环境册"抛印记"那一列逐条对照，鸟群/蝴蝶是唯一不单独抛的一类，其余五类都抛；
	// 不认识的事件名一律返回 false，避免将来有人塞一个新名字就顺带获得拍照资格。
	bool DoesEventProduceImprintCandidate(const FName EventId)
	{
		return EventId == Rainbow || EventId == Fireflies || EventId == SunsetGlow
			|| EventId == MoonlitLake || EventId == ForestLakeSchool;
	}
}

namespace CatEnvironmentSchedule
{
	/** 一天切成的调度段数：晨、昼、暮、夜。槽号按它做进位，改它会整体改变槽号的含义，不是可调参数。 */
	constexpr int64 SlotsPerDay = 4;

	/** 夜晚在一天四段里的段序号；白天三段按 Morning/Day/Dusk 的顺序占 0/1/2，夜晚排在最后。 */
	constexpr int64 NightSegmentIndex = 3;

	/** 天气抽样流的盐；它与事件流的盐不同，只是为了让同一槽的两次抽样互相独立，不共用同一条随机序列。 */
	constexpr uint32 WeatherSalt = 0x57454154u;

	/** 森林湖鱼群涌现抽样流的盐；作用同 WeatherSalt。 */
	constexpr uint32 SchoolSalt = 0x5343484Fu;

	// 随机流构造流程：把种子、槽号和用途盐哈希成一个整数，再用它初始化 FRandomStream。
	// 一条流因此完全由这三项决定，与调用次数、调用顺序和引擎全局随机状态都无关；
	// 测试只要显式推进局内时钟就能复现整条天气/事件序列，不需要注入 mock，也不会因为多调一次就抽出不同结果。
	static FRandomStream MakeSlotStream(const int64 SlotIndex, const int32 RunSeed, const uint32 Salt)
	{
		uint32 Hash = GetTypeHash(RunSeed);
		Hash = HashCombine(Hash, GetTypeHash(SlotIndex));
		Hash = HashCombine(Hash, Salt);
		return FRandomStream(static_cast<int32>(Hash));
	}
}

// 可用性流程：要求显式启用、严格递增的晨/暮分界，以及一条真的能产出天气的路——要么钉死了 ConfiguredWeather，
// 要么三个权重里至少有一个正数。两条路都没有时返回 false，Run 因此保持 fail-closed，而不是退回一个隐藏的晴天默认。
// 无效比例同样不会被夹取，宁可整段不可运行也不猜晨昏分界。
bool UCatEnvironmentSettings::IsRuntimeReady() const
{
	const double TotalWeatherWeight = WeatherClearWeight + WeatherRainWeight + WeatherFogWeight;
	const bool bHasWeatherSource = ConfiguredWeather != ECatEnvironmentWeather::Unknown
		|| (FMath::IsFinite(TotalWeatherWeight) && TotalWeatherWeight > 0.0
			&& WeatherClearWeight >= 0.0 && WeatherRainWeight >= 0.0 && WeatherFogWeight >= 0.0);
	return bEnableEnvironmentRuntime && bHasWeatherSource
		&& FMath::IsFinite(MorningEndFraction) && FMath::IsFinite(DuskStartFraction)
		&& MorningEndFraction > 0.0 && DuskStartFraction > MorningEndFraction && DuskStartFraction < 1.0;
}

// 时段计算流程：直接问调度槽解析器要这一刻的白天时段。夜晚和不拥有时段的阶段在那边都会给出 Unknown，
// 所以这里不再保留第二份分界算法——两份算法一旦漂移，快照里的时段和计时器排的分界就会对不上。
ECatEnvironmentTimeOfDay UCatEnvironmentSettings::ResolveTimeOfDay(const FCatRunPhaseSnapshot& RunSnapshot,
	const double ServerNowSeconds) const
{
	int64 SlotIndex = 0;
	ECatEnvironmentTimeOfDay TimeOfDay = ECatEnvironmentTimeOfDay::Unknown;
	TryResolveScheduleSlot(RunSnapshot, ServerNowSeconds, SlotIndex, TimeOfDay);
	return TimeOfDay;
}

// 调度槽解析流程：
// 1. 配置未就绪或 DayIndex 非正一律返回 false；DayIndex 是槽号的进位基数，为 0 时算出来的槽号和"第 0 天"混在一起。
// 2. NormalNight 不做任何时间换算就占掉当天最后一个槽：夜晚在飞书口径里是一整段（不钓鱼、只等翻天），内部没有分段。
// 3. DayActive 要求存在有效白天区间，把当前服务器秒规范化成 0..1 的白天进度，再按显式的晨末/暮初两条分界切成三段。
// 4. 其余阶段（未开局、两种结算夜、收口）都不拥有环境时段，返回 false 让调用方 fail-closed，而不是补一个中性槽。
// 槽号用 DayIndex 做进位，因此随天数单调递增；天气与事件抽样直接拿它当"第几段"，不必再存一份"上一段是什么"的运行态。
bool UCatEnvironmentSettings::TryResolveScheduleSlot(const FCatRunPhaseSnapshot& RunSnapshot,
	const double ServerNowSeconds, int64& OutSlotIndex, ECatEnvironmentTimeOfDay& OutTimeOfDay) const
{
	OutSlotIndex = 0;
	OutTimeOfDay = ECatEnvironmentTimeOfDay::Unknown;
	if (!IsRuntimeReady() || RunSnapshot.DayIndex <= 0)
	{
		return false;
	}
	const int64 DayBase = static_cast<int64>(RunSnapshot.DayIndex) * CatEnvironmentSchedule::SlotsPerDay;
	if (RunSnapshot.Phase == ECatRunPhase::NormalNight)
	{
		OutSlotIndex = DayBase + CatEnvironmentSchedule::NightSegmentIndex;
		return true;
	}
	if (RunSnapshot.Phase != ECatRunPhase::DayActive || !RunSnapshot.bHasDeadline
		|| !FMath::IsFinite(ServerNowSeconds) || RunSnapshot.DeadlineServerTimeSeconds <= RunSnapshot.ServerTimeAnchorSeconds)
	{
		return false;
	}
	const double Progress = FMath::Clamp((ServerNowSeconds - RunSnapshot.ServerTimeAnchorSeconds)
		/ (RunSnapshot.DeadlineServerTimeSeconds - RunSnapshot.ServerTimeAnchorSeconds), 0.0, 1.0);
	if (Progress < MorningEndFraction)
	{
		OutTimeOfDay = ECatEnvironmentTimeOfDay::Morning;
		OutSlotIndex = DayBase;
	}
	else if (Progress < DuskStartFraction)
	{
		OutTimeOfDay = ECatEnvironmentTimeOfDay::Day;
		OutSlotIndex = DayBase + 1;
	}
	else
	{
		OutTimeOfDay = ECatEnvironmentTimeOfDay::Dusk;
		OutSlotIndex = DayBase + 2;
	}
	return true;
}

// 天气抽样流程：显式覆盖优先返回；否则按三个权重做一次带权抽样。
// 用 Roll 逐段减权重而不是先归一化，是为了让"权重全为零"这种脏配置在上面的有效性检查里就被判成 Unknown，
// 而不是在除法里变成 NaN 之后悄悄落到某一档。同一 (RunSeed, 槽号) 每次都重建同一条流，所以本函数对外是纯函数。
ECatEnvironmentWeather UCatEnvironmentSettings::ResolveWeatherForSlot(const int64 SlotIndex, const int32 RunSeed) const
{
	if (ConfiguredWeather != ECatEnvironmentWeather::Unknown)
	{
		return ConfiguredWeather;
	}
	const double TotalWeight = WeatherClearWeight + WeatherRainWeight + WeatherFogWeight;
	if (!FMath::IsFinite(TotalWeight) || TotalWeight <= 0.0
		|| WeatherClearWeight < 0.0 || WeatherRainWeight < 0.0 || WeatherFogWeight < 0.0)
	{
		return ECatEnvironmentWeather::Unknown;
	}
	FRandomStream Stream = CatEnvironmentSchedule::MakeSlotStream(SlotIndex, RunSeed, CatEnvironmentSchedule::WeatherSalt);
	double Roll = static_cast<double>(Stream.GetFraction()) * TotalWeight;
	if (Roll < WeatherClearWeight)
	{
		return ECatEnvironmentWeather::Clear;
	}
	Roll -= WeatherClearWeight;
	return Roll < WeatherRainWeight ? ECatEnvironmentWeather::Rain : ECatEnvironmentWeather::Fog;
}

// 事件选择流程：天气未知时不产生任何事件（六类事件的出场条件全都要靠天气或时段说话）；随后按固定优先级取第一条满足条件的。
// 优先级是"条件越苛刻越靠前"：森林湖鱼群要同时满足白天、聚鱼时刻不在冷却和一次抽样命中，最稀缺，排最前；
// 彩虹要上一槽真的在下雨；月光湖面要夜晚且晴；萤火虫只要夜晚；晚霞只要黄昏；鸟群蝴蝶是白天兜底，永远排最后。
// 飞书没有裁过同槽多事件时谁优先，这个顺序是工程暂定（决策记录 D-26）。
FName UCatEnvironmentSettings::ResolveNaturalEventId(const int64 SlotIndex, const int32 RunSeed,
	const ECatEnvironmentTimeOfDay TimeOfDay, const ECatEnvironmentWeather Weather,
	const ECatEnvironmentWeather PreviousWeather, const bool bAggregationAvailable) const
{
	if (Weather == ECatEnvironmentWeather::Unknown)
	{
		return NAME_None;
	}
	// 白天三段用 TimeOfDay 表达，夜晚在时段轴上没有取值，所以调用方给出的 Unknown 在这里恰好等价于"这一槽是夜晚"。
	const bool bNight = TimeOfDay == ECatEnvironmentTimeOfDay::Unknown;
	if (!bNight && bAggregationAvailable && FMath::IsFinite(NaturalSchoolEmergenceChance)
		&& NaturalSchoolEmergenceChance > 0.0)
	{
		FRandomStream Stream = CatEnvironmentSchedule::MakeSlotStream(SlotIndex, RunSeed, CatEnvironmentSchedule::SchoolSalt);
		if (static_cast<double>(Stream.GetFraction()) < NaturalSchoolEmergenceChance)
		{
			return CatEnvironmentEvents::ForestLakeSchool;
		}
	}
	if (!bNight && Weather == ECatEnvironmentWeather::Clear && PreviousWeather == ECatEnvironmentWeather::Rain)
	{
		return CatEnvironmentEvents::Rainbow;
	}
	if (bNight)
	{
		return Weather == ECatEnvironmentWeather::Clear
			? CatEnvironmentEvents::MoonlitLake : CatEnvironmentEvents::Fireflies;
	}
	return TimeOfDay == ECatEnvironmentTimeOfDay::Dusk
		? CatEnvironmentEvents::SunsetGlow : CatEnvironmentEvents::BirdsAndButterflies;
}

// 种子解析流程：配置里钉了非零种子就用它，让所有局跑同一条序列（复现问题和人工验收用）；
// 否则把 RunId 哈希成种子，使不同局的天气/事件序列不同，而同一局内反复求值又完全一致。这里不引入任何全局随机源。
int32 UCatEnvironmentSettings::ResolveScheduleSeed(const FCatRunPhaseSnapshot& RunSnapshot) const
{
	return WeatherScheduleSeed != 0 ? WeatherScheduleSeed : static_cast<int32>(GetTypeHash(RunSnapshot.RunId));
}

// 分界求解流程：先复用与 ResolveTimeOfDay 完全相同的白天前置条件，再把晨末、暮初两个比例还原成绝对服务器时刻，
// 返回第一个严格晚于当前时刻的那个；已经进入 Dusk 就没有下一个分界，由 Run 自己的白天截止事件收尾。
// 这里用严格大于而不是大于等于，是为了让"刚被这个分界唤醒的那次重算"不会又把同一个时刻当成下一个目标而空转。
// 夜晚、无截止、Phase 不是 DayActive 一律走前置失败分支，所以这个接口结构上无法给夜晚提供任何计时。
bool UCatEnvironmentSettings::TryGetNextTimeOfDayBoundarySeconds(const FCatRunPhaseSnapshot& RunSnapshot,
	const double ServerNowSeconds, double& OutBoundaryServerTimeSeconds) const
{
	OutBoundaryServerTimeSeconds = 0.0;
	if (!IsRuntimeReady() || RunSnapshot.Phase != ECatRunPhase::DayActive || !RunSnapshot.bHasDeadline
		|| !FMath::IsFinite(ServerNowSeconds) || RunSnapshot.DeadlineServerTimeSeconds <= RunSnapshot.ServerTimeAnchorSeconds)
	{
		return false;
	}
	const double DaySpanSeconds = RunSnapshot.DeadlineServerTimeSeconds - RunSnapshot.ServerTimeAnchorSeconds;
	const double BoundaryFractions[] = { MorningEndFraction, DuskStartFraction };
	for (const double BoundaryFraction : BoundaryFractions)
	{
		const double BoundarySeconds = RunSnapshot.ServerTimeAnchorSeconds + BoundaryFraction * DaySpanSeconds;
		if (BoundarySeconds > ServerNowSeconds)
		{
			OutBoundaryServerTimeSeconds = BoundarySeconds;
			return true;
		}
	}
	return false;
}

// 窝料可用性流程：环境总 gate 之外只再要求半径、衰减周期、衰减比例和安全夹本身是有效数值。
// 这里不额外设一个"窝料已裁决"开关，是因为半径 5 米、每 30 秒乘 0.9、Total 小于 1 归零这些数值策划已经给全，
// 不存在需要 fail-closed 等待的未裁项；衰减比例必须严格小于 1，否则池子不收敛，窝点永远不会消散。
bool UCatEnvironmentSettings::IsChumRuntimeReady() const
{
	return bEnableEnvironmentRuntime
		&& FMath::IsFinite(ChumSpotRadius) && ChumSpotRadius > 0.0
		&& FMath::IsFinite(ChumDecayIntervalSeconds) && ChumDecayIntervalSeconds > 0.0
		&& FMath::IsFinite(ChumDecayFactor) && ChumDecayFactor > 0.0 && ChumDecayFactor < 1.0
		&& FMath::IsFinite(ChumDecayFloorTotal) && ChumDecayFloorTotal > 0.0
		&& FMath::IsFinite(ChumTotalSafetyCap) && ChumTotalSafetyCap > 0.0;
}

// 咬钩间隔流程：先要求窝料配置就绪且两档基准与 K 都是有效正值，再按 Total 是否为正选 T_base，最后套 T_base / (1 + Total / K)。
// Total 恰好为 0 时公式退化成 T_base 本身，正好等于"没有窝料"那一档的 120 秒，所以两档不需要各写一条分支。
bool UCatEnvironmentSettings::TryResolveBiteIntervalSeconds(const double ChumTotal, double& OutIntervalSeconds) const
{
	OutIntervalSeconds = 0.0;
	if (!IsChumRuntimeReady() || !FMath::IsFinite(ChumTotal) || ChumTotal < 0.0
		|| !FMath::IsFinite(ChumBiteBaseSecondsWithChum) || ChumBiteBaseSecondsWithChum <= 0.0
		|| !FMath::IsFinite(ChumBiteBaseSecondsWithoutChum) || ChumBiteBaseSecondsWithoutChum <= 0.0
		|| !FMath::IsFinite(ChumBiteTotalScaleK) || ChumBiteTotalScaleK <= 0.0)
	{
		return false;
	}
	const double BaseSeconds = ChumTotal > 0.0 ? ChumBiteBaseSecondsWithChum : ChumBiteBaseSecondsWithoutChum;
	OutIntervalSeconds = BaseSeconds / (1.0 + ChumTotal / ChumBiteTotalScaleK);
	return true;
}

// 自然投窝读取流程：先清输出，再要求环境总配置就绪且三轴本身合法；成功只复制这一个向量。
// 落点不在这里出：它由关卡里 NaturalAggregationRegionId 对应那片水域的几何决定，
// 把坐标写进配置会在关卡一改就把窝投到岸上，而 RegionId 与鱼表「出没地点」列同源、改关卡也不会失效。
bool UCatEnvironmentSettings::TryGetNaturalAggregationContribution(FCatChumVector& OutContribution) const
{
	OutContribution = FCatChumVector();
	if (!IsRuntimeReady() || !NaturalAggregationContribution.IsValidContribution())
	{
		return false;
	}
	OutContribution = NaturalAggregationContribution;
	return true;
}
