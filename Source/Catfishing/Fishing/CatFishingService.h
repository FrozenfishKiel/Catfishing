#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Fishing/CatFishingTypes.h"
#include "Fishing/Integration/CatFishingCommandTypes.h"
#include "CatFishingService.generated.h"

class ACatCharacter;
class ACatFishingRodActor;
class ACatFishingSession;
class APlayerState;
class UCatFishDefinition;
class UCatEquipmentComponent;
class ACatFishingResourceCustodian;
class FCatFishingServiceRodBoundSessionRoutingTest;

/** 一局服务器 Fishing 入口；创建/查询/终止会话并把所有阶段写入留给会话内 StateTree。 */
UCLASS()
class CATFISHING_API UCatFishingService : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 每人场上合计最多两根实体竿；手持和损坏但尚未收回的竿也占名额。 */
	static constexpr int32 MaximumDeployedRodsPerPlayer = 2;

	/** 只在 authority Game World 创建服务；客户端通过复制 Session 观察。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** World 销毁时先终止所有未结算会话，再清弱映射。 */
	virtual void Deinitialize() override;

	/** 从当前 Run/Environment、水域和统一参战能力快照抽取鱼种与重量，并为该身份建立唯一 StateTree 会话；巨鱼成功后才附带广播可选 Social 提示。 */
	FCatBeginCastResult BeginCast(AController* FisherController, const FCatBeginCastCommand& Command);
	FCatFishingCommandResult PlaceRod(AController* Controller, const FCatPlaceRodCommand& Command);
	FCatFishingCommandResult OperateRod(AController* Controller, const FCatOperateRodCommand& Command);
	FCatFishingCommandResult LeaveRod(AController* Controller, const FCatLeaveRodCommand& Command);
	FCatFishingCommandResult PackRod(AController* Controller, const FCatPackRodCommand& Command);

	/** 旧协作协议转到指定会话，再统一走 OperateRod 的距离、资格与容量校验。 */
	FCatDomainCommandResult SubmitFightAssist(FGuid FishingSessionId, AController* AssistingController,
		FGuid RequestId, int64 ExpectedRevision);

	/** 把 NearShore 抢抄意图转给指定会话；服务不自己创建鱼或选择胜者。 */
	FCatScoopResult RequestScoop(FGuid FishingSessionId, AController* ScoopingController, const FCatScoopCommand& Command);

	/** Character 失去占有、倒地或销毁时撤销本人主控；其它抓握不取得会话。 */
	void ReleaseFishingOperatorForCharacter(const ACatCharacter* Character);
	/** 原角色装备真正销毁前转存精确场上竿/预约饵；保留原物资归属，不复制普通背包。 */
	bool PreserveFishingResourcesForEquipmentShutdown(UCatEquipmentComponent* Equipment);
	/** Runner 完成一次冻结参与者结算后处理不能丢弃的身体失效通知。 */
	void FlushDeferredOperatorRemovalsFromAuthority();

	/**
	 * Run 启动失败或进入结束阶段时终止当前会话、释放全部竿位并恢复角色移动；夜晚不调用。
	 * 该入口不永久关闭 World 内的 FishingService，下一天仍可重新使用已部署鱼竿。
	 */
	void SuspendFishingAndReleaseOperators();
	/** Run 更新新咬钩准入后刷新等待计时，不结束真咬窗口或搏斗、不释放竿位。 */
	void RefreshBiteAvailabilityFromAuthority();

	/** Host teardown 关闭入口并终止所有未结算会话。 */
	void CloseCommandsAndTerminateAll();

	/** 查询指定存活且未终态的服务器 Session；未知或失效身份返回空且不创建索引项。 */
	ACatFishingSession* FindSession(FGuid FishingSessionId);

	/** 按 Controller 当前占据的主操作位查询该鱼竿上的活动 Session；离开竿位后不再把旧会话路由给玩家输入。 */
	bool TryGetActiveSessionForController(const AController* Controller, FGuid& OutFishingSessionId,
		FCatFishingSessionSnapshot& OutSnapshot);

	/** 只读查询该玩家任意一根存活登记竿；不表示当前操作或收纳目标，业务命令须按 RodActorId 解析。 */
	ACatFishingRodActor* FindDeployedRod(const APlayerState* PlayerState);
	/** 统计本人场上实体竿；无人值守竿仍占本人名额，助手抓握不改变名额。 */
	int32 GetDeployedRodCount(const APlayerState* PlayerState) const;
	/** 本人范围内最近的无人操作、无活动会话部署竿；损坏竿也能收回。跨玩家收纳尚未开放。 */
	ACatFishingRodActor* FindNearestPackableRod(const APlayerState* PlayerState,
		const FVector& WorldLocation, double MaxDistanceCentimeters);

	/** 本人范围内最近的无人操作、未损坏部署竿；允许原活动会话继续。 */
	ACatFishingRodActor* FindNearestOperableOwnedRod(const APlayerState* PlayerState,
		const FVector& WorldLocation, double MaxDistanceCentimeters);

	/** 按公开 RodActorId 查询；取得主控仍只允许竿主显式 R。 */
	ACatFishingRodActor* FindDeployedRodById(FGuid RodActorId);

	/** 查询 PlayerState 当前显式主控的本人竿；没有则空。 */
	ACatFishingRodActor* FindRodOperatedBy(const APlayerState* PlayerState);

	/** 最近的无人值守活动会话鱼竿；供原持竿者/竿主在不先拾起时主动切线止损。 */
	ACatFishingRodActor* FindNearestUnattendedSessionRod(const FVector& WorldLocation,
		double MaxDistanceCentimeters);

	/** 查找绑定在指定竿上的存活未终态会话（操作位与会话解耦后，竿是会话的空间锚）；没有则空。 */
	ACatFishingSession* FindActiveSessionByRod(const ACatFishingRodActor* RodActor) const;
	/** Only validates or revokes explicit owner control. Physical helpers never become Session members. */
	bool ReconcilePrimaryControlFromPhysicalGrip(ACatFishingRodActor* Rod);

	/** 抄网目标粗筛：按鱼与请求者的水平距离找最近的已上钩会话；精确范围仍由 Session 裁决。 */
	ACatFishingSession* FindNearestScoopableSession(const FVector& WorldLocation, double MaxDistanceCentimeters);

	/**
	 * 原物品主人取回操控：会话唯一性属于鱼竿，主控始终只能是其 Owner；
	 * 这里只调用会话 ResumeOwnerControlFromAuthority 恢复本人钓手事实。
	 */
	bool ResumeOwnedSessionControl(ACatFishingSession* Session, AController* NewFisherController);

	/** 为 PlayerState 登记部署竿；同一 Actor 重放成功，超过两根或跨玩家重复登记被拒绝。 */
	bool RegisterDeployedRod(APlayerState* PlayerState, ACatFishingRodActor* RodActor);

	/** 仅当当前登记值精确匹配 ExpectedRodActor 时注销，避免旧 Actor 迟到回调删除替代鱼竿。 */
	void UnregisterDeployedRod(const APlayerState* PlayerState, const ACatFishingRodActor* ExpectedRodActor);

	/** 仅统计当前存活且未终态的 Session，不暴露服务器索引。 */
	int32 GetTrackedSessionCountForDiagnostics() const;

	/** 仅统计 key/value 都存活的已部署鱼竿弱索引；诊断只看数量，不暴露服务内部表。 */
	int32 GetDeployedRodCountForDiagnostics() const;

