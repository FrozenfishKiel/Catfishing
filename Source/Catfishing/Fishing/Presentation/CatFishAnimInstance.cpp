#include "Fishing/Presentation/CatFishAnimInstance.h"

#include "Fishing/Actors/CatFishEncounterActor.h"

void UCatFishAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	RefreshFromFishOwner(0.0f, true);
}

void UCatFishAnimInstance::NativeUpdateAnimation(const float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	RefreshFromFishOwner(DeltaSeconds, false);
}

void UCatFishAnimInstance::RefreshFromFishOwner(const float DeltaSeconds, const bool bSnapPlayRate)
{
	const ACatFishEncounterActor* Fish = Cast<ACatFishEncounterActor>(GetOwningActor());
	if (!Fish)
	{
		MotionIntent = ECatFishMotionIntent::None;
		IntendedSwimSpeedCentimetersPerSecond = 0.0f;
		SwimPlayRate = 1.0f;
		FishLineAlignment = 0.0f;
		NormalizedLineLoad = 0.0f;
		bStrongConfrontation = false;
		return;
	}

	const FCatFishEncounterPresentationState& State = Fish->GetPresentationState();
	MotionIntent = State.MotionIntent;
	IntendedSwimSpeedCentimetersPerSecond = FMath::Max(0.0f,
		State.IntendedSwimSpeedCentimetersPerSecond);
	FishLineAlignment = FMath::Clamp(State.FishLineAlignment, -1.0f, 1.0f);
	NormalizedLineLoad = FMath::Clamp(State.NormalizedLineLoad, 0.0f, 1.0f);
	bStrongConfrontation = State.bStrongConfrontation;

	// None/AutoHauling 使用各自的待机或侧翻图，游泳倍率回到中性值；只有两种活鱼意图按自由游速映射。
	const bool bHasLiveSwimIntent = MotionIntent == ECatFishMotionIntent::CalmOrInward
		|| MotionIntent == ECatFishMotionIntent::StrugglingOutward;
	const float SafeReferenceSpeed = FMath::Max(1.0f, ReferenceSwimSpeedCentimetersPerSecond);
	const float SafeMinimumPlayRate = FMath::Max(0.0f, MinimumSwimPlayRate);
	const float SafeMaximumPlayRate = FMath::Max(SafeMinimumPlayRate, MaximumSwimPlayRate);
	const float TargetPlayRate = bHasLiveSwimIntent && IntendedSwimSpeedCentimetersPerSecond > 0.0f
		? FMath::Clamp(IntendedSwimSpeedCentimetersPerSecond / SafeReferenceSpeed,
			SafeMinimumPlayRate, SafeMaximumPlayRate)
		: 1.0f;

	const float SafeInterpolationSpeed = FMath::Max(0.0f, SwimPlayRateInterpolationSpeed);
	SwimPlayRate = bSnapPlayRate || DeltaSeconds <= 0.0f || SafeInterpolationSpeed <= 0.0f
		? TargetPlayRate
		: FMath::FInterpTo(SwimPlayRate, TargetPlayRate, DeltaSeconds, SafeInterpolationSpeed);
}
