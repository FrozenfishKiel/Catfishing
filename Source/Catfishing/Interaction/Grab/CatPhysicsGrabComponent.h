#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CatPhysicsGrabComponent.generated.h"

class UCatPhysicalBodyComponent;
class UPrimitiveComponent;
class USphereComponent;
class UPhysicsConstraintComponent;
class UCatPhysicsGrabComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogCatPhysicsGrab, Log, All);

/** Server-owned contact, independent of fishing membership and cosmetic hand posing. */
USTRUCT(BlueprintType)
struct FCatPhysicsGripState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) bool bReaching = false;
	UPROPERTY(BlueprintReadOnly) bool bGripped = false;
	/** Authority has adopted this exact grip; a later mouse release cannot cancel that ownership. */
	UPROPERTY(BlueprintReadOnly) bool bExplicitHold = false;
	/** Primary input owns a controlled prop pose, without a self-constraining hand joint. */
	UPROPERTY(BlueprintReadOnly) bool bControlledHold = false;
	UPROPERTY(BlueprintReadOnly) FGuid GripId;
	UPROPERTY() uint32 Revision = 0;
	UPROPERTY(BlueprintReadOnly) TObjectPtr<AActor> TargetActor = nullptr;
	UPROPERTY(BlueprintReadOnly) FName TargetComponentName;
	UPROPERTY(BlueprintReadOnly) FName TargetBone;
	UPROPERTY(BlueprintReadOnly) FVector TargetLocalPoint = FVector::ZeroVector;
	/** Shoulder-to-hand distance captured at contact, in world cm; zero outside this grip. */
	UPROPERTY(BlueprintReadOnly) double HeldReachDistanceCm = 0.0;
	/** CMC grip offset in the view frame at capture, in world cm. Keeps the first frame force-free. */
	UPROPERTY() FVector HeldAimLocalOffset = FVector::ZeroVector;
};

DECLARE_MULTICAST_DELEGATE_FourParams(FCatPhysicalGripChanged, UCatPhysicsGrabComponent*, bool, const FCatPhysicsGripState&, const FCatPhysicsGripState&);

