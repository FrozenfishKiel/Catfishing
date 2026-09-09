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
	UPROPERTY() bool bMouseActive = false;
	/** 每段连续鼠标移动的标识；停止快照保留当前段，尚未移动时可为零。 */
	UPROPERTY() int64 MouseStrokeSequence = 0;
	/** 本段第一帧增量加入前的全局累计角度；每包完整携带，供首包丢失时重建。 */
	UPROPERTY() FVector2D MouseStrokeStartLookDegrees = FVector2D::ZeroVector;

	bool IsValid() const
	{
		return Sequence > 0 && MouseStrokeSequence >= 0 && (!bMouseActive || MouseStrokeSequence > 0)
			&& !CumulativeLookDegrees.ContainsNaN() && !MouseStrokeStartLookDegrees.ContainsNaN()
			&& FMath::Abs(CumulativeLookDegrees.X) < 1.e12 && FMath::Abs(CumulativeLookDegrees.Y) < 1.e12
			&& FMath::Abs(MouseStrokeStartLookDegrees.X) < 1.e12 && FMath::Abs(MouseStrokeStartLookDegrees.Y) < 1.e12;
	}
};

/** 权威鼠标目标。每段移动锚定实际杆向；停止或输入超时立即撤销主动目标，旧控制角不再参与。 */
struct CATFISHING_API FCatFishingRodAimState
{
	static constexpr double InputTimeoutSeconds = 0.15;

	bool AcceptSample(const FCatFishingRodAimSample& Sample, const FRotator& ActualAim,
		double MinimumPitch, double MaximumPitch, double NowSeconds);
	bool Rebase(const FCatFishingRodAimSample& EdgeSample, const FRotator& ActualAim,
		double MinimumPitch, double MaximumPitch, double NowSeconds);
	bool IsMouseActive(double NowSeconds) const;
	bool ExpireInput(double NowSeconds, const FRotator& ActualAim);
	bool IsRebased() const { return bRebased; }
	FRotator GetRequestedAim() const { return RequestedAim; }
	int64 GetLastSequence() const { return LatestSample.Sequence; }
	int64 GetLastRebaseSequence() const { return LastRebaseSequence; }
	void Reset() { *this = {}; }

private:
	void AnchorAtActual(const FRotator& ActualAim, const FVector2D& LookDegrees);
	void RefreshRequestedAim(double MinimumPitch, double MaximumPitch);
	FCatFishingRodAimSample LatestSample;
	FVector2D RebaseLookDegrees = FVector2D::ZeroVector;
	FRotator RebaseAim = FRotator::ZeroRotator;
	FRotator RequestedAim = FRotator::ZeroRotator;
	int64 LastRebaseSequence = 0;
	/** 停止/超时恢复已经丢弃的输入边界；晚到按键不能把这部分累计量重新灌回。 */
	int64 DiscardedThroughSequence = 0;
	double LastSampleTimeSeconds = 0.0;
	bool bRebased = false;
	bool bMouseActive = false;
};
