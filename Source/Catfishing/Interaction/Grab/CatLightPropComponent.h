#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CatLightPropComponent.generated.h"

class UPrimitiveComponent;

UENUM()
enum class ECatLightPropMode : uint8 { Inactive, Falling, Held, Loaded };

USTRUCT()
struct FCatLightPropState
{
	GENERATED_BODY()
	UPROPERTY() FGuid PropId;
	UPROPERTY() uint32 Revision = 0;
	UPROPERTY() int32 GripCount = 0;
	UPROPERTY() ECatLightPropMode Mode = ECatLightPropMode::Inactive;
	UPROPERTY() bool bExternalLoad = false;
};

/** Weight-free carrying and gentle release. Actual grip constraints and external loads stay bidirectional. */
UCLASS()
class CATFISHING_API UCatLightPropComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UCatLightPropComponent();
	void Initialize(UPrimitiveComponent* InBody);
	void RestoreOrdinaryPhysics();
	void RefreshGripsFromAuthority(FName Reason);
	void SetExternalLoadFromAuthority(bool bActive);
	/** A controlled prop routes physical grips to its real carrier; null restores the dynamic prop. */
	void SetGripCarrierFromAuthority(UPrimitiveComponent* Carrier);
	UPrimitiveComponent* GetGripCarrier() const { return GripCarrier.Get(); }
	void RefreshGripConstraintsFromAuthority();
	const FCatLightPropState& GetState() const { return State; }
	UPrimitiveComponent* GetBody() const { return Body; }
	static UCatLightPropComponent* FindFor(const UPrimitiveComponent* Target);
	static constexpr float ReleasedGravityScale = 0.25f;
	static constexpr float FallingLinearDamping = 2.0f;
	static constexpr float FallingAngularDamping = 4.0f;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;
private:
	void RefreshMode(FName Reason);
	void ApplyPhysicsPolicy();
	void LogState(FName Event, FName Reason) const;
	UFUNCTION() void OnRep_State();
	UPROPERTY(Transient) TObjectPtr<UPrimitiveComponent> Body;
	UPROPERTY(ReplicatedUsing=OnRep_State) FCatLightPropState State;
	float OriginalLinearDamping = 0;
	float OriginalAngularDamping = 0;
	bool bEndingPlay = false;
	TWeakObjectPtr<UPrimitiveComponent> GripCarrier;
};
