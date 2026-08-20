#pragma once

#include "CoreMinimal.h"

#include "Fishing/CatFishingTypes.h"
#include "Integration/Fishing/CatFishingOperationJournal.h"

/** Fight cursor 的 run-local 顺序账；它只约束 Attempt 内搏斗资源帧，不持有 GAS 或 Equipment writer。 */
class CATFISHING_API FCatFishingFightCursorLedger
{
public:
	/** 接受或重放一次 Fight cursor；Last+1 才能首次接受，同 cursor 同 hash 返回首次 Operation，同 cursor 异 hash 拒绝。 */
	FCatFishingBoundaryResultHeader AcceptOrReplay(
		const FCatFishingFightExchangeRequest& Request,
		const FCatFishingPayloadHash& PayloadHash);

	/** 提交 Fight 资源写入完成结果；首次提交发行 Receipt，重复提交返回首次 Receipt 且不改写 RodBroken。 */
	FCatFishingFightResult CommitResourcesApplied(
		const FCatFishingOperationKey& Operation,
		int64 DomainRevision,
		bool bRodBroken);

	/** 封存 Attempt 的最终 Fight cursor；成功后更大的 cursor 都不能再进入资源写入阶段。 */
	FCatFishingBoundaryResultHeader SealFinalCursor(
		const FCatFishingFinalFightCursor& FinalCursor,
		FGuid RequestId);

	/** 关闭 Attempt 的新 Fight cursor；已经接受的 cursor 仍可按原记录重放，新 cursor 交给 Journal 拒绝。 */
	void CloseAttempt(const FCatFishingAttemptId& AttemptId);

private:
	/** 单个 cursor 已接受的 operation 事实；同 cursor 重放只返回这份结果，不重新分配 OperationId。 */
	struct FCursorRecord
	{
		/** 首次接受该 cursor 时的 canonical payload；后续同 cursor 必须逐字节一致。 */
		FCatFishingPayloadHash PayloadHash;

		/** 首次接受该 cursor 时的 Boundary 结果头；重放时只把 bReplay 改为 true。 */
		FCatFishingBoundaryResultHeader Result;
	};

	/** 一个 Attempt 的 Fight 顺序状态；LastCursor 与 FinalCursor 共同决定还能否接受新帧。 */
	struct FAttemptState
	{
		/** 已首次接受的最大 cursor；初始为 0，所以第一帧必须是 1。 */
		int64 LastCursor = 0;

		/** 是否已经封存最终 cursor；封存后 Capture 侧可以安全拒绝补帧。 */
		bool bFinalCursorSealed = false;

		/** 已封存的最终 cursor；只有 bFinalCursorSealed 为 true 时有意义。 */
		int64 FinalCursor = 0;

		/** cursor 到首次结果的索引；用于同 cursor 重放和 PayloadMismatch 判定。 */
		TMap<int64, FCursorRecord> RecordsByCursor;

		/** Final cursor seal 的首次结果；重复 seal 返回它而不是创建新 Operation。 */
		FCatFishingBoundaryResultHeader FinalSealResult;
	};

	/** 用错误构造无副作用拒绝头；调用方提供 Attempt/Revision/Hash 方便日志和测试定位。 */
	static FCatFishingBoundaryResultHeader Reject(
		const FCatFishingAttemptId& AttemptId,
		const FCatFishingPayloadHash& PayloadHash,
		int64 Revision,
		ECatFishingBoundaryError Error);

	/** 构造 Fight Journal request；Principal 固定来自 request，避免 cursor ledger 绕过身份漂移检查。 */
	static FCatFishingJournalRequest MakeFightJournalRequest(
		const FCatFishingFightExchangeRequest& Request,
		const FCatFishingPayloadHash& PayloadHash);

	/** 为 Final cursor seal 构造稳定 payload hash；seal 不携带资源消耗，只携带最终 cursor。 */
	static FCatFishingPayloadHash MakeFinalCursorPayloadHash(const FCatFishingFinalFightCursor& FinalCursor);

	/** Fight operation 的通用 Journal；它负责 OperationId、RequestId 重放和 Inbox 稳定性。 */
	FCatFishingOperationJournal Journal;

	/** 已提交资源写入的 Fight result；重复资源提交必须返回首次 Receipt 而不是重新发行。 */
	TMap<FString, FCatFishingFightResult> FightResultByOperationKey;

	/** AttemptId 到 Fight cursor 状态；状态只在当前 World/run 生命周期内保留。 */
	TMap<FGuid, FAttemptState> Attempts;
};
