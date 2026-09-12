#pragma once

#include "CoreMinimal.h"

/**
 * 主控在搏斗中的腿部移动附加（钓鱼规则 §4.4）。按秒计费、只看操作方向：
 * 前移（靠水）与后退（离水）两个常数，实际位移、力量差与线上负载都不进这笔账——
 * 负载由收线/转杆/顶住三项承担。转杆、收线与竿端支撑仍各自独立结算。
 */
struct CATFISHING_API FCatFightOperatorMovementCostInput
{
    /** 服务器接受的世界空间移动意图，已限幅到单位长度。零意图即零费用：被鱼拖走不另收费。 */
    FVector MoveIntentWorld = FVector::ZeroVector;
    /** 输入坐标系的前向（玩家视角朝向）。W/S 相对它判定，不随身体转向翻转。 */
    FVector InputForwardWorld = FVector::ForwardVector;
    double FixedStepSeconds = 0.0;
    /** 力竭或禁用移动时为 0，此时不产生腿部附加；它只是开关，不参与定价。 */
    double ActiveStrength = 0.0;
    double ForwardStaminaPerSecond = 1.5;
    double BackwardStaminaPerSecond = 3.0;
    double MovementStaminaMultiplier = 1.0;
};

/** 一段移动样本的腿部账单。距离与运动缺失不再参与定价，因此不再在结果里出现。 */
struct CATFISHING_API FCatFightOperatorMovementCostResult
{
    double StaminaDrain = 0.0;
    /** 本段按后退（离水）档计费；仅供诊断与表现读取。 */
    bool bBackward = false;
};

/** 把主控的移动意图折算成按秒的腿部附加；鱼与物理协助仍走 CatIntentMotionModel 的意图缺口模型。 */
class CATFISHING_API FCatFishingOperatorWorkModel
{
public:
    static bool ComputeMovementStaminaDrain(const FCatFightOperatorMovementCostInput& Input,
        FCatFightOperatorMovementCostResult& OutResult);
};
