#include "Environment/CatChumSpotSubsystem.h"

#include "Engine/World.h"
#include "Environment/CatEnvironmentSettings.h"
#include "Stats/Stats.h"

// 创建条件流程：只允许 Game/PIE 的 authority World 持有可写窝点集合；客户端 World 不创建第二份窝料真相。
bool UCatChumSpotSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

// 反初始化流程：先按局末口径清空窝点、节拍与终态缓存，再交还 Tickable 生命周期；顺序不能反，Super 之后禁止再触碰运行态。
void UCatChumSpotSubsystem::Deinitialize()
{
	ResetRuntimeChumFromAuthority();
	Super::Deinitialize();
}

// 每帧流程：先让基类完成初始化断言，再把本帧流逝时间交给全局衰减节拍；本函数不做任何窝点判定，规则集中在 AdvanceDecay。
void UCatChumSpotSubsystem::Tick(const float DeltaTime)
{
	Super::Tick(DeltaTime);
	AdvanceDecay(DeltaTime);
}

// 统计标识流程：返回固定的 Tickables 分组条目，仅供 profiler 归类。
TStatId UCatChumSpotSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCatChumSpotSubsystem, STATGROUP_Tickables);
}

// 命中查找流程：遍历全部窝点，只保留「落点到圆心的水平距离不超过该窝半径」的候选，并在多个相交窝点里取圆心最近的那个。
// 只比 XY 不比 Z：窝点在策划口径里是水面上的一个圆，而漂和落点的高度会随水面、抛投弧线和地形浮动，
// 把 Z 计进距离会让一个明明在窝里的漂因为差几十厘米高度而判成没命中。
int32 UCatChumSpotSubsystem::FindNearestSpotIndex(const FVector& WorldLocation) const
{
	if (WorldLocation.ContainsNaN())
	{
		return INDEX_NONE;
	}
	int32 NearestIndex = INDEX_NONE;
	double NearestDistanceSquared = 0.0;
	for (int32 SpotIndex = 0; SpotIndex < Spots.Num(); ++SpotIndex)
	{
		const FChumSpot& Spot = Spots[SpotIndex];
		const double DistanceSquared = FVector::DistSquaredXY(WorldLocation, Spot.Center);
		if (DistanceSquared > Spot.Radius * Spot.Radius)
		{
			continue;
		}
		if (NearestIndex == INDEX_NONE || DistanceSquared < NearestDistanceSquared)
		{
			NearestIndex = SpotIndex;
			NearestDistanceSquared = DistanceSquared;
		}
	}
	return NearestIndex;
}

// 快照构造流程：无效下标返回只带当前 Revision 的「无窝」快照，让调用方能用同一个结构表达 Total=0 那一档；
// 有效下标复制圆心、半径与三轴，不暴露内部数组下标或指针。
FCatChumSpotSnapshot UCatChumSpotSubsystem::MakeSpotSnapshot(const int32 SpotIndex) const
{
	FCatChumSpotSnapshot Snapshot;
	Snapshot.AggregationRevision = AggregationRevision;
	if (!Spots.IsValidIndex(SpotIndex))
	{
		return Snapshot;
	}
	const FChumSpot& Spot = Spots[SpotIndex];
	Snapshot.bHasSpot = true;
	Snapshot.Center = Spot.Center;
	Snapshot.Radius = Spot.Radius;
	Snapshot.Pool = Spot.Pool;
	return Snapshot;
}

// 查询流程：按圆心最近命中规则找窝并复制快照；一个都没命中时返回 bHasSpot=false，调用方据此走「无窝料」档。
FCatChumSpotSnapshot UCatChumSpotSubsystem::QueryChumSpot(const FVector& WorldLocation) const
{
	return MakeSpotSnapshot(FindNearestSpotIndex(WorldLocation));
}

