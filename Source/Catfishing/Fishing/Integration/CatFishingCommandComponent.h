#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Fishing/Integration/CatFishingCommandTypes.h"
#include "Fishing/Integration/CatFishingRodAimState.h"
#include "CatFishingCommandComponent.generated.h"

class UCatInventoryComponent;
struct FCatInventoryItemUseContext;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FCatFishingCommandResultReceived,
	const FCatFishingCommandResult&, Result);

/** 单个玩家的服务器冷却闸门；只保存下一次允许时间，不参与客户端表现或网络复制。 */
struct CATFISHING_API FCatFishingCooldownGate
{
	bool TryConsume(double NowSeconds, double DurationSeconds, double& OutRemainingSeconds);
	void Reset() { NextAllowedServerTime = 0.0; }

private:
	double NextAllowedServerTime = 0.0;
};

USTRUCT(BlueprintType)
struct FCatFishingInputEdge
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FGuid RequestId;

	UPROPERTY(BlueprintReadOnly)
	FGuid ActivationCorrelationId;

	UPROPERTY(BlueprintReadOnly)
	int64 InputSequence = 0;
	/** 收线/放线按键绑定产生时看到的操竿权；换主后的迟到包不能获得新权限。 */
	UPROPERTY() FGuid ControlRodActorId;
	UPROPERTY() uint32 ControlEpoch = 0;
	/** 松开时采集的鼠标/镜头输入，服务器验证后自行与水面求交。 */
	UPROPERTY() bool bHasCastViewRay = false;
	UPROPERTY() FVector CastViewOrigin = FVector::ZeroVector;
	UPROPERTY() FVector CastViewDirection = FVector::ZeroVector;
	/** 右键按下时的输入累计量；不携带任何客户端权威竿角。 */
	UPROPERTY() FCatFishingRodAimSample RodAimSample;
	UPROPERTY() TObjectPtr<AActor> FishingTarget = nullptr;
};

