#include "Fishing/Simulation/CatFishingFightWorkModel.h"

bool FCatFishingFightWorkModel::ComputeCatWorkDrain(const FCatFightCatWorkInput& Input, double& OutDrain)
{
	OutDrain = 0.0;
	for (const double Value : {Input.PositiveWorkUnits, Input.CostPerWorkUnit, Input.NormalizedLoad,
		Input.UnloadedWorkMultiplier, Input.LoadStaminaMultiplier, Input.ActionMultiplier})
	{
		if (!FMath::IsFinite(Value) || Value < 0.0) return false;
	}
	if (Input.NormalizedLoad > 1.0) return false;
	OutDrain = Input.PositiveWorkUnits * Input.CostPerWorkUnit * Input.ActionMultiplier
		* (Input.UnloadedWorkMultiplier + Input.NormalizedLoad * Input.LoadStaminaMultiplier);
	return FMath::IsFinite(OutDrain);
}

bool FCatFishingFightWorkModel::ComputeFishIntentDrain(const FCatFightFishIntentInput& Input,
	FCatFightFishIntentResult& OutResult)
{
	OutResult = FCatFightFishIntentResult{};
	for (const double Value : {Input.IntendedDisplacementCentimeters.X, Input.IntendedDisplacementCentimeters.Y,
		Input.IntendedDisplacementCentimeters.Z, Input.ActualDisplacementCentimeters.X,
		Input.ActualDisplacementCentimeters.Y, Input.ActualDisplacementCentimeters.Z,
		Input.StaminaPerUnfulfilledMeter})
	{
		if (!FMath::IsFinite(Value)) return false;
	}
	if (Input.StaminaPerUnfulfilledMeter < 0.0) return false;

	FCatFightFishIntentResult Result;
	Result.IntendedDistanceCentimeters = Input.IntendedDisplacementCentimeters.Size();
	if (!FMath::IsFinite(Result.IntendedDistanceCentimeters)) return false;
	// 没有主动意图时，纯被动位移不能单独产生费用，也不存在可用于投影的主动方向。
	if (Result.IntendedDistanceCentimeters <= 0.0) return true;
	const FVector IntentDirection = Input.IntendedDisplacementCentimeters / Result.IntendedDistanceCentimeters;
	Result.ActualProgressCentimeters = FVector::DotProduct(Input.ActualDisplacementCentimeters, IntentDirection);
	if (!FMath::IsFinite(Result.ActualProgressCentimeters)) return false;
	const double MissingProgress = Result.IntendedDistanceCentimeters - Result.ActualProgressCentimeters;
	if (!FMath::IsFinite(MissingProgress)) return false;
	// 稳速斜游的世界位置相减可能留下极小正误差，不能让它触发体力尾数吸附。
	// 容差只作用于 cm 距离；真实距离仍按原米价收费，不把小金额当作零。
	Result.UnfulfilledDistanceCentimeters = MissingProgress > UE_DOUBLE_SMALL_NUMBER ? MissingProgress : 0.0;
	// 意图已包含当前出力与步长，不再乘出力比例或时间；先将 cm 转为 m，再按独立米价结算。
	Result.StaminaDrain = Result.UnfulfilledDistanceCentimeters / 100.0 * Input.StaminaPerUnfulfilledMeter;
	if (!FMath::IsFinite(Result.StaminaDrain)) return false;
	OutResult = Result;
	return true;
}
