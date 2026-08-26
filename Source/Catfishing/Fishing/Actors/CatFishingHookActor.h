#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Fishing/Actors/CatFishingActorTypes.h"
#include "CatFishingHookActor.generated.h"

class USceneComponent;
class UProjectileMovementComponent;
class UCableComponent;

UCLASS(Blueprintable, meta=(ChildCannotTick))
class CATFISHING_API ACatFishingHookActor : public AActor
{
	GENERATED_BODY()

public:
	ACatFishingHookActor();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	bool InitializeAuthoritativeIdentity(FGuid InFishingSessionId, FGuid InCastAttemptId);
	bool BeginAuthoritativeFlight(const FVector& InitialVelocity, const FVector& ExpectedLandingWorldPoint);
	bool FinalizeAuthoritativeLandingOnce(bool bSucceeded, const FVector& LandingWorldPoint);
	/** Authority 只同步离散浮漂模式；客户端按起始服务器时间自行播放连续位移。 */
	bool SetBobberPresentationModeFromAuthority(ECatFishingBobberPresentationMode Mode);
	/** 服务器只复制鱼线标量；Cable 粒子仍由每台有画面的机器本地模拟。 */
	bool SetFishingLinePresentationFromAuthority(double PaidOutLineLengthCentimeters,
		double StraightLineDistanceCentimeters, double SlackLineLengthCentimeters,
		float NormalizedTension, bool bLineTaut);
	void DeferInitialPresentationFromAuthority();
	void PublishInitialPresentationFromAuthority();
	const FCatFishingHookPresentationState& GetPresentationState() const;
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category="Fishing|Hook")
	void BP_OnHookPresentationChanged(const FCatFishingHookPresentationState& Previous, const FCatFishingHookPresentationState& Current);
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category="Fishing|Hook") void BP_PlayHookPresentationEvent(FGameplayTag EventTag);

	/**
	 * 本地表现：把 VisualRoot 整体抬/沉 OffsetZ 厘米（浮漂慢浮/点动/黑漂下沉的共用入口）。
	 * 只影响表现子树，不改 Actor 权威位置——落点、线长、抄网判定不受影响。
	 */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category="Fishing|Hook")
	void SetPresentationBobOffset(float OffsetZ);
	/** 当前 VisualRoot 世界位置，供调试线与纯表现附着读取；不属于玩法锚点。 */
	UFUNCTION(BlueprintPure, BlueprintCosmetic, Category="Fishing|Hook")
	FVector GetPresentationVisualWorldLocation() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	/** 远端客户端若晚于 Hook 才解析到 Rod Owner，在复制回调里补接鱼线，避免只在主机显示。 */
	virtual void OnRep_Owner() override;
	/** 远端客户端若先收到 CastFlight、后解析到抛竿者，在 Instigator 回调里补播一次投杆动画。 */
	virtual void OnRep_Instigator() override;

private:
	UFUNCTION()
	void OnRep_PresentationState(const FCatFishingHookPresentationState& Previous);
	void QueueOrDispatchPresentationChanged(const FCatFishingHookPresentationState& Previous, const FCatFishingHookPresentationState& Current);
	void DispatchPresentationChanged(const FCatFishingHookPresentationState& Previous, const FCatFishingHookPresentationState& Current);
	void RefreshBobberPresentationTimer();
	void UpdateBobberPresentation();
	/** 用有界轮询计时器（非 Actor Tick）等待抛物线穿过目标水面高度，再一次性提交落水终态。 */
	void PollAuthoritativeLanding();
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> SceneRoot;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> VisualRoot;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> HookVisualAnchor;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> BobberVisualAnchor;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> BaitVisualAnchor;
	/** 与 Hook 权威根节点解耦的本地绝对坐标锚点，用 60Hz 插值吸收服务器/复制的 20Hz 位置阶跃。 */
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> FishingLineStartAnchor;
	/**
	 * 纯表现鱼线：每台机器依据已复制的 Hook/Rod Transform 本地模拟，不承载线长、受力或命中判定。
	 * 起点跟随 Hook 的 VisualRoot，末端在 BeginPlay/表现状态到达时绑定到鱼竿的 RodTipMarker（缺失时回退权威锚点）。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Fishing|Presentation", meta=(AllowPrivateAccess="true"))
	TObjectPtr<UCableComponent> FishingLine;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UProjectileMovementComponent> ProjectileMovement;
	/** 在本机解析鱼竿表现竿尖并接好 Cable；Owner/复制状态尚未到达时保持隐藏。 */
	void RefreshFishingLineAttachment();
	/** 把已复制的 L_paid/Slack 写成纯表现目标；实际 Cable 参数由本地定时器平滑追赶。 */
	void RefreshFishingLineShape();
	void RefreshFishingLinePresentationTimer();
	void UpdateFishingLinePresentation();
	/** 仅在本机第一次观察到有效 CastFlight + Instigator 时播放；不复制 Montage 播放进度。 */
	void TryPlayCastMontageFromPresentation();
	UPROPERTY(ReplicatedUsing=OnRep_PresentationState, VisibleInstanceOnly, BlueprintReadOnly, meta=(AllowPrivateAccess="true"))
	FCatFishingHookPresentationState PresentationState;
	bool bIdentityInitialized = false;
	bool bLandingFinalized = false;
	bool bPresentationDeferred = false;
	FVector PendingAuthoritativeLandingWorldPoint = FVector::ZeroVector;
	FTimerHandle LandingPollTimerHandle;
	FTimerHandle BobberPresentationTimerHandle;
	FTimerHandle FishingLinePresentationTimerHandle;
	double TargetFishingLineLengthCentimeters = 75.0;
	double DisplayedFishingLineLengthCentimeters = 75.0;
	double TargetFishingLineSlackRatio = 0.0;
	double DisplayedFishingLineSlackRatio = 0.0;
	bool bFishingLineSmoothingInitialized = false;
	FVector VisualRootBaseRelativeLocation = FVector::ZeroVector;
	bool bVisualRootBaseLocationInitialized = false;
	bool bHasPendingPresentationNotification = false;
	FCatFishingHookPresentationState PendingPreviousPresentationState;
	FCatFishingHookPresentationState PendingCurrentPresentationState;
	bool bCastMontagePlayed = false;
};
