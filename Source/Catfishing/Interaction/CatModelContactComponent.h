#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "CatModelContactComponent.generated.h"

class USkeletalBodySetup;
class UPoseableMeshComponent;

/** One authored PhysicsAsset body, following the final visible bone without simulating it. */
UCLASS(Transient)
class CATFISHING_API UCatModelContactBody : public UPrimitiveComponent
{
    GENERATED_BODY()
public:
    UCatModelContactBody();
    void Initialize(USkeletalBodySetup* Setup);
    FName GetBoneName() const;
    virtual UBodySetup* GetBodySetup() override;
    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
private:
    UPROPERTY(Transient) TObjectPtr<USkeletalBodySetup> AuthoredBody;
};

/** Model surfaces for grabbing and peer contact; terrain movement remains owned by CMC. */
UCLASS(ClassGroup=(Catfishing))
class CATFISHING_API UCatModelContactComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UCatModelContactComponent();
    bool Initialize(UPoseableMeshComponent* FinalPose);
    void RefreshPose();
    bool HasModelContacts() const { return !Bodies.IsEmpty(); }
    const TArray<TObjectPtr<UCatModelContactBody>>& GetBodies() const { return Bodies; }
    /** Normal points from this character toward Other; depth is in world centimeters. */
    bool FindPeerContact(const UCatModelContactComponent* Other, FVector& Normal, double& Depth) const;
    static bool UsesModelContacts(const AActor* Actor);
    static bool IsLegacyContactProxy(const UPrimitiveComponent* Component);
protected:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Tick) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UPROPERTY(Transient) TObjectPtr<UPoseableMeshComponent> Pose;
    UPROPERTY(Transient) TArray<TObjectPtr<UCatModelContactBody>> Bodies;
};
