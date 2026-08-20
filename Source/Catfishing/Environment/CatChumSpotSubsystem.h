#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Environment/CatWaterTypes.h"
#include "CatChumSpotSubsystem.generated.h"

/**
 * 一局之内所有窝点的唯一服务器真相。
 *
 * 概念：窝点不是关卡里预先划好的河段，而是"某次投料落在哪儿"临时长出来的圆形区域——圆心是落点，半径是建窝那一刻的配置快照。
 * 它会随时间衰减，衰减到几乎没有味道时整个窝点消散。因此窝料池挂在这里，而不是挂在关卡静态水域 Actor 上：
 * ACatWaterRegion 描述的是"哪里是水"，与"哪里有窝"是两件事，一个窝点也完全可能横跨两片水域几何。
 *
 * 边界：只在 authority World 创建，客户端不持有第二份池；本子系统不复制、不发事件，也不决定钓上什么鱼——
 * 它只回答"这个坐标上现在有多少腥香酵"，选鱼与咬钩判定由消费方按快照自己算。
 */
UCLASS()
class CATFISHING_API UCatChumSpotSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 仅在 authority Game World 创建窝点池；客户端根本不创建本子系统，因此不存在第二份窝料真相。
	 *  窝点在客户端的可见表现（猫爪标记、剩余时间）目前还没有接线，它属于 UI 侧的另一批工作。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** World 销毁时清空本局窝点、衰减节拍与终态缓存；窝料不跨局残留。 */
	virtual void Deinitialize() override;

	/** 每帧把真实流逝时间喂给全局衰减节拍；节拍本身的推进规则在 AdvanceDecay。 */
	virtual void Tick(float DeltaTime) override;

	/** Tickable 统计标识；只用于 profiler 分组，不参与任何玩法判定。 */
	virtual TStatId GetStatId() const override;

	/** 玩家窝料与自然事件共用的唯一投窝写口；按 RequestId 幂等、按载荷签名防复用、按 Revision 防陈旧地原子提交。 */
	FCatAggregationResult ContributeChum(const FCatAggregationCommand& Command);

	/** 只读验证一条投窝命令是否可立即提交；跨 Equipment 到 Environment 的协调先调用它，避免扣除耗材后才发现被拒。 */
	ECatDomainCommandError ValidateChum(const FCatAggregationCommand& Command) const;

	/** 查询某个世界坐标当前落在哪个窝点里；命中多个相交窝点时取圆心最近的一个，一个都没命中时返回 bHasSpot=false 的空快照。 */
	FCatChumSpotSnapshot QueryChumSpot(const FVector& WorldLocation) const;

	/** 当前窝点集合的 Revision；调用方据此构造投窝命令的 ExpectedRevision。 */
	int64 GetAggregationRevision() const { return AggregationRevision; }

	/**
	 * 判断"聚鱼时刻"这一个机制现在是否还在冷却里。
	 *
	 * 聚鱼时刻有玩家叠窝和森林湖自然涌现两个触发源，但只有一本账：任何一次成功投窝都会重置这里的计时，
	 * 所以自然涌现不会叠在玩家刚投好的窝上再送一份收益。玩家自己投窝不查这个谓词——给玩家投窝加冷却是产品改动，飞书没裁过。
	 * CooldownSeconds 非正或非有限时视为没有配置冷却，一律返回 false；本局一次都还没投过时也返回 false。
	 */
	bool IsAggregationMomentOnCooldown(double CooldownSeconds) const;

	/** 按流逝秒数推进全局衰减节拍；Tick 走这里，自动化测试也直接调它以获得确定的节拍时序。 */
	void AdvanceDecay(double DeltaSeconds);

	/** 局末或 World 收口时清空所有窝点与终态缓存并推进 Revision；旧查询与重试因此自然失效。 */
	void ResetRuntimeChumFromAuthority();

private:
	/** 一个正在生效的窝点；它没有稳定 ID，因为投窝与查询都按坐标解析，外部拿不到也不需要窝点句柄。 */
	struct FChumSpot
	{
		/** 建窝那次投料的落点，之后作为圆心固定不动；并池沿用它，不做质心平移。 */
		FVector Center = FVector::ZeroVector;
		/** 建窝那一刻从配置快照下来的半径；之后改配置只影响新窝，不改动已有窝的判定范围。 */
		double Radius = 0.0;
		/** 当前三轴余量；投料累加，全局衰减节拍按比例缩小。 */
		FCatChumVector Pool;
	};

	/** 找出圆心离目标点最近、且目标点确实在其圆内的窝点下标；没有命中返回 INDEX_NONE。 */
	int32 FindNearestSpotIndex(const FVector& WorldLocation) const;

	/** 把指定下标的窝点复制成只读快照；INDEX_NONE 返回带当前 Revision 的"无窝"快照。 */
	FCatChumSpotSnapshot MakeSpotSnapshot(int32 SpotIndex) const;

	/** 执行一次全局衰减：所有窝点按比例缩小，缩到 Total 低于地板的窝点整体归零并移除。 */
	void ApplyOneDecayStep();

	/** 本局当前存活的全部窝点；顺序无语义，命中判定一律靠圆心距离。 */
	TArray<FChumSpot> Spots;

	/** 窝点集合的写入版本，从 1 起，使默认 ExpectedRevision=0 天然冲突；只有投窝提交和局末清空推进它。
	 *  衰减刻意不推进它：衰减是确定性的全局过程，而投料是纯累加操作，两者可交换；
	 *  如果衰减也推 Revision，玩家读完快照再投料这段时间里只要跨过一次衰减就会被误判成陈旧写入。 */
	int64 AggregationRevision = 1;

	/** 距离下一次全局衰减还差多少秒的倒数累加器；投料不碰它，所以补窝不会重置衰减计时。 */
	double DecayAccumulatorSeconds = 0.0;

	/** 距离本局上一次成功投窝已经过去了多少秒；它是聚鱼时刻共享冷却的唯一计时来源，与衰减节拍分开，
	 *  因为衰减描述的是"窝在变淡"，而这里描述的是"这个机制刚被触发过"。任何一次 committed 投窝都把它清零。 */
	double SecondsSinceLastAggregation = 0.0;

	/** 本局是否已经有过一次成功投窝；没有过时冷却无从谈起，第一次自然涌现不应该被一个"上次投窝在开局那一刻"的假前提挡住。 */
	bool bHasAggregatedThisRun = false;

	/** 投窝命令首次终态缓存；同 RequestId 重放只回放首次结果，不再次加池。 */
	TMap<FString, FCatAggregationResult> ChumTerminalCache;

	/** 投窝首次终态对应的业务载荷签名；同 RequestId 换落点、三轴、Revision 或来源都会被判成载荷复用。 */
	TMap<FString, FString> ChumPayloadByKey;
};