// 投窝提交流程：两个 Source 共用同一份身份/RequestId 终态缓存，并把落点、三轴、来源与 Revision 固化成载荷签名；
// 首次受理通过校验后，落点命中已有窝就沿用原圆心与半径向量相加，没命中就按当前配置半径新建一个以落点为圆心的窝。
// 无论并池还是新建都只推进一次集合 Revision，且不触碰衰减累加器，因此补窝不会把这一轮衰减推迟。
FCatAggregationResult UCatChumSpotSubsystem::ContributeChum(const FCatAggregationCommand& Command)
{
	FCatAggregationResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	const FString CacheKey = FString::Printf(TEXT("%s|%s"), *Command.Context.StableNetId,
		*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	const FString PayloadSignature = FString::Printf(
		TEXT("ExpectedRevision=%lld|Drop=%.17g,%.17g,%.17g|Fishy=%.17g|Fragrant=%.17g|Fermented=%.17g|Source=%d"),
		Command.Context.ExpectedRevision, Command.DropLocation.X, Command.DropLocation.Y, Command.DropLocation.Z,
		Command.Contribution.Fishy, Command.Contribution.Fragrant, Command.Contribution.Fermented,
		static_cast<int32>(Command.Source));
	switch (CatQueryTerminalReplay(ChumTerminalCache, ChumPayloadByKey, CacheKey, PayloadSignature, Result,
		[](FCatAggregationResult& R){ MarkCommandReplayed(R.Command); }))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		// 拒绝也必须带回当前 Revision 与落点处的现状，避免调用方拿着陈旧快照反复重试。
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		Result.Command.Revision = AggregationRevision;
		Result.Spot = QueryChumSpot(Command.DropLocation);
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
	}
	Result.Command.Error = ValidateChum(Command);
	if (Result.Command.Error == ECatDomainCommandError::None)
	{
		int32 SpotIndex = FindNearestSpotIndex(Command.DropLocation);
		if (SpotIndex == INDEX_NONE)
		{
			FChumSpot NewSpot;
			NewSpot.Center = Command.DropLocation;
			NewSpot.Radius = GetDefault<UCatEnvironmentSettings>()->ChumSpotRadius;
			SpotIndex = Spots.Add(MoveTemp(NewSpot));
		}
		FChumSpot& TargetSpot = Spots[SpotIndex];
		TargetSpot.Pool.Fishy += Command.Contribution.Fishy;
		TargetSpot.Pool.Fragrant += Command.Contribution.Fragrant;
		TargetSpot.Pool.Fermented += Command.Contribution.Fermented;
		++AggregationRevision;
		// 两个触发源共用的那一本账在这里记：无论这次是玩家投窝还是自然事件投窝，聚鱼时刻的共享冷却都从此刻重新开始计时。
		SecondsSinceLastAggregation = 0.0;
		bHasAggregatedThisRun = true;
		Result.Command.bCommitted = true;
		Result.Spot = MakeSpotSnapshot(SpotIndex);
	}
	else
	{
		Result.Spot = QueryChumSpot(Command.DropLocation);
	}
	Result.Command.Revision = AggregationRevision;
	ChumTerminalCache.Add(CacheKey, Result);
	ChumPayloadByKey.Add(CacheKey, PayloadSignature);
	return Result;
}

// 投窝预检流程：按窝料运行配置、身份与 RequestId、落点与三轴、集合 Revision、数值安全夹逐项验证；
// 只返回拒绝语义，不建窝、不加池、不推 Revision，也不写终态缓存。
ECatDomainCommandError UCatChumSpotSubsystem::ValidateChum(const FCatAggregationCommand& Command) const
{
	const UCatEnvironmentSettings* Settings = GetDefault<UCatEnvironmentSettings>();
	if (!Settings || !Settings->IsChumRuntimeReady())
	{
		return ECatDomainCommandError::PolicyUndecided;
	}
	if (!Command.Context.RequestId.IsValid() || Command.Context.StableNetId.IsEmpty()
		|| Command.DropLocation.ContainsNaN() || !Command.Contribution.IsValidContribution())
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	if (Command.Context.ExpectedRevision != AggregationRevision)
	{
		return ECatDomainCommandError::RevisionConflict;
	}
	// 下面这条不是「窝料上限」这种游戏规则，而是数值异常保险丝：策划口径下窝料没有设计上限，稳态由衰减收敛，
	// 正常游玩不可能把单个窝点堆到安全夹。它只拦住脏配置或溢出产生的 inf 与天文数字，避免污染整局的占比与咬钩判定。
	const int32 ExistingSpotIndex = FindNearestSpotIndex(Command.DropLocation);
	const double ExistingTotal = Spots.IsValidIndex(ExistingSpotIndex) ? Spots[ExistingSpotIndex].Pool.Total() : 0.0;
	const double MergedTotal = ExistingTotal + Command.Contribution.Total();
	return !FMath::IsFinite(MergedTotal) || MergedTotal > Settings->ChumTotalSafetyCap
		? ECatDomainCommandError::CapacityExceeded : ECatDomainCommandError::None;
}

