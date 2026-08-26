#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Fishing/Integration/CatFishingCommandTypes.h"
#include "CatFishingCommandComponent.generated.h"

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
};

UCLASS(ClassGroup=(Catfishing), meta=(BlueprintSpawnableComponent))
class CATFISHING_API UCatFishingCommandComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCatFishingCommandComponent();
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
	FCatFishingInputEdge SubmitRodInteract();
	FCatFishingInputEdge SubmitPrimaryPressed();
	FCatFishingInputEdge SubmitPrimaryReleased();
	FCatFishingInputEdge SubmitSlackPressed();
	FCatFishingInputEdge SubmitSlackReleased();
	FCatFishingInputEdge SubmitChumPressed();
	FCatFishingInputEdge SubmitChumReleased();
	FCatFishingInputEdge SubmitCancel();
	FCatFishingInputEdge SubmitScoop();
	FCatFishingInputEdge SubmitChum();
	void ForwardLegacyAssist(FGuid FishingSessionId, FGuid RequestId, int64 ExpectedRevision);
	void ForwardLegacyScoop(FGuid FishingSessionId, FCatScoopCommand Command);

	UPROPERTY(BlueprintAssignable)
	FCatFishingCommandResultReceived OnResultReceived;

private:
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

	static constexpr int32 MaxStoredResults = 32;

	bool IsSupportedOwner() const;
	void ReceiveResultLocally(const FCatFishingCommandResult& Result);
	FCatFishingInputEdge MakeDiscreteEdge();
	void DispatchAbilityCommand(ECatFishingCommandType CommandType, const FCatFishingInputEdge& Edge);
	void HandleAbilityCommandFromAuthority(ECatFishingCommandType CommandType, const FCatFishingInputEdge& Edge);
	/**
	 * 权威侧广播一次性表现事件；Character 按标签决定是否跳过已经由 Ability 预测过的本地动作。
	 * 只用于"失败时不留任何权威痕迹"的动作；有复制状态可读的动作走各自的表现事件，走这条会播两遍。
	 * 调用点必须在语义已经确定之后——左键按下有瞄准/提竿/收线三种含义，不能在分派前统一广播。
	 */
	void BroadcastCosmeticEventFromAuthority(const FGameplayTag& EventTag) const;
	/** 服务器按 Controller 视线射线∩水面重建抛竿命令（规格 3.1：点哪落哪、无蓄力）；所有 ID/Revision/Handle 由服务器填。 */
	void BeginCastFromViewOnAuthority(APlayerController* Controller, const FGuid& RequestId);
	/** 服务器按 Q 按住时长换算蓄力并投放窝料（规格 3.1 打窝：蓄力抛掷）；落点用与客户端预览相同的弹道预测。 */
	void ThrowChumFromChargeOnAuthority(APlayerController* Controller, const FGuid& RequestId, double HeldSeconds);
	/** 服务器记录的 Q 按下时刻（世界时间）；<0 表示当前未蓄力。 */
	double ChumChargeStartServerTime = -1.0;

	/**
	 * 本地记录的 Q 按下时刻（世界时间）；<0 表示当前未蓄力。
	 * 与 ChumChargeStartServerTime 分开的理由：后者只在 HandleAbilityCommandFromAuthority 里写，
	 * 远端客户端本地那一份永远是 -1，拿它画预览会变成"只有主机看得见"。
	 * 这一份在本地提交边沿时就写好，纯表现用途，不参与任何裁决（实际蓄力时长仍以服务器那份为准）。
	 */
	double LocalChumChargeStartTime = -1.0;

	/** 服务器记录的"本次左键按住=瞄准抛竿"关联 ID；只有同一次按住的松开才触发抛竿，防止提竿失败后的松开误抛。 */
	FGuid ServerAimingCorrelationId;

	/** 每个 PlayerController 独立的抄网权威冷却；目标鱼/Session 切换不会绕过。 */
	FCatFishingCooldownGate ScoopCooldownGate;

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
