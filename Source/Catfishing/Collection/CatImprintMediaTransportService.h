#pragma once

#include "CoreMinimal.h"
#include "Collection/CatImprintMediaTypes.h"
#include "Collection/CatImprintTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "CatImprintMediaTransportService.generated.h"

/** 一局服务器印记媒体传输服务；它只保存 Host 提交的同一份字节和收件人 ACK cursor，不保存本地相册索引。 */
UCLASS()
class CATFISHING_API UCatImprintMediaTransportService : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 只在 authority Game World 创建；客户端本地文件仍由 Profile/外部成像桥持有。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** World 销毁时关闭新命令并清除本局媒体字节、授权快照和幂等缓存。 */
	virtual void Deinitialize() override;

	/** 为一个 CapturePlan 所属共同候选建立 Host 单次媒体传输；同 Candidate/Album 重放返回同一 MediaId。 */
	FCatImprintMediaResult BeginHostMediaTransfer(FGuid RequestId, const FCatCapturePlan& Plan,
		const FString& HostStableNetId, const TArray<FCatImprintMediaRecipientAuthorization>& Recipients);

	/** Host 冻结完整媒体 Manifest；Manifest 与计划、设置和容量全部匹配后才允许提交 Chunk。 */
	FCatImprintMediaResult CommitHostMediaManifest(FGuid RequestId, const FString& HostStableNetId,
		const FCatImprintMediaManifest& Manifest);

	/** Host 提交一个媒体块；块大小、块 hash 与最终整体 hash 都通过后，媒体才对收件人开放。 */
	FCatImprintMediaResult CommitHostMediaChunk(FGuid RequestId, const FString& HostStableNetId,
		FGuid MediaId, int32 ChunkIndex, const TArray<uint8>& Bytes);

	/** 按收件人授权版本和 cursor 读取下一块；读取不推进 ACK，失败也不会修改服务端状态。 */
	FCatImprintMediaResult ReadRecipientChunk(FGuid MediaId, const FString& RecipientStableNetId,
		int64 MembershipRevision, int64 PermissionRevision, int32 ChunkIndex, FCatImprintMediaChunk& OutChunk) const;

	/** 收件人确认已经完整落下某一块；只有顺序 ACK 且 hash 匹配才推进 cursor。 */
	FCatImprintMediaResult AcknowledgeRecipientChunk(FGuid RequestId, FGuid MediaId,
		const FString& RecipientStableNetId, int64 MembershipRevision, int64 PermissionRevision,
		int32 ChunkIndex, const FString& ChunkHashHex);

	/** 只读查询收件人的断点续传 cursor；授权版本不匹配时拒绝并清空输出。 */
	FCatImprintMediaResult GetRecipientCursor(FGuid MediaId, const FString& RecipientStableNetId,
		int64 MembershipRevision, int64 PermissionRevision, FCatImprintMediaCursor& OutCursor) const;

	/** Host teardown 前关闭新媒体命令，并把尚未完整 ACK 的传输标记失败；已完成收件人的本地副本不回滚。 */
	void CloseCommandsAndFailOpenTransfers();

	/** 对任意字节计算 SHA-256 十六进制摘要；媒体 Manifest、Chunk 和测试都使用同一实现。 */
	static FString ComputePayloadHashHex(const TArray<uint8>& Bytes);

