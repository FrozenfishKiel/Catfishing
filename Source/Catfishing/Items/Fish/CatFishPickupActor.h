#pragma once

#include "CoreMinimal.h"
#include "Interaction/Carry/CatCarryableActor.h"
#include "Interaction/CatInteractable.h"
#include "FishContainers/CatFishContainerTypes.h"
#include "Framework/Core/CatProfileContracts.h"
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
class UCatInventoryComponent;

UENUM(BlueprintType)
enum class ECatFishPickupState : uint8
{
	Available,
	/** 已被某只猫叼在嘴里；仍是世界 Actor，不进入 Equipment 背包或任何容器。 */
	Carried
};

/** 所有客户端可见的可携带世界鱼只读状态；归属只以服务器解析后的 PlayerState 出网，StableNetId、候选参与者和容器 Revision 永不复制。 */
USTRUCT(BlueprintType)
struct FCatFishPickupPresentationState
{
	GENERATED_BODY()
	friend class FCatFishingSessionScoopMouthCarryTest;

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
	/** 这条实物鱼已归档的捕获者；服务器把私有 OwnerStableNetId 现场解析成可复制 PlayerState，表现层据此画主人色环。尚未归档或捕获者当前不在场时为空，身份字符串本身仍不出网。 */
	UPROPERTY(BlueprintReadOnly) TObjectPtr<APlayerState> OwnerPlayerState = nullptr;
};

/**
 * 上钩鱼被抄取或力竭拖岸后生成的服务器权威世界物品。Actor 没有“原钓手所有权”；可用时任何合法玩家都可先到先得。
 */
UCLASS(Blueprintable, meta=(ChildCannotTick))
class CATFISHING_API ACatFishPickupActor : public ACatCarryableActor, public ICatInteractable, public ICatInventoryWorldItem
{
	GENERATED_BODY()
	friend class FCatFishingSessionScoopMouthCarryTest;

public:
	/** 为世界鱼装配独立的刚体与准星探测组件，防止交互半径影响物理支撑；鱼身份由后续初始化提供。 */
	ACatFishPickupActor();
	FVector GetFishingCollisionCenter() const;
	bool ResolveFishingPickupFromAuthority(AController* RequestingController, FGuid RequestId);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 接收库存落地的一条鱼，恢复冻结重量和表现；这条鱼已入过正式库存，不再次生成捕获奖励。 */
	virtual bool InitializeFromInventoryFromAuthority(UCatInventoryItemInstance* Item, int32 Quantity) override;

	/** Carry 为新生成载体初始化冻结鱼身份但不改来源实例引用；调用方在静默移除原格成功后才把 WorldActor 和运行宿主交给载体。 */
	bool InitializeFromInventoryForCarryFromAuthority(UCatFishInventoryItemInstance* Item, int32 Quantity);

	/** 读取服务器持有的鱼定义供售价等权威计算使用；客户端表现仍沿原鱼种 ID 解析。 */
	UCatFishDefinition* GetFishDefinition() const;

	/** 只读预检单鱼消费所需的身份、携带归属和捕获记录依赖；请求门与交互范围由出售等调用方裁决。 */
	bool CanConsumeFromAuthority(AController* RequestingController) const;
	/** 为跨多条鱼的同一献祭批次独占实物；只写消费预留门，不归档、不隐藏、不销毁，失败后仍可正常操作。 */
	bool PrepareConsumptionFromAuthority(AController* Controller, FGuid RequestId);
	/** 完成已准备实物的消费或撤销占用；同请求提交不再运行会失败的预检，接受时沿用原有捕获归档并销毁实物。 */
	void FinishConsumptionFromAuthority(AController* Controller, FGuid RequestId, bool bCommit);


	/** 消费已预检的世界鱼；可选提交回调在独占鱼后执行，失败保留实物，成功再补捕获记录并清理销毁；外层保存请求重放结果。 */
	bool ConsumeFromAuthority(AController* RequestingController, FGuid RequestId,
		TFunction<bool()> CommitBeforeConsumption = {});

