#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CatInteractionTargetingComponent.generated.h"

class APlayerController;

/** 准星目标刷新通知；相同目标也会通知提示消费者，但不重复切换本地高亮。 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FCatInteractionTargetRefreshed, AActor* /*Previous*/, AActor* /*Current*/);

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

	/** 当前准星目标及其提示可重新读取的本机通知；唯一目标检测发布，UI 订阅后更新文本。 */
	FCatInteractionTargetRefreshed OnTargetRefreshed;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	AActor* TraceInteractableFromCrosshair() const;
	/** 目标变化时成对切换高亮；目标不变时仅通知消费者重读动态提示。 */
	void ApplyTarget(AActor* NewTarget);
	APlayerController* GetOwningPlayerController() const;

	TWeakObjectPtr<AActor> CurrentTarget;
	TWeakObjectPtr<AActor> LastTarget;
	FTimerHandle TargetingTimer;
};