private:
	/** 单个收件人的授权与 ACK 进度；授权版本被冻结，后续请求必须重新带入匹配。 */
	struct FRecipientRecord
	{
		/** 该收件人在传输开始时的身份、成员和权限基线；读取与 ACK 都用它和请求版本相互校验，避免授权变化被旧 cursor 绕过。 */
		FCatImprintMediaRecipientAuthorization Authorization;

		/** 下一块必须 ACK 的序号；它也是断点续传 cursor。 */
		int32 NextChunkIndex = 0;

		/** 已经由该收件人 ACK 的字节数。 */
		int64 AcknowledgedBytes = 0;

		/** 该收件人的媒体副本是否完整落地；其他收件人不受影响。 */
		bool bComplete = false;
	};

	/** 一份 Host 单次成像媒体的服务端记录；所有收件人共享同一 Manifest 和 Chunk 字节。 */
	struct FTransferRecord
	{
		/** 服务器分配的媒体 ID。 */
		FGuid MediaId;

		/** 来源 CapturePlan；服务用其中 Candidate/Album 保证同一候选共享同一字节。 */
		FCatCapturePlan SourcePlan;

		/** 本局 Host 的 StableNetId；只有它能提交 Manifest 和 Chunk。 */
		FString HostStableNetId;

		/** 已冻结 Manifest；未提交前 Chunk 和收件人读取都会被拒绝。 */
		FCatImprintMediaManifest Manifest;

		/** Manifest 是否已经通过容量、MIME 和计划一致性校验。 */
		bool bManifestCommitted = false;

		/** 所有 Chunk 是否已入齐并通过整体 hash 校验。 */
		bool bReadyForRecipients = false;

		/** Host 失败、整体 hash 不匹配或 teardown 后的终态；失败后不再接受新读写。 */
		bool bFailed = false;

		/** 媒体聚合版本；Manifest、Chunk、ACK 或失败终态都会递增。 */
		int64 Revision = 0;

		/** ChunkIndex 到原始字节；只在本局内存保存，不进入复制属性。 */
		TMap<int32, TArray<uint8>> ChunksByIndex;

		/** ChunkIndex 到块 hash；重复块和 ACK 都用它验证是否同一份字节。 */
		TMap<int32, FString> ChunkHashByIndex;

		/** 收件人 StableNetId 到其授权与 cursor。 */
		TMap<FString, FRecipientRecord> Recipients;
	};

	/** 构造带当前 Revision/Ready/Cursor 的结果；调用方再填 committed 或具体错误。 */
	static FCatImprintMediaResult MakeResult(const FTransferRecord* Record, FGuid RequestId,
		ECatDomainCommandError Error);

	/** 构造幂等缓存键；不同操作、身份、媒体和 RequestId 彼此隔离。 */
	static FString MakeTerminalKey(const FString& StableNetId, const TCHAR* Operation, FGuid MediaId,
		FGuid RequestId, int32 ChunkIndex = INDEX_NONE);

	/** 构造 Candidate/Album 共享媒体索引键；同一共同事件只能对应一份 Host 字节。 */
	static FString MakePlanKey(const FCatCapturePlan& Plan);

	/** 判断字符串是否是 64 位十六进制 SHA-256；比较前由调用方再统一 ToLower。 */
	static bool IsSha256Hex(const FString& HashHex);

	/** 比较 Manifest 是否与已冻结事实完全一致；一致重放可以安全返回 AlreadyResolved。 */
	static bool AreManifestsEquivalent(const FCatImprintMediaManifest& Left,
		const FCatImprintMediaManifest& Right);

	/** 构造 Begin 请求的业务载荷签名；同一 RequestId 只能重放同一计划和同一批收件人授权。 */
	static FString MakeBeginPayloadSignature(const FCatCapturePlan& Plan,
		const TArray<FCatImprintMediaRecipientAuthorization>& Recipients);

	/** 构造 Manifest 请求的业务载荷签名；Host 不能用旧 RequestId 偷换媒体合同。 */
	static FString MakeManifestPayloadSignature(const FCatImprintMediaManifest& Manifest);

	/** 构造 Chunk 请求的业务载荷签名；同一块重试必须仍是同一份字节。 */
	static FString MakeChunkPayloadSignature(const TArray<uint8>& Bytes);

	/** 构造 ACK 请求的业务载荷签名；同一 ACK 重试必须带同一授权版本和同一块 hash。 */
	static FString MakeAckPayloadSignature(int64 MembershipRevision, int64 PermissionRevision,
		const FString& ChunkHashHex);

	/**
	 * 尝试从终态缓存解析幂等重放；内核复用共享模板 CatQueryTerminalReplay，签名漂移会带着已知 MediaId/Revision 返回
	 * InvalidPayload 而不是复用旧成功。
	 */
	bool TryResolveTerminalReplay(const FString& TerminalKey, const FString& PayloadSignature,
		FCatImprintMediaResult& OutResult) const;

	/** 写入一次命令终态及其业务载荷签名；后续同键请求必须先通过签名匹配。 */
	void CacheTerminalResult(const FString& TerminalKey, const FString& PayloadSignature,
		const FCatImprintMediaResult& Result);

	/** 校验 CapturePlan 是否足以成为媒体传输来源；缺稳定键时不创建半条传输记录。 */
	static bool IsPlanComplete(const FCatCapturePlan& Plan);

	/** 计算指定块的期望大小；非最后一块必须等于 Manifest.ChunkSizeBytes。 */
	static int32 GetExpectedChunkSize(const FCatImprintMediaManifest& Manifest, int32 ChunkIndex);

	/** 校验收件人授权版本并返回 cursor 记录；版本不匹配时不会返回可写引用。 */
	static ECatDomainCommandError ValidateRecipient(const FTransferRecord& Record,
		const FString& RecipientStableNetId, int64 MembershipRevision, int64 PermissionRevision,
		const FRecipientRecord*& OutRecipient);

	/** 可写版本的收件人校验；ACK 成功前必须重新验证同一授权快照。 */
	static ECatDomainCommandError ValidateRecipientMutable(FTransferRecord& Record,
		const FString& RecipientStableNetId, int64 MembershipRevision, int64 PermissionRevision,
		FRecipientRecord*& OutRecipient);

	/** 校验并规范化 Host Manifest；失败时不写入 TransferRecord。 */
	ECatDomainCommandError ValidateManifest(const FTransferRecord& Record,
		const FCatImprintMediaManifest& Manifest, FCatImprintMediaManifest& OutNormalizedManifest) const;

	/** 在所有块入齐时重新拼接媒体并校验整体 hash；未入齐返回 true，hash 不符返回 false。 */
	bool TryFinalizeTransfer(FTransferRecord& Record);

	/** MediaId 到本局传输记录。 */
	TMap<FGuid, FTransferRecord> Transfers;

	/** Candidate/Album 到唯一 MediaId，保证共同候选不会产生多份字节。 */
	TMap<FString, FGuid> TransferByPlanKey;

	/** 写命令幂等终态缓存；重试只返回首个终态，不重复写字节或 ACK。 */
	TMap<FString, FCatImprintMediaResult> TerminalCache;

	/** 终态缓存对应的业务载荷签名；它守住同 RequestId 改 payload 的伪重试风险。 */
	TMap<FString, FString> TerminalPayloadSignatures;

	/** 一局媒体命令门；teardown 后 Begin/Manifest/Chunk/ACK 都拒绝新写入。 */
	bool bCommandsOpen = true;
};
