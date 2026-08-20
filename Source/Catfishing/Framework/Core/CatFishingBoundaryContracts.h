#pragma once

#include "CoreMinimal.h"
#include "CatFishingBoundaryContracts.generated.h"

/** Boundary 请求在外围协调器里的稳定状态；它描述事务处理状态，不代表 Fishing 玩法终态。 */
UENUM(BlueprintType)
enum class ECatFishingBoundaryDisposition : uint8
{
	/** 请求未被接受；外围保证没有业务副作用。 */
	Rejected,
	/** 请求已被接受；调用方只能用原 OperationKey 读取或推进同一条恢复记录。 */
	Pending,
	/** 本次 operation 要求的外围 Receipt 已经齐备。 */
	Committed
};

/** Boundary 合同错误；这些错误只说明请求或系统协调失败，不能被当作鱼逃脱、鱼竿断裂等玩法结果。 */
UENUM(BlueprintType)
enum class ECatFishingBoundaryError : uint8
{
	/** 请求没有错误，通常只和 Pending 或 Committed 同时出现。 */
	None,
	/** Schema 版本不受当前运行时支持时必须 fail-closed，避免旧客户端按过期字段解释新合同。 */
	UnsupportedSchema,
	/** Attempt 缺失、格式无效或不属于当前运行时；已关闭但格式有效的 Attempt 使用 AttemptClosed。 */
	InvalidAttempt,
	/** 服务器无法从 authority PlayerState 得到可信身份。 */
	InvalidIdentity,
	/** 请求字段缺失、格式无效或不满足当前 operation 的基本前置条件。 */
	InvalidRequest,
	/** 同一个幂等键携带了不同业务载荷，必须稳定拒绝而不是创建第二次副作用。 */
	PayloadMismatch,
	/** ExpectedRevision 落后于领域当前版本；服务拒绝写入，让调用方刷新快照后用新 RequestId 再提交。 */
	RevisionConflict,
	/** operation 顺序不符合 Attempt 或 Cursor 合同。 */
	InvalidOrder,
	/** 产品、配置或策略尚未裁决，生产路径必须 fail-closed。 */
	PolicyUndecided,
	/** 生产侧领域 writer、World、Data 或配置暂时不可用。 */
	DependencyUnavailable,
	/** authority 身份与目标资源权限不匹配；该错误阻止客户端把自己伪装成实物或记录归属者。 */
	PermissionDenied,
	/** 容器、资源或预算容量不足，外围没有写入事实。 */
	CapacityExceeded,
	/** Fight cursor 跳过了必须按序提交的中间帧。 */
	CursorGap,
	/** Attempt 已经关闭，不接受新的 operation。 */
	AttemptClosed,
	/** Attempt 或目标聚合已经有唯一终态，本次请求不能再次裁决。 */
	AlreadySettled,
	/** Poll 指向的 Operation 在当前 Inbox 中不存在。 */
	OperationNotFound,
	/** 终态结果已经过了有界保留窗口，调用方必须重新读取公开状态。 */
	ResultExpired,
	/** 请求在不可逆提交点之前被取消。 */
	CancelledBeforeCommit,
	/** 请求在不可逆提交点之前超时；越过不可逆点后不能使用这个错误回滚。 */
	TimedOutBeforeCommit
};

/** Boundary public 方法的稳定种类；OperationKind 是 Hash 和幂等键的一部分。 */
UENUM(BlueprintType)
enum class ECatFishingBoundaryOperationKind : uint8
{
	/** 只读外围事实，构造 PreCast 所需 StartContext。 */
	Start,
	/** 在 Fishing 接受 Cast 后冻结 EncounterSpec。 */
	Cast,
	/** 在有效 Bite 后消费或确认一次 bait 语义。 */
	Bait,
	/** 协调 Equipment reserve、Environment deposit 与 Equipment confirm。 */
	Chum,
	/** 协调 GAS/Equipment 的搏斗资源写入与 Cursor 封存。 */
	Fight,
	/** 提交 Capture、Escape 或 RodBroken 等外围 terminal intent。 */
	Terminal,
	/** 读取或推进同一 Pending operation 的 Result Inbox。 */
	Poll,
	/** 关闭 Attempt 的新 operation，并 drain 已越过不可逆点的记录。 */
	Close
};

/**
 * 外围 Receipt 的稳定种类；Receipt 的意义是"某个领域确实写成功了"。
 * Receipt 的实际发行动作由协调器完成（Bait 在 Boundary、FightResourcesApplied 在 Ledger），
 * 但只能在对应领域 writer 真的提交成功之后发；写失败或根本没调用 writer 时不得凭空造一张。
 */
