#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CatPhysicalBodyComponent.generated.h"

class UCatCharacterMovementComponent;
class UBoxComponent;
class USphereComponent;
class UPhysicsConstraintComponent;
class UCatPhysicsGrabComponent;
struct FCollisionQueryParams;

class UCatPhysicalBodyComponent;

/** Frozen motor input for side-effect-free candidate prediction; world cm and kg*cm/s^2. */
struct CATFISHING_API FCatBodyDriveSample
{
    FVector MoveIntent = FVector::ZeroVector;
    FVector HoldLocation = FVector::ZeroVector;
    double MaxSpeed = 0;
    double MaxForce = 0;
    bool bFishing = false;
    bool bCooperative = false;
    bool bLocomotion = false;
    bool bConnected = false;
    bool bUnderLoad = false;
    /** Only ungripped body contact: passive displacement does not request a full-strength stance. */
    bool bPassiveBodyContact = false;
    /** A peer is still moving or transmitting grab/fishing load; otherwise allow prompt braking. */
    bool bBodyContactDriven = false;
    bool bHoldActive = false;
};

/** Advances formal CMC after submitted loads, then publishes the completed authority pose. */
USTRUCT()
struct FCatPhysicalBodyPostPhysicsTick : public FTickFunction
{
	GENERATED_BODY()
	UCatPhysicalBodyComponent* Target = nullptr;
	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread,
		const FGraphEventRef& CompletionEvent) override;
	virtual FString DiagnosticMessage() override;
};
template<> struct TStructOpsTypeTraits<FCatPhysicalBodyPostPhysicsTick> : TStructOpsTypeTraitsBase2<FCatPhysicalBodyPostPhysicsTick>
{
	enum { WithCopy = false };
};

/** Server observations of body and hand poses, in cm and cm/s. */
USTRUCT()
struct FCatPhysicalBodySnapshot
{
	GENERATED_BODY()
	UPROPERTY() FVector BodyLocation = FVector::ZeroVector;
	UPROPERTY() FRotator BodyRotation = FRotator::ZeroRotator;
	UPROPERTY() FVector Velocity = FVector::ZeroVector;
	UPROPERTY() FVector MoveIntent = FVector::ZeroVector;
	UPROPERTY() FVector LeftHandLocation = FVector::ZeroVector;
	UPROPERTY() FVector RightHandLocation = FVector::ZeroVector;
	UPROPERTY() uint32 Revision = 0;
	UPROPERTY() uint32 ResetEpoch = 0;
	UPROPERTY() bool bGrounded = false;
	UPROPERTY() bool bSupportSampleReady = false;
};

