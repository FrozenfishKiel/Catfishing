#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Fishing/Actors/CatFishingActorTypes.h"
#include "Fishing/Behavior/CatFishBehaviorTypes.h"
#include "CatFishEncounterActor.generated.h"

class USceneComponent;
class USkeletalMeshComponent;
class UStateTree;
class UStateTreeComponent;
class UCatFishingFightRunner;

UCLASS(Blueprintable, meta=(ChildCannotTick))
class CATFISHING_API ACatFishEncounterActor : public AActor
{
	GENERATED_BODY()

public:
	ACatFishEncounterActor();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	bool InitializeAuthoritativeIdentity(FGuid InFishingSessionId, FGuid InCastAttemptId, FName InFishDefinitionId,
		double InInitialLineLength, double InVisualScale);
	void DeferInitialPresentationFromAuthority();
	void PublishInitialPresentationFromAuthority();
	/**
	 * 推进一步搏斗表现：写行为/发力/线长、落位到权威位置，活鱼朝向主动游动方向。
	 * StepDeltaSeconds > 0 时按 MaximumTurnRateDegreesPerSecond 限速转向（搏斗固定步进传 FixedStepSeconds）；
	 * <= 0 表示首次落位，直接对准不插值。
	 */
	bool ApplyFightStepFromAuthority(ECatFishMotionIntent MotionIntent, double CurrentLineLength,
		const FVector& FishWorldPosition, float StepDeltaSeconds = 0.0f, float FishLineAlignment = 0.0f,
		float NormalizedLineLoad = 0.0f, float IntendedSwimSpeedCentimetersPerSecond = 0.0f,
		bool bStrongConfrontation = false, bool bGrounded = false, FVector GroundNormal = FVector::UpVector,
		FVector SwimHeading = FVector::ZeroVector, ECatFishBehavior Behavior = ECatFishBehavior::None,
		float FishEffortRatio = 0.0f);
	/** 服务器把高层鱼行为交给独立 StateTree；客户端永不启动平行行为树。 */
	bool StartFishBehaviorFromAuthority(UStateTree* BehaviorStateTree, UCatFishingFightRunner* FightRunner);
	void StopFishBehaviorFromAuthority();
	/** 仅由 Runner 固定步推进；组件不再独立按帧计时。 */
	bool TickFishBehaviorFromAuthority(float FixedStepSeconds);
	/** StateTree Task 只提交行为，Condition 只读取固定步反馈。 */
	bool BeginFishBehaviorFromStateTree(ECatFishBehavior Behavior);
	bool TestFishBehaviorConditionFromStateTree(ECatFishBehaviorCondition Condition) const;
	UFUNCTION(BlueprintPure, Category="Fishing|Fish")
	const FCatFishEncounterPresentationState& GetPresentationState() const;
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category="Fishing|Fish")
	void BP_OnFishPresentationChanged(const FCatFishEncounterPresentationState& Previous, const FCatFishEncounterPresentationState& Current);
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category="Fishing|Fish") void BP_PlayFishPresentationEvent(FGameplayTag EventTag);

	/** Mesh 组件的世界位置；调试球和鱼线用它对齐实际可见资源。 */
	UFUNCTION(BlueprintPure, Category="Fishing|Fish")
	FVector GetVisualWorldLocation() const;

protected:
	virtual void BeginPlay() override;
	virtual void OnRep_ReplicatedMovement() override;

	/**
	 * 转向速率上限（度/秒）：鱼调头不会瞬间完成，避免挣扎/收线切换时朝向瞬移。
	 * 只影响表现观感；线长、近岸、抄网等一切判定都只用位置，与朝向无关。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fishing|Presentation", meta=(ClampMin="0", Units="deg/s"))
	float MaximumTurnRateDegreesPerSecond = 180.0f;

private:
	friend class FCatFishBehaviorStateTreeRuntimeTest;
	UFUNCTION()
	void OnRep_PresentationState(const FCatFishEncounterPresentationState& Previous);
	void QueueOrDispatchPresentationChanged(const FCatFishEncounterPresentationState& Previous, const FCatFishEncounterPresentationState& Current);
	void DispatchPresentationChanged(const FCatFishEncounterPresentationState& Previous, const FCatFishEncounterPresentationState& Current);
	void RefreshFishPresentation();
	void ApplyVisualScale();
	void ApplyVisualPose();
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> VisualRoot;
	/** 鱼种库表现定义的唯一可见 Mesh 消费者；蓝图子类不得再添加平行鱼 Mesh。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta=(AllowPrivateAccess="true"))
	TObjectPtr<USkeletalMeshComponent> FishMesh;
	/** 只在服务器启动；树选择冲刺/侧游/缓游，不直接移动 Actor。 */
	UPROPERTY(VisibleAnywhere, Category="Fishing|Behavior") TObjectPtr<UStateTreeComponent> FishBehaviorStateTree;
	UPROPERTY(ReplicatedUsing=OnRep_PresentationState, VisibleInstanceOnly, BlueprintReadOnly, meta=(AllowPrivateAccess="true"))
	FCatFishEncounterPresentationState PresentationState;
	bool bIdentityInitialized = false;
	bool bPresentationDeferred = false;
	bool bHasPendingPresentationNotification = false;
	/** 是否已经有过一次有效的移动方向；首次落位直接对准，之后才做限速转向。 */
	bool bFacingInitialized = false;
	bool bBehaviorStartupInProgress = false;
	FName AppliedPresentationFishDefinitionId = NAME_None;
	double AppliedExhaustedVisualRollDegrees = 90.0;
	TWeakObjectPtr<UCatFishingFightRunner> AuthorityFightRunner;
	FTransform EncounterMeshBaseTransform = FTransform::Identity;
	FCatFishEncounterPresentationState PendingPreviousPresentationState;
	FCatFishEncounterPresentationState PendingCurrentPresentationState;
};