UENUM(BlueprintType)
enum class ECatFishingReceiptKind : uint8
{
	/** Bait 语义已经被 Equipment 或无限普通饵规则接受。 */
	BaitAccepted,
	/** Equipment 已经预留待投放窝料。 */
	EquipmentReserved,
	/** Environment 已经完成不可逆的窝料落水写入。 */
	EnvironmentDeposited,
	/** Equipment 已经确认窝料消耗。 */
	EquipmentConsumptionConfirmed,
	/** GAS 与 Equipment 已经接受一次 Fight 资源写入。 */
	FightResourcesApplied,
	/** Fight 的最终 Cursor 已经封存，Capture 之后不能继续提交搏斗帧。 */
	FightCursorSealed,
	/** Capture winner CAS 已经裁决唯一实物获得者。 */
	CaptureWinnerClaimed,
	/** Items 已经公开 FishInstance；此后不得删除鱼来回滚。 */
	FishMaterialized,
	/** Collection/Profile Grant 已经入队，允许后续前向重投。 */
	CollectionEnqueued
};

/**
 * Fishing Attempt 的稳定身份，一次 Start 到终态的整段生命周期都用它串起来。
 * 它由 Boundary 在接受 Start 时生成（FindOrCreateAttemptId），同身份同 RequestId 重放拿回同一个；
 * Fishing 侧从不自己造 AttemptId，只从 EncounterSpec 里拿到以后往下传。
 */
USTRUCT(BlueprintType)
struct FCatFishingAttemptId
{
	GENERATED_BODY()

	/** Attempt 的 opaque GUID；空值表示调用方还没有合法进入 PreCast 生命周期。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid Value;
};

/** 一次语义意图的幂等身份；它只负责重放归并与 PayloadMismatch 检测，陈旧写保护必须看 Revision。 */
USTRUCT(BlueprintType)
struct FCatFishingRequestId
{
	GENERATED_BODY()

	/** 幂等请求 GUID；同 Attempt 和 OperationKind 下命中同一请求槽，Principal 漂移由 Journal 单独拒绝。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid Value;
};

/** Boundary 接受请求后生成的恢复键；所有 Receipt 和 Poll 都绑定它。 */
USTRUCT(BlueprintType)
struct FCatFishingOperationId
{
	GENERATED_BODY()

	/** 单个 Boundary operation 的 opaque GUID；只有接受请求后才有效。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid Value;
};

/** 领域 writer 发行的提交证明；它只能证明某个外围事实已写入，不能推进 Fishing phase。 */
USTRUCT(BlueprintType)
struct FCatFishingReceiptId
{
	GENERATED_BODY()

	/** Receipt 的 opaque GUID；同 OperationId 与 ReceiptKind 重放必须返回同一个值。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid Value;
};

/** 服务器从 PlayerState::UniqueId 规范化得到的身份键；客户端载荷中的同名值不可信。 */
USTRUCT(BlueprintType)
struct FCatFishingStableId
{
	GENERATED_BODY()

	/** 带身份来源前缀的 canonical 字符串；它不是显示名，也不是第二套可写身份系统。 */
	UPROPERTY(BlueprintReadWrite)
	FString CanonicalValue;
};

/** Boundary canonical hash；固定为 32 字节 SHA-256 输出，用来判定幂等请求是否携带同一业务语义。 */
USTRUCT(BlueprintType)
struct FCatFishingPayloadHash
{
	GENERATED_BODY()

	/** SHA-256 原始字节；空数组只允许出现在尚未计算或拒绝前的默认对象中。 */
	UPROPERTY(BlueprintReadWrite)
	TArray<uint8> Bytes;

	/** 等价判断只比较原始字节，避免把调试字符串或构造来源纳入合同。 */
	bool operator==(const FCatFishingPayloadHash& Other) const
	{
		return Bytes == Other.Bytes;
	}

	/** 不等价判断供测试和 Journal 分支表达 PayloadMismatch。 */
	bool operator!=(const FCatFishingPayloadHash& Other) const
	{
		return !(*this == Other);
	}
};

/** Poll 与 Receipt 共享的 operation 复合键；AttemptId 防止跨 Attempt 误读同名 OperationId。 */
USTRUCT(BlueprintType)
struct FCatFishingOperationKey
{
	GENERATED_BODY()

	/** Operation 所属 Attempt；CloseAttempt 和 Poll 都以它限制生命周期。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishingAttemptId AttemptId;

	/** Boundary 接受请求后分配的 operation 身份。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishingOperationId OperationId;

	/**
	 * 把这个复合键展开成缓存映射用的字符串，形式固定为 "Attempt|Operation"。
	 * Boundary、Journal、FightCursorLedger 和 Fishing Service 四处缓存必须用完全相同的字符串才能对得上，
	 * 以前四边各拄一份逐字相同的拼接；放到键类型自身上之后，改格式只能改这一处。
	 * AttemptId 必须在前，否则不同 Attempt 的同名 OperationId 会共享同一条 Receipt。
	 */
	FString ToCacheKey() const
	{
		return FString::Printf(TEXT("%s|%s"),
			*AttemptId.Value.ToString(EGuidFormats::Digits),
			*OperationId.Value.ToString(EGuidFormats::Digits));
	}
};

/** 所有 Boundary request 的公共头；它把幂等、身份和陈旧写保护分成三个独立概念。 */
USTRUCT(BlueprintType)
struct FCatFishingBoundaryRequestHeader
{
	GENERATED_BODY()

