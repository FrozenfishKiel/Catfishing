#include "Fishing/Simulation/CatFishingOperatorWorkModel.h"

// 计费流程：
// 1. 先拒绝非法向量与负数价格，保持与其他做功模型一致的 fail-closed。
// 2. 再把意图和输入前向压到水平面；没有出力能力、没有意图或前向不可用时不产生费用。
// 3. 最后只按意图在输入前向上的符号选一档按秒常数：前移（靠水）一档、后退（离水）一档。
//    不读实际位移，所以被鱼拖住走不动不会多扣——力量差归搏斗项，不进腿部消耗。
bool FCatFishingOperatorWorkModel::ComputeMovementStaminaDrain(const FCatFightOperatorMovementCostInput& Input,
    FCatFightOperatorMovementCostResult& OutResult)
{
    OutResult = {};
    if (Input.MoveIntentWorld.ContainsNaN() || Input.InputForwardWorld.ContainsNaN()) return false;
    for (const double Value : {Input.FixedStepSeconds, Input.ActiveStrength, Input.ForwardStaminaPerSecond,
        Input.BackwardStaminaPerSecond, Input.MovementStaminaMultiplier})
        if (!FMath::IsFinite(Value) || Value < 0) return false;
    const FVector Intent = FVector(Input.MoveIntentWorld.X, Input.MoveIntentWorld.Y, 0).GetClampedToMaxSize(1);
    const FVector Forward = FVector(Input.InputForwardWorld.X, Input.InputForwardWorld.Y, 0).GetSafeNormal();
    if (Input.ActiveStrength <= 0 || Intent.IsNearlyZero() || Forward.IsNearlyZero()) return true;
    const double ForwardAxis = FVector::DotProduct(Intent, Forward);
    // 纯侧移（A/D）在设计里没有档位，先不计腿部附加，等裁决落下来再补一档。
    if (FMath::IsNearlyZero(ForwardAxis)) return true;
    const bool bBackward = ForwardAxis < 0;
    const double Drain = (bBackward ? Input.BackwardStaminaPerSecond : Input.ForwardStaminaPerSecond)
        * Input.FixedStepSeconds * Input.MovementStaminaMultiplier;
    // 溢出时保持 OutResult 的清零状态，拒绝不泄漏部分费用（与竿端结算同一套 fail-closed）。
    if (!FMath::IsFinite(Drain)) return false;
    OutResult.bBackward = bBackward;
    OutResult.StaminaDrain = Drain;
    return true;
}
