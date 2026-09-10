#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/CatInteractable.h"
#include "FishContainers/CatFishContainerTypes.h"
#include "Inventory/CatInventoryWorldItem.h"
#include "CatFishPickupActor.generated.h"

class APlayerState;
class ACatCharacter;
class ACatFishingSession;
class UCatFishDefinition;
class UCatFishPresentationDefinition;
class USkeletalMeshComponent;
class USphereComponent;
class UBoxComponent;
class UCatFishInventoryItemInstance;

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
	/** 初始化或退出嘴叼时查询到的接触法线，供服务器把该接触点换算为物理中心；库存放置和后续抛落不更新此字段，不代表当前支撑面。 */
	UPROPERTY(BlueprintReadOnly) FVector GroundNormal = FVector::UpVector;
	UPROPERTY(BlueprintReadOnly) ECatFishPickupState State = ECatFishPickupState::Available;
	UPROPERTY(BlueprintReadOnly) TObjectPtr<APlayerState> CarriedByPlayerState = nullptr;
};

/**
 * 上钩鱼被抄取或力竭拖岸后生成的服务器权威世界物品。Actor 没有“原钓手所有权”；可用时任何合法玩家都可先到先得。
 */
UCLASS(Blueprintable, meta=(ChildCannotTick))
class CATFISHING_API ACatFishPickupActor : public AActor, public ICatInteractable, public ICatInventoryWorldItem
{
	GENERATED_BODY()

public:
	/** 为世界鱼装配独立的刚体与准星探测组件，防止交互半径影响物理支撑；鱼身份由后续初始化提供。 */
	ACatFishPickupActor();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 接收库存落地的一条鱼，恢复冻结重量和表现；这条鱼已入过正式库存，不再次生成捕获奖励。 */
	virtual bool InitializeFromInventoryFromAuthority(UCatInventoryItemInstance* Item, int32 Quantity) override;

	/** 读取服务器持有的鱼定义供售价等权威计算使用；客户端表现仍沿原鱼种 ID 解析。 */
	UCatFishDefinition* GetFishDefinition() const;

	/** 只读预检单鱼消费所需的身份、携带归属和捕获记录依赖；请求门与交互范围由出售等调用方裁决。 */
	bool CanConsumeFromAuthority(AController* RequestingController) const;

	/** 消费已预检的世界鱼；可选提交回调在独占鱼后执行，失败保留实物，成功再补捕获记录并清理销毁；外层保存请求重放结果。 */
	bool ConsumeFromAuthority(AController* RequestingController, FGuid RequestId,
		TFunction<bool()> CommitBeforeConsumption = {});

	/** 按服务器冻结的鱼身份、重量和表现参数初始化一次；库存鱼允许省略新捕获地域，参数无效时不公开半份实物。 */
	bool InitializeFromAuthority(FGuid InFishingSessionId, FGuid InFishInstanceId,
		UCatFishDefinition* InFishDefinition, double InWeightKilograms, double InVisualScale, FName InRegionId,
		const TArray<FString>& InFishingParticipantStableNetIds, FVector GroundNormal = FVector::UpVector);

	const FCatFishPickupPresentationState& GetPresentationState() const { return PresentationState; }

	/** 查找该角色当前嘴上叼着的唯一世界鱼；没有或附件状态不一致时返回空。 */
	static ACatFishPickupActor* FindCarriedFish(const ACatCharacter* Character);

	/**
	 * authority 把这条嘴叼鱼提交到射线命中的地面鱼护。
	 * 只有目标鱼护正式库存已接收同一个鱼 ItemInstance 才销毁世界鱼；箱满或权限失败时继续叼着。
	 */
	FCatCaptureCommitResult StoreInFishGuardFromAuthority(AController* RequestingController, FGuid RequestId,
		AActor* TargetInventoryHost);

	/** 查询有效请求者能否操作尚未被消费占用的鱼；最终距离、身体和嘴部资格仍由服务器交互提交复核。 */
	virtual bool CanInteract_Implementation(AController* RequestingController) const override;
	virtual void BeginLocalFocus_Implementation() override;
	virtual void EndLocalFocus_Implementation() override;
	virtual FText GetInteractionPrompt_Implementation() const override;
	virtual double GetInteractionRadius_Implementation() const override;
	/** 对死鱼按 E 后由服务器附着到角色嘴部；只改变世界 Actor 的携带状态，不写 Character 背包。 */
	virtual bool Interact_Implementation(AController* RequestingController, FGuid RequestId) override;

	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category="Catfishing|FishContainers")
	void BP_OnPickupPresentationChanged(const FCatFishPickupPresentationState& Previous,
		const FCatFishPickupPresentationState& Current);

