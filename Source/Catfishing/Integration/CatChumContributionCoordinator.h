#pragma once

#include "CoreMinimal.h"
#include "Environment/CatWaterTypes.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "CatChumContributionCoordinator.generated.h"

class ACatCharacter;

/**
 * 一名玩家"消耗自己身上一份窝料、把它投到水里某个坐标"的完整意图。
 * 它刻意带两个版本前提：耗材在角色的 Equipment 上、窝料池在世界的窝点集合里，这是两个各自独立推进版本的聚合，
 * 只带其中一个就意味着另一边的并发前提没人检查。三轴贡献值不在这里——它由服务器从窝料定义现场取，客户端报的不会被读。
 */
USTRUCT()
struct FCatChumContributionCommand
{
	GENERATED_BODY()

	/** RequestId 与服务器重建的身份；ExpectedRevision 在这条命令里指窝点集合版本。 */
	FCatDomainCommandContext Context;

	/** 窝料要落到的世界坐标；它同时决定并入哪个已有窝点还是新建一个，也是可达判定的一端。 */
	FVector DropLocation = FVector::ZeroVector;

	/** 要消耗的那一份正式窝料的装备定义 ID；服务器按它查目录拿三轴贡献，查不到或不是窝料一律拒绝。 */
	FName ChumDefinitionId;

	/** 投掷者角色 Equipment 的并发前提版本；预留、提交和释放三步都用它，避免陈旧视图扣错库存。 */
	int64 ExpectedEquipmentRevision = 0;
};

/**
 * 把"扣一份窝料"和"往水里加一份窝料池"串成一条链的服务器协调器：预留耗材 → 写入窝点 → 提交耗材消耗。
 *
 * 它存在的理由和献祭、商店订单那两条链一样：耗材是角色 Equipment 的事实，窝料池是 Environment 窝点集合的事实，
 * 两边都不该反过来调对方（技术方案 §2.1 明确 Environment 只可依赖 Core/Data，不能依赖 Equipment），
 * 所以需要第三个地方按固定顺序推这条链。这也是它落在 `Integration/` 而不是 `Environment/` 的原因。
 *
 * 它拥有的只有这条链的首次终态缓存：同一个人用同一 RequestId 重投，不会再预留、再扣耗材或再加一次池。
 * 它不拥有耗材库存、不拥有窝点池，也不做玩法准入——"这个 Controller 现在能不能发玩法命令"由 GameMode 的 gate 回答，
 * 调用方必须先过那道门再进来。
 *
 * 失败回退边界只有一条：耗材预留成功之后、窝点写入失败时释放预留；窝点写入成功之后不存在任何回退分支——
 * 池已经加进去了，这时候把耗材还回去等于凭空多出一份窝料。提交耗材那一步真失败时只记 Error 留痕，不回滚窝点。
 */
UCLASS()
class CATFISHING_API UCatChumContributionCoordinator : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 只在服务器 Game World 创建；客户端没有这条链，也不能本地推进投窝。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/**
	 * 声明：按一条已经带上服务器身份的投窝意图跑完整条链，返回窝点侧的终态（含落点当前窝的快照）。
	 *       ThrowerCharacter 是投掷者本人的身体，由 RPC 层从它自己的 Pawn 取好交进来：耗材从这只猫身上扣，
	 *       可达判定也用这只猫的权威位置，两者都不接受客户端上报。
	 * 实现：先按"身份 + RequestId"查首次终态，命中且载荷一致就原样重放；确认是首次之后才校验落点、窝料定义与可达性，
	 *       再依次预留耗材、写入窝点、提交耗材消耗，最后把终态和载荷签名一起写进缓存并打一行终态日志。
	 * 边界：RequestId 无效或身份为空时构造不出幂等键，只能稳定拒绝且不进缓存——重试会重新走一遍同样的拒绝，没有副作用。
	 *       其余所有终态（成功与拒绝）都进缓存，与项目其他写口一致。
	 */
	FCatAggregationResult ContributePlayerChum(const FCatChumContributionCommand& Command, ACatCharacter* ThrowerCharacter);

private:
	/** 组合服务器身份与 RequestId 的幂等键；两名玩家即使生成了同一个 RequestId 也落在各自的键空间里。 */
	static FString MakeTerminalKey(const FString& StableNetId, const FGuid& RequestId);

	/** 投窝链的首次终态；覆盖预留、窝点写入和耗材消耗三步，重放不会再扣耗材或增加窝料池。 */
	TMap<FString, FCatAggregationResult> TerminalCache;

	/** 首次终态对应的业务载荷签名；同 RequestId 换窝料、落点、装备 Revision 或窝点 Revision 会被拒绝。 */
	TMap<FString, FString> TerminalPayloadByKey;
};
