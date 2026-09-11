#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CatPhysicsPrototypePlayerController.generated.h"

class ACatPhysicsPrototypePawn;

/** Native keys are confined to the opt-in trial and do not modify production input assets. */
UCLASS()
class CATFISHING_API ACatPhysicsPrototypePlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	ACatPhysicsPrototypePlayerController();
	virtual void PlayerTick(float DeltaTime) override;
	virtual void FlushPressedKeys() override;
	virtual bool ShouldFlushKeysWhenViewportFocusChanges() const override { return true; }

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void OnUnPossess() override;

private:
	void LookYaw(float Value);
	void LookPitch(float Value);
	void BeginLeftGrab();
	void EndLeftGrab();
	void BeginRightGrab();
	void EndRightGrab();
	void Jump();
	void Reset();
	void SwitchCat();
	void ToggleDiagnostics();
	void EnsureLocalLighting();
	bool HasPrototypeInputFocus() const;
	void SetObservedGrabInput(bool bLeft, bool bHeld);
	void ReleasePrototypeInput(FName Reason);
	TWeakObjectPtr<ACatPhysicsPrototypePawn> InputPawn;
	bool bObservedGrabHeld[2] = {false, false};
	bool bObservedInputFocus = true;
};
