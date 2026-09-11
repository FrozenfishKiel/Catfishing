#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "CatPhysicsPrototypePawn.generated.h"

class UBoxComponent;
class USphereComponent;
class UPhysicsConstraintComponent;
class UCatPhysicsGrabComponent;
class UCatPhysicsPrototypeVisualComponent;

/** Isolated arena host for the same physical body used by production characters. */
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
	FRotator GetPrototypeView() const { return PhysicalBody->GetViewIntent(); }
	bool IsPrototypeGrounded() const { return PhysicalBody->IsGrounded(); }
	bool HasPrototypeMovementSample() const { return PhysicalBody->HasMovementSample(); }
	uint32 GetPrototypeResetEpoch() const { return PhysicalBody->GetResetEpoch(); }
	FGuid GetPrototypeId() const { return PhysicalBody->GetBodyId(); }
	virtual FVector GetVelocity() const override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void UnPossessed() override;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	UFUNCTION(Server, Reliable) void ServerRequestReset(uint32 Epoch);
	UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Body;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> LeftHand;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> RightHand;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPhysicsConstraintComponent> LeftArm;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UPhysicsConstraintComponent> RightArm;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCatPhysicsGrabComponent> Grab;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCatPhysicsPrototypeVisualComponent> Visual;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCatPhysicalBodyComponent> PhysicalBody;
	FTransform SpawnTransform;
	bool bShowDiagnostics=true;
};
