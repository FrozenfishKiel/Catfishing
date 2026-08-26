#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CatInteractionTargetingComponent.generated.h"

class APlayerController;

DECLARE_MULTICAST_DELEGATE_TwoParams(FCatInteractionTargetChanged, AActor* /*Previous*/, AActor* /*Current*/);

/** owning client 的准星目标状态机；不复制 Current/Last，也不在服务器远端 Controller 上运行检测。 */
UCLASS(ClassGroup=(Catfishing), meta=(BlueprintSpawnableComponent))
class CATFISHING_API UCatInteractionTargetingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCatInteractionTargetingComponent();

	UFUNCTION(BlueprintPure, Category="Catfishing|Interaction")
	AActor* GetCurrentTarget() const { return CurrentTarget.Get(); }

	UFUNCTION(BlueprintPure, Category="Catfishing|Interaction")
	AActor* GetLastTarget() const { return LastTarget.Get(); }

	UFUNCTION(BlueprintCallable, Category="Catfishing|Interaction")
	void TryInteract();

	/** 公开给自动化测试与未来显式 UI 刷新；正常运行由低频 Timer 调用。 */
	void RefreshTargetFromCrosshair();
	void ClearTarget();

	FCatInteractionTargetChanged OnTargetChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	AActor* TraceInteractableFromCrosshair() const;
	void ApplyTarget(AActor* NewTarget);
	APlayerController* GetOwningPlayerController() const;

	TWeakObjectPtr<AActor> CurrentTarget;
	TWeakObjectPtr<AActor> LastTarget;
	FTimerHandle TargetingTimer;
};
