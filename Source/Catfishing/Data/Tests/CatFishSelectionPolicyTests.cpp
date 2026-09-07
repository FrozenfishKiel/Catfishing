#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Animation/AnimSequenceBase.h"
#include "Curves/CurveFloat.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Data/CatFishSelectionTypes.h"
#include "Engine/SkeletalMesh.h"
#include "Fishing/Presentation/CatFishAnimInstance.h"
#include "Fishing/Presentation/CatFishPresentationDefinition.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishSelectionOptionalEligibilityGateTest,
	"Catfishing.Unit.Data.FishSelection.OptionalTimeAndWeatherGatesRemainNeutralUntilEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishSelectionPostFilterNormalizationTest,
	"Catfishing.Unit.Data.FishSelection.FiltersBeforeNormalizingRemainingCandidates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFormalFishSelectionWeightStrengthTest,
	"Catfishing.Unit.Data.FishSelection.FormalCatalogFreezesWeightDerivedStrength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatFishSelectionPolicyTestsPrivate
{
	static UCatFishPresentationDefinition* MakePresentationDefinition()
	{
		UCatFishPresentationDefinition* Presentation = NewObject<UCatFishPresentationDefinition>(GetTransientPackage());
		Presentation->SkeletalMesh = TSoftObjectPtr<USkeletalMesh>(
			FSoftObjectPath(TEXT("/Game/Test/FishMesh.FishMesh")));
		Presentation->AnimInstanceClass = TSoftClassPtr<UCatFishAnimInstance>(
			FSoftObjectPath(TEXT("/Script/Catfishing.CatFishAnimInstance")));
		Presentation->CalmAnimation = TSoftObjectPtr<UAnimSequenceBase>(
			FSoftObjectPath(TEXT("/Game/Test/Calm.Calm")));
		Presentation->StruggleAnimation = TSoftObjectPtr<UAnimSequenceBase>(
			FSoftObjectPath(TEXT("/Game/Test/Struggle.Struggle")));
		Presentation->ExhaustedAnimation = TSoftObjectPtr<UAnimSequenceBase>(
			FSoftObjectPath(TEXT("/Game/Test/Exhausted.Exhausted")));
		Presentation->LandedAnimation = Presentation->ExhaustedAnimation;
		Presentation->MeshReferenceWeightKilograms = 1.0;
		Presentation->MinimumUniformScale = 0.5;
		Presentation->MaximumUniformScale = 2.0;
		return Presentation;
	}

	static UCatFishDefinition* MakeFishDefinition(const FName FishId, const double SpawnWeight,
		UCatFishPresentationDefinition* Presentation)
	{
		UCatFishDefinition* Definition = NewObject<UCatFishDefinition>(GetTransientPackage());
		Definition->bEnableRuntimeDefinition = true;
		Definition->FishDefinitionId = FishId;
		Definition->PresentationDefinition = Presentation;
		Definition->BodyClass = ECatFishBodyClass::Standard;
		Definition->SacrificeContribution = 1;
		Definition->RarityTierId = TEXT("TestRarity");
		Definition->RegionIds = {TEXT("TestLake")};
		Definition->TimeOfDay = {ECatEnvironmentTimeOfDay::Morning};
		Definition->Weather = {ECatEnvironmentWeather::Clear};
		Definition->SpawnWeight = SpawnWeight;
		Definition->MinimumWeightKilograms = 1.0;
		Definition->MaximumWeightKilograms = 1.0;
		Definition->MinimumFightParticipants = 1;
		Definition->FishStrength = 5.0;
		Definition->FishFightStamina = 5.0;
		Definition->BitePersonalityId = TEXT("TestBite");
		Definition->FightPersonalityId = TEXT("TestFight");
		Definition->FoodSafety = ECatFishFoodSafety::Safe;
		Definition->EatingExperience = 1.0;
		return Definition;
	}
}

bool FCatFishSelectionOptionalEligibilityGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UCatFishDefinition* Definition = NewObject<UCatFishDefinition>(GetTransientPackage());
	if (!TestNotNull(TEXT("creates transient fish definition"), Definition))
	{
		return false;
	}
	Definition->TimeOfDay = {ECatEnvironmentTimeOfDay::Morning};
	Definition->Weather = {ECatEnvironmentWeather::Clear};
	Definition->MinimumFightParticipants = 2;

	TestTrue(TEXT("disabled time gate ignores unmatched context"),
		FCatFishEligibilityPolicy::PassesTimeOfDay(*Definition, ECatEnvironmentTimeOfDay::Dusk, false));
	TestTrue(TEXT("disabled weather gate ignores unmatched context"),
		FCatFishEligibilityPolicy::PassesWeather(*Definition, ECatEnvironmentWeather::Rain, false));
	TestFalse(TEXT("enabled time gate rejects unmatched context"),
		FCatFishEligibilityPolicy::PassesTimeOfDay(*Definition, ECatEnvironmentTimeOfDay::Dusk, true));
	TestTrue(TEXT("enabled time gate accepts configured context"),
		FCatFishEligibilityPolicy::PassesTimeOfDay(*Definition, ECatEnvironmentTimeOfDay::Morning, true));
	TestFalse(TEXT("enabled weather gate rejects unmatched context"),
		FCatFishEligibilityPolicy::PassesWeather(*Definition, ECatEnvironmentWeather::Rain, true));
	TestTrue(TEXT("enabled weather gate accepts configured context"),
		FCatFishEligibilityPolicy::PassesWeather(*Definition, ECatEnvironmentWeather::Clear, true));
	TestFalse(TEXT("participant gate rejects insufficient active players"),
		FCatFishEligibilityPolicy::PassesActivePlayerCount(*Definition, 1));
	TestTrue(TEXT("participant gate accepts the configured minimum"),
		FCatFishEligibilityPolicy::PassesActivePlayerCount(*Definition, 2));
	return !HasAnyErrors();
}

bool FCatFishSelectionPostFilterNormalizationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UCatFishCatalogSettings* Settings = NewObject<UCatFishCatalogSettings>(GetTransientPackage());
	UCurveFloat* SaturationCurve = NewObject<UCurveFloat>(GetTransientPackage());
	UCatFishPresentationDefinition* Presentation =
		CatFishSelectionPolicyTestsPrivate::MakePresentationDefinition();
	UCatFishDefinition* LightFish = CatFishSelectionPolicyTestsPrivate::MakeFishDefinition(
		TEXT("LightFish"), 1.0, Presentation);
	UCatFishDefinition* HeavyFish = CatFishSelectionPolicyTestsPrivate::MakeFishDefinition(
		TEXT("HeavyFish"), 3.0, Presentation);
	// 旧静态字段故意制造巨大差异；选鱼必须只使用本次抽到的重量和统一换算系数。
	HeavyFish->FishStrength = 500.0;
	if (!TestNotNull(TEXT("creates transient catalog settings"), Settings)
		|| !TestNotNull(TEXT("creates transient saturation curve"), SaturationCurve)
		|| !TestNotNull(TEXT("creates transient presentation"), Presentation)
		|| !TestNotNull(TEXT("creates first transient fish"), LightFish)
		|| !TestNotNull(TEXT("creates second transient fish"), HeavyFish))
	{
		return false;
	}
	TestTrue(TEXT("transient presentation is runtime-ready"), Presentation->IsRuntimeDefinitionReady());
	TestTrue(TEXT("first transient fish is runtime-ready"), LightFish->IsRuntimeDefinitionReady());
	TestTrue(TEXT("second transient fish is runtime-ready"), HeavyFish->IsRuntimeDefinitionReady());

	SaturationCurve->FloatCurve.AddKey(0.0f, 1.0f);
	SaturationCurve->FloatCurve.AddKey(1.0f, 3.0f);
	Settings->Definitions = {LightFish, HeavyFish};
	Settings->ChumSaturationCurve = SaturationCurve;
	Settings->ChumAffinityHalfSaturation = 10.0;
	Settings->MaximumChumModifier = 3.0;
	Settings->ComfortChallengeMaximumRatio = 0.65;
	Settings->MatchedChallengeMaximumRatio = 1.05;
	Settings->MaximumChallengeRatio = 1.35;
	Settings->TargetChallengeRatio = 0.9;
	Settings->ComfortChallengeBandWeight = 1.0;
	Settings->MatchedChallengeBandWeight = 0.0;
	Settings->RiskyChallengeBandWeight = 0.0;
	Settings->MinimumChallengeWeightMultiplier = 0.25;
	Settings->bEnableTimeOfDayEligibilityFilter = false;
	Settings->bEnableWeatherEligibilityFilter = false;

	FCatFishSelectionContext Context;
	Context.WaterRegion.RegionId = TEXT("TestLake");
	Context.WaterRegion.GeometryRevision = 1;
	Context.ChumSample.bSucceeded = true;
	Context.ChumSample.WaterRegion = Context.WaterRegion;
	Context.TimeOfDay = ECatEnvironmentTimeOfDay::Dusk;
	Context.Weather = ECatEnvironmentWeather::Rain;
	Context.ActivePlayerCount = 1;
	Context.CombinedFishingStrength = 10.0;
	Context.CombinedFightStamina = 10.0;
	Context.StrengthPerKilogram = 10.0;
	Context.RandomSeed = 20260901;

	const FCatFishSelectionResult BypassedResult = Settings->SelectRuntimeDefinition(Context);
	TestTrue(TEXT("disabled time and weather filters leave valid candidates selectable"),
		BypassedResult.bSelected);
	TestEqual(TEXT("both fish remain after challenge and participant filters"),
		BypassedResult.EligibleCandidateCount, 2);
	TestEqual(TEXT("both fish remain in the selected challenge band"),
		BypassedResult.SelectedBandCandidateCount, 2);
	const double ExpectedProbability = BypassedResult.FishDefinitionId == TEXT("HeavyFish") ? 0.75 : 0.25;
	TestEqual(TEXT("reported probability is normalized only across remaining candidates"),
		BypassedResult.SelectedNormalizedProbability, ExpectedProbability, UE_DOUBLE_SMALL_NUMBER);
	TestEqual(TEXT("selected individual weight is frozen once"),
		BypassedResult.WeightKilograms, 1.0, UE_DOUBLE_SMALL_NUMBER);
	TestEqual(TEXT("fish strength is sampled weight times shared coefficient"),
		BypassedResult.BaseFishStrength, 10.0, UE_DOUBLE_SMALL_NUMBER);

	Settings->bEnableTimeOfDayEligibilityFilter = true;
	const FCatFishSelectionResult EnabledResult = Settings->SelectRuntimeDefinition(Context);
	TestFalse(TEXT("enabling the time gate activates the existing definition data"), EnabledResult.bSelected);
	TestEqual(TEXT("unmatched time removes both candidates before normalization"),
		EnabledResult.EligibleCandidateCount, 0);
	return !HasAnyErrors();
}

