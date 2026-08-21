#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "CatImprintMediaTypes.generated.h"

/** 一个收件人在媒体传输中的授权快照；Membership/Permission 版本必须在 Manifest、首块和 Resume 前重新带入校验。 */
USTRUCT(BlueprintType)
struct FCatImprintMediaRecipientAuthorization
{
	GENERATED_BODY()

	/** 收件人的服务器私有 StableNetId；它只用来做本局授权索引，不进入本地相册。 */
	UPROPERTY(BlueprintReadWrite)
	FString RecipientStableNetId;

	/** 收件人进入本次媒体分发时的成员资格纪元；后续拉取或 ACK 必须带回同一纪元，服务器借此拒绝旧房间成员继续访问。 */
	UPROPERTY(BlueprintReadWrite)
	int64 MembershipRevision = 0;

	/** 收件人进入本次媒体分发时的媒体权限纪元；权限表一旦变化，旧 cursor 会失效，避免撤权后继续续传。 */
	UPROPERTY(BlueprintReadWrite)
	int64 PermissionRevision = 0;
};

/** Host 单次成像后冻结的媒体 Manifest；它描述同一份字节，而不是某个收件人的本地文件。 */
USTRUCT(BlueprintType)
struct FCatImprintMediaManifest
{
	GENERATED_BODY()

	/** 媒体传输唯一 ID；由服务器创建，Host 只能回填同一个 ID。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid MediaId;

	/** 来源 CandidateId；同一候选的所有收件人必须共享同一媒体内容。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid CandidateId;

	/** 一局相册 ID；Profile Grant 后续只保存这个稳定分组，不保存服务器字节。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid RunAlbumId;

	/** 媒体 MIME 类型；只接受 settings 中显式允许的类型。 */
	UPROPERTY(BlueprintReadWrite)
	FString MimeType;

	/** 完整媒体字节数；它决定最后一块大小，并受 settings 上限约束。 */
	UPROPERTY(BlueprintReadWrite)
	int64 TotalSizeBytes = 0;

	/** 每个普通块的字节数；最后一块可以更短，且不能超过 settings 上限。 */
	UPROPERTY(BlueprintReadWrite)
	int32 ChunkSizeBytes = 0;

	/** 总块数；由 TotalSizeBytes 和 ChunkSizeBytes 推导，Host 不得随意声明。 */
	UPROPERTY(BlueprintReadWrite)
	int32 ChunkCount = 0;

	/** 完整媒体的 SHA-256 十六进制摘要；所有块入齐后必须重新计算并一致。 */
	UPROPERTY(BlueprintReadWrite)
	FString Sha256Hex;
};

/** 收件人按 cursor 拉取到的一块媒体字节；ACK 只接受同一块的 hash。 */
USTRUCT(BlueprintType)
struct FCatImprintMediaChunk
{
	GENERATED_BODY()

	/** 所属媒体传输 ID。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid MediaId;

	/** 块序号；收件人按 NextChunkIndex 顺序拉取和 ACK。 */
	UPROPERTY(BlueprintReadOnly)
	int32 ChunkIndex = INDEX_NONE;

	/** 该块原始字节；不会通过 GameState/PlayerState 复制。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<uint8> Bytes;

	/** 该块字节的 SHA-256 十六进制摘要；ACK 用它发现错块或乱序。 */
	UPROPERTY(BlueprintReadOnly)
	FString Sha256Hex;
};

/** 单个收件人的断点续传 cursor；它只描述该收件人的进度，不代表其他人成功。 */
USTRUCT(BlueprintType)
struct FCatImprintMediaCursor
{
	GENERATED_BODY()

	/** 所属媒体传输 ID。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid MediaId;

	/** 当前 cursor 对应的收件人 StableNetId。 */
	UPROPERTY(BlueprintReadOnly)
	FString RecipientStableNetId;

	/** 下一块应拉取并 ACK 的序号；重复 ACK 只能回放已完成块，不能跳过。 */
	UPROPERTY(BlueprintReadOnly)
	int32 NextChunkIndex = 0;

	/** 总块数；UI 或传输层可以用它展示进度。 */
	UPROPERTY(BlueprintReadOnly)
	int32 ChunkCount = 0;

	/** 已被该收件人 ACK 的字节数。 */
	UPROPERTY(BlueprintReadOnly)
	int64 AcknowledgedBytes = 0;

	/** 完整媒体字节数。 */
	UPROPERTY(BlueprintReadOnly)
	int64 TotalSizeBytes = 0;

	/** 该收件人是否已经 ACK 完整媒体；不影响其他收件人。 */
	UPROPERTY(BlueprintReadOnly)
	bool bComplete = false;
};

/** 媒体传输命令与读取的统一结果；None 可以表示只读拉取成功，bCommitted 只表示首次写入。 */
USTRUCT(BlueprintType)
struct FCatImprintMediaResult
{
	GENERATED_BODY()

	/** 本次是否产生首次服务端写入，例如创建传输、冻结 Manifest、提交块或推进 ACK。 */
	UPROPERTY(BlueprintReadOnly)
	bool bCommitted = false;

	/** Host Manifest 与全部 Chunk 已校验完成，收件人才可以拉取。 */
	UPROPERTY(BlueprintReadOnly)
	bool bReadyForRecipients = false;

	/** 当前收件人是否已经 ACK 完整媒体；只在收件人操作中有意义。 */
	UPROPERTY(BlueprintReadOnly)
	bool bRecipientComplete = false;

	/** 输入 RequestId；只读拉取没有 RequestId 时保持无效。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid RequestId;

	/** 当前媒体传输 ID；失败时也尽量带回已知 ID 方便重读。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid MediaId;

	/** 当前媒体传输 Revision；拒绝时返回最新值，避免调用方继续用旧 cursor 写入。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;

	/** 收件人下一块 cursor；Host 操作中通常为 0。 */
	UPROPERTY(BlueprintReadOnly)
	int32 NextChunkIndex = 0;

	/** 结构化终态；None 表示本次读/写请求被接受。 */
	UPROPERTY(BlueprintReadOnly)
	ECatDomainCommandError Error = ECatDomainCommandError::InvalidPayload;
};
