#pragma once

#include "CoreMinimal.h"
#include "CatFishingCastTrajectory.generated.h"

/** 一次抛竿冻结的弹道；服务器与客户端按同一服务器时间求值，不累积帧误差。 */
USTRUCT(BlueprintType)
struct CATFISHING_API FCatFishingCastTrajectory
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FVector Origin = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FVector Landing = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FVector InitialVelocity = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) double GravityZ = 0.0;
	UPROPERTY(BlueprintReadOnly) double DurationSeconds = 0.0;
	UPROPERTY(BlueprintReadOnly) double StartedServerTime = 0.0;

	bool Initialize(const FVector& InOrigin, const FVector& InLanding, double InGravityZ, double InStartedServerTime);
	FVector Evaluate(double ServerTime) const;
};