UCLASS(ClassGroup=(Catfishing), meta=(BlueprintSpawnableComponent))
class CATFISHING_API UCatFishingCommandComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCatFishingCommandComponent();
	/** 选中鱼竿的库存 Use 进入权威放竿事务；只接受组件所属 Controller 和指定实例，失败不会改写装备选择。 */
	FCatDomainCommandResult PlaceRodFromInventoryUseOnAuthority(APlayerController* RequestingController,
		const FCatPlaceRodCommand& Command);
	/** 选中窝料的持续 Use 开始入口；保存本次库存槽位和实例身份，后续 End 只能结算这同一份物品。 */
	FCatDomainCommandResult BeginChumUseFromInventoryOnAuthority(APlayerController* RequestingController,
		const FCatInventoryItemUseContext& UseContext, FGuid ChumItemInstanceId, FName ChumDefinitionId);
	/** 选中窝料的持续 Use 结束入口；取消只清理已固定的状态，正常结束才沿用既有蓄力弹道与扣量服务。 */
	FCatDomainCommandResult EndChumUseFromInventoryOnAuthority(APlayerController* RequestingController,
		const FCatInventoryItemUseContext& UseContext, bool bCancelled);
	/** 选中抄网的库存 Use 复用原 RequestScoop 命令，只在同步分派期间携带指定实例给 Session 权威复核。 */
	FCatDomainCommandResult ScoopFromInventoryUseOnAuthority(APlayerController* RequestingController,
		const FCatInventoryItemUseContext& UseContext, FGuid ScoopItemInstanceId);
	void DeliverResultFromAuthority(const FCatFishingCommandResult& Result);
	void DeliverBeginCastResultFromAuthority(const FCatBeginCastResult& Result);
	void DeliverPlaceChumResultFromAuthority(const FCatPlaceChumResult& Result);

	UFUNCTION(BlueprintCallable, Category="Catfishing|Chum")
	void SubmitPlaceChum(const FCatPlaceChumCommand& Command);
	UFUNCTION(BlueprintCallable, Category="Catfishing|Fishing") void SubmitBeginCast(const FCatBeginCastCommand& Command);
	UFUNCTION(BlueprintCallable, Category="Catfishing|Fishing") void SubmitPlaceRod(const FCatPlaceRodCommand& Command);
	UFUNCTION(BlueprintCallable, Category="Catfishing|Fishing") void SubmitOperateRod(const FCatOperateRodCommand& Command);
	UFUNCTION(BlueprintCallable, Category="Catfishing|Fishing") void SubmitLeaveRod(const FCatLeaveRodCommand& Command);
	UFUNCTION(BlueprintCallable, Category="Catfishing|Fishing") void SubmitPackRod(const FCatPackRodCommand& Command);
	UFUNCTION(BlueprintCallable, Category="Catfishing|Fishing")
	bool TryGetBeginCastResult(FGuid RequestId, FCatBeginCastResult& OutResult) const;

	UFUNCTION(BlueprintCallable, Category="Catfishing|Chum")
	bool TryGetPlaceChumResult(FGuid RequestId, FCatPlaceChumResult& OutResult) const;

	UFUNCTION(BlueprintCallable)
	bool TryGetResult(FGuid RequestId, FCatFishingCommandResult& OutResult) const;

	UFUNCTION(BlueprintCallable)
	void ConsumeResult(FGuid RequestId);

	void ResetTransientCommandState();
	/** 本地窝料 Use 表现边沿写入或清除预览开始时间；不发送 RPC、不影响服务器蓄力。 */
	void SetChumUsePreviewActiveLocally(bool bActive);
	/** 放下或取回本人竿时清持续按键并保留递增序号，取回后需要新的按键边沿。 */
	void ClearHeldFightInputForControlTransferFromAuthority();
	/** Focus/menu/body exit cancels pending aims and held effort without casting or cutting an existing line. */
	void ClearHeldInputForLifecycle(FName Reason);
	/**
	 * 读取服务器最后确认的连续搏斗输入。该状态属于玩家输入生命周期，不属于某个 FishingSession；
	 * 新 Runner 用它恢复跨断线边界仍真实按住的按键，避免必须松开再按一次。
	 */
	bool TryGetHeldFightInputStateFromAuthority(bool& OutPrimaryHeld, bool& OutSlackHeld,
		int64& OutInputSequence) const;
	FCatFishingInputEdge SubmitRodInteract();
	/**
	 * 换人握手输入（多人钓鱼附篇 §2.4）。同一个键在两种身份下含义不同，服务器按当时身份分派：
	 * 主钓手按＝发起或取消换人请求；岸上替补按＝接手最近一根挂着请求的竿（要过体力门槛）。
	 * 键位随装备栏重构另定，现按 E 设计；本入口不关心是哪个键，只负责把意图发出去。
	 */
	FCatFishingInputEdge SubmitFishingHandoff();
	FCatFishingInputEdge SubmitPrimaryPressed();
	FCatFishingInputEdge SubmitPrimaryReleased();
	FCatFishingInputEdge SubmitSlackPressed();
	FCatFishingInputEdge SubmitSlackReleased();
	/** 由本地 Controller::UpdateRotation 每帧提交一次已缩放的鼠标增量；组件自身不 Tick。 */
	void UpdateLocalRodAimInput(double DeltaSeconds, const FRotator& LookDeltaDegrees);
	/** 输入失焦或生命周期退出时撤掉主动转杆；不回绕累计量、样本序号或鼠标段序号。 */
	void StopLocalRodAimInput();
	FCatFishingInputEdge SubmitChumPressed();
	FCatFishingInputEdge SubmitChumReleased();
	FCatFishingInputEdge SubmitCancel();
	FCatFishingInputEdge SubmitCancelReleased();
	/** 显式切线入口；现有取消键也会在可切线阶段由服务器改派到同一命令。 */
	FCatFishingInputEdge SubmitCutLine();
	FCatFishingInputEdge SubmitScoop();
	FCatFishingInputEdge SubmitChum();

	UPROPERTY(BlueprintAssignable)
	FCatFishingCommandResultReceived OnResultReceived;

