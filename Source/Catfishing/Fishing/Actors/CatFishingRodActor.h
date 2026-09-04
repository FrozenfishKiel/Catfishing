#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Fishing/Actors/CatFishingActorTypes.h"
#include "Fishing/Simulation/CatFishingRodResistanceModel.h"
#include "CatFishingRodActor.generated.h"

class APlayerState;
class USceneComponent;
class UCharacterMovementComponent;

/** 高频复制的手持鱼线约束目标；不推进鱼竿业务 Revision，也不保存第二份搏斗终态。 */
USTRUCT(BlueprintType)
struct CATFISHING_API FCatFishingCarrierConstraintState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FVector_NetQuantizeNormal PullDirection = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly)
	float PullAccelerationCentimetersPerSecondSquared = 0.0f;
	/** 本约束步要求猫端达到的向鱼速度；本地移动帧平滑追赶该目标，不作为可累积冲量。 */
	UPROPERTY(BlueprintReadOnly)
	float TargetPullSpeedCentimetersPerSecond = 0.0f;
	UPROPERTY(BlueprintReadOnly)
	float MaximumAwaySpeedMultiplier = 1.0f;
	UPROPERTY(BlueprintReadOnly)
	float NormalizedTension = 0.0f;
	UPROPERTY(BlueprintReadOnly)
	float ConstraintErrorCentimeters = 0.0f;
	/** 当前搏斗是否要求鱼竿使用受力后的实际姿态，而不是瞬时跟随控制器。 */
	UPROPERTY(BlueprintReadOnly)
	bool bFightActive = false;
	UPROPERTY(BlueprintReadOnly)
	FVector_NetQuantizeNormal RodPullAxis = FVector::ForwardVector;
	/** 垂直鱼线时的最大转矩；实际有向转矩随杆姿态连续计算。 */
	UPROPERTY(BlueprintReadOnly)
	float MaximumFishTorqueStrengthMeters = 0.0f;
	UPROPERTY(BlueprintReadOnly)
	float CatTorqueCapacityStrengthMeters = 0.0f;
	UPROPERTY(BlueprintReadOnly)
	bool bActive = false;
};

