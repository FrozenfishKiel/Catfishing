#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CatForceReactionComponent.generated.h"

class UAnimMontage;
class UCatPhysicalBodyComponent;

/** Direction the body moves toward, never the side the other player stands on. */
UENUM(BlueprintType)
enum class ECatForceReactionDirection : uint8 { Forward, Backward, Left, Right };

/** One event per uninterrupted load; animation completion and direction changes cannot rearm it. */
struct CATFISHING_API FCatForceReactionGate
{
    bool bLoaded = false;
    double ReleasedSeconds = 0;
    bool Step(double LoadNewtons, double DeltaSeconds, double TriggerNewtons, double ReleaseNewtons, double RearmSeconds);
    static ECatForceReactionDirection Direction(const FVector& Forward, const FVector& Force);
};

/** Read-only cosmetic consumer of completed character interaction forces. No motor or damage writes. */
UCLASS(ClassGroup=(Catfishing), meta=(BlueprintSpawnableComponent))
class CATFISHING_API UCatForceReactionComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UCatForceReactionComponent();
    UPROPERTY(EditDefaultsOnly, Category="Catfishing|ForceReaction") bool bEnabled = false;
    UPROPERTY(EditDefaultsOnly, Category="Catfishing|ForceReaction", meta=(ClampMin="0.001", Units="N")) double TriggerNewtons = 5;
    UPROPERTY(EditDefaultsOnly, Category="Catfishing|ForceReaction", meta=(ClampMin="0", Units="N")) double ReleaseNewtons = 2;
    UPROPERTY(EditDefaultsOnly, Category="Catfishing|ForceReaction", meta=(ClampMin="0.01", Units="s")) double RearmSeconds = .2;
    /** Forward, Backward, Left, Right; original montage identities resolved by the skin's AnimationOverrides. */
    UPROPERTY(EditDefaultsOnly, Category="Catfishing|ForceReaction") TArray<TObjectPtr<UAnimMontage>> DirectionalMontages;
    uint32 GetObservedEvent() const { return ObservedEvent; }
    uint32 GetPlayedCount() const { return PlayedCount; }
    ECatForceReactionDirection GetObservedDirection() const { return ObservedDirection; }
protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UFUNCTION(NetMulticast, Reliable)
    void MulticastReact(uint32 EventId, ECatForceReactionDirection Direction, float LoadNewtons);
    void LogEvent(const TCHAR* Event, uint32 EventId, ECatForceReactionDirection Direction, double Load, const TCHAR* Result) const;
    UPROPERTY(Transient) TObjectPtr<UCatPhysicalBodyComponent> Body;
    UPROPERTY(Transient) TObjectPtr<UAnimMontage> PlayingMontage;
    FCatForceReactionGate Gate;
    uint32 AuthorityEvent = 0, ObservedEvent = 0, PlayedCount = 0;
    ECatForceReactionDirection ObservedDirection = ECatForceReactionDirection::Forward;
};
