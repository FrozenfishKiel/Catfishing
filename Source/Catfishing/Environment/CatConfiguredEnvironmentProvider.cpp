#include "Environment/CatConfiguredEnvironmentProvider.h"

#include "Environment/CatChumSpotSubsystem.h"
#include "Environment/CatEnvironmentSettings.h"
#include "Engine/World.h"

// 环境求值流程：
// 1. 先验证 Run 身份、Revision、配置与 World；任一缺失都返回结构化失败，不产出中性天气。
// 2. 把当前服务器世界时间折算成调度槽。拿不到槽说明这个阶段本来就不拥有环境（未开局、结算夜、收口），
//    此时快照保持全 Unset，并只在白天拿不到槽时才算失败——白天是唯一必须有时段的阶段。
// 3. 拿到槽后按槽求天气，再取上一槽的天气供"雨后放晴"判定；开局第一槽没有前驱，传 Unknown，彩虹因此不会凭空出现。
// 4. 事件选择还要知道聚鱼时刻是不是在冷却里，这一项来自窝点子系统那一本共享账；子系统不在（客户端 World、测试 World）
//    时按"不在冷却"处理，因为那种 World 本来也不会真的去投窝。
// 本实现只读，不写 Run、不写窝点，也不启动任何计时器；天气与事件何时重算由 GameMode 的发布节拍决定。
FCatEnvironmentResult UCatConfiguredEnvironmentProvider::EvaluateEnvironment(const FCatRunPhaseSnapshot& RunSnapshot,
	const int64 RunRevision) const
{
	FCatEnvironmentResult Result;
	const UCatEnvironmentSettings* Settings = GetDefault<UCatEnvironmentSettings>();
	const UWorld* World = GetWorld();
	if (!RunSnapshot.RunId.IsValid() || RunRevision <= 0 || !Settings || !Settings->IsRuntimeReady() || !World)
	{
		Result.Error = TEXT("EnvironmentConfigurationUnavailable");
		return Result;
	}
	Result.Snapshot.SourceRunRevision = RunRevision;
	int64 SlotIndex = 0;
	ECatEnvironmentTimeOfDay TimeOfDay = ECatEnvironmentTimeOfDay::Unknown;
	if (!Settings->TryResolveScheduleSlot(RunSnapshot, World->GetTimeSeconds(), SlotIndex, TimeOfDay))
	{
		// 白天是唯一必须有时段的阶段，但"白天截止已到、正要入夜"的那一瞬是例外：
		// HandleDayDeadlineElapsed 会先清掉截止、再发布一次快照（要把"不再咬钩"立刻推给客户端），
		// 那一次发布里相位仍是 DayActive 而截止已经没了。这不是配置错误，是入夜边界本身——
		// 按"该阶段不拥有时段"处理、沿用上一份环境快照，否则每天入夜都会刷一条假 Error。
		// 真正的异常形态（白天却从未建立截止）不会出现：进入白天的唯一写口总是同时建立截止。
		Result.bSucceeded = RunSnapshot.Phase != ECatRunPhase::DayActive || !RunSnapshot.bHasDeadline;
		if (!Result.bSucceeded)
		{
			Result.Error = TEXT("TimeOfDayUnavailable");
		}
		return Result;
	}
	const int32 RunSeed = Settings->ResolveScheduleSeed(RunSnapshot);
	const ECatEnvironmentWeather Weather = Settings->ResolveWeatherForSlot(SlotIndex, RunSeed);
	const ECatEnvironmentWeather PreviousWeather = SlotIndex > 0
		? Settings->ResolveWeatherForSlot(SlotIndex - 1, RunSeed) : ECatEnvironmentWeather::Unknown;
	const UCatChumSpotSubsystem* ChumSpots = World->GetSubsystem<UCatChumSpotSubsystem>();
	const bool bAggregationAvailable = !ChumSpots
		|| !ChumSpots->IsAggregationMomentOnCooldown(Settings->AggregationMomentCooldownSeconds);
	Result.Snapshot.Weather = Weather;
	Result.Snapshot.TimeOfDay = TimeOfDay;
	Result.Snapshot.ActiveEventId = Settings->ResolveNaturalEventId(SlotIndex, RunSeed, TimeOfDay, Weather,
		PreviousWeather, bAggregationAvailable);
	Result.Snapshot.bHasActiveEvent = !Result.Snapshot.ActiveEventId.IsNone();
	Result.bSucceeded = true;
	return Result;
}
