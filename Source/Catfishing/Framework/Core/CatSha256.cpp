#include "Framework/Core/CatSha256.h"

namespace
{
	/**
	 * FIPS 180-4 规定的 64 个轮常量：前 64 个素数立方根小数部分的前 32 位。
	 * 这张表连同下面的初始状态一起决定输出，写错任何一项都会得到一个"看着像 SHA-256"、却和外部工具对不上的摘要，
	 * 所以它是照抄标准的固定数据，不允许按需要改动或重排。
	 */
	constexpr uint32 KRoundConstants[64] =
	{
		0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5, 0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
		0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3, 0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
		0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC, 0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
		0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7, 0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
		0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13, 0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
		0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3, 0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
		0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5, 0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
		0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208, 0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2
	};

	/**
	 * 32 位循环右移，SHA-256 的扩散全靠它。
	 * Bits 只由下面几个 Sigma 里写死的算法常量传入，取值恒在 1..31，因此不必处理 Bits 为 0 时 Value << 32 属于
	 * 未定义行为这个坑；不要把这个函数当成通用工具向外暴露。
	 */
	uint32 RotateRight(const uint32 Value, const uint32 Bits)
	{
		return (Value >> Bits) | (Value << (32 - Bits));
	}

	/** FIPS 180-4 的 Ch：逐位以 X 当选择器，在 Y 和 Z 之间取值，是压缩函数每一轮的非线性来源之一。 */
	uint32 Choose(const uint32 X, const uint32 Y, const uint32 Z)
	{
		return (X & Y) ^ (~X & Z);
	}

	/** FIPS 180-4 的 Maj：逐位取三个输入的多数位，和 Ch 一起构成压缩函数的非线性部分。 */
	uint32 Majority(const uint32 X, const uint32 Y, const uint32 Z)
	{
		return (X & Y) ^ (X & Z) ^ (Y & Z);
	}

	/** FIPS 180-4 的 Σ0，只作用于工作变量 A；三个旋转量是标准定死的，改动即换成另一个哈希算法。 */
	uint32 BigSigma0(const uint32 Value)
	{
		return RotateRight(Value, 2) ^ RotateRight(Value, 13) ^ RotateRight(Value, 22);
	}

	/** FIPS 180-4 的 Σ1，只作用于工作变量 E；三个旋转量同样来自标准。 */
	uint32 BigSigma1(const uint32 Value)
	{
		return RotateRight(Value, 6) ^ RotateRight(Value, 11) ^ RotateRight(Value, 25);
	}

	/** FIPS 180-4 的 σ0，只用于把 message schedule 扩展到第 16 项之后。 */
	uint32 SmallSigma0(const uint32 Value)
	{
		return RotateRight(Value, 7) ^ RotateRight(Value, 18) ^ (Value >> 3);
	}

	/** FIPS 180-4 的 σ1，同样只用于 message schedule 的扩展项。 */
	uint32 SmallSigma1(const uint32 Value)
	{
		return RotateRight(Value, 17) ^ RotateRight(Value, 19) ^ (Value >> 10);
	}

	/**
	 * 把 64 位值以 big-endian 追加到字节流，专用于 padding 末尾的消息 bit 长度字段。
	 * SHA-256 内部的字节序由标准规定，与调用方领域里怎么编码字段无关，不要为了"和上层统一"改成 little-endian。
	 */
	void AppendBigEndian64(TArray<uint8>& OutBytes, const uint64 Value)
	{
		for (int32 Shift = 56; Shift >= 0; Shift -= 8)
		{
			OutBytes.Add(static_cast<uint8>((Value >> Shift) & 0xFF));
		}
	}

	/** 把一个状态字以 big-endian 追加到摘要，保证最终 32 字节与外部 SHA-256 工具逐字节一致。 */
	void AppendBigEndian32(TArray<uint8>& OutBytes, const uint32 Value)
	{
		OutBytes.Add(static_cast<uint8>((Value >> 24) & 0xFF));
		OutBytes.Add(static_cast<uint8>((Value >> 16) & 0xFF));
		OutBytes.Add(static_cast<uint8>((Value >> 8) & 0xFF));
		OutBytes.Add(static_cast<uint8>(Value & 0xFF));
	}

