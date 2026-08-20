#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Fishing/Actors/CatFishingActorTypes.h"
#include "CatFishEncounterActor.generated.h"

class USceneComponent;

UCLASS(Blueprintable, meta=(ChildCannotTick))
class CATFISHING_API ACatFishEncounterActor : public AActor
{
	GENERATED_BODY()

public:
	ACatFishEncounterActor();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	bool InitializeAuthoritativeIdentity(FGuid InFishingSessionId, FGuid InCastAttemptId, FName InFishDefinitionId, double InInitialLineLength);
	void DeferInitialPresentationFromAuthority();
	void PublishInitialPresentationFromAuthority();
	/**
	 * 推进一步搏斗表现：写运动意图/线长、落位到权威位置，并把朝向转向本步的移动方向。
	 * StepDeltaSeconds > 0 时按 MaximumTurnRateDegreesPerSecond 限速转向（搏斗固定步进传 FixedStepSeconds）；
	 * <= 0 表示首次落位，直接对准不插值。
	 */
	bool ApplyFightStepFromAuthority(ECatFishMotionIntent MotionIntent, double CurrentLineLength,
		const FVector& FishWorldPosition, float StepDeltaSeconds = 0.0f);
	const FCatFishEncounterPresentationState& GetPresentationState() const;
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category="Fishing|Fish")
	void BP_OnFishPresentationChanged(const FCatFishEncounterPresentationState& Previous, const FCatFishEncounterPresentationState& Current);
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category="Fishing|Fish") void BP_PlayFishPresentationEvent(FGameplayTag EventTag);

	/**
	 * Mesh 表现子树（VisualRoot）的世界位置：已经包含下沉/前后/左右三个偏移和朝向。
	 * 调试球和鱼线用它对齐到 Mesh 实际所在处，而不是水面上的权威位置。
	 */
	UFUNCTION(BlueprintPure, Category="Fishing|Fish")
	FVector GetVisualWorldLocation() const;

protected:
	virtual void BeginPlay() override;

	/**
	 * 表现下沉深度（厘米）：鱼的权威位置在水面（线长/近岸/抄网判定都用水面口径），
	 * 表现子树 VisualRoot 整体下移该距离，让鱼 Mesh 看起来在钩子下方水里而不是浮在水面。
	 * 蓝图里可直接调；0 = 不下沉。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fishing|Presentation", meta=(ClampMin="0", Units="cm"))
	float VisualDepthCentimeters = 5.0f;

	/**
	 * Mesh 前后偏移（厘米）：正值 = 沿鱼游动方向往【前】，负值 = 往【后】。
	 * 三个偏移都在 Actor 本地空间，且 Actor 前向已经等于鱼真实游动方向（Mesh 也已被 VisualYawOffsetDegrees 掰正对齐），
	 * 所以"前"就是你看到鱼头指的方向，不受美术前向轴影响。
	 * 典型用途：让钩子看起来咬在鱼嘴上而不是穿在身体中段。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fishing|Presentation", meta=(Units="cm"))
	float VisualForwardOffsetCentimeters = 0.0f;

	/** Mesh 左右偏移（厘米）：正值 = 鱼的【右】侧，负值 = 【左】侧。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fishing|Presentation", meta=(Units="cm"))
	float VisualRightOffsetCentimeters = 0.0f;

	/**
	 * Mesh 前向轴修正（度）：Actor 的朝向永远等于"鱼实际游动的方向"，
	 * 但美术资源的前向不一定是 +X（常见是模型朝 -Y 或 +Y）。这里给 VisualRoot 加一个相对偏航把 Mesh 掰正。
	 * 判断方法：让鱼游起来，如果它侧着走就填 ±90，如果倒着走就填 180。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fishing|Presentation", meta=(Units="deg"))
	float VisualYawOffsetDegrees = 0.0f;

	/**
	 * 转向速率上限（度/秒）：鱼调头不会瞬间完成，避免挣扎/收线切换时朝向瞬移。
	 * 只影响表现观感；线长、近岸、抄网等一切判定都只用位置，与朝向无关。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Fishing|Presentation", meta=(ClampMin="0", Units="deg/s"))
	float MaximumTurnRateDegreesPerSecond = 180.0f;

private:
	UFUNCTION()
	void OnRep_PresentationState(const FCatFishEncounterPresentationState& Previous);
	void QueueOrDispatchPresentationChanged(const FCatFishEncounterPresentationState& Previous, const FCatFishEncounterPresentationState& Current);
	void DispatchPresentationChanged(const FCatFishEncounterPresentationState& Previous, const FCatFishEncounterPresentationState& Current);
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> VisualRoot;
	UPROPERTY(ReplicatedUsing=OnRep_PresentationState, VisibleInstanceOnly, BlueprintReadOnly, meta=(AllowPrivateAccess="true"))
	FCatFishEncounterPresentationState PresentationState;
	bool bIdentityInitialized = false;
	bool bPresentationDeferred = false;
	bool bHasPendingPresentationNotification = false;
	/** 是否已经有过一次有效的移动方向；首次落位直接对准，之后才做限速转向。 */
	bool bFacingInitialized = false;
	FCatFishEncounterPresentationState PendingPreviousPresentationState;
	FCatFishEncounterPresentationState PendingCurrentPresentationState;
};
