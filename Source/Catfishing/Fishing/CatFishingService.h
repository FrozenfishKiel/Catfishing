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

/** 一局服务器 Fishing 入口；创建/查询/终止会话并把所有阶段写入留给会话内 StateTree。 */
UCLASS()
class CATFISHING_API UCatFishingService : public UWorldSubsystem
{
	GENERATED_BODY()

public:
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

	/** 把巨鱼搏斗协作意图转给指定会话；会话用统一谓词拒绝非 Active、倒地、无当前 Character 或力量/体力非正的请求者。 */
	FCatDomainCommandResult SubmitFightAssist(FGuid FishingSessionId, AController* AssistingController,
		FGuid RequestId, int64 ExpectedRevision);

	/** 把 NearShore 抢抄意图转给指定会话；服务不自己创建鱼或选择胜者。 */
	FCatScoopResult RequestScoop(FGuid FishingSessionId, AController* ScoopingController, const FCatScoopCommand& Command);

	/** Character 失去占有、倒地或销毁时终止所有相关未结算会话；不恢复旧半场。 */
	void TerminateSessionsForCharacter(const ACatCharacter* Character);

	/** Host teardown 关闭入口并终止所有未结算会话。 */
	void CloseCommandsAndTerminateAll();

	/** 查询指定存活且未终态的服务器 Session；未知或失效身份返回空且不创建索引项。 */
	ACatFishingSession* FindSession(FGuid FishingSessionId);

	/** 从 Controller 的服务器私有身份查询其唯一活动 Session，并只复制公开 Snapshot 到输出。 */
	bool TryGetActiveSessionForController(const AController* Controller, FGuid& OutFishingSessionId,
		FCatFishingSessionSnapshot& OutSnapshot);

	/** 查询 PlayerState 当前登记的存活部署鱼竿；未知身份返回空。 */
	ACatFishingRodActor* FindDeployedRod(const APlayerState* PlayerState);

	/** 按公开 RodActorId 在全部部署鱼竿中查找（多人：允许操作别人的竿）；未知返回空。 */
	ACatFishingRodActor* FindDeployedRodById(FGuid RodActorId);

	/** 查询 PlayerState 当前占用任意操作槽的竿（不限竿主、主辅位）；没有则空。 */
	ACatFishingRodActor* FindRodOperatedBy(const APlayerState* PlayerState);

	/** 最近的可加入竿：已部署、未损坏、仍有空槽，且下一个槽位在 MaxDistance 内；不限竿主。 */
	ACatFishingRodActor* FindNearestOperableRod(const FVector& WorldLocation, double MaxDistanceCentimeters);

	/** 查找绑定在指定竿上的存活未终态会话（操作位与会话解耦后，竿是会话的空间锚）；没有则空。 */
	ACatFishingSession* FindActiveSessionByRod(const ACatFishingRodActor* RodActor);

	/** 抢抄目标搜索：按鱼与请求者的水平距离找最近的 NearShore 阶段会话；没有则空。 */
	ACatFishingSession* FindNearestScoopableSession(const FVector& WorldLocation, double MaxDistanceCentimeters);

	/**
	 * 钓手接力转移编排（多人用别人的竿继续钓）：校验新钓手无自己的活跃会话（单活跃槽位），
	 * 调用会话 TransferFisherFromAuthority，成功后同步更新服务器正反索引。
	 */
	bool TransferSessionFisher(ACatFishingSession* Session, AController* NewFisherController);

	/** 为 PlayerState 登记唯一部署鱼竿；相同 Actor 重放成功，不同存活 Actor 被拒绝。 */
	bool RegisterDeployedRod(APlayerState* PlayerState, ACatFishingRodActor* RodActor);

	/** 仅当当前登记值精确匹配 ExpectedRodActor 时注销，避免旧 Actor 迟到回调删除替代鱼竿。 */
	void UnregisterDeployedRod(const APlayerState* PlayerState, const ACatFishingRodActor* ExpectedRodActor);

	/** 仅统计当前存活且未终态的 Session，不暴露服务器索引。 */
	int32 GetTrackedSessionCountForDiagnostics() const;

	/** 仅统计 key/value 都存活的鱼竿登记，不暴露服务器 Registry。 */
	int32 GetDeployedRodCountForDiagnostics() const;

private:
	friend class ACatFishingSession;

	/** 清除已销毁或已终态 Session 弱引用，并同时释放对应钓手的单活跃槽位。 */
	void CompactSessions();

	/** 清除 PlayerState 或 Rod Actor 任一端已经失效的部署登记。 */
	void CompactDeployedRods();

	/** 从 Controller 的 APlayerState::UniqueId 读取服务器私有身份；无效身份不能进入开始终态缓存。 */
	static FString ResolveStableNetId(const AController* Controller);

	/** 用同一服务器谓词解析单个战斗参与者；必须是 Active Controller/当前 Character、未倒地且两项独立能力都为正有限值。 */
	static bool TryGetFightCapability(const AController* Controller, FString& OutStableNetId,
		ACatCharacter*& OutCharacter, double& OutFishingStrength, double& OutFightStamina);

	/** 从当前所有服务器 Controller 汇总合法参与者人数、力量与搏斗体力；任一依赖缺失时输出保持零。 */
	void BuildFightCapabilitySnapshot(int32& OutParticipantCount, double& OutFishingStrength,
		double& OutFightStamina) const;

	/** FishingSessionId 到服务器 Actor 弱引用；Actor/StateTree 自己持有阶段真相。 */
	TMap<FGuid, TWeakObjectPtr<ACatFishingSession>> Sessions;

	/** 会话 ID 到初始钓手私有身份；服务压缩终态时据此精确释放单活跃索引。 */
	TMap<FGuid, FString> SessionFisherById;

	/** 钓手私有身份到当前唯一非终态会话；同一玩家不能并行抽取第二条鱼。 */
	TMap<FString, FGuid> ActiveSessionByFisher;

	/** 身份+开始操作+RequestId 到首次同步结果；成功重试复用原 SessionId，失败重试不重新抽鱼。 */
	TMap<FString, FCatBeginCastResult> BeginCastTerminalCache;
	TSet<FString> BeginCastInProgress;

	/** PlayerState 到其当前唯一部署鱼竿的服务器弱 Registry；不强持 Actor，也不扫描 World 重建。 */
	TMap<TWeakObjectPtr<APlayerState>, TWeakObjectPtr<ACatFishingRodActor>> DeployedRodByPlayerState;

	/** teardown 后永久拒绝本 World 新会话。 */
	bool bCommandsOpen = true;
};