	/** 按 big-endian 从 chunk 里读出一个 32 位 word；这是逐字节拼装，因此运行平台自身的端序不影响结果。 */
	uint32 ReadBigEndian32(const uint8* Data)
	{
		return (static_cast<uint32>(Data[0]) << 24)
			| (static_cast<uint32>(Data[1]) << 16)
			| (static_cast<uint32>(Data[2]) << 8)
			| static_cast<uint32>(Data[3]);
	}
}

/**
 * 实现流程：先按 FIPS 180-4 的 padding 规则把输入复制一份并补齐——追加 0x80、补 0x00 直到长度模 64 等于 56，
 * 最后写入 8 字节 big-endian 的原始消息 bit 长度（用补齐前的字节数算，不是补齐后的）。
 * 然后从标准初始状态（前 8 个素数平方根小数部分的前 32 位）开始，按 64 字节一个 chunk 迭代：
 * 前 16 个 word 直接读 chunk，第 16..63 项由 σ0/σ1 递推出来，接着用 8 个工作变量跑 64 轮压缩，
 * chunk 结束时把工作变量加回状态（这一步不能省，去掉就退化成可逆变换）。
 * 全部状态都是局部变量，只读入参不改入参，没有任何副作用；最后把 8 个状态字按 big-endian 摊成 32 字节返回。
 * 这里走的是 one-shot 路径而不是引擎平台层的 SHA 入口，因为命令行自动化测试环境里 Generic 平台实现不保证可用。
 */
TArray<uint8> CatSha256::Compute(const TArray<uint8>& Input)
{
	TArray<uint8> Padded = Input;
	Padded.Add(0x80);
	while ((Padded.Num() % 64) != 56)
	{
		Padded.Add(0x00);
	}
	AppendBigEndian64(Padded, static_cast<uint64>(Input.Num()) * 8ull);

	uint32 HashState[8] =
	{
		0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
		0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19
	};

	for (int32 Offset = 0; Offset < Padded.Num(); Offset += 64)
	{
		uint32 Schedule[64];
		for (int32 Index = 0; Index < 16; ++Index)
		{
			Schedule[Index] = ReadBigEndian32(&Padded[Offset + Index * 4]);
		}
		for (int32 Index = 16; Index < 64; ++Index)
		{
			Schedule[Index] = SmallSigma1(Schedule[Index - 2])
				+ Schedule[Index - 7]
				+ SmallSigma0(Schedule[Index - 15])
				+ Schedule[Index - 16];
		}

		uint32 A = HashState[0];
		uint32 B = HashState[1];
		uint32 C = HashState[2];
		uint32 D = HashState[3];
		uint32 E = HashState[4];
		uint32 F = HashState[5];
		uint32 G = HashState[6];
		uint32 H = HashState[7];

		for (int32 Index = 0; Index < 64; ++Index)
		{
			const uint32 Temp1 = H + BigSigma1(E) + Choose(E, F, G) + KRoundConstants[Index] + Schedule[Index];
			const uint32 Temp2 = BigSigma0(A) + Majority(A, B, C);
			H = G;
			G = F;
			F = E;
			E = D + Temp1;
			D = C;
			C = B;
			B = A;
			A = Temp1 + Temp2;
		}

		HashState[0] += A;
		HashState[1] += B;
		HashState[2] += C;
		HashState[3] += D;
		HashState[4] += E;
		HashState[5] += F;
		HashState[6] += G;
		HashState[7] += H;
	}

	TArray<uint8> Digest;
	Digest.Reserve(32);
	for (const uint32 Word : HashState)
	{
		AppendBigEndian32(Digest, Word);
	}
	return Digest;
}