	/**
	 * 按服务器冻结的鱼身份、重量和表现参数初始化一次；库存鱼允许省略新捕获地域，参数无效时不公开半份实物。
	 * InCaptureCondition 是抛钩会话在真咬那一刻冻结的图鉴条件（地域＋时段＋天气），只在首次收集时写进图鉴；
	 * InHookerStableNetId 是这一竿的上钩者，收集层归他一人（钓鱼规则 §5.6:285「归上钩者：实物被队友抢走不取消登记」），
	 * 与 InFishingParticipantStableNetIds（只喂演出贡献名单）是两件事，不能互相顶替。
	 */
	bool InitializeFromAuthority(FGuid InFishingSessionId, FGuid InFishInstanceId,
		UCatFishDefinition* InFishDefinition, double InWeightKilograms, double InVisualScale,
		const FCatCaptureConditionSnapshot& InCaptureCondition, const FString& InHookerStableNetId,
		const TArray<FString>& InFishingParticipantStableNetIds, FVector GroundNormal = FVector::UpVector);

	const FCatFishPickupPresentationState& GetPresentationState() const { return PresentationState; }

	/** 读取角色唯一嘴部占用并转换为世界鱼；空嘴或叼着其它类型时返回空，不以客户端附件推导占用。 */
	static ACatFishPickupActor* FindCarriedFish(const ACatCharacter* Character);

	/** 权威占用空嘴并附着本鱼；抄网和地面拾取立即发布，库存 Carry 可延后发布到静默移格完成后，失败会撤销本次 expected-actor 认领。 */
	bool BeginMouthCarryFromAuthority(ACatCharacter* Character, APlayerState* PlayerState, bool bPublish = true);


	/** 只读核对保管 Actor 与库存鱼实例是否仍是一对一；Carry 的预检用它拒绝槽位复用，不在预检阶段改实例归属、可见性或附着。 */
	bool CanCarryInventoryItemFromAuthority(const UCatFishInventoryItemInstance* ExpectedItem) const;

	/** Carry 在静默扣格失败时把本次附着的原 Actor 恢复为隐藏保管态；不写库存格和实例归属，保留回调中其它合法转移的结果。 */
	void RestoreInventoryRetentionFromAuthority(UCatFishInventoryItemInstance* ExpectedItem,
		const FTransform& ExpectedWorldTransform);


	/**
	 * authority 把这条嘴叼鱼提交到射线命中的地面鱼护。
	 * 只有目标鱼护正式库存静默接收同一个鱼 ItemInstance 后，才归档捕获、结束嘴部携带并把原 Actor 隐藏为库存保管载体；箱满或权限失败时继续叼着。
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
	/** 为公共释放提供这条鱼对应的库存实例；世界新鱼尚未入库时允许为空，公共层不会为它创建替代实例。 */
	virtual UCatInventoryItemInstance* GetCarriedInventoryItem() const override;
	/** 只读预检鱼的落地姿态与形状；主动 Q 缺少身份或表现资源时拒绝，强制释放可保留公共层位置继续清嘴，不在这里扣库存或改 Actor。 */
	virtual bool PrepareCarryRelease(ACatCharacter* Character, FTransform& Transform, bool bThrow) const override;
	/** 公共解绑后清理鱼专属监听、表现和投掷效果；根物理、库存离库和网络发布由共同携带基类完成。 */
	virtual void OnCarryReleased(ACatCharacter* Character, bool bThrow) override;
	/** 完成生成后设置独立交互范围并恢复当前鱼姿态；客户端按复制的身份配置同一尺寸的物理根。 */
	virtual void BeginPlay() override;
	/** Actor 被售出、消费或容器清理销毁时按 expected actor 清除嘴部引用；不再依赖角色附件树是否已经先解绑。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	/** 只根据共同同步入口实际应用的附件刷新鱼体姿态和交互碰撞；不根据鱼的表现状态重新挂接根组件。 */
	virtual void RefreshCarryPresentation(bool bForceRefresh) override;

private:
	friend class FCatFishThrowReceiverTest;
	UFUNCTION() void HandleThrownFishHit(UPrimitiveComponent* Component, AActor* Other,
		UPrimitiveComponent* OtherComponent, FVector Impulse, const FHitResult& Hit);
	void DisarmThrowEffect();
	bool bThrowEffectArmed = false;
	TWeakObjectPtr<ACatCharacter> ThrowingCharacter;
	friend class ACatFishingSession;
	friend class UCatInventoryComponent;
	friend class FCatFishPickupMouthCarryAndGuardStoreTest;

