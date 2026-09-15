#pragma once

#include "CoreMinimal.h"
#include "Fishing/Simulation/CatFishSteeringModel.h"

class UCatFightPersonalityDefinition;
class UCatFishDefinition;

/**
 * 一条鱼进搏斗时真正生效的行为参数。
 *
 * 正式来源是鱼表格四列（食性／发力段长／休息段长／游速系数），2026-09-09 晚裁定逐鱼配、
 * 四套 Fight_* 性格模板退为测试用（台账 D-16「不恢复旧模型」同批被推翻）。
 * 鱼表某列还没填时该列退回模板原值——模板没有被删，只是不再是正式口径。
 */
struct CATFISHING_API FCatFishResolvedBehavior
{
	/** 交给 Runner 的转向/出力/段长配置；段长两列已按鱼表覆盖。 */
	FCatFishSteeringConfig SteeringConfig;

	/** 本鱼满力游速（厘米/秒）＝ 模板满力游速 × 鱼表「游速系数」。 */
	double FullEffortSpeedCentimetersPerSecond = 0.0;

	/** 四列里实际来自鱼表的有几列；0 表示这条鱼完全按测试模板跑。只用于日志与验收，不进玩法。 */
	int32 FieldsTakenFromFishTable = 0;
};

/** 把鱼定义与测试期性格模板合成一份行为参数；无 World/Actor，服务器与测试可直接调用。 */
struct CATFISHING_API FCatFishBehaviorProfileResolver
{
	/**
	 * 解析本鱼的搏斗行为参数。
	 * Fish 必须有效；TestingTemplate 允许为空——那时只有鱼表已填的列生效，其余保持结构默认值，
	 * 调用方仍要用 SteeringConfig.IsValid() 决定能不能开场，本函数不替它 fail-open。
	 * 返回 false 表示输入不足以合成一份可用参数。
	 */
	static bool Resolve(const UCatFishDefinition& Fish, const UCatFightPersonalityDefinition* TestingTemplate,
		FCatFishResolvedBehavior& OutBehavior);
};
