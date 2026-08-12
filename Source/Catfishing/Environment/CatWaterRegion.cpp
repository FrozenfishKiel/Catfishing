#include "Environment/CatWaterRegion.h"

// 构造流程：关闭每帧更新与网络复制；水域仅由查询子系统按需读取，避免建立第二套 Environment 模拟。
ACatWaterRegion::ACatWaterRegion()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
}

// 配置校验流程：按 gate、ID、Revision、有限中心与三个正半尺寸逐项检查；任何 Unset 都阻止水域进入查询候选。
bool ACatWaterRegion::IsRuntimeConfigured() const
{
	return bEnablePrototypeBounds && !RegionId.IsNone() && RegionRevision > 0
		&& !LocalCenterOffset.ContainsNaN() && !HalfExtent.ContainsNaN()
		&& HalfExtent.X > 0.0 && HalfExtent.Y > 0.0 && HalfExtent.Z > 0.0;
}

// 点包含流程：配置无效或坐标非有限时立即失败；否则只对显式世界 AABB 做包含测试，不猜测岸线、深度或 WaterBody 语义。
bool ACatWaterRegion::ContainsWorldPoint(const FVector& WorldPoint) const
{
	if (!IsRuntimeConfigured() || WorldPoint.ContainsNaN())
	{
		return false;
	}
	const FVector WorldCenter = GetActorLocation() + LocalCenterOffset;
	return FBox::BuildAABB(WorldCenter, HalfExtent).IsInsideOrOn(WorldPoint);
}

// 快照构造流程：复制稳定 ID、配置 Revision 与本次世界 AABB；不暴露 Actor 指针，调用方不能反向修改区域。
FCatWaterRegionSnapshot ACatWaterRegion::MakeSnapshot() const
{
	FCatWaterRegionSnapshot Snapshot;
	Snapshot.RegionId = RegionId;
	Snapshot.RegionRevision = RegionRevision;
	Snapshot.WorldCenter = GetActorLocation() + LocalCenterOffset;
	Snapshot.HalfExtent = HalfExtent;
	Snapshot.ChumPool = ChumPool;
	return Snapshot;
}

// 聚鱼提交流程：两个 Source 共用相同身份/RequestId 缓存、Region/Revision/三轴/预算校验；成功只增加同一 ChumPool 与 RegionRevision，不创建自然事件旁路。
FCatAggregationResult ACatWaterRegion::ContributeAggregation(const FCatAggregationCommand& Command)
{
	FCatAggregationResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	const FString CacheKey = FString::Printf(TEXT("%s|%s"), *Command.Context.StableNetId,
		*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	if (const FCatAggregationResult* Cached = AggregationTerminalCache.Find(CacheKey))
	{
		Result = *Cached;
		Result.Command.bCommitted = false;
		Result.Command.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	Result.Command.Error = ValidateAggregation(Command);
	if (Result.Command.Error == ECatDomainCommandError::None)
	{
		ChumPool.Fishy += Command.Contribution.Fishy;
		ChumPool.Fragrant += Command.Contribution.Fragrant;
		ChumPool.Fermented += Command.Contribution.Fermented;
		++RegionRevision;
		Result.Command.bCommitted = true;
		Result.Command.Error = ECatDomainCommandError::None;
	}
	Result.Command.Revision = RegionRevision;
	Result.ChumPool = ChumPool;
	AggregationTerminalCache.Add(CacheKey, Result);
	return Result;
}

// 聚鱼预检流程：按 authority、运行 gate、身份、Region/Revision、三轴与共享总预算逐项验证；只返回拒绝语义，不写池、Revision 或终态缓存。
ECatDomainCommandError ACatWaterRegion::ValidateAggregation(const FCatAggregationCommand& Command) const
{
	const double ExistingTotal = ChumPool.Fishy + ChumPool.Fragrant + ChumPool.Fermented;
	const double AddedTotal = Command.Contribution.Fishy + Command.Contribution.Fragrant + Command.Contribution.Fermented;
	if (!HasAuthority() || !bEnableAggregation || !FMath::IsFinite(AggregationBudget) || AggregationBudget <= 0.0)
	{
		return ECatDomainCommandError::PolicyUndecided;
	}
	if (!Command.Context.RequestId.IsValid() || Command.Context.StableNetId.IsEmpty()
		|| Command.RegionId != RegionId || !Command.Contribution.IsValidContribution())
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	if (Command.Context.ExpectedRevision != RegionRevision)
	{
		return ECatDomainCommandError::RevisionConflict;
	}
	return !FMath::IsFinite(ExistingTotal + AddedTotal) || ExistingTotal + AddedTotal > AggregationBudget
		? ECatDomainCommandError::CapacityExceeded : ECatDomainCommandError::None;
}
