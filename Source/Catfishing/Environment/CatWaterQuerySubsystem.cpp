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
