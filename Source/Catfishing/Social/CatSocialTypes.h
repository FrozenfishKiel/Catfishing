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

// 这里曾经有 FCatTheftCommand / FCatTheftResult 两个偷鱼协议 DTO，2026-09-11 整条退役。
// 「偷」是玩家玩的时候才产生的主观意识，不是机制；机制层只有客观的拿鱼，不问动机也不问归属，
// 因此追回窗口、物归原主、扑倒反制三个概念一并消失，没有协议 ID 也没有阶段可暴露。
// 拿鱼走的是通用库存移动（ACatfishingPlayerController::ServerMoveInventoryItemBetweenHosts），
// 规则＝够得着、鱼护在地面、一嘴一条，见联机社交册 §3.1.5。
