#include "Environment/CatChumFieldTypes.h"

#include "Curves/CurveFloat.h"
#include "GameFramework/Actor.h"

namespace CatChumFieldTypesPrivate
{
	static bool BakeCurve(const TSoftObjectPtr<UCurveFloat>& CurveReference, FCatChumFalloffTable& OutTable)
	{
		OutTable = FCatChumFalloffTable();
		const UCurveFloat* Curve = CurveReference.Get();
		if (!Curve || Curve->FloatCurve.GetNumKeys() == 0)
		{
			return false;
		}
		for (int32 Index = 0; Index < FCatChumFalloffTable::SampleCount; ++Index)
		{
			const float Input = static_cast<float>(Index) / static_cast<float>(FCatChumFalloffTable::SampleCount - 1);
			const double Value = static_cast<double>(Curve->GetFloatValue(Input));
			if (!FMath::IsFinite(Value) || Value < 0.0)
			{
				OutTable = FCatChumFalloffTable();
				return false;
			}
			OutTable.Samples[Index] = Value;
		}
		return OutTable.IsRuntimeReady();
	}
}

bool FCatChumInfluenceSpec::IsRuntimeReady() const
{
	FCatChumRuntimeInfluence Runtime;
	return BuildRuntimeInfluence(1, Runtime);
}

bool FCatChumInfluenceSpec::IsUnconfigured() const
{
	return RadiusCentimeters == 0.0 && DurationSeconds == 0.0
		&& BaseContribution.Fishy == 0.0 && BaseContribution.Fragrant == 0.0 && BaseContribution.Fermented == 0.0
		&& DistanceFalloffCurve.IsNull() && TimeFalloffCurve.IsNull() && MaximumQuantityPerPlacement == 0
		&& PresentationId.IsNone() && PresentationClass.IsNull();
}

bool FCatChumInfluenceSpec::BuildRuntimeInfluence(const int32 Quantity, FCatChumRuntimeInfluence& OutRuntime) const
{
	OutRuntime = FCatChumRuntimeInfluence();
	if (!FMath::IsFinite(RadiusCentimeters) || RadiusCentimeters <= 0.0
		|| !FMath::IsFinite(DurationSeconds) || DurationSeconds <= 0.0
		|| !BaseContribution.IsValidContribution() || MaximumQuantityPerPlacement <= 0
		|| Quantity <= 0 || Quantity > MaximumQuantityPerPlacement
		|| DistanceFalloffCurve.IsNull() || TimeFalloffCurve.IsNull())
	{
		return false;
	}
	if (!PresentationClass.IsNull())
	{
		const UClass* LoadedClass = PresentationClass.Get();
		if (!LoadedClass || !LoadedClass->IsChildOf(AActor::StaticClass()))
		{
			return false;
		}
	}

	FCatChumRuntimeInfluence Candidate;
	Candidate.RadiusCentimeters = RadiusCentimeters;
	Candidate.DurationSeconds = DurationSeconds;
	Candidate.BaseContribution = BaseContribution.ScaledBy(static_cast<double>(Quantity));
	Candidate.PresentationId = PresentationId;
	if (!Candidate.BaseContribution.IsValidContribution()
		|| !CatChumFieldTypesPrivate::BakeCurve(DistanceFalloffCurve, Candidate.DistanceFalloff)
		|| !CatChumFieldTypesPrivate::BakeCurve(TimeFalloffCurve, Candidate.TimeFalloff))
	{
		return false;
	}
	OutRuntime = MoveTemp(Candidate);
	return true;
}

double FCatChumFalloffTable::Evaluate(const double NormalizedInput) const
{
	if (!FMath::IsFinite(NormalizedInput) || !IsRuntimeReady())
	{
		return 0.0;
	}
	const double Clamped = FMath::Clamp(NormalizedInput, 0.0, 1.0);
	const double Position = Clamped * static_cast<double>(SampleCount - 1);
	const int32 Lower = FMath::Clamp(FMath::FloorToInt(Position), 0, SampleCount - 1);
	const int32 Upper = FMath::Min(Lower + 1, SampleCount - 1);
	return FMath::Lerp(Samples[Lower], Samples[Upper], Position - static_cast<double>(Lower));
}

bool FCatChumFalloffTable::IsRuntimeReady() const
{
	for (const double Value : Samples)
	{
		if (!FMath::IsFinite(Value) || Value < 0.0)
		{
			return false;
		}
	}
	return Samples[0] > 0.0;
}
