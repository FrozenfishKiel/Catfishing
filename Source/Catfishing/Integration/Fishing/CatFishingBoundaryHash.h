#pragma once

#include "CoreMinimal.h"

#include "Framework/Core/CatFishingBoundaryContracts.h"

/** Boundary canonical hash helper；它只负责稳定编码和 SHA-256，不保存任何 operation 状态。 */
class FCatFishingBoundaryHash
{
public:
	/**
	 * 按 WORK-00 固定顺序计算 operation payload hash。
	 * 本函数自己从 Header 里取的字段不包含 RequestId、OperationId、ReceiptId 和时间戳，因为幂等重放的前提就是
	 * "同一业务动作换一个 RequestId 仍然数得出同一个 hash"。
	 * 但 CanonicalBusinessPayload 是调用方拼的，里面写了什么本函数不干预；现在 Cast 的 payload 确实把 RequestId
	 * 写进去了（见 BuildCastPayload），所以不要把这里当成"全链路不含 RequestId"的保证。
	 */
	static FCatFishingPayloadHash HashOperation(
		ECatFishingBoundaryOperationKind OperationKind,
		const FCatFishingBoundaryRequestHeader& Header,
		const TArray<uint8>& CanonicalBusinessPayload);

	/**
	 * 下面这组 Append* 是 Boundary canonical byte stream 的唯一编码规则，对外公开。
	 * 它们曾经是 private，结果 FightCursorLedger 和 BoundarySubsystem 各自在匿名命名空间里抄了一份一模一样的；
	 * canonical 编码一旦分叉，同一业务请求会算出不同 hash 而让幂等重放静默失效，所以宁可公开也不允许第二份。
	 * 所有多字节整数一律 little-endian，字符串和 payload 一律带 uint32 长度前缀；改动任何一条都会改变 Golden。
	 */

	/** 把 uint8 追加到 canonical byte stream；所有上层类型最终都落到这个固定字节流。 */
	static void AppendUInt8(TArray<uint8>& OutBytes, uint8 Value);

	/** 以 little-endian 追加 uint16，避免平台端序影响 Golden。 */
	static void AppendUInt16(TArray<uint8>& OutBytes, uint16 Value);

	/** 以 little-endian 追加 uint32，供 FGuid 四段稳定编码。 */
	static void AppendUInt32(TArray<uint8>& OutBytes, uint32 Value);

	/** 以 little-endian 追加 int64，供 Revision 与后续 Cursor 类型复用。 */
	static void AppendInt64(TArray<uint8>& OutBytes, int64 Value);

	/** 按 A/B/C/D 四段追加 FGuid；不使用 ToString，避免大小写和格式策略进入合同。 */
	static void AppendGuid(TArray<uint8>& OutBytes, const FGuid& Value);

	/** 按 UTF-8 字节长度前缀追加字符串；display name 和本地化文本不应传入这里。 */
	static void AppendString(TArray<uint8>& OutBytes, const FString& Value);

private:
	/** 按长度前缀追加业务 payload，防止两个字段拼接后产生同字节歧义。 */
	static void AppendPayload(TArray<uint8>& OutBytes, const TArray<uint8>& Payload);
};

