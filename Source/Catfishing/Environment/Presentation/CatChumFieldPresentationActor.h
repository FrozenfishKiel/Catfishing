#pragma once

#include "CoreMinimal.h"
#include "Environment/CatChumFieldReplicationComponent.h"
#include "GameFramework/Actor.h"

#include "CatChumFieldPresentationActor.generated.h"

class USceneComponent;

UCLASS(Blueprintable, meta=(ChildCannotTick))
class CATFISHING_API ACatChumFieldPresentationActor : public AActor
{
	GENERATED_BODY()

public:
	ACatChumFieldPresentationActor();
	void ApplyPublicState(const FCatChumFieldPublicItem& NewState, bool bAdded);
	void NotifyFieldRemoved();
	const FCatChumFieldPublicItem& GetPublicState() const { return PublicState; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<USceneComponent> VisualRoot;

	UFUNCTION(BlueprintImplementableEvent, meta=(DisplayName="Chum Field Added"))
	void BP_OnFieldAdded(const FCatChumFieldPublicItem& State);

	UFUNCTION(BlueprintImplementableEvent, meta=(DisplayName="Chum Field Changed"))
	void BP_OnFieldChanged(const FCatChumFieldPublicItem& State);

	UFUNCTION(BlueprintImplementableEvent, meta=(DisplayName="Chum Field Removed"))
	void BP_OnFieldRemoved(const FCatChumFieldPublicItem& State);

private:
	UPROPERTY(Transient)
	FCatChumFieldPublicItem PublicState;
};
