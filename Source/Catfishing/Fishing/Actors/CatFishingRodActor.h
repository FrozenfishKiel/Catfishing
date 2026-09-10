#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Fishing/Actors/CatFishingActorTypes.h"
#include "Fishing/Integration/CatFishingRodAimState.h"
#include "Fishing/Simulation/CatFishingRodResistanceModel.h"
#include "CatFishingRodActor.generated.h"

class APlayerState;
class USceneComponent;
class UCharacterMovementComponent;
class UCatRodBendComponent;
class UBoxComponent;
class UPrimitiveComponent;
class ACatCharacter;
class UCatFishingPhysicalRodComponent;

/** Read-only control observation; no predicted pose or second physics integration. */
struct CATFISHING_API FCatFishingRodControlObservation
{
	FRotator ActualAim = FRotator::ZeroRotator;
	FRotator RequestedAim = FRotator::ZeroRotator;
	FVector AngularVelocityRadiansPerSecond = FVector::ZeroVector;
	bool bMouseDriveActive = false;
	bool bWaitingForNewHolder = false;
};

/** 鱼线负载与鼠标域的复制观察。旧运动字段仅保留尚未确认的 Blueprint 序列化引用。 */
USTRUCT(BlueprintType)
struct CATFISHING_API FCatFishingCarrierConstraintState
{
	GENERATED_BODY()

	/** 将受力快照绑定到当时的持有人，拒绝与换人复制乱序的旧快照。 */
	UPROPERTY()
	TObjectPtr<APlayerState> ConstraintHolderPlayerState;
	UPROPERTY() uint32 RosterVersion = 0;
	UPROPERTY() uint32 ControlEpoch = 0;

	UPROPERTY(BlueprintReadOnly)
	FVector_NetQuantizeNormal PullDirection = FVector::ZeroVector;
	/** 废弃的运动指令，当前恒零，物理接收方不读取。 */
	UPROPERTY(BlueprintReadOnly, meta=(DeprecatedProperty, DeprecationMessage="Physical body owns motion; this legacy acceleration is always zero"))
	float PullAccelerationCentimetersPerSecondSquared = 0.0f;
	/** 废弃的运动指令，当前恒零。 */
	UPROPERTY(BlueprintReadOnly, meta=(DeprecatedProperty, DeprecationMessage="Physical body owns motion; legacy braking is always zero"))
	float PullBrakingDecelerationCentimetersPerSecondSquared = 0.0f;
	/** 废弃的 CMC 开关，当前恒 false。 */
	UPROPERTY(BlueprintReadOnly, meta=(DeprecatedProperty, DeprecationMessage="Physical constraints own traction; this legacy switch is always false"))
	bool bUseContinuousTraction = false;
	/** 废弃的速度目标，当前恒零。 */
	UPROPERTY(BlueprintReadOnly, meta=(DeprecatedProperty, DeprecationMessage="Observe physical body velocity; this legacy target is always zero"))
	float TargetPullSpeedCentimetersPerSecond = 0.0f;
	/** 旧蓝图载荷兼容，恒为 1；新移动不读取这个硬限速字段。 */
	UPROPERTY(BlueprintReadOnly)
	float MaximumAwaySpeedMultiplier = 1.0f;
	UPROPERTY(BlueprintReadOnly)
	float NormalizedTension = 0.0f;
	UPROPERTY(BlueprintReadOnly)
	float ConstraintErrorCentimeters = 0.0f;
	/** 当前搏斗是否要求鱼竿使用受力后的实际姿态，而不是瞬时跟随控制器。 */
	UPROPERTY(BlueprintReadOnly)
	bool bFightActive = false;
	/** 开始搏斗/换主生成新域；停止后保留末次域作为拒绝旧组快照的边界。 */
	UPROPERTY()
	uint32 AimInputEpoch = 0;
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
	friend class UCatFishingPhysicalRodComponent;
	friend class UCatFishingService;
	friend class FCatFishingActorIdentityContractTest;
	friend class FCatFishingServiceOwnerControlLookupTest;
	friend class FCatFishingParticipantStrengthTest;
	friend class FCatFishBehaviorStateTreeRuntimeTest;
	friend class FCatFishingSlackAimNetworkTest;
	friend class FCatFishingOperatorRunnerIntegrationTest;
	friend class FCatFishingPhysicalGripGraphTest;
	friend class FCatFishingPhysicalCouplingTest;
	friend class FCatFishingFormalPhysicalRunnerTest;
	friend class FCatFishingRodEffortSnapshotLifecycleTest;
	friend class FCatFishingOwnedRodLifecycleTest;
	friend class FCatHUDFishingOwnerBindingTest;
	friend class FCatFishingSlackAimCommandRoutingTest;
	friend class FCatPhysicalInputRouteTest;
	friend class FCatHUDPhysicalGrabProjectionTest;