	/** 客户端消费鱼身份与携带状态后刷新鱼种网格、鱼体姿态和碰撞，通知表现蓝图；根附件与物理由共同携带基类收敛。 */
	UFUNCTION() void OnRep_PresentationState(const FCatFishPickupPresentationState& Previous);
	UFUNCTION() void HandleAuthorityCarrierDestroyed(AActor* DestroyedActor);
	bool IsAuthorityRequestSpatiallyValid(const AController* RequestingController) const;
	/** 服务器建立鱼的嘴部附件并应用鱼专用相对姿态；客户端只消费共同基类的服务器附件结果。 */
	bool AttachCarriedRootToMouth(ACatCharacter* Character);
	/** 结束服务器嘴部携带生命周期，解除宿主回调、附着和归属；落点与物理由主动丢弃或宿主销毁入口决定。 */
	void EndMouthCarryFromAuthority();
	void ApplyLocalFocus(bool bFocused);
	/** 沿 FishDefinition 的直接引用解析 Mesh/落地动画；客户端不会维护独立鱼种映射，也不触碰根附件。 */
	void RefreshFishPresentation();
	/** 恢复侧躺姿态与冻结重量缩放，并把网格中心对齐盒形物理根；不修改 Actor 世界位置或运动状态。 */
	void ApplyLandedVisualTransform();
	/** Carried 状态清除落地专用 Mesh 位置和旋转，使鱼原点直接对齐嘴部骨骼，同时保留冻结重量缩放。 */
	void ApplyCarriedVisualTransform();
	void ApplyVisualScale();
	/** 把实物鱼实例记的服务器私有捕获者身份现场解析成可复制 PlayerState 并发布归属；只写表现状态，不改实例归属，也不把 StableNetId 送出网。 */
	void PublishOwnerPresentationFromAuthority(const FString& InOwnerStableNetId);
	/**
	 * 首次消费或入护后归档捕获并提交图鉴候选；已归档的库存鱼再次落地不重复生成奖励。
	 * PickerStableNetId 只是「谁把这条鱼收进来的」，用于实物归属与演出贡献名单；
	 * 图鉴收集层的收件人是 HookerStableNetId，两者在「A 上钩、B 跑过去叼走」时不是同一个人。
	 */
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
	/** 本世界鱼承载的唯一实物鱼实例；入容器、取回和落地均沿用同一对象，运行宿主随归属切换，捕获者、重量和身份不变。 */
	UPROPERTY(Transient) TObjectPtr<UCatFishInventoryItemInstance> InventoryItem;
	/** 这条实物鱼是否已经提交过捕获记录；库存落地和首次归档后写入，消费或再次入护时据此避免重复授予。 */
	bool bCaptureRecorded = false;
	/** 本鱼是否正被不可逆消费提交占用；入护成功会解除该占用以便同一保管 Actor 后续 Carry，真正售出或吃掉才保持到销毁。 */
	bool bConsumptionCommitted = false;
	/** 当前实物消费预留的关联标识；PrepareConsumptionFromAuthority 写入，FinishConsumptionFromAuthority 只允许同请求完成或取消。 */
	FGuid PreparedConsumptionRequest;
	// 仅正式Store静默入库作用域内授权该接收器，不让通用Add直接接走世界/嘴部原鱼。
	TWeakObjectPtr<UCatInventoryComponent> InventoryStoreTarget;
	FTransform LandedMeshBaseTransform = FTransform::Identity;
	FTransform CarriedMeshBaseTransform = FTransform::Identity;
	FName AppliedPresentationFishDefinitionId = NAME_None;
	/** 抛钩会话冻结的图鉴首次条件（地域＋时段＋天气）；库存落地的鱼没有新捕获条件，保持全 None。 */
	FCatCaptureConditionSnapshot CaptureCondition;
	/** 这一竿的上钩者；图鉴收集层的唯一收件人，实物被别人叼走也不改。库存落地的鱼为空（早已归档）。 */
	FString HookerStableNetId;
	TArray<FString> FishingParticipantStableNetIds;
	TWeakObjectPtr<ACatCharacter> AuthorityCarrier;
	bool bIdentityInitialized = false;
	bool bLocallyFocused = false;
	TMap<FString, FCatDomainCommandResult> PickupTerminalByRequester;
};
