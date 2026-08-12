#include "Environment/CatConfiguredEnvironmentProvider.h"

#include "Environment/CatEnvironmentSettings.h"
#include "Engine/World.h"

// 环境求值流程：先验证 Run/Revision/设置和 World，再用局内服务器秒计算时段；成功只返回环境 DTO，事件 None 明确表示本次无公共自然事件。
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
	Result.Snapshot.Weather = Settings->ConfiguredWeather;
	Result.Snapshot.TimeOfDay = Settings->ResolveTimeOfDay(RunSnapshot, World->GetTimeSeconds());
	Result.Snapshot.ActiveEventId = Settings->ActiveEventId;
	Result.Snapshot.bHasActiveEvent = !Settings->ActiveEventId.IsNone();
	Result.Snapshot.SourceRunRevision = RunRevision;
	Result.bSucceeded = RunSnapshot.Phase != ECatRunPhase::DayActive
		|| Result.Snapshot.TimeOfDay != ECatEnvironmentTimeOfDay::Unknown;
	if (!Result.bSucceeded)
	{
		Result.Error = TEXT("TimeOfDayUnavailable");
	}
	return Result;
}