	/** 当前 typed schema 版本；版本变化必须改变 PayloadHash，不暴露给蓝图避免 uint16 反射限制污染合同。 */
	uint16 SchemaVersion = 1;

	/** 本次结果属于哪个 Attempt；值由 Boundary 接受 Start 时生成，这里只是回传给调用方。 */
	UPROPERTY(BlueprintReadWrite)
	FCatFishingAttemptId AttemptId;

	/** 本次语义意图的幂等键；同 key 同 payload 返回首次结果，同 key 不同 payload 必须拒绝。 */
	UPROPERTY(BlueprintReadWrite)
	FCatFishingRequestId RequestId;

	/** authority 规范化身份；生产代码必须由服务器填写，不能信任客户端。 */
	UPROPERTY(BlueprintReadWrite)
	FCatFishingStableId PrincipalId;

	/** 调用方读取领域聚合时看到的版本；它只阻止覆盖较新事实，不参与 RequestId 重放身份。 */
	UPROPERTY(BlueprintReadWrite)
	int64 ExpectedRevision = 0;
};

/** Boundary result 的公共头；typed result 在它后面附加各阶段自己的 gameplay 数据。 */
USTRUCT(BlueprintType)
struct FCatFishingBoundaryResultHeader
{
	GENERATED_BODY()

	/** 与请求对应的 schema 版本；Poll 重放必须保留首次接受时的版本，不作为 Blueprint 字段导出。 */
	uint16 SchemaVersion = 1;

	/** 本 operation 当前事务状态；它不等同于 Captured、FishEscaped 或 RodBroken。 */
	UPROPERTY(BlueprintReadOnly)
	ECatFishingBoundaryDisposition Disposition = ECatFishingBoundaryDisposition::Rejected;

	/** 结构化合同错误；None 只允许和 Pending 或 Committed 的有效结果一起出现。 */
	UPROPERTY(BlueprintReadOnly)
	ECatFishingBoundaryError Error = ECatFishingBoundaryError::InvalidRequest;

	/** Accepted operation 的恢复键；Rejected-before-side-effect 保持无效。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishingOperationKey Operation;

	/** 本次 operation 对应的 canonical 业务载荷；Poll 和 Receipt 用它防止错配。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishingPayloadHash PayloadHash;

	/** 领域 writer 返回的当前版本；Pending seed 使用调用方 ExpectedRevision。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;

	/** 当前结果是否来自已有 Journal 或 Inbox 记录；它帮助调用方区分首次接受和重放读取。 */
	UPROPERTY(BlueprintReadOnly)
	bool bReplay = false;
};

/** 单个外围领域写入的提交证明；Receipt 只描述领域事实，不携带任意 payload。 */
USTRUCT(BlueprintType)
struct FCatFishingDomainReceipt
{
	GENERATED_BODY()

	/** 领域 writer 发行的稳定 Receipt 身份。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishingReceiptId ReceiptId;

	/** Receipt 所属 Boundary operation。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishingOperationKey Operation;

	/** Receipt 对应的固定领域写入种类。 */
	UPROPERTY(BlueprintReadOnly)
	ECatFishingReceiptKind Kind = ECatFishingReceiptKind::BaitAccepted;

	/** 领域写入接受的 canonical payload hash。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishingPayloadHash PayloadHash;

	/** 领域 writer 提交后的聚合 Revision。 */
	UPROPERTY(BlueprintReadOnly)
	int64 DomainRevision = 0;
};

/** Result Inbox 的可见条目；私有恢复 stage、reservation token 和 backoff 不进入 public contract。 */
USTRUCT(BlueprintType)
struct FCatFishingInboxEntry
{
	GENERATED_BODY()

	/** Inbox 里的唯一 operation 键；Poll 必须原样携带它。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishingOperationKey Operation;

	/** 首次接受时冻结的 canonical payload hash。 */
	UPROPERTY(BlueprintReadOnly)
	FCatFishingPayloadHash PayloadHash;

	/** 当前可见事务状态；Pending 说明同一 operation 仍在前向恢复。 */
	UPROPERTY(BlueprintReadOnly)
	ECatFishingBoundaryDisposition Disposition = ECatFishingBoundaryDisposition::Pending;
};