private:
	friend class FCatFishingGroupNetworkTest;
	friend class FCatFishingSlackAimCommandRoutingTest;
	friend class FCatFishingCommandComponentHeldFightInputTest;
	UFUNCTION(Client, Reliable)
	void ClientReceiveFishingCommandResult(const FCatFishingCommandResult& Result);

	UFUNCTION(Client, Reliable)
	void ClientReceivePlaceChumResult(const FCatPlaceChumResult& Result);
	UFUNCTION(Client, Reliable) void ClientReceiveBeginCastResult(const FCatBeginCastResult& Result);

	UFUNCTION(Server, Reliable)
	void ServerSubmitPlaceChum(const FCatPlaceChumCommand& Command);
	UFUNCTION(Server, Reliable) void ServerSubmitBeginCast(const FCatBeginCastCommand& Command);
	UFUNCTION(Server, Reliable) void ServerSubmitPlaceRod(const FCatPlaceRodCommand& Command);
	UFUNCTION(Server, Reliable) void ServerSubmitOperateRod(const FCatOperateRodCommand& Command);
	UFUNCTION(Server, Reliable) void ServerSubmitLeaveRod(const FCatLeaveRodCommand& Command);
	UFUNCTION(Server, Reliable) void ServerSubmitPackRod(const FCatPackRodCommand& Command);

	UFUNCTION(Server, Reliable)
	void ServerSubmitFishingAbilityCommand(ECatFishingCommandType CommandType, FCatFishingInputEdge Edge);
	UFUNCTION(Server, Reliable)
	void ServerClearHeldInputForLifecycle(FName Reason, FCatFishingInputEdge Edge);
	UFUNCTION(Client, Reliable)
	void ClientReceiveHeldInputCleared(FName Reason, int64 InputSequence, bool bAccepted);
	UFUNCTION(Server, Unreliable)
	void ServerSubmitRodAimSample(FCatFishingRodAimSample Sample);
	/** 鼠标启停绕过30Hz节流；仍与普通快照共用同一服务器序号裁决。 */
	UFUNCTION(Server, Reliable)
	void ServerSubmitRodAimTransition(FCatFishingRodAimSample Sample);

	static constexpr int32 MaxStoredResults = 32;

	bool IsSupportedOwner() const;
	/** 在路由到具体 Session 前先记录按下/松开事实；即使当前无会话或玩法 gate 关闭，Release 也必须能清掉旧状态。 */
	void TrackHeldFightInputFromAuthority(ECatFishingCommandType CommandType,
		const FCatFishingInputEdge& Edge);
	void ReceiveResultLocally(const FCatFishingCommandResult& Result);
	FCatFishingInputEdge MakeDiscreteEdge();
	FCatFishingRodAimSample MakeRodAimSample(const class ACatFishingRodActor* Rod);
	void SendLocalRodAimSample(const FCatFishingRodAimSample& Sample, bool bTransition);
	void HandleRodAimSampleFromAuthority(const FCatFishingRodAimSample& Sample, bool bTransition);
	void DispatchAbilityCommand(ECatFishingCommandType CommandType, const FCatFishingInputEdge& Edge);
	/** 权威侧统一处理 Ability 输入命令；抄网会搜索已上钩目标并交给 Session 完成嘴叼世界鱼交接。 */
	void HandleAbilityCommandFromAuthority(ECatFishingCommandType CommandType, const FCatFishingInputEdge& Edge,
		FGuid RequestedScoopItemInstanceId = FGuid());
	/**
	 * 权威侧广播一次性表现事件；Character 按标签决定是否跳过已经由 Ability 预测过的本地动作。
	 * 只用于"失败时不留任何权威痕迹"的动作；有复制状态可读的动作走各自的表现事件，走这条会播两遍。
	 * 调用点必须在语义已经确定之后——左键按下有瞄准/提竿/收线三种含义，不能在分派前统一广播。
	 */
	void BroadcastCosmeticEventFromAuthority(const FGameplayTag& EventTag) const;
	/** 验证松开时采集的镜头/鼠标射线并与水面求交；所有 ID/Revision/Handle 由服务器填。 */
	void BeginCastFromViewOnAuthority(APlayerController* Controller, const FCatFishingInputEdge& Edge);
	/** 服务器按已固定的窝料实例和按住时长投放；结束时重读原槽位，拒绝换物或移动后的迟到释放。 */
	void ThrowChumFromChargeOnAuthority(APlayerController* Controller, const FCatInventoryItemUseContext& UseContext,
		FGuid ChumItemInstanceId, FName ChumDefinitionId, double HeldSeconds);
	/** 清空权威持续窝料会话的全部固定身份；End 先取必要快照再调用它，生命周期路径直接清理避免新 Use 卡在旧 Phase。 */
	void ClearChumUseState();
	/** 服务器记录的选中窝料 Use 开始时刻（世界时间）；<0 表示当前未蓄力。 */
	double ChumChargeStartServerTime = -1.0;

	/** 当前持续窝料 Use 的请求身份；开始阶段写入，End/Cancel 必须完全匹配它才能清理或结算。 */
	FGuid ActiveChumUseRequestId;

	/** 当前持续窝料 Use 固定的物品实例；防止松开时扫描背包并扣除另一堆同类窝料。 */
	FGuid ActiveChumItemInstanceId;

	/** 当前持续窝料 Use 固定的定义身份；End 时与原槽位重读结果比对，防止槽位换物后继续投放。 */
	FName ActiveChumDefinitionId = NAME_None;

	/** 当前持续窝料 Use 的来源库存；只接受角色个人背包仍是同一组件的结束请求。 */
	TWeakObjectPtr<UCatInventoryComponent> ActiveChumSourceInventory;

	/** 当前持续窝料 Use 的来源槽位；End 在该格复查实例、定义与数量，不自动寻找替代物。 */
	int32 ActiveChumInventorySlotIndex = INDEX_NONE;


	/**
	 * 本地记录的选中窝料持续输入开始时刻（世界时间）；<0 表示当前未蓄力。
	 * 与 ChumChargeStartServerTime 分开的理由：后者只在 HandleAbilityCommandFromAuthority 里写，
	 * 远端客户端本地那一份永远是 -1，拿它画预览会变成"只有主机看得见"。
	 * 这一份在本地提交边沿时就写好，纯表现用途，不参与任何裁决（实际蓄力时长仍以服务器那份为准）。
	 */
	double LocalChumChargeStartTime = -1.0;

	/** 服务器记录的"本次左键按住=瞄准抛竿"关联 ID；只有同一次按住的松开才触发抛竿，防止提竿失败后的松开误抛。 */
	FGuid ServerAimingCorrelationId;

	/**
	 * 服务器最后确认的物理按键状态。它不随单场 Session 终止而清除：断线时仍按住右键，下一场仍应保持线杯解锁；
	 * 只有对应 Release 或真正的输入生命周期重置（换角色/旅行）才能清除。
	 */
	bool bServerPrimaryHeld = false;
	bool bServerSlackHeld = false;
	int64 LastServerHeldInputSequence = 0;

	// 输入采样序号与累计量只在 Controller 构造时归零。ResetTransientCommandState 不得回绕，
	// 否则同一根竿尚未退出时新的采样会被误判为旧包。
	int64 NextRodAimSequence = 0;
	FVector2D CumulativeRodLookDegrees = FVector2D::ZeroVector;
	int64 MouseStrokeSequence = 0;
	FVector2D MouseStrokeStartLookDegrees = FVector2D::ZeroVector;
	FGuid LocalMouseAimRodActorId;
	uint32 LocalMouseAimEpoch = 0;
	bool bLocalMouseActive = false;
	TWeakObjectPtr<const class ACatFishingRodActor> LocalPitchAimRod;
	uint32 LocalPitchAimEpoch = 0;
	double LocalRequestedRodPitch = 0.0;
	bool bLocalPitchAimInitialized = false;
	bool bLocalSlackHeld = false;
	double RodAimSendElapsedSeconds = 0.0;
	double NextLocalRodAimDiagnosticSeconds = 0.0;
	double NextServerRodAimDiagnosticSeconds = 0.0;

	/** 每个 PlayerController 独立的抄网权威冷却；目标鱼/Session 切换不会绕过。 */
	FCatFishingCooldownGate ScoopCooldownGate;
	bool bResolvingCatch = false;
	TSet<FGuid> PendingScoopRequests;
	TMap<FGuid, FCatFishingCommandResult> ScoopResults;

public:
	/** 调试可视化只读：当前 Q 蓄力起始世界时间；<0 表示未蓄力。仅在权威端有效。 */
	double GetChumChargeStartServerTime() const { return ChumChargeStartServerTime; }

	/** 表现只读：本地 Q 蓄力起始世界时间；<0 表示未蓄力。主机与客户端都有效，预览线用这个。 */
	double GetLocalChumChargeStartTime() const { return LocalChumChargeStartTime; }
	void ReceivePlaceChumResultLocally(const FCatPlaceChumResult& Result);
	void ReceiveBeginCastResultLocally(const FCatBeginCastResult& Result);

	TMap<FGuid, FCatFishingCommandResult> ResultsByRequestId;
	TArray<FGuid> ResultOrder;
	FGuid PrimaryActivationCorrelationId;
	int64 NextInputSequence = 0;
	TMap<FGuid, FCatPlaceChumResult> PlaceChumResultsByRequestId;
	TArray<FGuid> PlaceChumResultOrder;
	TMap<FGuid, FCatBeginCastResult> BeginCastResultsByRequestId;
	TArray<FGuid> BeginCastResultOrder;
};