// 衰减推进流程：把流逝时间累加进全局节拍，每攒满一个周期就执行一次全局衰减；
// 用 while 而不是 if，是因为一帧的 DeltaTime 或一次手动推进可能跨过多个周期（掉帧、断点、测试里一次推进几分钟），
// 漏掉的周期必须补上，否则卡一下就等于给玩家白送窝料时长。
// 配置未就绪时直接丢弃这段时间而不是攒着：攒下来会在配置被改好的瞬间集中触发一批衰减，比不衰减更难解释。
void UCatChumSpotSubsystem::AdvanceDecay(const double DeltaSeconds)
{
	const UCatEnvironmentSettings* Settings = GetDefault<UCatEnvironmentSettings>();
	if (!Settings || !Settings->IsChumRuntimeReady() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0)
	{
		return;
	}
	// 聚鱼时刻的共享冷却和衰减共用同一条时间输入，但各记各的：衰减是攒满一个周期就触发一次的节拍，
	// 冷却是"距离上次触发过了多久"的单向累加，所以它不参与下面的 while 扣减。
	SecondsSinceLastAggregation += DeltaSeconds;
	DecayAccumulatorSeconds += DeltaSeconds;
	while (DecayAccumulatorSeconds >= Settings->ChumDecayIntervalSeconds)
	{
		DecayAccumulatorSeconds -= Settings->ChumDecayIntervalSeconds;
		ApplyOneDecayStep();
	}
}

// 单次衰减流程：所有窝点同时按保留比例缩小；缩完 Total 低于地板的窝点视为已经散掉，整条移除而不是留一个空壳。
// 倒序遍历是为了在移除时不打乱尚未处理的下标；用 RemoveAtSwap 是因为窝点数组的顺序本身没有语义，命中判定只看圆心距离。
void UCatChumSpotSubsystem::ApplyOneDecayStep()
{
	const UCatEnvironmentSettings* Settings = GetDefault<UCatEnvironmentSettings>();
	if (!Settings)
	{
		return;
	}
	for (int32 SpotIndex = Spots.Num() - 1; SpotIndex >= 0; --SpotIndex)
	{
		FChumSpot& Spot = Spots[SpotIndex];
		Spot.Pool.Fishy *= Settings->ChumDecayFactor;
		Spot.Pool.Fragrant *= Settings->ChumDecayFactor;
		Spot.Pool.Fermented *= Settings->ChumDecayFactor;
		if (Spot.Pool.Total() < Settings->ChumDecayFloorTotal)
		{
			Spots.RemoveAtSwap(SpotIndex);
		}
	}
}

// 运行态重置流程：清掉本局全部窝点、衰减节拍与终态缓存，并推进集合 Revision，
// 使跨局残留的查询快照和重试命令都因 Revision 冲突而失效，不会把上一局的窝复活。
void UCatChumSpotSubsystem::ResetRuntimeChumFromAuthority()
{
	Spots.Reset();
	DecayAccumulatorSeconds = 0.0;
	SecondsSinceLastAggregation = 0.0;
	bHasAggregatedThisRun = false;
	ChumTerminalCache.Reset();
	ChumPayloadByKey.Reset();
	++AggregationRevision;
}

// 冷却判定流程：本局一次都没投过窝时不存在冷却；冷却秒数没配或非法时同样不设冷却，
// 因为"忘了配就永远压着自然涌现"比"没有冷却"更难被发现。其余情况只比较距上次投窝的秒数。
bool UCatChumSpotSubsystem::IsAggregationMomentOnCooldown(const double CooldownSeconds) const
{
	return bHasAggregatedThisRun && FMath::IsFinite(CooldownSeconds) && CooldownSeconds > 0.0
		&& SecondsSinceLastAggregation < CooldownSeconds;
}
