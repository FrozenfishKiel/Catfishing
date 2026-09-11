#include "Fishing/Integration/CatFishingRodAimState.h"

namespace
{
	bool IsPitchRangeValid(const double MinimumPitch, const double MaximumPitch)
	{
		return FMath::IsFinite(MinimumPitch) && FMath::IsFinite(MaximumPitch)
			&& MinimumPitch <= MaximumPitch;
	}

	bool IsInputTimeValid(const double NowSeconds)
	{
		return FMath::IsFinite(NowSeconds) && NowSeconds >= 0.0;
	}
}

void FCatFishingRodAimState::AnchorAtActual(const FRotator& ActualAim, const FVector2D& LookDegrees)
{
	RebaseAim = ActualAim.GetNormalized();
	RebaseAim.Roll = 0.0;
	RebaseLookDegrees = LookDegrees;
	RequestedAim = RebaseAim;
	bRebased = true;
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
	const FRotator& ActualAim, const double MinimumPitch, const double MaximumPitch, const double NowSeconds)
{
	if (!Sample.IsValid() || Sample.Sequence <= LatestSample.Sequence
		|| ActualAim.ContainsNaN() || !IsPitchRangeValid(MinimumPitch, MaximumPitch)
		|| !IsInputTimeValid(NowSeconds) || (bRebased && NowSeconds < LastSampleTimeSeconds)) return false;

	const bool bSameStroke = bRebased && Sample.MouseStrokeSequence == LatestSample.MouseStrokeSequence;
	if (!Sample.bMouseActive)
	{
		AnchorAtActual(ActualAim, Sample.CumulativeLookDegrees);
		DiscardedThroughSequence = Sample.Sequence;
	}
	else if (!bSameStroke || !IsMouseActive(NowSeconds))
	{
		// 同段中断后只接收后续新增量；新段保留完整起点，使首包丢失不吞掉本段移动。
		AnchorAtActual(ActualAim, bSameStroke ? Sample.CumulativeLookDegrees : Sample.MouseStrokeStartLookDegrees);
		if (bSameStroke) DiscardedThroughSequence = Sample.Sequence;
	}
	LatestSample = Sample;
	LastSampleTimeSeconds = NowSeconds;
	bMouseActive = Sample.bMouseActive;
	if (bMouseActive) RefreshRequestedAim(MinimumPitch, MaximumPitch);
	return true;
}

bool FCatFishingRodAimState::Rebase(const FCatFishingRodAimSample& EdgeSample,
	const FRotator& ActualAim, const double MinimumPitch, const double MaximumPitch, const double NowSeconds)
{
	if (!EdgeSample.IsValid() || EdgeSample.Sequence <= LastRebaseSequence || ActualAim.ContainsNaN()
		|| !IsPitchRangeValid(MinimumPitch, MaximumPitch) || !IsInputTimeValid(NowSeconds)
		|| (bRebased && NowSeconds < LastSampleTimeSeconds)) return false;

	LastRebaseSequence = EdgeSample.Sequence;
	const bool bSameStroke = bRebased && EdgeSample.MouseStrokeSequence == LatestSample.MouseStrokeSequence;
	if (LatestSample.Sequence > EdgeSample.Sequence)
	{
		// 旧按键仍可执行放线，但只能重锚仍在活动的同一段，不能覆盖更新的停止或新段。
		if (!bSameStroke || !EdgeSample.bMouseActive || !LatestSample.bMouseActive
			|| EdgeSample.Sequence <= DiscardedThroughSequence) return true;
		if (!IsMouseActive(NowSeconds))
		{
			ExpireInput(NowSeconds, ActualAim);
			return true;
		}
		AnchorAtActual(ActualAim, EdgeSample.CumulativeLookDegrees);
		RefreshRequestedAim(MinimumPitch, MaximumPitch);
		return true;
	}

	// 新鲜按键携带的 active 是输入事实；过期同段的迟到按键不能重新激活鼠标。
	const bool bCanRemainActive = EdgeSample.bMouseActive && (!bSameStroke || IsMouseActive(NowSeconds));
	LatestSample = EdgeSample;
	LastSampleTimeSeconds = NowSeconds;
	AnchorAtActual(ActualAim, EdgeSample.CumulativeLookDegrees);
	bMouseActive = bCanRemainActive;
	if (!bMouseActive) DiscardedThroughSequence = EdgeSample.Sequence;
	return true;
}

bool FCatFishingRodAimState::IsMouseActive(const double NowSeconds) const
{
	return bMouseActive && IsInputTimeValid(NowSeconds) && NowSeconds >= LastSampleTimeSeconds
		&& NowSeconds - LastSampleTimeSeconds < InputTimeoutSeconds;
}

bool FCatFishingRodAimState::ExpireInput(const double NowSeconds, const FRotator& ActualAim)
{
	if (!bMouseActive || !IsInputTimeValid(NowSeconds) || ActualAim.ContainsNaN()
		|| NowSeconds < LastSampleTimeSeconds || IsMouseActive(NowSeconds)) return false;
	return StopInput(ActualAim);
}

bool FCatFishingRodAimState::StopInput(const FRotator& ActualAim)
{
	if (!bMouseActive || ActualAim.ContainsNaN()) return false;
	bMouseActive = false;
	DiscardedThroughSequence = LatestSample.Sequence;
	AnchorAtActual(ActualAim, LatestSample.CumulativeLookDegrees);
	return true;
}
