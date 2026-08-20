#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "Fishing/CatFishingTypes.h"
#include "Integration/Fishing/CatFishingOperationJournal.h"
#include "Integration/Fishing/CatFishingFightCursorLedger.h"

#include "CatFishingBoundarySubsystem.generated.h"

class ACatCharacter;

/**
 * Fishing 的 typed Boundary 适配入口；它不推进 Fishing phase，也不拥有玩法真相。
 * 当前它有两类入口：Start/Cast 是只读采集与 encounter 冻结，已经接在生产路径上；
 * Bait/Fight/Commit/Seal 另成一套平行协议，其中 Bait 带真实写副作用（扣特殊饵），其余目前只被测试驱动。
 * 不要把它当成"只读协调器"而在这里随手加写入，也不要以为 Bait 没有副作用。
 */
UCLASS()
class CATFISHING_API UCatFishingBoundarySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 只在 authority Game World 创建 Boundary；客户端不能持有并行 Journal 或冻结 encounter。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** World 释放时清空 run-local Journal 和结果缓存；本原子不承诺进程消失后的 durable recovery。 */
	virtual void Deinitialize() override;

	/** 采集可进入 PreCast 的 Start 上下文；成功不抽鱼、不查水域、不创建 Session。 */
	FCatFishingBoundaryStartResult Start(AController* FisherController, FGuid RequestId);

	/** 在 Fishing 接受 Cast 后冻结 EncounterSpec；成功后同 RequestId 重放返回同一份规格。 */
	FCatFishingBoundaryCastResult CastAccepted(AController* FisherController, const FCatFishingCastAcceptedRequest& Request);

	/** 在有效 Bite 后接收 bait 语义；只有已接受 Start 的 Attempt 才能继续进入资源写入或语义 Receipt 阶段。 */
	FCatFishingBaitResult BaitAccepted(const FCatFishingBiteAcceptedRequest& Request);

#if WITH_DEV_AUTOMATION_TESTS
	/** 自动化测试注入已接受 Start 上下文；只用于绕开完整 Controller/ASC/Run fixture 来验证后续 Boundary 原子。 */
	void AddAcceptedStartContextForAutomation(const FCatFishingStartContext& Context);
#endif

	/** 接收一帧 HookedFight 资源交换意图；只有已接受 Start 的 Attempt 才能进入 cursor 顺序账。 */
	FCatFishingFightResult FightAccepted(const FCatFishingFightExchangeRequest& Request);

	/**
	 * 提交 Fight 资源 writer 已完成的结果；成功时为原 Operation 发行稳定 FightResourcesApplied Receipt。
	 * 目前没有任何生产调用方，也没有测试直接调它（测试走 Ledger）：搭斗那一轮已把“接通还是暂撤”登记为 D-22 本轮不接，
	 * 所以它暂时保留。先确认那条裁决再动它，不要当成无主死代码顺手删。
	 */
	FCatFishingFightResult CommitFightResourcesApplied(const FCatFishingOperationKey& Operation,
		int64 DomainRevision, bool bRodBroken);

	/**
	 * 封存 Attempt 的最终 Fight cursor；封存后更大的 Fight cursor 都不能再进入资源写入。
	 * 与上面那个一样属于 D-22 本轮不接的平行协议，当前零生产调用。
	 */
	FCatFishingBoundaryResultHeader SealFinalFightCursor(const FCatFishingFinalFightCursor& FinalCursor, FGuid RequestId);

	/** 关闭 Attempt 的新 Boundary operation 入口；已接受的 Start/Cast 结果仍可被重放读取。 */
	void CloseAttempt(const FCatFishingAttemptId& AttemptId);

	/**
	 * 全项目唯一的“这个玩家现在能不能参与搜斗”判定；成功时同时输出服务器身份、当前角色和两项正有限能力值。
	 * Start/Cast 的 encounter 决策和既有 Session 的 Fight/Scoop 阶段必须用同一份实现：此前 Service 里另有一份
	 * 逐行相同的副本，只改一边会让两条链对同一个玩家给出不同结论，而构建和测试都不会报错。
	 * 任一层不过（GameMode active gate、Pawn、Condition 未倒地、ASC、力量与体力为正有限）都返回 false 并先清空全部输出。
	 */
	static bool TryGetFightCapability(const AController* Controller, FString& OutStableNetId,
		ACatCharacter*& OutCharacter, double& OutFishingStrength, double& OutFightStamina);

