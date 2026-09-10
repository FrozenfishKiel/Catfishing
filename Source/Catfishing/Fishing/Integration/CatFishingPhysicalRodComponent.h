#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CatFishingPhysicalRodComponent.generated.h"

class ACatCharacter;
class ACatFishingRodActor;
class APlayerState;
class AController;
class UBoxComponent;
class UPrimitiveComponent;
class UCatPhysicalBodyComponent;
class UCatPhysicsGrabComponent;
class UCatFishingFightRunner;
struct FCatPhysicsGripState;
struct FCatFightRodConstraintInput;
struct FCatFishingRodRotationInput;
DECLARE_MULTICAST_DELEGATE_OneParam(FCatFishingBeforePhysicsForces, float);

/** Physical receiver and mechanical observations. Owner control and resource transactions remain in Service/Session. */
UCLASS()
class CATFISHING_API UCatFishingPhysicalRodComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UCatFishingPhysicalRodComponent();
	void Initialize(UBoxComponent* InBody, const FTransform& GripLocal, const FTransform& TipLocal);
	bool IsReady() const { return bReady; }
	UBoxComponent* GetBody() const { return Body; }
	FGuid GetLineLoadSessionIdForDiagnostics() const { return LoadSessionId; }
	uint64 GetLineLoadStepForDiagnostics() const { return LoadStep; }
	FVector GetLineForceNewtonsForDiagnostics() const { return LineForceNewtons; }
	FVector GetAppliedLineImpulseNewtonSecondsForDiagnostics() const { return AppliedLineImpulse; }
	FVector GetSubmittedLineImpulseNewtonSecondsForDiagnostics() const { return SubmittedLineImpulse; }
	FVector GetDiscardedLineImpulseNewtonSecondsForDiagnostics() const { return DiscardedLineImpulse; }
	FVector GetQueuedLineImpulseNewtonSecondsForDiagnostics() const;
	double GetQueuedLineSecondsForDiagnostics() const;
	/** The owning rod calls this from its PostPhysics tick, after Chaos consumes this frame's force. */
	void FinishPhysicsFrame();
	FTransform GetObservedActorTransform() const;
	FVector GetPointVelocity(const FVector& WorldPoint) const;
	FVector GetAngularVelocityRadiansPerSecond() const;
	bool BeginPrimaryHold(APlayerState* Player, bool bPositionNewRod);
	bool CommitPrimaryHold(APlayerState* Player);
	bool IsHeldBy(const APlayerState* Player) const;
	void ReleasePrimaryHold(APlayerState* Player, FName Reason);
	void ReleaseAllConnections(FName Reason);
	void RefreshObservedPose();
	void RefreshPrimaryControl();
	void PopulateEndpointResponse(FCatFightRodConstraintInput& OutInput);
	void SetLineLoad(FGuid SessionId, uint64 Step, const FVector& ForceNewtons, double SimulatedSeconds, double LifetimeSeconds);
	void ClearLineLoad(FGuid SessionId = FGuid());
protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	friend class FCatFishingPhysicalGripGraphTest;
	friend class UCatFishingFightRunner;
	FCatFishingBeforePhysicsForces BeforePhysicsForces;
	FSimpleMulticastDelegate PhysicsReceiverUnavailable;
	void UpdatePrimaryMotorBudget();
	void RefreshInputTickPrerequisites();
	void AdvanceControlledAim(float DeltaTime);
	bool BuildControlledRotationInput(FCatFishingRodRotationInput& Input) const;
	void PopulateCMCEndpointPrediction(FCatFightRodConstraintInput& OutInput);
	void RefreshControlledCarrier();
	void PositionControlledRod();
	TWeakObjectPtr<UPrimitiveComponent> ControlledBody;
	FVector ControlledAngularVelocity = FVector::ZeroVector;
	FVector SmoothedFishPull = FVector::ZeroVector;
	uint64 ControlledEffortEpoch = 0;
	void ObserveGrab(UCatPhysicsGrabComponent* Grab);
	void HandleGripChanged(UCatPhysicsGrabComponent* Grab, bool bLeft,
		const FCatPhysicsGripState& Previous, const FCatPhysicsGripState& Current);
	UPROPERTY(Transient) TObjectPtr<UBoxComponent> Body;
	FTransform BodyLocal = FTransform::Identity;
	FTransform GripLocalTransform = FTransform::Identity;
	FGuid LoadSessionId;
	uint64 LoadStep = 0;
	FVector LineForceNewtons = FVector::ZeroVector;
	double LoadExpiresAt = 0.0;
	struct FLineForceSegment { FVector ForceNewtons; double RemainingSeconds; };
	TArray<FLineForceSegment> LineSegments;
	FVector AppliedLineImpulse = FVector::ZeroVector;
	FVector SubmittedLineImpulse = FVector::ZeroVector;
	FVector DiscardedLineImpulse = FVector::ZeroVector;
	FVector PendingPhysicsImpulse = FVector::ZeroVector;
	double PendingPhysicsSeconds = 0;
	double AppliedPhysicsSeconds = 0;
	double LastSampleAppliedPhysicsSeconds = 0;
	FVector LastSampleAppliedImpulse = FVector::ZeroVector;
	FVector ObservedAppliedAverageForce = FVector::ZeroVector;
	double LastPhysicsSubstepSeconds = 0;
	TWeakObjectPtr<UCatPhysicalBodyComponent> BudgetBody;
	TWeakObjectPtr<AController> InputTickController;
	TWeakObjectPtr<UCatPhysicsGrabComponent> InputTickGrab;
	TArray<TWeakObjectPtr<UCatPhysicsGrabComponent>> ObservedGrabs;
	bool bEndingPlay = false;
	bool bReady = false;
	bool bRefreshingPrimaryControl = false;
	bool bLastMouseMotorActive = false;
	bool bProducingCurrentPhysicsFrame = false;
	double NextLoadLogSeconds = 0.0;
	double LastEndpointSampleSeconds = -1.0;
	FVector LastEndpointVelocity = FVector::ZeroVector;
	FVector LastEndpointPosition = FVector::ZeroVector;
	FVector ObservedEndpointAcceleration = FVector::ZeroVector;
	uint32 LastMechanicalTopologyHash = 0;
	double NextEndpointLogSeconds = 0.0;
};