	friend class FCatFishingServiceRodOperationsPreserveMovementTest;
	friend class FCatFishingHeldFacingFollowsControlRotationTest;
	friend class FCatFishingServiceRodBoundSessionRoutingTest;
	friend class FCatFishingFirstPersonCameraTest;
	friend class FCatBrokenRodPackCapacityTest;
	friend class FCatFishingMotionDiagnosticTest;


public:
	/** 创建鱼竿表现 Actor 的组件和默认复制姿态；身份和锚点仍要等服务器初始化后才可信。 */
	ACatFishingRodActor();
	UBoxComponent* GetPhysicalRodBody() const { return PhysicsBody; }
	UCatFishingPhysicalRodComponent* GetPhysicalRodComponent() const { return PhysicalRod; }
	bool IsUsingPhysicalRod() const;
	bool BeginPhysicalHoldFromAuthority(APlayerState* Player, bool bPositionNewRod = false);
	void ReleasePhysicalPrimaryHoldFromAuthority(APlayerState* Player, FName Reason);
	void RefreshPrimaryControlFromAuthority();
	virtual void Tick(float DeltaSeconds) override;
	/** 注册鱼竿表现状态复制；客户端只读 PresentationState，并通过 OnRep 驱动蓝图表现刷新。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** 初始化这根场景鱼竿的权威身份；ActorId 负责场景对象，ItemInstanceId 负责回到库存里的同一件物品。 */
	bool InitializeAuthoritativeIdentity(FGuid InRodActorId, FGuid InItemInstanceId, FName InRodDefinitionId,
		FName InRodSkinDefinitionId, APlayerState* InOwnerPlayerState, APlayerState* InOperatorPlayerState,
		bool bInDeployed, bool bInBroken);
	/** 写入这根竿的权威本地锚点；必须在身份初始化前完成，之后蓝图和钓鱼逻辑都从这些锚点取世界坐标。 */
	bool ConfigureCanonicalAnchorsFromAuthority(const FTransform& InRodTip, const FTransform& InStand, const FTransform& InGrip);
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
	/** 读取当前占用操作位的玩家数量；表现和交互只把它当只读计数。 */
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") int32 GetOperatorCount() const;
	/** 查询某个玩家当前占用的操作位编号；未加入或空玩家返回 INDEX_NONE。 */
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") int32 GetOperatorSlotIndex(APlayerState* PlayerState) const;
	/** 判断玩家是否是当前主操作位；兼容旧单人逻辑读取 OperatorPlayerState 的场景。 */
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") bool IsPrimaryOperator(APlayerState* PlayerState) const;
	uint32 GetOperatorMembershipEpoch(APlayerState* PlayerState) const;
	uint32 GetControlEpoch() const { return PresentationState.ControlEpoch; }
	uint32 GetRosterVersion() const { return PresentationState.RosterVersion; }
	/** 读取握持点世界坐标；角色手部 IK 和竿体表现用它对齐。 */
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") FTransform GetGripWorldTransform() const;
	/** 服务器读取实际物理竿姿态并更新只读锚点，不覆盖物理身体位置。 */
	bool RefreshHeldTransformFromAuthority(double DeltaSeconds = 0.0);
	bool GetControlObservationFromAuthority(FCatFishingRodControlObservation& OutObservation) const;
	bool CanRebaseHeldAimFromAuthority(APlayerState* Player, const FCatFishingRodAimSample& Sample) const;
	void RebaseHeldAimFromAuthority(APlayerState* Player, const FCatFishingRodAimSample& Sample,
		FGuid RequestId, int64 InputSequence);
	bool AcceptHeldAimSampleFromAuthority(APlayerState* Player, const FCatFishingRodAimSample& Sample);
	void StopHeldAimInputFromAuthority(APlayerState* Player);
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") FVector GetAuthoritativeRodForwardVector() const;
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") FVector GetAuthoritativeRodTipVelocity() const;
	UFUNCTION(BlueprintPure, Category="Fishing|Rod") FVector GetAuthoritativeHolderVelocity() const
	{
		return AuthoritativeHolderVelocity;
	}
	/** 仅服务器积分写入；固定步消费累计差值，不能把同一渲染帧的努力重复结算。 */
	const FCatFishingRodRotationEffortSnapshot& GetAuthoritativeRotationEffortSnapshot() const
	{
		return AuthoritativeRotationEffort;
	}
	/** 历史反射约束状态的观察投影；旧移动载荷只发布零值，真实线力由 PhysicalRod 单独接收。 */
	bool SetFightConstraintObservationFromAuthority(const FVector& PullDirection,
		double NormalizedTension, double ConstraintErrorCentimeters, bool bFightActive = false,
		double MaximumFishTorqueStrengthMeters = 0.0, double CatTorqueCapacityStrengthMeters = 0.0,
		const FVector& RodPullAxis = FVector::ForwardVector);
	void ClearFightConstraintAndLoadFromAuthority(FGuid SessionId = FGuid());
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
	/** The Service alone commits a roster projected from authority grip connectivity. */
	bool SetPrimaryOperatorFromAuthority(APlayerState* PlayerOrNull, int64 ExpectedRevision);
	/** 客户端收到表现状态复制后的入口；Previous 由引擎提供，用来让蓝图比较前后变化。 */
	UFUNCTION()
	void OnRep_PresentationState(const FCatFishingRodPresentationState& Previous);
	UFUNCTION()
	void OnRep_CarrierConstraintState();
	UFUNCTION()
	void OnRep_GripCanonicalLocalTransform();
	/** 分发表现变化或延迟到 BeginPlay 后再分发；保证蓝图事件只在组件可用时触发。 */
	void QueueOrDispatchPresentationChanged(const FCatFishingRodPresentationState& Previous, const FCatFishingRodPresentationState& Current);
	/** 立即应用皮肤、隐藏状态和蓝图通知；服务器与客户端各自在本地执行这一层表现副作用。 */
	void DispatchPresentationChanged(const FCatFishingRodPresentationState& Previous, const FCatFishingRodPresentationState& Current);
	/** 初始化与图投影变更共用成员元数据；不改变任何身体或竿姿态。 */
	void PrepareOperatorMemberships(FCatFishingRodPresentationState& Next);
	void ResetAuthoritativeRotationEffort();
	/** 提交一次权威可变状态；它保留 Actor/Item/Owner 身份，只允许操作位、皮肤、部署和断竿状态变化。 */
	bool CommitAuthoritativeMutation(const FCatFishingRodPresentationState& Next, int64 ExpectedRevision);
	/** 鱼竿 Actor 的场景根节点；所有可视锚点跟随它接受 Actor Transform。 */
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> SceneRoot;
	/** Detached on authority when simulating; SceneRoot observes it and never drives it. */
	UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> PhysicsBody;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCatFishingPhysicalRodComponent> PhysicalRod;
	/** 美术表现根节点；皮肤和特效挂在这里，不参与权威锚点计算。 */
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> VisualRoot;
	/** Local deformation of the existing art; never changes canonical anchors or the fight simulation. */
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCatRodBendComponent> RodBend;
	/** 竿尖的本地锚点组件；鱼线和浮漂表现从它换算世界坐标。 */
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> RodTipAnchor;
	/** BP_Rod/TestMap 已序列化的历史站位标定组件，不参与主控或身体移动。 */
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> StandAnchor;
	/** 已序列化的历史右参考组件；资产迁移前保留名字与姿态，无玩法读取。 */
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> RightStandAnchor;
	/** 已序列化的历史左参考组件；资产迁移前保留名字与姿态，无玩法读取。 */
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> LeftStandAnchor;
	/** 握持点的本地锚点组件；角色手部 IK 和竿体视觉对齐会读取它。 */
	UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> GripAnchor;
	/** 鱼竿 Actor 的唯一复制表现事实；服务器写入，客户端通过 OnRep 转成蓝图视觉事件。 */
	UPROPERTY(ReplicatedUsing=OnRep_PresentationState, VisibleInstanceOnly, BlueprintReadOnly, meta=(AllowPrivateAccess="true"))
	FCatFishingRodPresentationState PresentationState;
	UPROPERTY(ReplicatedUsing=OnRep_CarrierConstraintState, VisibleInstanceOnly, BlueprintReadOnly, meta=(AllowPrivateAccess="true"))
	FCatFishingCarrierConstraintState CarrierConstraintState;
	uint32 NextMembershipEpoch = 0;
	/** 竿尖权威本地 Transform；配置后不再读蓝图组件作为数据源，避免表现改动反向污染玩法坐标。 */
	UPROPERTY(Replicated) FTransform RodTipCanonicalLocalTransform = FTransform::Identity;
	/** 装备提供的历史站位标定，只保持已序列化参考组件数据。 */
	UPROPERTY(Replicated) FTransform StandCanonicalLocalTransform = FTransform::Identity;
	/** 不变握把标定随初始复制发送；客户端相机必须组合它与实际 Actor 姿态。 */
	UPROPERTY(ReplicatedUsing=OnRep_GripCanonicalLocalTransform)
	FTransform GripCanonicalLocalTransform = FTransform::Identity;
	FVector AuthoritativeRodTipVelocity = FVector::ZeroVector;
	FVector AuthoritativeHolderVelocity = FVector::ZeroVector;
	FRotator AuthoritativeHeldAimRotation = FRotator::ZeroRotator;
	FCatFishingRodAimState HeldAimInput;
	uint32 NextAimInputEpoch = 0;
	TWeakObjectPtr<APawn> AuthoritativeAimHolder;
	FCatFishingRodRotationEffortSnapshot AuthoritativeRotationEffort;
	double LastConstraintUpdateWorldSeconds = -1.0;
	double NextCarrierReceiptDiagnosticWorldSeconds = 0.0;
	bool bLastReceivedFightActive = false;
	bool bHeldAimInitialized = false;
	/** 换主保留实际竿向，直到新主在新输入域提交首个有效采样。 */
	bool bAwaitingNewHolderAim = false;
	/** 身份是否已经完成权威初始化；为真后 Actor/Item/Owner 身份不可再改。 */
	bool bIdentityInitialized = false;
	/** BeginPlay 前是否积压了一次表现变化；用于延迟蓝图通知而不丢掉状态跳变。 */
	bool bHasPendingPresentationNotification = false;
	/** BeginPlay 前积压变化的最早前值；蓝图收到时仍能看到一次完整 Previous → Current。 */
	FCatFishingRodPresentationState PendingPreviousPresentationState;
	/** BeginPlay 前积压变化的最新当前值；多次变化会合并成最后状态再分发。 */
	FCatFishingRodPresentationState PendingCurrentPresentationState;
};
