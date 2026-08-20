#pragma once

#include "CoreMinimal.h"

#include "Framework/Core/CatFishingBoundaryContracts.h"

/** Journal 接受请求所需的最小输入；真实 Chum/Capture stage 会在后续原子追加私有恢复字段。 */
struct FCatFishingJournalRequest
{
	/** 当前 operation 的 typed 方法种类；它参与幂等键和 PayloadHash。 */
	ECatFishingBoundaryOperationKind OperationKind = ECatFishingBoundaryOperationKind::Start;

	/** Boundary public request header；Journal 只读取 Attempt、RequestId、Principal、Revision，不推进 Fishing phase。 */
	FCatFishingBoundaryRequestHeader Header;

	/** 已按 canonical 规则计算好的业务载荷 Hash；Journal 用它比较重放语义，不需要理解具体 payload 字段。 */
	FCatFishingPayloadHash PayloadHash;
};

/** Run-local Operation Journal 与 Result Inbox；它集中处理幂等、PayloadMismatch 和 Poll 可见结果。 */
class FCatFishingOperationJournal
{
public:
	/** 接受新请求或返回首次缓存；同 Attempt、OperationKind 和 RequestId 的身份漂移或语义变化会稳定拒绝且不分配新 OperationId。 */
	FCatFishingBoundaryResultHeader AcceptOrReplay(const FCatFishingJournalRequest& Request);

	/** 将同一 Operation 的 Pending Inbox 写成第一个合法终态；未知 Operation、非法错误组合、Pending 降级和终态二次覆盖都不写入。 */
	bool CommitResult(const FCatFishingOperationKey& Operation, const FCatFishingBoundaryResultHeader& Result);

	/** 按原 OperationKey 读取 Inbox；Poll 不创建新请求，也不接受新的业务 payload。 */
	bool TryPoll(const FCatFishingOperationKey& Operation, FCatFishingBoundaryResultHeader& OutResult) const;

	/** 关闭 Attempt 的新请求入口；已接受的 operation 仍保留给后续 Poll 或 drain。 */
	void CloseAttempt(const FCatFishingAttemptId& AttemptId);

private:
	/** 单条 Journal 记录；私有 stage 后续会加在这里，不进入 public Boundary contract。 */
	struct FEntry
	{
		/** 请求所属 operation kind；用于调试和后续 typed result 分派。 */
		ECatFishingBoundaryOperationKind OperationKind = ECatFishingBoundaryOperationKind::Start;

		/** 首次接受时冻结的请求头；重放只和这份头做幂等比较。 */
		FCatFishingBoundaryRequestHeader Header;

		/** 首次接受时冻结的 canonical payload hash。 */
		FCatFishingPayloadHash PayloadHash;

		/** Result Inbox 当前对外可见结果。 */
		FCatFishingBoundaryResultHeader Result;
	};

	/** 生成幂等键；Principal 与 PayloadHash 不进入 key，才能检测同 RequestId 的身份漂移和语义变化。 */
	static FString MakeRequestKey(const FCatFishingJournalRequest& Request);

	/** 查询 Attempt 的新写入口是否关闭；Close 只阻止新 operation，保留旧 Inbox 供不可逆记录继续 Poll/drain。 */
	bool IsAttemptClosed(const FCatFishingAttemptId& AttemptId) const;

	/** 为 accepted request 生成 operation id；当前 run-local 实现只要求同进程唯一和非空。 */
	static FCatFishingOperationId MakeOperationId();

	/** 幂等键到 Journal 记录；同键重放必须落回同一条记录。 */
	TMap<FString, FEntry> EntriesByRequestKey;

	/** OperationKey 到幂等键的反查索引；Poll 通过它找到 Inbox entry。 */
	TMap<FString, FString> RequestKeyByOperationKey;

	/** 已关闭 Attempt 集合；CloseAttempt 后新 operation 返回 AttemptClosed。 */
	TSet<FGuid> ClosedAttempts;
};

