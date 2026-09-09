#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "CatPhysicsPrototypePawn.generated.h"

class UBoxComponent;
class USphereComponent;
class UPhysicsConstraintComponent;
class UCatPhysicsGrabComponent;
class UCatPhysicsPrototypeVisualComponent;

/** Authority samples the real bodies. Clients interpolate these observations; no claim of rollback prediction. */
USTRUCT()
struct FCatPhysicsPrototypeSnapshot
{
	GENERATED_BODY()
	UPROPERTY() FVector BodyLocation = FVector::ZeroVector;
	UPROPERTY() FRotator BodyRotation = FRotator::ZeroRotator;
	UPROPERTY() FVector Velocity = FVector::ZeroVector;
	UPROPERTY() FVector LeftHandLocation = FVector::ZeroVector;
	UPROPERTY() FVector RightHandLocation = FVector::ZeroVector;
	UPROPERTY() uint32 Revision = 0;
	UPROPERTY() uint32 ResetEpoch = 0;
	UPROPERTY() bool bGrounded = false;
};

/** Isolated physical-body experiment. The production ACatCharacter/CMC is not a second motion writer. */
UCLASS()
class CATFISHING_API ACatPhysicsPrototypePawn : public APawn
{
	GENERATED_BODY()
public:
	ACatPhysicsPrototypePawn();
	/** Move.X is forward, Move.Y is right; normalized intent, not force or velocity. */
	UFUNCTION(BlueprintCallable, Category="Catfishing|PhysicsPrototype")
	void SetPrototypeInput(FVector2D Move, FRotator View);
	UFUNCTION(BlueprintCallable, Category="Catfishing|PhysicsPrototype")
	void SetGrabInput(bool bLeft, bool bHeld);
	UFUNCTION(BlueprintCallable, Category="Catfishing|PhysicsPrototype")
	void RequestJump();
	UFUNCTION(BlueprintCallable, Category="Catfishing|PhysicsPrototype")
	void RequestReset();
	UFUNCTION(BlueprintCallable, Category="Catfishing|PhysicsPrototype")
	void TogglePrototypeDiagnostics() { bShowDiagnostics = !bShowDiagnostics; }
	UBoxComponent* GetPhysicsBody() const { return Body; }
	USphereComponent* GetLeftHand() const { return LeftHand; }
	USphereComponent* GetRightHand() const { return RightHand; }
	UCatPhysicsGrabComponent* GetGrabComponent() const { return Grab; }
	FRotator GetPrototypeView() const { return ViewInput; }
	bool IsPrototypeGrounded() const { return HasAuthority() ? bGrounded : Snapshot.bGrounded; }
	FGuid GetPrototypeId() const { return PrototypeId; }
	virtual FVector GetVelocity() const override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	UFUNCTION(Server, Unreliable) void ServerSetPrototypeInput(FVector2D Move, FRotator View, uint32 Epoch, uint32 Sequence);
	UFUNCTION(Server, Reliable) void ServerRequestJump(uint32 Epoch);
	UFUNCTION(Server, Reliable) void ServerRequestReset(uint32 Epoch);
	UFUNCTION() void OnRep_PhysicsSnapshot();
	void ConfigureArm(bool bLeft);
	void UpdatePhysicalMovement(float DeltaSeconds);
	void ResetFromAuthority();
	void ReleaseConnections(FName Reason);
	void CaptureSnapshot();
	UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Body;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> LeftHand;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> RightHand;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPhysicsConstraintComponent> LeftArm;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPhysicsConstraintComponent> RightArm;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCatPhysicsGrabComponent> Grab;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCatPhysicsPrototypeVisualComponent> Visual;
	UPROPERTY(ReplicatedUsing=OnRep_PhysicsSnapshot) FCatPhysicsPrototypeSnapshot Snapshot;
	UPROPERTY(Replicated) FGuid PrototypeId;
	UPROPERTY(Replicated) uint32 ControlEpoch = 1;
	FVector2D MoveInput = FVector2D::ZeroVector;
	FRotator ViewInput = FRotator::ZeroRotator;
	FTransform SpawnTransform;
	uint32 LocalInputSequence = 0;
	uint32 AcceptedInputSequence = 0;
	uint32 ClientResetEpoch = 0;
	double LastInputSeconds = 0.0;
	double LastSendSeconds = -1.0;
	double LastSnapshotSeconds = -1.0;
	double SupportDisabledUntilSeconds = 0.0;
	double NextMotionLogSeconds = 0.0;
	double NextInputRejectLogSeconds = 0.0;
	bool bShowDiagnostics = true;
	bool bGrounded = false;
	bool bReceivedSnapshot = false;
};
