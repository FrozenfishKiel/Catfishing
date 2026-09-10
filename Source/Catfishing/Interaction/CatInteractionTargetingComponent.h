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

	/** 按下交互键：普通目标立即交互，鱼护等待松开或长按阈值，避免拾取前先打开页面。 */
	void BeginInteractionInput();
	/** 松开时仅对未达阈值的原鱼护短按；取消输入不提交任何命令。 */
	void EndInteractionInput(bool bCanceled);

	/** 公开给自动化测试与未来显式 UI 刷新；正常运行由低频 Timer 调用。 */
	void RefreshTargetFromCrosshair();
	void ClearTarget();

	/** 当前准星目标及其提示可重新读取的本机通知；唯一目标检测发布，UI 订阅后更新文本。 */
	FCatInteractionTargetRefreshed OnTargetRefreshed;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 长按阈值到达后复核目标与本机输入锁，再向服务器请求拾起原鱼护。 */
	void CompleteGuardHold();
	/** 本次按下选中的鱼护；松开、取消或阈值到达时取走并清空，再复核准星，不能把同次输入交给另一目标。 */
	TWeakObjectPtr<AActor> PendingGuardTarget;
	/** 鱼护长按计时器；只用于本机区分短按与长按，清理时必须成对取消。 */
	FTimerHandle GuardHoldTimer;
	/** 拾起鱼护所需的持续按键秒数；设计配置默认半秒，不改变普通交互时序。 */
	UPROPERTY(EditDefaultsOnly, Category="Catfishing|Interaction", meta=(ClampMin="0.1", Units="s"))
	float GuardHoldSeconds = 0.5f;
	AActor* TraceInteractableFromCrosshair() const;
	/** 目标变化时成对切换高亮；目标不变时仅通知消费者重读动态提示。 */
	void ApplyTarget(AActor* NewTarget);
	APlayerController* GetOwningPlayerController() const;

	TWeakObjectPtr<AActor> CurrentTarget;
	TWeakObjectPtr<AActor> LastTarget;
	FTimerHandle TargetingTimer;
};
