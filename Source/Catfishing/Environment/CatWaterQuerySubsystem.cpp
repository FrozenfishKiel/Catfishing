#include "Environment/CatWaterQuerySubsystem.h"

#include "EngineUtils.h"
#include "Environment/CatWaterRegion.h"

// 水域查询流程：先拒绝非白天/关闭钓鱼/无效 Revision 与坐标，再扫描当前 World 的完整区域；零命中与多命中分别返回明确失败，唯一命中才复制只读快照。
FCatWaterQueryResult UCatWaterQuerySubsystem::QueryWaterRegion(const FCatWaterQuery& Query) const
{
	FCatWaterQueryResult Result;
	Result.RunRevision = Query.RunRevision;
	if (Query.RunRevision <= 0)
	{
		Result.Error = ECatWaterQueryError::RevisionConflict;
		return Result;
	}
	if (Query.RunPhase.Phase != ECatRunPhase::DayActive || !Query.RunPhase.bFishingAllowed)
	{
		Result.Error = ECatWaterQueryError::FishingClosed;
		return Result;
	}
	if (Query.WorldLocation.ContainsNaN())
	{
		Result.Error = ECatWaterQueryError::InvalidLocation;
		return Result;
	}

	const ACatWaterRegion* MatchedRegion = nullptr;
	for (TActorIterator<ACatWaterRegion> It(GetWorld()); It; ++It)
	{
		const ACatWaterRegion* Candidate = *It;
		if (!Candidate || !Candidate->ContainsWorldPoint(Query.WorldLocation))
		{
			continue;
		}
		if (MatchedRegion)
		{
			Result.Error = ECatWaterQueryError::AmbiguousRegion;
			return Result;
		}
		MatchedRegion = Candidate;
	}
	if (!MatchedRegion)
	{
		Result.Error = ECatWaterQueryError::RegionNotFound;
		return Result;
	}

	Result.bSucceeded = true;
	Result.Error = ECatWaterQueryError::None;
	Result.Region = MatchedRegion->MakeSnapshot();
	return Result;
}

// 区域中心查找流程：先清输出，空 ID 直接拒绝；再扫一遍当前 World 的已配置水域，按 RegionId 精确匹配。
// 命中第二个同名区域时立刻返回 false 并保持输出为零向量，不做"取第一个"的兜底——关卡重名是配置错误，
// 悄悄挑一个会让自然事件的落点漂到另一片水里，而那种漂移在运行时几乎不可能被看出来。
bool UCatWaterQuerySubsystem::TryGetRegionCenter(const FName RegionId, FVector& OutWorldCenter) const
{
	OutWorldCenter = FVector::ZeroVector;
	if (RegionId.IsNone())
	{
		return false;
	}
	const ACatWaterRegion* MatchedRegion = nullptr;
	for (TActorIterator<ACatWaterRegion> It(GetWorld()); It; ++It)
	{
		const ACatWaterRegion* Candidate = *It;
		if (!Candidate || !Candidate->IsRuntimeConfigured() || Candidate->RegionId != RegionId)
		{
			continue;
		}
		if (MatchedRegion)
		{
			return false;
		}
		MatchedRegion = Candidate;
	}
	if (!MatchedRegion)
	{
		return false;
	}
	OutWorldCenter = MatchedRegion->MakeSnapshot().WorldCenter;
	return true;
}