private:
	friend class FCatFishingBiteTimingWorldTest;
	friend class FCatFishingPhysicalGripGraphTest;
	friend class FCatFishingPhysicalCouplingTest;
	friend class FCatFishingCMCStabilityTest;
	friend class FCatFishingFormalPhysicalRunnerTest;
	friend class FCatFishBehaviorStateTreeRuntimeTest;
	friend class FCatFishingSlackAimCommandRoutingTest;
	friend class ACatFishingSession;
	friend class FCatFishingServiceRodBoundSessionRoutingTest;

	/** 清除已销毁或已终态 Session 弱引用；活动会话由其绑定鱼竿定位，不维护玩家唯一槽位。 */
	void CompactSessions();

	/** 清除失效的竿登记；原 PlayerState 已离场但精确竿资源仍在托管时保留同一 RodActorId 定位。 */
	void CompactDeployedRods();

	/** 终止全部存活会话并释放所有竿位；DiagnosticReason 只进入 Session 终态诊断。 */
	void TerminateAllSessionsAndReleaseOperators(const TCHAR* DiagnosticReason);

	/** 正常放下与异常失效共用的主控撤销事务；会话进入无人值守。 */
	bool RemoveOperatorAndReconcileSession(ACatFishingRodActor* Rod, APlayerState* PlayerState,
		int64 ExpectedRevision, const ACatCharacter* LeavingCharacter, const TCHAR* Reason);

	/** 清空所有存活鱼竿的操作槽；鱼竿仍保持部署并切到地面姿态。 */
	void ReleaseAllRodOperators();

	/** 清空单根鱼竿的操作槽；用于窗口关闭和鱼竿异常注销的同一补偿路径。 */
	void ReleaseRodOperators(ACatFishingRodActor* Rod);

	/** 从 Controller 的 APlayerState::UniqueId 读取服务器私有身份；无效身份不能进入开始终态缓存。 */
	static FString ResolveStableNetId(const AController* Controller);

	/** 新 Fishing 写口的身体 gate；要求请求者仍拥有当前 Character 且未倒地，防止绕过 CommandComponent 的调用继续放竿、接竿或抛竿。 */
	static bool CanControllerStartFishingAction(const AController* Controller);

	/** 用同一服务器谓词解析单个战斗参与者；必须是 Active Controller/当前 Character、未倒地且两项独立能力都为正有限值。 */
	static bool TryGetFightCapability(const AController* Controller, FString& OutStableNetId,
		ACatCharacter*& OutCharacter, double& OutFishingStrength, double& OutFightStamina);

	/** 从当前所有服务器 Controller 汇总合法参与者人数、力量与搏斗体力；任一依赖缺失时输出保持零。 */
	void BuildFightCapabilitySnapshot(int32& OutParticipantCount, double& OutFishingStrength,
		double& OutFightStamina) const;

	/** FishingSessionId 到服务器 Actor 弱引用；Actor/StateTree 自己持有阶段真相。 */
	TMap<FGuid, TWeakObjectPtr<ACatFishingSession>> Sessions;

	/** 身份+开始操作+RequestId 到首次同步结果；成功重试复用原 SessionId，失败重试不重新抽鱼。 */
	TMap<FString, FCatBeginCastResult> BeginCastTerminalCache;
	TSet<FString> BeginCastInProgress;

	/** PlayerState 到其场上实体竿的多值弱索引；所有权不随操作手变化，不强持 Actor。 */
	TMultiMap<TWeakObjectPtr<APlayerState>, TWeakObjectPtr<ACatFishingRodActor>> DeployedRodsByPlayerState;

	/** 已离场原宿主的精确竿实例结算入口；RodActorId 仍由原部署登记唯一定位。 */
	TMap<FGuid, TWeakObjectPtr<UCatEquipmentComponent>> PreservedRodEquipment;
	UPROPERTY(Transient)
	TArray<TObjectPtr<ACatFishingResourceCustodian>> ResourceCustodians;
	struct FDeferredOperatorRemoval
	{
		FGuid RodActorId;
		TWeakObjectPtr<APlayerState> PlayerState;
		TWeakObjectPtr<ACatCharacter> Character;
	};
	TArray<FDeferredOperatorRemoval> DeferredOperatorRemovals;
	TSet<TWeakObjectPtr<ACatFishingRodActor>> DeferredPrimaryControlChecks;

	/** teardown 后永久拒绝本 World 新会话。 */
	bool bCommandsOpen = true;
};
