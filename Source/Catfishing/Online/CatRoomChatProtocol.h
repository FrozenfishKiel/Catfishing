#pragma once

#include "CoreMinimal.h"

/** 房间文字协议；仅承载纯文本，身份由 Steam 回调提供，不接受客户端声明身份或业务命令。 */
namespace CatRoomChatProtocol
{
inline constexpr int32 MaxCharacters = 200;
inline constexpr int32 MaxTextBytes = 1024;
inline constexpr int32 HeaderBytes = 21;
inline constexpr int32 HistoryLimit = 100;

inline bool ValidateText(const FString& Text)
{
    if (Text.IsEmpty() || Text.TrimStartAndEnd().IsEmpty()) { return false; }
    int32 Characters = 0;
    for (int32 I = 0; I < Text.Len(); ++I)
    {
        const uint32 C = Text[I];
        if (C < 32 || (C >= 127 && C <= 159)) { return false; }
        if (C >= 0xd800 && C <= 0xdbff)
        {
            if (++I >= Text.Len() || Text[I] < 0xdc00 || Text[I] > 0xdfff) { return false; }
        }
        else if (C >= 0xdc00 && C <= 0xdfff) { return false; }
        if (++Characters > MaxCharacters) { return false; }
    }
    return FTCHARToUTF8(*Text).Length() <= MaxTextBytes;
}

inline bool Encode(const FGuid& Id, const FString& Text, TArray<uint8>& Out)
{
    Out.Reset();
    if (!Id.IsValid() || !ValidateText(Text)) { return false; }
    Out.Append({ 'C', 'A', 'T', 'C', 1 });
    for (uint32 Word : { Id.A, Id.B, Id.C, Id.D })
    {
        for (int32 Byte = 0; Byte < 4; ++Byte) { Out.Add(uint8(Word >> (Byte * 8))); }
    }
    const FTCHARToUTF8 Utf8(*Text);
    Out.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
    return true;
}

inline bool Decode(TConstArrayView<uint8> Bytes, FGuid& Id, FString& Text)
{
    if (Bytes.Num() <= HeaderBytes || Bytes.Num() > HeaderBytes + MaxTextBytes
        || Bytes[0] != 'C' || Bytes[1] != 'A' || Bytes[2] != 'T' || Bytes[3] != 'C' || Bytes[4] != 1) { return false; }
    uint32 Words[4] = {};
    for (int32 Word = 0; Word < 4; ++Word)
    {
        for (int32 Byte = 0; Byte < 4; ++Byte) { Words[Word] |= uint32(Bytes[5 + Word * 4 + Byte]) << (Byte * 8); }
    }
    Id = FGuid(Words[0], Words[1], Words[2], Words[3]);
    const FUTF8ToTCHAR Decoded(reinterpret_cast<const ANSICHAR*>(Bytes.GetData() + HeaderBytes), Bytes.Num() - HeaderBytes);
    Text = FString(Decoded.Length(), Decoded.Get());
    if (!Id.IsValid() || !ValidateText(Text)) { return false; }
    // 回编码逐字节核对，拒绝截断、非法 UTF-8 和内嵌 NUL，禁止容错替换后进入 UI。
    const FTCHARToUTF8 Encoded(*Text);
    return Encoded.Length() == Bytes.Num() - HeaderBytes
        && FMemory::Memcmp(Encoded.Get(), Bytes.GetData() + HeaderBytes, Encoded.Length()) == 0;
}

struct FRateLimit
{
    double LastTime = 0;
    double Tokens = 3;
    bool Accept(double Now)
    {
        Tokens = FMath::Min(3.0, Tokens + FMath::Max(0.0, Now - LastTime));
        LastTime = Now;
        if (Tokens < 1) { return false; }
        Tokens -= 1;
        return true;
    }
};
}
