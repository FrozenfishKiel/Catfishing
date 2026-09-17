#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Fishing/Actors/CatFishingActorTypes.h"
#include "Fishing/Behavior/CatFishBehaviorTypes.h"
#include "CatFishEncounterActor.generated.h"

class USceneComponent;
class UBoxComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UStateTree;
class UStateTreeComponent;
class UCatFishingFightRunner;
class UCatFishDefinition;
class UCatFishPresentationDefinition;

UCLASS(Blueprintable, meta=(ChildCannotTick))
class CATFISHING_API ACatFishEncounterActor : public AActor
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> FishingCollision;

public:
	ACatFishEncounterActor();
	FVector GetFishingCollisionCenter() const;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	bool InitializeAuthoritativeIdentity(FGuid InFishingSessionId, FGuid InCastAttemptId, int32  InItemId,
		double InInitialLineLength, double InVisualScale);
	void DeferInitialPresentationFromAuthority();
	void PublishInitialPresentationFromAuthority();
	/**
	 * 应用已求解的权威落点和鱼身朝向。SwimHeading 保留 AI 目标；ResolvedBodyHeading 为物理结果。
	 * 转动的惯量与限速归 Simulator；本 Actor 不再用第二层转向改变线端几何。
	 */
	bool ApplyFightStepFromAuthority(ECatFishMotionIntent MotionIntent, double CurrentLineLength,
		const FVector& FishWorldPosition, float StepDeltaSeconds = 0.0f, float FishLineAlignment = 0.0f,
		float NormalizedLineLoad = 0.0f, float IntendedSwimSpeedCentimetersPerSecond = 0.0f,
		bool bStrongConfrontation = false, bool bGrounded = false, FVector GroundNormal = FVector::UpVector,
		FVector SwimHeading = FVector::ZeroVector, ECatFishBehavior Behavior = ECatFishBehavior::None,
		float FishEffortRatio = 0.0f, FVector ResolvedBodyHeading = FVector::ZeroVector);
	/** 与权威求解同源的静态鱼嘴点；所有端可用，不依赖 Mesh/动画加载。 */
	UFUNCTION(BlueprintPure, Category="Fishing|Fish")
	FVector GetMouthWorldLocation() const;
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

	/** Mesh 组件的世界位置，供调试可见资源；钩和鱼线锚点使用 GetMouthWorldLocation。 */
	UFUNCTION(BlueprintPure, Category="Fishing|Fish")
	FVector GetVisualWorldLocation() const;

	/**
	 * 本场这条鱼的鱼种定义；表现层（浮漂、水面特效、HUD）按它取逐鱼数据，不再自己按 ID 查目录。
	 * 身份尚未初始化或鱼表里查不到时返回空。
	 */
	UFUNCTION(BlueprintPure, Category="Fishing|Fish")
	UCatFishDefinition* GetFishDefinition() const;

	/**
	 * 本场这条鱼的表现定义；漂讯与水面三个逐鱼槽位（BiteBobberCue／BiteWaterSurfaceCue／FightWaterSurfaceCue）
	 * 就挂在它上面。这是表现层拿到那三列的唯一接线点。
	 */
	UFUNCTION(BlueprintPure, Category="Fishing|Fish")
	UCatFishPresentationDefinition* GetFishPresentationDefinition() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnRep_ReplicatedMovement() override;

private:
	friend class FCatFishBehaviorStateTreeRuntimeTest;
	UFUNCTION()
	void OnRep_PresentationState(const FCatFishEncounterPresentationState& Previous);
	void QueueOrDispatchPresentationChanged(const FCatFishEncounterPresentationState& Previous, const FCatFishEncounterPresentationState& Current);
	void DispatchPresentationChanged(const FCatFishEncounterPresentationState& Previous, const FCatFishEncounterPresentationState& Current);
	void RefreshFishPresentation();
	void ApplyVisualScale();
	void ApplyVisualPose();
	void LogAnimatedMouthFallback(const TCHAR* Reason);
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
	bool bBehaviorStartupInProgress = false;
	bool bRefreshingFishPresentation = false;
	bool bApplyingVisualPose = false;
	bool bLoggedAnimatedMouthFallback = false;
	double NextBodyDiagnosticWorldSeconds = 0.0;
	int32  AppliedPresentationItemId = 0;
	double AppliedExhaustedVisualRollDegrees = 90.0;
	TWeakObjectPtr<UCatFishingFightRunner> AuthorityFightRunner;
	FTransform EncounterMeshBaseTransform = FTransform::Identity;
	// 从正式参考嘴点解析一次；只供动画表现补偿，不参与权威嘴点和运动求解。
	TWeakObjectPtr<USkeletalMesh> AnimatedMouthMesh;
	FName AnimatedMouthBone = NAME_None;
	int32 AnimatedMouthBoneIndex = INDEX_NONE;
	FDelegateHandle BoneTransformsFinalizedHandle;
	FCatFishEncounterPresentationState PendingPreviousPresentationState;
	FCatFishEncounterPresentationState PendingCurrentPresentationState;
};
