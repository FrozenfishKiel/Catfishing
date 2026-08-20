#include "Integration/Fishing/CatFishingBoundaryHash.h"
#include "Framework/Core/CatSha256.h"

// Hash 流程：先按固定字段顺序把 Header 与业务载荷摊成 canonical bytes，再交给共享的 SHA-256 原语出 32 字节摘要。
// 字段顺序、长度前缀和 little-endian 约定都属于 Boundary 自己的合同，只能写在这里，不能下沉到通用哈希原语里；
// RequestId/OperationId/ReceiptId 不进入输入，幂等重放才能在换了 RequestId 之后仍然算出同一个 hash。
FCatFishingPayloadHash FCatFishingBoundaryHash::HashOperation(
	const ECatFishingBoundaryOperationKind OperationKind,
	const FCatFishingBoundaryRequestHeader& Header,
	const TArray<uint8>& CanonicalBusinessPayload)
{
	TArray<uint8> Bytes;
	AppendUInt16(Bytes, Header.SchemaVersion);
	AppendUInt8(Bytes, static_cast<uint8>(OperationKind));
	AppendGuid(Bytes, Header.AttemptId.Value);
	AppendString(Bytes, Header.PrincipalId.CanonicalValue);
	AppendInt64(Bytes, Header.ExpectedRevision);
	AppendPayload(Bytes, CanonicalBusinessPayload);

	FCatFishingPayloadHash Result;
	Result.Bytes = CatSha256::Compute(Bytes);
	return Result;
}

// 编码流程：uint8 没有端序问题，直接追加原始值。
void FCatFishingBoundaryHash::AppendUInt8(TArray<uint8>& OutBytes, const uint8 Value)
{
	OutBytes.Add(Value);
}

// 编码流程：低字节先写，保证不同平台得到同一 Golden byte stream。
void FCatFishingBoundaryHash::AppendUInt16(TArray<uint8>& OutBytes, const uint16 Value)
{
	OutBytes.Add(static_cast<uint8>(Value & 0xFF));
	OutBytes.Add(static_cast<uint8>((Value >> 8) & 0xFF));
}

// 编码流程：FGuid 的四段以 little-endian 写入，避免 ToString 的格式策略影响合同。
void FCatFishingBoundaryHash::AppendUInt32(TArray<uint8>& OutBytes, const uint32 Value)
{
	OutBytes.Add(static_cast<uint8>(Value & 0xFF));
	OutBytes.Add(static_cast<uint8>((Value >> 8) & 0xFF));
	OutBytes.Add(static_cast<uint8>((Value >> 16) & 0xFF));
	OutBytes.Add(static_cast<uint8>((Value >> 24) & 0xFF));
}

// 编码流程：Revision 和未来 Cursor 都使用有符号 64 位整数；这里按无符号视图逐字节写入。
void FCatFishingBoundaryHash::AppendInt64(TArray<uint8>& OutBytes, const int64 Value)
{
	const uint64 Bits = static_cast<uint64>(Value);
	for (int32 Shift = 0; Shift < 64; Shift += 8)
	{
		OutBytes.Add(static_cast<uint8>((Bits >> Shift) & 0xFF));
	}
}

// 编码流程：FGuid 保持 UE 内部四段数值语义，不引入文本分隔符。
void FCatFishingBoundaryHash::AppendGuid(TArray<uint8>& OutBytes, const FGuid& Value)
{
	AppendUInt32(OutBytes, Value.A);
	AppendUInt32(OutBytes, Value.B);
	AppendUInt32(OutBytes, Value.C);
	AppendUInt32(OutBytes, Value.D);
}

// 编码流程：字符串先转 UTF-8，再写入字节数和内容；空串和缺失身份由上层校验区分。
void FCatFishingBoundaryHash::AppendString(TArray<uint8>& OutBytes, const FString& Value)
{
	FTCHARToUTF8 Converted(*Value);
	AppendUInt32(OutBytes, static_cast<uint32>(Converted.Length()));
	if (Converted.Length() > 0)
	{
		OutBytes.Append(reinterpret_cast<const uint8*>(Converted.Get()), Converted.Length());
	}
}

// 编码流程：业务 payload 自带长度前缀，避免后续扩展字段时出现拼接歧义。
void FCatFishingBoundaryHash::AppendPayload(TArray<uint8>& OutBytes, const TArray<uint8>& Payload)
{
	AppendUInt32(OutBytes, static_cast<uint32>(Payload.Num()));
	OutBytes.Append(Payload);
}