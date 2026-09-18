#pragma once

#include "CoreMinimal.h"
#include "Items/CatItem.h"
#include "CatWhipActor.generated.h"

class ACatCharacter;
class UAnimSequence;

/** 地面载体沿用库存交互；使用副本只负责骨骼表现和服务器扫掠，不再发放物品。 */
UCLASS()
class CATFISHING_API ACatWhipActor : public ACatItem
{
    GENERATED_BODY()
public:
    ACatWhipActor();
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual FCatInventoryReceiveBatch GetPickupInventory() const override;
    virtual bool CanInteract_Implementation(AController* Controller) const override;
    virtual void InitializeActorSpawnConfig() override;
    bool IsWhipConfigurationReady() const;
    bool StartSwingFromAuthority(ACatCharacter* Character, FGuid RequestId, FGuid ItemId);
    float GetSwingDuration() const;
    int32 GetHitCount() const { return HitTargets.Num(); }
    int32 GetOccludedTargetCount() const { return OccludedTargets.Num(); }
    USkeletalMeshComponent* GetWhipMesh() const { return SkeletalMesh; }

    /** 拖入关卡时发放的单件物品；落地已有实物载荷优先，绝不重新造身份。 */
    UPROPERTY(EditDefaultsOnly, Category="Whip") TObjectPtr<UCatInventoryItemDefinition> ItemDefinition;
    UPROPERTY(EditDefaultsOnly, Category="Whip") TObjectPtr<UAnimSequence> AttackAnimation;
    UPROPERTY(EditDefaultsOnly, Category="Whip", meta=(Units="s", ClampMin="0")) float HitWindowStart = 0.42f;
    UPROPERTY(EditDefaultsOnly, Category="Whip", meta=(Units="s", ClampMin="0")) float HitWindowEnd = 0.86f;
    UPROPERTY(EditDefaultsOnly, Category="Whip", meta=(Units="cm", ClampMin="1")) float TraceRadiusCm = 5.f;
    /** 试玩初值：牛顿秒，提交至身体时乘 100 转 kg·cm/s；没有伤害或力量 buff。 */
    UPROPERTY(EditDefaultsOnly, Category="Whip", meta=(ClampMin="0")) float ImpulseNewtonSeconds = 3.f;
    UPROPERTY(EditDefaultsOnly, Category="Whip", meta=(Units="cm", ClampMin="1")) float MaxTargetDistanceCm = 180.f;
    /** 只作演示握持校正，不把猫 Mesh 的缩放传给 120 cm 皮鞭。 */
    UPROPERTY(EditDefaultsOnly, Category="Whip") FVector GripOffsetCm = FVector(0,0,3);
    UPROPERTY(EditDefaultsOnly, Category="Whip") FRotator MeshRotationOffset = FRotator::ZeroRotator;

protected:
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
private:
    UPROPERTY(ReplicatedUsing=OnRep_Swing) TObjectPtr<ACatCharacter> SwingOwner;
    UPROPERTY(Replicated) FGuid SwingRequestId;
    UPROPERTY(Replicated) FGuid SourceItemId;
    UPROPERTY(Replicated) double SwingStartServerTime = 0;
    UPROPERTY(Replicated) float SwingYaw = 0;
    UFUNCTION() void OnRep_Swing();
    UFUNCTION(NetMulticast, Unreliable) void MulticastHitObserved(ACatCharacter* Target);
    bool bPresentationStarted = false;
    double LastSampleTime = 0;
    FTransform LastGripTransform;
    TArray<FVector> PreviousPoints;
    TSet<TWeakObjectPtr<ACatCharacter>> HitTargets;
    TSet<TWeakObjectPtr<ACatCharacter>> OccludedTargets;
    double ServerTime() const;
    FTransform GetGripTransform() const;
    void EvaluatePose(double Time, const FTransform& Grip);
    void GetTracePoints(TArray<FVector>& Points) const;
    void Sweep(const FVector& Start, const FVector& End);
};
