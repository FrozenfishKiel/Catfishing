#pragma once

#include "CoreMinimal.h"

struct CATFISHING_API FCatFishMotionSolveInput
{
	FVector RodTipWorldPosition = FVector::ZeroVector;
	FVector ProposedFishWorldPosition = FVector::ZeroVector;
	FBox WaterBounds = FBox(ForceInit);
	double MaximumLineLengthCentimeters = 0.0;
};

struct CATFISHING_API FCatFishMotionSolveResult
{
	bool bSucceeded = false;
	FVector FishWorldPosition = FVector::ZeroVector;
};

/** 真实岸线把活鱼候选点修回水内时，用于消除位置回弹的纯输入。 */
struct CATFISHING_API FCatFishShoreContactInput
{
	FVector CurrentFishWorldPosition = FVector::ZeroVector;
	FVector CandidateFishWorldPosition = FVector::ZeroVector;
	FVector ResolvedWaterWorldPosition = FVector::ZeroVector;
	FVector WaterwardDirection = FVector::ForwardVector;
	FVector RodTipWorldPosition = FVector::ZeroVector;
	/** 本步开始时的 L_paid；只允许左键收线受岸线阻挡时回退到这个上限。 */
	double PreviousLineLengthCentimeters = 0.0;
	double ProposedLineLengthCentimeters = 0.0;
	/** 双端约束本步已经允许的实际端点距离；可大于 L_paid，0 表示沿用旧的刚性线长。 */
	double MaximumConstraintDistanceCentimeters = 0.0;
	bool bReeling = false;
	/** 右键松开线杯；最终 L_paid 只能按岸线校正后的真实鱼距增长，不能按候选点凭空出线。 */
	bool bSlacking = false;
	double CorrectionToleranceCentimeters = 1.0;
};

struct CATFISHING_API FCatFishShoreContactResult
{
	bool bSucceeded = false;
	bool bShoreContact = false;
	FVector FishWorldPosition = FVector::ZeroVector;
	double LineLengthCentimeters = 0.0;
};

/** Pure deterministic projection into the frozen water geometry and rod line reach. */
class CATFISHING_API FCatFishFightMotionSolver
{
public:
	static FCatFishMotionSolveResult Solve(const FCatFishMotionSolveInput& Input);

	/**
	 * 活鱼撞岸时移除水域安全点造成的法向跳变，但保留本步沿岸切向位移，让下一步靠 Steering 平滑游离；
	 * 修正后的鱼位置仍截在本步双端求解允许的距离内；松开线杯时，最终线长只跟随岸线校正后的真实鱼距。
	 */
	static FCatFishShoreContactResult ResolveLiveFishShoreContact(
		const FCatFishShoreContactInput& Input);
};