private:
	/** 从当前 World 遍历合法参战者并生成 Cast 时刻能力快照；任何依赖缺失都保持零输出。 */
	void BuildFightCapabilitySnapshot(int32& OutParticipantCount, double& OutFishingStrength,
		double& OutFightStamina) const;

	/** 为 Start operation 构造或复用 Attempt；同身份同 RequestId 重放必须拿回同一个 AttemptId。 */
	FCatFishingAttemptId FindOrCreateAttemptId(const FString& StableNetId, FGuid RequestId);

	/** 生成 Start 的私有缓存键；它只服务 run-local Attempt 复用，不进入 public contract。 */
	static FString MakeStartRequestCacheKey(const FString& StableNetId, FGuid RequestId);

	/** 把 BaitDefinition、BiteToken 与是否消耗特殊饵编码成 Bait PayloadHash 输入；RequestId 不进入语义载荷。 */
	static TArray<uint8> BuildBaitPayload(const FCatFishingBiteAcceptedRequest& Request);

	/** 把 RequestId 和 Cast 位置编码成 Cast PayloadHash 输入；该 payload 只描述 Cast 冻结时刻的业务语义。 */
	static TArray<uint8> BuildCastPayload(const FCatFishingCastAcceptedRequest& Request);

	/** 把 Fight cursor 与资源消耗量编码成 PayloadHash 输入；RequestId 不进入语义载荷，避免重试污染顺序帧。 */
	static TArray<uint8> BuildFightPayload(const FCatFishingFightExchangeRequest& Request);

	/** 用 Boundary 错误构造 Start 拒绝结果；拒绝不会创建 OperationId 或 AttemptId。 */
	static FCatFishingBoundaryStartResult RejectStart(FGuid RequestId, ECatFishingBoundaryError Error);

	/** 用 Boundary 错误构造 Cast 拒绝结果；拒绝不会冻结 EncounterSpec。 */
	static FCatFishingBoundaryCastResult RejectCast(const FCatFishingCastAcceptedRequest& Request,
		ECatFishingBoundaryError Error);

	/** 用 Boundary 错误构造 Bait 拒绝结果；拒绝不会消费特殊饵，也不会伪造普通饵 Receipt。 */
	static FCatFishingBaitResult RejectBait(const FCatFishingBiteAcceptedRequest& Request,
		ECatFishingBoundaryError Error);

	/** 用 Boundary 错误构造 Fight 拒绝结果；拒绝不会扣参战者体力、扣鱼体力或伪造资源 Receipt。 */
	static FCatFishingFightResult RejectFight(const FCatFishingFightExchangeRequest& Request,
		ECatFishingBoundaryError Error);

	/** Run-local Operation Journal；它统一处理 Start/Cast 的幂等、重放和 PayloadMismatch。 */
	FCatFishingOperationJournal Journal;

	/** Bait OperationKey 到首次结果；普通饵和扣库存成功的特殊饵都会写进来，重放必须返回同一份 Receipt。 */
	TMap<FString, FCatFishingBaitResult> BaitResultByOperation;

	/** Fight cursor 的 run-local 顺序账；它只管顺序、重放和资源 Receipt，不直接写 GAS 或 Equipment。 */
	FCatFishingFightCursorLedger FightCursorLedger;

	/** 身份+Start RequestId 到 AttemptId 的 run-local 映射；它让 Start 重放不会生成第二个 Attempt。 */
	TMap<FString, FCatFishingAttemptId> AttemptByStartRequestKey;

	/** AttemptId 到 Start 上下文；Cast 用它确认 Attempt 来自已接受 Start。 */
	TMap<FGuid, FCatFishingStartContext> StartContextByAttempt;

	/** Start OperationKey 到 Context；Journal 重放时用它返回首次上下文。 */
	TMap<FString, FCatFishingStartContext> StartContextByOperation;

	/** Cast OperationKey 到 EncounterSpec；Journal 重放时用它返回首次冻结规格。 */
	TMap<FString, FCatFishingEncounterSpec> EncounterSpecByOperation;
};

