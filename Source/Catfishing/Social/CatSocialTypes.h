#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "CatSocialTypes.generated.h"

class AActor;

/** 手动/系统求助信号类型；普通帮助必须手动，只有巨鱼搏斗允许系统全体提示。 */
UENUM(BlueprintType)
enum class ECatHelpSignalKind : uint8
{
	/** 未配置信号。 */
	Unknown,
	/** 玩家手动请求钓鱼协助。 */
	ManualFishing,
	/** 倒地玩家手动请求救援。 */
	ManualDowned,
	/** Giant FishingSession 开始时由系统发布的全体提示。 */
	GiantFishSystem
};

/** GameState 复制的最近求助信号；客户端表现只消费，不据此自动加入玩法。 */
USTRUCT(BlueprintType)
struct FCatHelpSignalSnapshot
{
	GENERATED_BODY()

	/** 每次服务器接受信号生成的稳定 ID。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid SignalId;

	/** 手动或 Giant 系统提示类型。 */
	UPROPERTY(BlueprintReadOnly)
	ECatHelpSignalKind Kind = ECatHelpSignalKind::Unknown;

	/** 信号来源的服务器世界坐标；附近提示按 Radius 表现。 */
	UPROPERTY(BlueprintReadOnly)
	FVector SourceLocation = FVector::ZeroVector;

	/** 非全局手动信号的感知半径，单位厘米；全局信号为 0。 */
	UPROPERTY(BlueprintReadOnly)
	double RadiusCentimeters = 0.0;

	/** 只有 GiantFishSystem 为 true；普通求助不能借此升级为全体任务。 */
	UPROPERTY(BlueprintReadOnly)
	bool bGlobal = false;

	/** GameState 发布序号；客户端忽略低于当前序号的信号。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;
};

/** 偷鱼开始命令；客户端只提供稳定意图，Social 在服务器重建身份并从正式库存裁决唯一实物鱼。 */
USTRUCT(BlueprintType)
struct FCatTheftCommand
{
	GENERATED_BODY()

	/** RequestId 与 StableNetId；服务器覆盖身份字段。 */
	UPROPERTY(BlueprintReadWrite)
	FCatDomainCommandContext Context;

	/** 要偷的一条鱼物品实例。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid FishItemInstanceId;

	/** 这条鱼当前所在的 Actor 宿主；服务器从它解析正式库存组件。 */
	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<AActor> SourceInventoryHost = nullptr;

	/** 这条鱼当前所在的库存槽位；服务器会重新读取该槽并核对 FishItemInstanceId。 */
	UPROPERTY(BlueprintReadWrite)
	int32 SourceInventorySlotIndex = INDEX_NONE;
};

/** Social 偷鱼协议公开结果；只暴露阶段和结构化错误，不复制受害者 StableNetId。 */
USTRUCT(BlueprintType)
struct FCatTheftResult
{
	GENERATED_BODY()

	/** 公共命令终态。 */
	UPROPERTY(BlueprintReadOnly)
	FCatDomainCommandResult Command;

	/** 进食窗口到期后提交到 Condition/Growth 的身体终态；追回或 Begin 拒绝时保持默认。 */
	UPROPERTY(BlueprintReadOnly)
	FCatDomainCommandResult Body;

	/** 首次合法 Begin 后由服务器分配的协议 ID；追回 RPC、Timer 和来源库存返还只使用此键。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid TheftProtocolId;

	/** 进入追回窗口的 FishInstanceId。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid FishInstanceId;

	/** 本次是否仍处于进食前追回窗口。 */
	UPROPERTY(BlueprintReadOnly)
	bool bRecoveryWindowOpen = false;

	/** 本次是否已被合法追回并物归原主。 */
	UPROPERTY(BlueprintReadOnly)
	bool bReturned = false;

	/** 本次是否已过窗口并不可逆吃掉。 */
	UPROPERTY(BlueprintReadOnly)
	bool bConsumed = false;
};