protected:
	/** 完成生成后设置独立交互范围并恢复当前鱼姿态；客户端按复制的身份配置同一尺寸的物理根。 */
	virtual void BeginPlay() override;
	virtual void OnRep_AttachmentReplication() override;

private:
	friend class ACatFishingSession;
	friend class FCatFishPickupMouthCarryAndGuardStoreTest;

	/** 客户端消费鱼身份与携带状态后刷新网格、碰撞和附着，通知表现蓝图；不生成捕获记录或经济事务。 */
	UFUNCTION() void OnRep_PresentationState(const FCatFishPickupPresentationState& Previous);
	UFUNCTION() void HandleAuthorityCarrierDestroyed(AActor* DestroyedActor);
	bool IsAuthorityRequestSpatiallyValid(const AController* RequestingController) const;
	/** 权威占用空嘴并附着本鱼；鱼和鱼护共用互斥约束，附着失败恢复地面状态，成功才发布复制。 */
	bool BeginMouthCarryFromAuthority(ACatCharacter* Character, APlayerState* PlayerState);
	/** 把根组件精确附着到角色 Mesh + Mouth Socket；不能只比较父 Actor。 */
	bool AttachCarriedRootToMouth(ACatCharacter* Character, const TCHAR* Source, bool bLogCorrection);
	/** 以复制的 Carried/Available 为最终事实，收敛 AttachmentReplication 与 PresentationState 的到达顺序。 */
	void ReconcileAttachmentFromPresentation(const TCHAR* Source);
	void ScheduleAttachmentReconcileRetry();
	void RetryAttachmentReconcile();
	/** 携带者退出后解除嘴部占用，在既有地面查询结果上固定鱼体；不触发新的捕获记录或物理抛掷。 */
	void ReleaseMouthCarryFromAuthority(const FVector& DropLocation);
	void ApplyLocalFocus(bool bFocused);
	/** 沿 FishDefinition 的直接引用解析 Mesh/落地动画；客户端不会维护独立鱼种映射。 */
	void RefreshFishPresentation();
	/** 恢复侧躺姿态与冻结重量缩放，并把网格中心对齐盒形物理根；不修改 Actor 世界位置或运动状态。 */
	void ApplyLandedVisualTransform();
	/** Carried 状态清除落地专用 Mesh 位置和旋转，使鱼原点直接对齐嘴部骨骼，同时保留冻结重量缩放。 */
	void ApplyCarriedVisualTransform();
	void ApplyVisualScale();
	/** 首次消费或入护后归档捕获并提交图鉴候选；已归档的库存鱼再次落地不重复生成奖励。 */
	void ArchiveCommittedCapture(const FCatCaptureCommittedResult& Committed, const FString& PickerStableNetId);

	/** 与冻结鱼体姿态匹配的盒形物理根；姿态刷新计算尺寸，库存落地求解与 Chaos 共用，不包含交互探测范围。 */
	UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> WorldCollision;
	/** 准星命中的独立探测球；设置读取交互半径，嘴叼时关闭，不承担落地支撑或物理模拟。 */
	UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> InteractionSphere;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta=(AllowPrivateAccess="true"))
	TObjectPtr<USkeletalMeshComponent> FishMesh;

	UPROPERTY(ReplicatedUsing=OnRep_PresentationState, VisibleInstanceOnly, BlueprintReadOnly,
		meta=(AllowPrivateAccess="true"))
	FCatFishPickupPresentationState PresentationState;

	/** 只在 authority 保存定义以构造捕获/图鉴事实，不下发 DataAsset。 */
	UPROPERTY(Transient) TObjectPtr<UCatFishDefinition> FishDefinition;
	UPROPERTY(Transient) TObjectPtr<UCatFishPresentationDefinition> FishPresentationDefinition;
	/** 从库存落地时保留的实物鱼实例；再次入护时复制到接收宿主，保留原捕获者、重量和实例身份。 */
	UPROPERTY(Transient) TObjectPtr<UCatFishInventoryItemInstance> InventoryItem;
	/** 这条实物鱼是否已经提交过捕获记录；库存落地和首次归档后写入，消费或再次入护时据此避免重复授予。 */
	bool bCaptureRecorded = false;
	/** 本鱼是否已被消费流程占用；提交前置true阻止重入，入护或入账失败释放，成功后保持到销毁。 */
	bool bConsumptionCommitted = false;
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