/** Shared two-hand physical contacts. Only authority creates joints or changes bodies. */
UCLASS(ClassGroup=(Catfishing), meta=(BlueprintSpawnableComponent))
class CATFISHING_API UCatPhysicsGrabComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UCatPhysicsGrabComponent();
	void InitializeHands(UPrimitiveComponent* InBody, USphereComponent* InLeft, USphereComponent* InRight,
		UPhysicsConstraintComponent* InLeftArm, UPhysicsConstraintComponent* InRightArm, double InGeometryScale = 1.0);
	void SetGrabInput(bool bLeft, bool bHeld);
	void RefreshKinematicHands();
	FVector GetLastTractionForceForDiagnostics(bool bLeft) const { return LastTractionForce[bLeft ? 0 : 1]; }
	UCatPhysicalBodyComponent* GetTractionReceiverForDiagnostics(bool bLeft) const { return TractionReceiver[bLeft ? 0 : 1].Get(); }
	FVector GetTractionErrorForDiagnostics(bool bLeft) const;
	/** Authority calls the same validation and constraint path for an explicitly positioned held prop. */
	bool GripFromAuthority(bool bLeft, UPrimitiveComponent* Target, const FVector& WorldPoint);
	/** Transfers an existing contact to an explicit authority hold without rebuilding its joint. */
	bool RetainGripFromAuthority(bool bLeft, UPrimitiveComponent* ExpectedTarget);
	bool ControlRetainedGripFromAuthority(bool bLeft, UPrimitiveComponent* ExpectedTarget);
	void RefreshTargetConstraintsFromAuthority(UPrimitiveComponent* Target);
	void ReleaseHandFromAuthority(bool bLeft, FName Reason);
	UPrimitiveComponent* GetGripTargetComponent(bool bLeft) const;
	/** Authority only; observers must not assume the previous target survives destruction. */
	FCatPhysicalGripChanged OnGripChanged;
	void ReleaseAllFromAuthority(FName Reason);
	void ReleaseTargetFromAuthority(AActor* Target, FName Reason);
	void BeginInputEpochFromAuthority();
	bool IsReaching(bool bLeft) const { return GetGripState(bLeft).bReaching; }
	bool IsGripping(bool bLeft) const { return GetGripState(bLeft).bGripped; }
	AActor* GetGripTarget(bool bLeft) const { return GetGripState(bLeft).TargetActor; }
	uint32 GetGripRevision(bool bLeft) const { return GetGripState(bLeft).Revision; }
	FVector GetGripWorldLocation(bool bLeft) const;
	const FCatPhysicsGripState& GetGripState(bool bLeft) const { return bLeft ? LeftGrip : RightGrip; }
	FVector GetShoulderWorldLocation(bool bLeft) const;
	double GetReachLengthCm() const;
	static FVector ShoulderLocal(bool bLeft) { return FVector(10.2, bLeft ? -3.4 : 3.4, -0.8); }
	static FVector RestHandLocal(bool bLeft) { return FVector(5.2, bLeft ? -2.3 : 2.3, -18.3); }
	static constexpr double ReachLengthCm = 18.0;
	static constexpr double HandRadiusCm = 1.8;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	UFUNCTION(Server, Reliable) void ServerSetGrabInput(bool bLeft, bool bHeld, uint32 Epoch, uint32 Sequence);
	UFUNCTION() void OnRep_GripState();
	void ApplyGrabInput(bool bLeft, bool bHeld);
	void UpdateHand(bool bLeft, const FVector& Aim);
	bool IsReachSurface(const UPrimitiveComponent* Target, FName Bone, bool bLeft) const;
	bool TraceReachSurface(bool bLeft, const FVector& Start, const FVector& End, FHitResult& Hit);
	bool UsesCharacterMovement() const;
	void ClearTraction(bool bLeft);
	void ApplyTraction(bool bLeft);
	void RefreshContact(bool bLeft, bool bForceRebind = false);
	UPrimitiveComponent* ResolveConstraintTarget(const FCatPhysicsGripState& State) const;
	void TryLatch(bool bLeft, const FHitResult& Hit);
	void ReleaseHand(bool bLeft, FName Reason, bool bStopReaching);
	UPrimitiveComponent* ResolveTarget(const FCatPhysicsGripState& State) const;
	void LogGrip(bool bLeft, FName Event, FName Result) const;
	UPROPERTY(ReplicatedUsing=OnRep_GripState) FCatPhysicsGripState LeftGrip;
	UPROPERTY(ReplicatedUsing=OnRep_GripState) FCatPhysicsGripState RightGrip;
	UPROPERTY(Replicated) uint32 InputEpoch = 1;
	UPROPERTY(Transient) TObjectPtr<UPrimitiveComponent> Body;
	UPROPERTY(Transient) TArray<TObjectPtr<USphereComponent>> Hands;
	UPROPERTY(Transient) TArray<TObjectPtr<UPhysicsConstraintComponent>> Arms;
	UPROPERTY(Transient) TArray<TObjectPtr<UPhysicsConstraintComponent>> Contacts;
	uint32 LocalSequence[2] = {0, 0};
	uint32 AcceptedSequence[2] = {0, 0};
	uint32 ObservedRevision[2] = {0, 0};
	TWeakObjectPtr<UCatPhysicalBodyComponent> TractionReceiver[2];
	FVector LastTractionForce[2] = {FVector::ZeroVector, FVector::ZeroVector};
	double NextTractionLogSeconds[2] = {0,0};
	TWeakObjectPtr<UPrimitiveComponent> ObservedReachSurface[2];
	int32 ObservedIgnoredReachVolumes[2] = {-1,-1};
	double NextReachLogSeconds[2] = {0,0};
	double GeometryScale = 1.0;
	bool bLatchedUntilRelease[2] = {false, false};
};
