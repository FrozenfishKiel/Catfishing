#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/CatInteractable.h"
#include "Items/CatItemTypes.h"
#include "CatFishPickupActor.generated.h"

class APlayerState;
class ACatCharacter;
class ACatFishingSession;
class UCatFishDefinition;
class UCatFishPresentationDefinition;
class USkeletalMeshComponent;
class USphereComponent;

UENUM(BlueprintType)
enum class ECatFishPickupState : uint8
{
	Available,
	/** 已被某只猫叼在嘴里；仍是世界 Actor，不进入 Equipment 背包或任何容器。 */
	Carried
};

/** 所有客户端可见的可携带世界鱼只读状态；StableNetId、候选参与者和容器 Revision 永不复制。 */
USTRUCT(BlueprintType)
struct FCatFishPickupPresentationState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FGuid FishingSessionId;
	UPROPERTY(BlueprintReadOnly) FGuid FishInstanceId;
	UPROPERTY(BlueprintReadOnly) FName FishDefinitionId = NAME_None;
	UPROPERTY(BlueprintReadOnly) double WeightKilograms = 0.0;
	/** 与水中 Encounter 完全相同的服务器冻结统一 Mesh 缩放。 */
	UPROPERTY(BlueprintReadOnly) double VisualScale = 1.0;
	UPROPERTY(BlueprintReadOnly) ECatFishPickupState State = ECatFishPickupState::Available;
	UPROPERTY(BlueprintReadOnly) TObjectPtr<APlayerState> CarriedByPlayerState = nullptr;
};

/**
 * 上钩鱼被抄取或力竭拖岸后生成的服务器权威世界物品。Actor 没有“原钓手所有权”；可用时任何合法玩家都可先到先得。
 */
UCLASS(Blueprintable, meta=(ChildCannotTick))
class CATFISHING_API ACatFishPickupActor : public AActor, public ICatInteractable
{
	GENERATED_BODY()

public:
	ACatFishPickupActor();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	bool InitializeFromAuthority(FGuid InFishingSessionId, FGuid InFishInstanceId,
		UCatFishDefinition* InFishDefinition, double InWeightKilograms, double InVisualScale, FName InRegionId,
		const TArray<FString>& InFishingParticipantStableNetIds);

	const FCatFishPickupPresentationState& GetPresentationState() const { return PresentationState; }

	/** 查找该角色当前嘴上叼着的唯一世界鱼；没有或附件状态不一致时返回空。 */
	static ACatFishPickupActor* FindCarriedFish(const ACatCharacter* Character);

	/**
	 * authority 把这条嘴叼鱼提交到射线命中的地面鱼护。
	 * 只有 Items 已提交成功（或同一请求重放）才销毁世界鱼；箱满、版本冲突或权限失败时继续叼着。
	 */
	FCatCaptureCommitResult StoreInFishGuardFromAuthority(AController* RequestingController, FGuid RequestId,
		FGuid TargetContainerId, int64 ExpectedTargetRevision);

	virtual bool CanInteract_Implementation(AController* RequestingController) const override;
	virtual void BeginLocalFocus_Implementation() override;
	virtual void EndLocalFocus_Implementation() override;
	virtual FText GetInteractionPrompt_Implementation() const override;
	virtual double GetInteractionRadius_Implementation() const override;
	/** 对死鱼按 E 后由服务器附着到角色嘴部；只改变世界 Actor 的携带状态，不写 Character 背包。 */
	virtual bool Interact_Implementation(AController* RequestingController, FGuid RequestId) override;

	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category="Catfishing|Items")
	void BP_OnPickupPresentationChanged(const FCatFishPickupPresentationState& Previous,
		const FCatFishPickupPresentationState& Current);

protected:
	virtual void BeginPlay() override;
	virtual void OnRep_AttachmentReplication() override;

private:
	friend class ACatFishingSession;
	friend class FCatFishPickupMouthCarryAndGuardStoreTest;

	UFUNCTION() void OnRep_PresentationState(const FCatFishPickupPresentationState& Previous);
	UFUNCTION() void HandleAuthorityCarrierDestroyed(AActor* DestroyedActor);
	bool IsAuthorityRequestSpatiallyValid(const AController* RequestingController) const;
	bool BeginMouthCarryFromAuthority(ACatCharacter* Character, APlayerState* PlayerState);
	/** 把根组件精确附着到角色 Mesh + Mouth Socket；不能只比较父 Actor。 */
	bool AttachCarriedRootToMouth(ACatCharacter* Character, const TCHAR* Source, bool bLogCorrection);
	/** 以复制的 Carried/Available 为最终事实，收敛 AttachmentReplication 与 PresentationState 的到达顺序。 */
	void ReconcileAttachmentFromPresentation(const TCHAR* Source);
	void ScheduleAttachmentReconcileRetry();
	void RetryAttachmentReconcile();
	void ReleaseMouthCarryFromAuthority(const FVector& DropLocation);
	void ApplyLocalFocus(bool bFocused);
	/** 沿 FishDefinition 的直接引用解析 Mesh/落地动画；客户端不会维护独立鱼种映射。 */
	void RefreshFishPresentation();
	/** Available 状态恢复落地专用 Mesh 位置和旋转，同时保留冻结重量缩放。 */
	void ApplyLandedVisualTransform();
	/** Carried 状态清除落地专用 Mesh 位置和旋转，使鱼原点直接对齐嘴部骨骼，同时保留冻结重量缩放。 */
	void ApplyCarriedVisualTransform();
	void ApplyVisualScale();
	void ArchiveCommittedCapture(const FCatCaptureCommittedResult& Committed, const FString& PickerStableNetId);

	UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> InteractionSphere;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta=(AllowPrivateAccess="true"))
	TObjectPtr<USkeletalMeshComponent> FishMesh;

	UPROPERTY(ReplicatedUsing=OnRep_PresentationState, VisibleInstanceOnly, BlueprintReadOnly,
		meta=(AllowPrivateAccess="true"))
	FCatFishPickupPresentationState PresentationState;

	/** 只在 authority 保存定义以构造捕获/图鉴事实，不下发 DataAsset。 */
	UPROPERTY(Transient) TObjectPtr<UCatFishDefinition> FishDefinition;
	UPROPERTY(Transient) TObjectPtr<UCatFishPresentationDefinition> FishPresentationDefinition;
	FTransform LandedMeshBaseTransform = FTransform::Identity;
	FTransform CarriedMeshBaseTransform = FTransform::Identity;
	FName AppliedPresentationFishDefinitionId = NAME_None;
	FName RegionId = NAME_None;
	TArray<FString> FishingParticipantStableNetIds;
	TWeakObjectPtr<ACatCharacter> AuthorityCarrier;
	bool bIdentityInitialized = false;
	bool bLocallyFocused = false;
	FTimerHandle AttachmentReconcileTimer;
	int32 AttachmentReconcileAttemptCount = 0;
	bool bAttachmentReconcileRetryExhausted = false;
	TMap<FString, FCatDomainCommandResult> PickupTerminalByRequester;
};