/** Authority input and pose channel: formal characters use CMC, diagnostic Pawns use Chaos. */
UCLASS(ClassGroup=(Catfishing), meta=(BlueprintSpawnableComponent))
class CATFISHING_API UCatPhysicalBodyComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UCatPhysicalBodyComponent();
	/** Completes animated-model separation and the existing snapshot, without a second motor step. */
	void FinalizeModelContactFromAuthority();
	static void ConfigureGeometry(UBoxComponent* Body, USphereComponent* Left, USphereComponent* Right);
	void Initialize(UBoxComponent* InBody, USphereComponent* InLeft, USphereComponent* InRight,
		UPhysicsConstraintComponent* InLeftArm, UPhysicsConstraintComponent* InRightArm, UCatPhysicsGrabComponent* InGrab,
		double InGeometryScale = 1.0);
	UBoxComponent* GetBody() const { return Body; }
	void UseCharacterMovement(UCatCharacterMovementComponent* Movement) { CharacterMovement = Movement; }
	bool UsesCharacterMovement() const { return CharacterMovement != nullptr; }
	double GetFacingYawDegrees() const { return FacingYawDegrees; }
	FTickFunction& GetPostMovementTick() { return PostPhysicsTick; }
	FVector GetExternalForceFromAuthority();
	/** Read-only character-pair load in N. Net force chooses direction; cancelling loads stay latched. */
	double GetCharacterInteractionLoadFromAuthority(FVector& OutDirectionForce) const;
	/** Any applied source, including cancelling or vertical loads; excludes gravity/floor support. */
	bool HasExternalLoadFromAuthority() const;
	double GetVerticalGripForceFromAuthority() const;
	/** A successful voluntary jump permits brief reciprocal vertical grip traction, never suspension. */
	double GetJumpTractionWeight() const;
	void NotifyGripLiftFromAuthority();
	FVector ComputeHorizontalDriveForce(const FVector& Velocity, double Mass, double StepSeconds);
	FCatBodyDriveSample CaptureDriveSample();
	static FVector ComputeDriveForce(FCatBodyDriveSample& Sample, const FVector& Position, const FVector& Velocity, double Mass, double StepSeconds);
	bool HasFishingMotor() const { return FishingMotorSource.IsValid(); }
	void AddExternalImpulseFromAuthority(FVector ImpulseKgCmS);
	USphereComponent* GetHand(bool bLeft) const { return bLeft ? LeftHand : RightHand; }
	UCatPhysicsGrabComponent* GetGrab() const { return Grab; }
	FVector GetVelocity() const;
	FVector GetMoveIntent() const;
	FRotator GetViewIntent() const { return ViewInput; }
	FGuid GetBodyId() const { return BodyId; }
	bool IsGrounded() const;
	bool HasMovementSample() const;
	bool IsLocomotionEnabled() const { return bLocomotionEnabled; }
	uint32 GetResetEpoch() const { return Snapshot.ResetEpoch; }
	uint32 GetControlEpoch() const { return ControlEpoch; }
	FVector GetSupportFootPointWorld() const;
	/** Shared by physical support and visual foot queries; preserves the body's collision response filtering. */
	void AppendSupportQueryIgnores(FCollisionQueryParams& Params) const;
	double GetStandRootHeightCm() const;
	double GetGeometryScale() const { return GeometryScale; }
	FVector GetShoulderLocalPoint(bool bLeft) const;
	FVector GetRestHandLocalPoint(bool bLeft) const;
	void SetMoveIntent(FVector WorldDirection);
	void SetViewIntent(FRotator View);
	/** Host-owned configuration: the production BP keeps its existing CMC settings as the editor entry. */
	void ConfigureMovementDefaults(double JumpSpeed, double InGravityScale, double WalkSpeed);
	void SetMovementSpeed(double SpeedCmS);
	void RequestJump();
	void ClearControlIntent(FName Reason);
	void BeginControlEpochFromAuthority();
	void ReleaseConnectionsFromAuthority(FName Reason);
	void SetLocomotionEnabledFromAuthority(bool bEnabled, FName Reason);
	bool TeleportBodyFromAuthority(const FTransform& Transform, FName Reason);
	/** Each source replaces its own force. Units are kg*cm/s^2; multiply Newtons by 100 once. */
	void SetExternalForceFromAuthority(const UObject* Source, FVector ForceKgCmS2, bool bVerticalGripTraction = false, bool bBodyContact = false, bool bCharacterInteraction = false);
	void ClearExternalForce(const UObject* Source);
	/** Replaces the ordinary motor budget; zero means no voluntary motor force, never unlimited. */
	void SetFishingMotorBudget(const UObject* Source, double MaxForceKgCmS2, double MaxSpeedCmS = 100.0);
	void ClearFishingMotorBudget(const UObject* Source);
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Catfishing|Physics") double JumpSpeedCmS = 420.0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Catfishing|Physics") double GravityScale = 1.0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Catfishing|Physics") double MaxMovementSpeedCmS = 100.0;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
	virtual void RegisterComponentTickFunctions(bool bRegister) override;
	virtual void TickComponent(float DeltaSeconds, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	friend struct FCatPhysicalBodyPostPhysicsTick;
	void PublishPostPhysicsSnapshot(float DeltaSeconds);
	void PublishCompletedSnapshot();
	FCatPhysicalBodyPostPhysicsTick PostPhysicsTick;
	double GeometryScale = 1.0;
	bool bPublishJumpAfterPhysics = false;
	UFUNCTION(Server, Unreliable) void ServerSetInput(FVector Move, FRotator View, uint32 Epoch, uint32 Sequence);
	UFUNCTION(Server, Reliable) void ServerRequestJump(uint32 Epoch);
	UFUNCTION(Server, Reliable) void ServerClearControlIntent(uint32 Epoch, uint32 Sequence);
	UFUNCTION() void OnRep_PhysicsSnapshot();
	void ConfigureArm(bool bLeft);
	void UpdatePhysicalMovement(float DeltaSeconds);
	bool HasPhysicalGrabConnection() const;
	void CaptureSnapshot();
	void SendLocalInput();
	bool HasAuthority() const;
	bool IsLocallyControlled() const;
	void LogState(FName Event, FName Reason) const;
	UPROPERTY(Transient) TObjectPtr<UCatCharacterMovementComponent> CharacterMovement;
	UPROPERTY(Transient) TObjectPtr<UBoxComponent> Body;
	UPROPERTY(Transient) TObjectPtr<USphereComponent> LeftHand;
	UPROPERTY(Transient) TObjectPtr<USphereComponent> RightHand;
	UPROPERTY(Transient) TObjectPtr<UPhysicsConstraintComponent> LeftArm;
	UPROPERTY(Transient) TObjectPtr<UPhysicsConstraintComponent> RightArm;
	UPROPERTY(Transient) TObjectPtr<UCatPhysicsGrabComponent> Grab;
	UPROPERTY(ReplicatedUsing=OnRep_PhysicsSnapshot) FCatPhysicalBodySnapshot Snapshot;
	UPROPERTY(Replicated) FGuid BodyId;
	UPROPERTY(Replicated) uint32 ControlEpoch = 1;
	UPROPERTY(Replicated) bool bLocomotionEnabled = true;
	struct FExternalForce
	{
		FVector Force = FVector::ZeroVector;
		bool bVerticalGripTraction = false;
		bool bBodyContact = false;
		bool bCharacterInteraction = false;
	};
	TMap<TWeakObjectPtr<const UObject>, FExternalForce> ExternalForces;
	TWeakObjectPtr<const UObject> FishingMotorSource;
	double FishingMotorMaxForce = 0.0;
	double FishingMotorMaxSpeed = 100.0;
	FVector FishingHoldLocation = FVector::ZeroVector;
	bool bFishingHoldActive = false;
	FVector MoveInput = FVector::ZeroVector;
	FRotator ViewInput = FRotator::ZeroRotator;
	/** Last commanded body heading; camera yaw remains independent outside an active reach/hold. */
	double FacingYawDegrees = 0.0;
	bool bPublishMovementAfterPhysics = false;
	uint32 LocalInputSequence = 0;
	uint32 AcceptedInputSequence = 0;
	uint32 ClientResetEpoch = 0;
	double LastInputSeconds = 0.0;
	double LastSendSeconds = -1.0;
	double LastSnapshotSeconds = -1.0;
	double SupportDisabledUntilSeconds = 0.0;
	double JumpTractionUntilSeconds = 0.0;
	double NextMotionLogSeconds = 0.0;
	double NextInputRejectLogSeconds = 0.0;
	double NextBudgetRejectLogSeconds = 0.0;
	double NextHoldYieldLogSeconds = 0.0;
	bool bGrounded = false;
	bool bSupportSampleReady = false;
	bool bGroundContactRecoveryActive = false;
	bool bReceivedSnapshot = false;
	bool bJumpSeparating = false;
};
