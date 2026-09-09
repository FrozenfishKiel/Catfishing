#pragma once

#include "CoreMinimal.h"
#include "CatFishingRodAimState.generated.h"

/** 持竿输入快照；累计的是经过输入灵敏度缩放的鼠标角增量，不是客户端竿姿态。 */
USTRUCT()
struct FCatFishingRodAimSample
{
	GENERATED_BODY()

	UPROPERTY() FGuid RodActorId;
	UPROPERTY() uint32 InputEpoch = 0;
	UPROPERTY() int64 Sequence = 0;
	/** X = Yaw、Y = Pitch，单位度。Pitch 在发送端逐帧去除越过身体限位的量；累计量不随按键/回执归零。 */
	UPROPERTY() FVector2D CumulativeLookDegrees = FVector2D::ZeroVector;

	bool IsValid() const
	{
		return Sequence > 0 && !CumulativeLookDegrees.ContainsNaN()
			&& FMath::Abs(CumulativeLookDegrees.X) < 1.e12 && FMath::Abs(CumulativeLookDegrees.Y) < 1.e12;
	}
};

/** 权威转杆目标。放线前只缓存输入；放线重设后只吃有序新增量，CMC 旧控制角不再参与。 */
struct CATFISHING_API FCatFishingRodAimState
{
	bool AcceptSample(const FCatFishingRodAimSample& Sample, double MinimumPitch, double MaximumPitch);
	bool Rebase(const FCatFishingRodAimSample& EdgeSample, const FRotator& ActualAim,
		double MinimumPitch, double MaximumPitch);
	bool IsRebased() const { return bRebased; }
	FRotator GetRequestedAim() const { return RequestedAim; }
	int64 GetLastSequence() const { return LatestSample.Sequence; }
	int64 GetLastRebaseSequence() const { return LastRebaseSequence; }
	void Reset() { *this = {}; }

private:
	void RefreshRequestedAim(double MinimumPitch, double MaximumPitch);
	FCatFishingRodAimSample LatestSample;
	FVector2D RebaseLookDegrees = FVector2D::ZeroVector;
	FRotator RebaseAim = FRotator::ZeroRotator;
	FRotator RequestedAim = FRotator::ZeroRotator;
	int64 LastRebaseSequence = 0;
	bool bRebased = false;
};
