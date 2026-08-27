#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/CatInteractable.h"
#include "Items/CatItemTypes.h"
#include "CatFishPickupActor.generated.h"

class APlayerState;
class UCatFishDefinition;
class USkeletalMeshComponent;
class USphereComponent;

UENUM(BlueprintType)
enum class ECatFishPickupState : uint8
{
	Available,
	Claimed
};

/** 所有客户端可见的岸上鱼只读状态；StableNetId、候选参与者和容器 Revision 永不复制。 */
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
	UPROPERTY(BlueprintReadOnly) TObjectPtr<APlayerState> ClaimedByPlayerState = nullptr;
};

/**
 * 鱼被拖过岸线后生成的服务器权威世界物品。Actor 没有“原钓手所有权”；任何合法玩家都可先到先得。
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

	virtual bool CanInteract_Implementation(AController* RequestingController) const override;
	virtual void BeginLocalFocus_Implementation() override;
	virtual void EndLocalFocus_Implementation() override;
	virtual FText GetInteractionPrompt_Implementation() const override;
	virtual double GetInteractionRadius_Implementation() const override;
	/** 正式独立鱼护尚未选定前只完成身份/距离校验并返回依赖缺失，不写 Character 背包或旧个人鱼护。 */
	virtual bool Interact_Implementation(AController* RequestingController, FGuid RequestId) override;

	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category="Catfishing|Items")
	void BP_OnPickupPresentationChanged(const FCatFishPickupPresentationState& Previous,
		const FCatFishPickupPresentationState& Current);

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION() void OnRep_PresentationState(const FCatFishPickupPresentationState& Previous);
	bool IsAuthorityRequestSpatiallyValid(const AController* RequestingController) const;
	void ApplyLocalFocus(bool bFocused);
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
	FName RegionId = NAME_None;
	TArray<FString> FishingParticipantStableNetIds;
	bool bIdentityInitialized = false;
	bool bLocallyFocused = false;
	TMap<FString, FCatDomainCommandResult> PickupTerminalByRequester;
};
