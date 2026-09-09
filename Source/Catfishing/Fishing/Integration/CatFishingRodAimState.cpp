#include "Fishing/Integration/CatFishingRodAimState.h"

namespace
{
	bool IsPitchRangeValid(const double MinimumPitch, const double MaximumPitch)
	{
		return FMath::IsFinite(MinimumPitch) && FMath::IsFinite(MaximumPitch)
			&& MinimumPitch <= MaximumPitch;
	}
}

void FCatFishingRodAimState::RefreshRequestedAim(
	const double MinimumPitch, const double MaximumPitch)
{
	const FVector2D Delta = LatestSample.CumulativeLookDegrees - RebaseLookDegrees;
	RequestedAim.Yaw = FRotator::NormalizeAxis(RebaseAim.Yaw + Delta.X);
	// 发送端逐帧去除越限量；从不变基准重建，丢包/合包不能改变最终目标。
	RequestedAim.Pitch = FMath::Clamp(RebaseAim.Pitch + Delta.Y, MinimumPitch, MaximumPitch);
	RequestedAim.Roll = 0.0;
}

bool FCatFishingRodAimState::AcceptSample(const FCatFishingRodAimSample& Sample,
	const double MinimumPitch, const double MaximumPitch)
{
	if (!Sample.IsValid() || Sample.Sequence <= LatestSample.Sequence
		|| !IsPitchRangeValid(MinimumPitch, MaximumPitch)) return false;
	LatestSample = Sample;
	if (bRebased) RefreshRequestedAim(MinimumPitch, MaximumPitch);
	return true;
}

bool FCatFishingRodAimState::Rebase(const FCatFishingRodAimSample& EdgeSample,
	const FRotator& ActualAim, const double MinimumPitch, const double MaximumPitch)
{
	if (!EdgeSample.IsValid() || EdgeSample.Sequence <= LastRebaseSequence || ActualAim.ContainsNaN()
		|| !IsPitchRangeValid(MinimumPitch, MaximumPitch)) return false;
	RebaseAim = ActualAim.GetNormalized();
	RebaseLookDegrees = EdgeSample.CumulativeLookDegrees;
	// 输入快照与按键 RPC 可乱序：保留已经收到、确实发生在按下之后的鼠标量。
	if (LatestSample.Sequence <= EdgeSample.Sequence)
	{
		LatestSample = EdgeSample;
	}
	LastRebaseSequence = EdgeSample.Sequence;
	bRebased = true;
	RefreshRequestedAim(MinimumPitch, MaximumPitch);
	return true;
}
