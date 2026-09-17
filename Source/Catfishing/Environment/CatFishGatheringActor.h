#pragma once

#include "CoreMinimal.h"
#include "Environment/CatWaterTypes.h"
#include "GameFramework/Actor.h"
#include "CatFishGatheringActor.generated.h"

class UInstancedStaticMeshComponent;

/** 聚鱼公开事实。事件独立于窝料寿命，不写浓度或库存。 */
USTRUCT(BlueprintType)
struct FCatFishGatheringState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FGuid EventId;
	UPROPERTY(BlueprintReadOnly) FGuid RequestId;
	UPROPERTY(BlueprintReadOnly) FCatWaterRegionHandle WaterRegion;
	UPROPERTY(BlueprintReadOnly) FVector Center = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly) double RadiusCentimeters = 0.0;
	UPROPERTY(BlueprintReadOnly) double StartedServerTime = 0.0;
	UPROPERTY(BlueprintReadOnly) double EndsServerTime = 0.0;
	UPROPERTY(BlueprintReadOnly) double BiteSpeedMultiplier = 1.0;

	bool IsActive(double ServerTime) const;
	bool Contains(const FVector& Point, const FCatWaterRegionHandle& Region, double ServerTime) const;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FCatFishGatheringEnded, FGuid);

/** 服务器生成/销毁，客户端只消费复制状态和原生占位演出。 */
UCLASS()
class CATFISHING_API ACatFishGatheringActor final : public AActor
{
	GENERATED_BODY()
public:
	ACatFishGatheringActor();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	bool InitializeFromAuthority(const FCatFishGatheringState& InState);
	const FCatFishGatheringState& GetPublicState() const { return State; }
	FCatFishGatheringEnded OnEnded;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION() void OnRep_State();
	void FinishFromAuthority();
	void AnimateFishShadows();
	double GetServerTime() const;
	UPROPERTY(ReplicatedUsing=OnRep_State) FCatFishGatheringState State;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> Ring;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> FishShadows;
	FTimerHandle EndTimer;
	FTimerHandle VisualTimer;
	bool bReceivedState = false;
};
