#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CatFishingCameraComponent.generated.h"

class AController;
class APlayerController;
class ACatFishingRodActor;
class USkeletalMeshComponent;
struct FMinimalViewInfo;

/** 只消费持杆/搏斗复制事实和实际握把姿态，不修改角力、输入意图或鱼竿 Transform。 */
UCLASS()
class CATFISHING_API UCatFishingCameraComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCatFishingCameraComponent();
	bool TryGetCameraView(float DeltaTime, FMinimalViewInfo& OutView);
	/** 权威 Registry 和客户端复制 Actor 共用的持杆查询；协作者也可找到其操作的杆。 */
	static const ACatFishingRodActor* FindHeldRodOperatedBy(const AController* Controller);
	/** 只有当前主握杆者且搏斗仍在进行时返回杆。 */
	static const ACatFishingRodActor* FindFightRodHeldBy(const AController* Controller);
	/** 上鱼时身体和移动使用实际杆 Yaw；其余时间使用控制器朝向。 */
	static FRotator ResolveFacingRotation(const AController* Controller);

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	friend class FCatFishingFirstPersonCameraTest;
	const ACatFishingRodActor* FindLocalViewRod() const;
	void RestoreView();
	void LogView(const TCHAR* Mode) const;
	TWeakObjectPtr<USkeletalMeshComponent> HiddenMesh;
	TWeakObjectPtr<APlayerController> ViewingController;
	FGuid ViewedRodId;
	/** 仅本地镜头的插值历史；不能写回杆姿态、控制意图或鱼线约束。 */
	FTransform SmoothedGrip = FTransform::Identity;
	FQuat LastTargetRotation = FQuat::Identity;
	FRotator LastViewRotation = FRotator::ZeroRotator;
	double MaximumTargetStepDegrees = 0.0;
	double MaximumViewStepDegrees = 0.0;
	double NextDiagnosticSeconds = 0.0;
	bool bSavedOwnerNoSee = false;
	bool bReportedInvalidView = false;
};