/** 场景中已经部署出来的鱼竿表现 Actor；它复制可见状态和操作位，但真实物品实例仍由 Equipment 的 Use/UnUse 记录持有。 */
UCLASS(Blueprintable, meta=(ChildCannotTick))
class CATFISHING_API ACatFishingRodActor : public AActor
{
	GENERATED_BODY()

public:
	/** 创建鱼竿表现 Actor 的组件和默认复制姿态；身份和锚点仍要等服务器初始化后才可信。 */
	ACatFishingRodActor();
	virtual void Tick(float DeltaSeconds) override;
	/** 注册鱼竿表现状态复制；客户端只读 PresentationState，并通过 OnRep 驱动蓝图表现刷新。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** 初始化这根场景鱼竿的权威身份；ActorId 负责场景对象，ItemInstanceId 负责回到库存里的同一件物品。 */
	bool InitializeAuthoritativeIdentity(FGuid InRodActorId, FGuid InItemInstanceId, FName InRodDefinitionId,
		FName InRodSkinDefinitionId, APlayerState* InOwnerPlayerState, APlayerState* InOperatorPlayerState,
		bool bInDeployed, bool bInBroken);
	/** 写入这根竿的权威本地锚点；必须在身份初始化前完成，之后蓝图和钓鱼逻辑都从这些锚点取世界坐标。 */
	bool ConfigureCanonicalAnchorsFromAuthority(const FTransform& InRodTip, const FTransform& InStand, const FTransform& InGrip);
	/** 兼容旧单操作手写口：传入玩家时重置为仅该玩家，传空时清空全部槽位。 */
	bool SetOperatorFromAuthority(APlayerState* InOperatorPlayerState, int64 ExpectedRevision);
	/** 把玩家追加到第一个空槽；OutSlotIndex 只有成功时有效。 */
	bool AddOperatorFromAuthority(APlayerState* InOperatorPlayerState, int64 ExpectedRevision, int32& OutSlotIndex);
	/** 移除玩家并压紧数组；主位离开时原 1 号位自动晋升为 0 号位。 */
	bool RemoveOperatorFromAuthority(APlayerState* InOperatorPlayerState, int64 ExpectedRevision,
		APlayerState*& OutPromotedPrimaryPlayerState);
	/** 切换鱼竿皮肤定义；成功后只改变表现状态，不改变库存实例和耐久。 */
	bool SetRodSkinFromAuthority(FName InRodSkinDefinitionId, int64 ExpectedRevision);
	/** 写入断竿表现状态；真正的耐久结算由 Equipment/Fishing 流程完成，这里只负责复制可见结果。 */
	bool SetBrokenFromAuthority(bool bInBroken, int64 ExpectedRevision);
	/** 写入部署/收起表现状态；收起后表现会隐藏，物品是否回库存由外层 UnUse 事务决定。 */
	bool SetDeployedFromAuthority(bool bInDeployed, int64 ExpectedRevision);
	/** 读取当前复制表现状态；调用方只能观察 Actor 身份、实例身份和操作位，不能绕过权威写口修改。 */
	const FCatFishingRodPresentationState& GetPresentationState() const;
	/** 读取竿尖世界坐标；鱼线、浮漂和蓝图表现都以这个锚点作为挂接点。 */
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") FTransform GetRodTipWorldTransform() const;
	/** 读取主操作位世界坐标；旧调用方把 Stand 视为第一个玩家站位。 */
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") FTransform GetStandWorldTransform() const;
	/** 所有玩家共用的 R 交互锚点；只决定能否加入，不随当前人数或下一个槽位变化。 */
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") FTransform GetOperatorInteractionWorldTransform() const;
	/** 槽位 0 是右侧，1 是左侧；更高索引左右交替向外扩展。非法索引回退到原始 Stand 中心。 */
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") FTransform GetOperatorStandWorldTransform(int32 SlotIndex) const;
	/** 读取当前占用操作位的玩家数量；表现和交互只把它当只读计数。 */
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") int32 GetOperatorCount() const;
	/** 查询某个玩家当前占用的操作位编号；未加入或空玩家返回 INDEX_NONE。 */
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") int32 GetOperatorSlotIndex(APlayerState* PlayerState) const;
	/** 判断玩家是否是当前主操作位；兼容旧单人逻辑读取 OperatorPlayerState 的场景。 */
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") bool IsPrimaryOperator(APlayerState* PlayerState) const;
	/** 读取下一个可用操作位编号；满员或布局配置无效时返回 INDEX_NONE。 */
	int32 GetFirstFreeOperatorSlotIndex() const;
	/** 读取握持点世界坐标；角色手部 IK 和竿体表现用它对齐。 */
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") FTransform GetGripWorldTransform() const;
	/** 服务器规范握持跟随：只读 PlayerController/Pawn 权威姿态，不信任客户端 Socket Transform。 */
	bool RefreshHeldTransformFromAuthority(double DeltaSeconds = 0.0);
	/** 最后一名操作者离开后把同一 Actor 放到服务器裁定的地面 Transform；不改会话或物品身份。 */
	bool PlaceOnGroundFromAuthority(const FTransform& GroundTransform);
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") FVector GetAuthoritativeRodForwardVector() const;
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") FVector GetAuthoritativeRodTipVelocity() const
	{
		return AuthoritativeRodTipVelocity;
	}
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") FVector GetAuthoritativeHolderVelocity() const
	{
		return AuthoritativeHolderVelocity;
	}
	/** 仅服务器积分写入；固定步消费累计差值，不能把同一渲染帧的努力重复结算。 */
	const FCatFishingRodRotationEffortSnapshot& GetAuthoritativeRotationEffortSnapshot() const
	{
		return AuthoritativeRotationEffort;
	}
	/** FightRunner 发布同一份双端求解目标；服务器与拥有客户端都在移动帧内平滑追赶，不直接写 Actor Transform。 */
	bool SetCarrierConstraintFromAuthority(const FVector& PullDirection,
		double PullAccelerationCentimetersPerSecondSquared, double TargetPullSpeedCentimetersPerSecond,
		double MaximumAwaySpeedMultiplier,
		double NormalizedTension, double ConstraintErrorCentimeters,
		bool bFightActive = false, double MaximumFishTorqueStrengthMeters = 0.0,
		double CatTorqueCapacityStrengthMeters = 0.0,
		const FVector& RodPullAxis = FVector::ForwardVector);
	void ClearCarrierConstraintFromAuthority();
	UFUNCTION(BlueprintPure, Category="Fishing|Rod")
	const FCatFishingCarrierConstraintState& GetCarrierConstraintState() const
	{
		return CarrierConstraintState;
	}
	APawn* GetHolderPawnFromAuthority() const;
	/** 蓝图表现刷新事件；C++ 提供前后状态，蓝图只做视觉响应，不能在这里改权威状态。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category="Fishing|Rod")
	void BP_OnRodPresentationChanged(const FCatFishingRodPresentationState& Previous, const FCatFishingRodPresentationState& Current);
	/** 蓝图皮肤应用事件；每次状态分发都会给当前皮肤定义，便于迟到客户端补齐外观。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category="Fishing|Rod") void BP_ApplyRodSkin(FName RodSkinDefinitionId);
	/** 蓝图一次性表现事件入口；C++ 只传事件标签，具体特效、音效和动画由蓝图决定。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category="Fishing|Rod") void BP_PlayRodPresentationEvent(FGameplayTag EventTag);

protected:
	/** 进入 World 后补发可能早于 BeginPlay 到达的表现变化；避免蓝图组件未就绪时直接触发事件。 */
	virtual void BeginPlay() override;
	/** 离开 World 时在权威端注销服务登记；避免 FishingService 继续引用已经销毁的场景鱼竿。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 客户端收到表现状态复制后的入口；Previous 由引擎提供，用来让蓝图比较前后变化。 */
	UFUNCTION()
	void OnRep_PresentationState(const FCatFishingRodPresentationState& Previous);
	UFUNCTION()
	void OnRep_CarrierConstraintState();
	/** 分发表现变化或延迟到 BeginPlay 后再分发；保证蓝图事件只在组件可用时触发。 */
	void QueueOrDispatchPresentationChanged(const FCatFishingRodPresentationState& Previous, const FCatFishingRodPresentationState& Current);
	/** 立即应用皮肤、隐藏状态和蓝图通知；服务器与客户端各自在本地执行这一层表现副作用。 */
	void DispatchPresentationChanged(const FCatFishingRodPresentationState& Previous, const FCatFishingRodPresentationState& Current);
	void ApplyCarrierConstraint(float DeltaSeconds);
	void ResetCarrierConstraintSmoothing();
	void ResetAuthoritativeRotationEffort();
	void UpdateCarrierConstraintTickDependency(UCharacterMovementComponent* Movement);
	/** 提交一次权威可变状态；它保留 Actor/Item/Owner 身份，只允许操作位、皮肤、部署和断竿状态变化。 */
	bool CommitAuthoritativeMutation(const FCatFishingRodPresentationState& Next, int64 ExpectedRevision);
	/** 鱼竿 Actor 的场景根节点；所有可视锚点跟随它接受 Actor Transform。 */
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> SceneRoot;
	/** 美术表现根节点；皮肤和特效挂在这里，不参与权威锚点计算。 */
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> VisualRoot;
	/** 竿尖的本地锚点组件；鱼线和浮漂表现从它换算世界坐标。 */
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> RodTipAnchor;
	/** 默认操作站位的本地锚点组件；旧单人逻辑和交互基准都从这里派生。 */
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> StandAnchor;
	/** 当前产品左右两位的编辑器可见参考锚；第三位及以后也统一由编号公式计算，不增加专用锚点。 */
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> RightStandAnchor;
	/** 当前产品左侧站位的编辑器可见参考锚；运行时位置仍由同一编号公式保持左右对称。 */
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> LeftStandAnchor;
	/** 握持点的本地锚点组件；角色手部 IK 和竿体视觉对齐会读取它。 */
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> GripAnchor;
	/** 鱼竿 Actor 的唯一复制表现事实；服务器写入，客户端通过 OnRep 转成蓝图视觉事件。 */
	UPROPERTY(ReplicatedUsing=OnRep_PresentationState, VisibleInstanceOnly, BlueprintReadOnly, meta=(AllowPrivateAccess="true"))
	FCatFishingRodPresentationState PresentationState;
	UPROPERTY(ReplicatedUsing=OnRep_CarrierConstraintState, VisibleInstanceOnly, BlueprintReadOnly, meta=(AllowPrivateAccess="true"))
	FCatFishingCarrierConstraintState CarrierConstraintState;
	/** 竿尖权威本地 Transform；配置后不再读蓝图组件作为数据源，避免表现改动反向污染玩法坐标。 */
	FTransform RodTipCanonicalLocalTransform = FTransform::Identity;
	/** 操作基准位权威本地 Transform；多人站位和交互锚点都从它计算。 */
	FTransform StandCanonicalLocalTransform = FTransform::Identity;
	/** 握持点权威本地 Transform；手部对齐读取它而不是直接信任组件当前值。 */
	FTransform GripCanonicalLocalTransform = FTransform::Identity;
	FVector AuthoritativeRodTipVelocity = FVector::ZeroVector;
	FVector AuthoritativeHolderVelocity = FVector::ZeroVector;
	FRotator AuthoritativeHeldAimRotation = FRotator::ZeroRotator;
	/** 权威旋转的连续负载状态；不得随猫端牵引 bActive 或一次松线目标清零。 */
	FVector SmoothedRodFishPullStrengthMeters = FVector::ZeroVector;
	TWeakObjectPtr<APawn> AuthoritativeAimHolder;
	FCatFishingRodRotationEffortSnapshot AuthoritativeRotationEffort;
	/** 20 Hz 权威目标在本机角色移动帧中的平滑速度；只属于瞬态表现/移动接缝，不复制。 */
	FVector SmoothedCarrierPullVelocity = FVector::ZeroVector;
	double SmoothedCarrierAwaySpeedMultiplier = 1.0;
	TWeakObjectPtr<APawn> SmoothedConstraintHolder;
	TWeakObjectPtr<UCharacterMovementComponent> CarrierConstraintTickDependency;
	double NextCarrierSmoothingDiagnosticWorldSeconds = 0.0;
	bool bLastCarrierSmoothingDiagnosticActive = false;
	double NextRodRotationResistanceDiagnosticWorldSeconds = 0.0;
	bool bLastRodTorqueBalanced = false;
	bool bHeldAimInitialized = false;
	/** 根据操作位编号计算本地站位；非法编号回退到基准 Stand，避免上层拿到 NaN 或随机位置。 */
	FTransform ResolveOperatorStandLocalTransform(int32 SlotIndex) const;
	/** 身份是否已经完成权威初始化；为真后 Actor/Item/Owner 身份不可再改。 */
	bool bIdentityInitialized = false;
	/** BeginPlay 前是否积压了一次表现变化；用于延迟蓝图通知而不丢掉状态跳变。 */
	bool bHasPendingPresentationNotification = false;
	/** BeginPlay 前积压变化的最早前值；蓝图收到时仍能看到一次完整 Previous → Current。 */
	FCatFishingRodPresentationState PendingPreviousPresentationState;
	/** BeginPlay 前积压变化的最新当前值；多次变化会合并成最后状态再分发。 */
	FCatFishingRodPresentationState PendingCurrentPresentationState;
};