bool FCatFormalFishSelectionWeightStrengthTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const UCatFishCatalogSettings* Settings = GetDefault<UCatFishCatalogSettings>();
	if (!TestNotNull(TEXT("loads formal fish catalog"), Settings))
	{
		return false;
	}
	FCatFishSelectionContext Context;
	Context.WaterRegion.RegionId = TEXT("River");
	Context.WaterRegion.GeometryRevision = 1;
	Context.ChumSample.bSucceeded = true;
	Context.ChumSample.WaterRegion = Context.WaterRegion;
	Context.ActivePlayerCount = 1;
	Context.CombinedFishingStrength = 50.0;
	Context.CombinedFightStamina = 60.0;
	Context.StrengthPerKilogram = 10.0;
	Context.RandomSeed = 20260903;
	const FCatFishSelectionResult First = Settings->SelectRuntimeDefinition(Context);
	const FCatFishSelectionResult Replay = Settings->SelectRuntimeDefinition(Context);
	if (!TestTrue(TEXT("formal River catalog still selects an eligible individual"), First.bSelected)
		|| !TestEqual(TEXT("same opportunity replays the fish id"),
			Replay.FishDefinitionId, First.FishDefinitionId)
		|| !TestEqual(TEXT("same opportunity replays the individual weight"),
			Replay.WeightKilograms, First.WeightKilograms, UE_DOUBLE_SMALL_NUMBER))
	{
		return false;
	}
	const UCatFishDefinition* Definition = Settings->FindRuntimeDefinition(First.FishDefinitionId);
	TestNotNull(TEXT("selected formal fish resolves"), Definition);
	TestTrue(TEXT("selected weight stays inside its formal definition"), Definition
		&& First.WeightKilograms >= Definition->MinimumWeightKilograms
		&& First.WeightKilograms <= Definition->MaximumWeightKilograms);
	TestEqual(TEXT("formal runtime strength comes from that exact individual weight"),
		First.BaseFishStrength, First.WeightKilograms * Context.StrengthPerKilogram, 1e-6);
	return !HasAnyErrors();
}

#endif
