#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Curves/CurveFloat.h"
#include "Fishing/CatFishingSettings.h"

namespace CatFishCatalogSettingsTest
{
	// 构造流程：创建一条只满足目录筛选所需公开字段的鱼定义；调用方用力量和体力表达它相对玩家的连续挑战度。
	static UCatFishDefinition* MakeReadyFishDefinition(const FName FishDefinitionId,
		const ECatFishBodyClass BodyClass, const int32 MinimumFightParticipants,
		const double FishStrength, const double FishFightStamina)
	{
		UCatFishDefinition* Definition = NewObject<UCatFishDefinition>(GetTransientPackage());
		Definition->bEnableRuntimeDefinition = true;
		Definition->FishDefinitionId = FishDefinitionId;
		Definition->BodyClass = BodyClass;
		Definition->SacrificeContribution = 2;
		Definition->RarityTierId = TEXT("TestTier");
		Definition->RegionIds = {TEXT("LakeA")};
		Definition->TimeOfDay = {ECatEnvironmentTimeOfDay::Day};
		Definition->Weather = {ECatEnvironmentWeather::Clear};
		Definition->SpawnWeight = 1.0;
		Definition->MinimumWeightKilograms = 1.0;
		Definition->MaximumWeightKilograms = 2.0;
		Definition->MinimumFightParticipants = MinimumFightParticipants;
		Definition->FishStrength = FishStrength;
		Definition->FishFightStamina = FishFightStamina;
		Definition->BitePersonalityId = TEXT("Bite");
		Definition->FightPersonalityId = TEXT("Fight");
		Definition->FoodSafety = ECatFishFoodSafety::Safe;
		Definition->EatingExperience = 1.0;
		return Definition;
	}

	static void ConfigureReadySelection(UCatFishCatalogSettings& Settings)
	{
		UCurveFloat* Saturation = NewObject<UCurveFloat>(GetTransientPackage());
		Saturation->FloatCurve.AddKey(0.0f, 1.0f);
		Saturation->FloatCurve.AddKey(1.0f, 1.0f);
		Settings.ChumSaturationCurve = Saturation;
		Settings.ChumAffinityHalfSaturation = 1.0;
		Settings.MaximumChumModifier = 1.0;
		Settings.ComfortChallengeMaximumRatio = 0.65;
		Settings.MatchedChallengeMaximumRatio = 1.05;
		Settings.MaximumChallengeRatio = 1.35;
		Settings.TargetChallengeRatio = 1.0;
		Settings.ComfortChallengeBandWeight = 0.25;
		Settings.MatchedChallengeBandWeight = 0.60;
		Settings.RiskyChallengeBandWeight = 0.15;
		Settings.MinimumChallengeWeightMultiplier = 0.25;
	}

	static FCatFishSelectionContext MakeSelectionContext(const double FishingStrength = 100.0,
		const double FightStamina = 100.0)
	{
		FCatFishSelectionContext Context;
		Context.WaterRegion.RegionId = TEXT("LakeA");
		Context.WaterRegion.GeometryRevision = 1;
		Context.ChumSample.bSucceeded = true;
		Context.ChumSample.WaterRegion = Context.WaterRegion;
		Context.TimeOfDay = ECatEnvironmentTimeOfDay::Day;
		Context.Weather = ECatEnvironmentWeather::Clear;
		Context.BaitDefinitionId = TEXT("UnlistedBait");
		Context.ActivePlayerCount = 1;
		Context.CombinedFishingStrength = FishingStrength;
		Context.CombinedFightStamina = FightStamina;
		Context.RandomSeed = 17;
		return Context;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishCatalogDuplicateIdTest,
	"Catfishing.Unit.Data.FishCatalog.DuplicateRuntimeDefinitionsFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishCatalogLocalContextTest,
	"Catfishing.Unit.Data.FishCatalog.ZeroLocalChumAndUnlistedBaitAreNeutral",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishCatalogChallengeSafetyCeilingTest,
	"Catfishing.Unit.Data.FishCatalog.RiskyFishAllowedUntilChallengeSafetyCeiling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishCatalogChallengeBandTest,
	"Catfishing.Unit.Data.FishCatalog.AvailableChallengeBandsFollowConfiguredMix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishCatalogChallengeBandFallbackTest,
	"Catfishing.Unit.Data.FishCatalog.MissingWeightedBandFallsBackToAvailableFish",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishCatalogChallengeAffinityTest,
	"Catfishing.Unit.Data.FishCatalog.TargetChallengeOutweighsDistantFishWithinBand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishCatalogShowcaseCompatibilityTest,
	"Catfishing.Unit.Data.FishCatalog.ShowcaseCompatibilityFishRemainsSelectableBeforeRegionMigration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：把两条同 ID 且都完整的定义放入瞬态目录；按 ID 查询必须拒绝重复命中，防止 Fishing 随机拿到不稳定资产。
bool FCatFishCatalogDuplicateIdTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatFishCatalogSettings* Settings = NewObject<UCatFishCatalogSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态鱼目录 Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	UCatFishDefinition* FirstDefinition = CatFishCatalogSettingsTest::MakeReadyFishDefinition(
		TEXT("DuplicateFish"), ECatFishBodyClass::Standard, 1, 1.0, 1.0);
	UCatFishDefinition* SecondDefinition = CatFishCatalogSettingsTest::MakeReadyFishDefinition(
		TEXT("DuplicateFish"), ECatFishBodyClass::Standard, 1, 1.0, 1.0);
	Settings->Definitions = {FirstDefinition, SecondDefinition};

	TestNull(TEXT("重复 FishDefinitionId 返回空而不是任选一条"),
		Settings->FindRuntimeDefinition(TEXT("DuplicateFish")));
	return !HasAnyErrors();
}

bool FCatFishCatalogLocalContextTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UCatFishCatalogSettings* Settings = NewObject<UCatFishCatalogSettings>(GetTransientPackage());
	UCatFishDefinition* Definition = CatFishCatalogSettingsTest::MakeReadyFishDefinition(
		TEXT("NeutralFish"), ECatFishBodyClass::Standard, 1, 1.0, 1.0);
	Settings->Definitions = {Definition};
	CatFishCatalogSettingsTest::ConfigureReadySelection(*Settings);
	const FCatFishSelectionContext Context = CatFishCatalogSettingsTest::MakeSelectionContext(1.0, 1.0);
	const FCatFishSelectionResult Result = Settings->SelectRuntimeDefinition(Context);
	TestTrue(TEXT("zero local chum and unlisted bait remain neutral"), Result.bSelected);
	TestEqual(TEXT("target-ratio neutral final weight is spawn weight"), Result.SelectedFinalWeight, 1.0);
	return !HasAnyErrors();
}

// 测试流程：1.2 倍的鱼不再被旧的“必须弱于玩家”门槛删除，而 1.36 倍仍被可配置安全上限拒绝。
bool FCatFishCatalogChallengeSafetyCeilingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UCatFishCatalogSettings* Settings = NewObject<UCatFishCatalogSettings>(GetTransientPackage());
	CatFishCatalogSettingsTest::ConfigureReadySelection(*Settings);
	Settings->ComfortChallengeBandWeight = 0.0;
	Settings->MatchedChallengeBandWeight = 0.0;
	Settings->RiskyChallengeBandWeight = 1.0;
	UCatFishDefinition* Risky = CatFishCatalogSettingsTest::MakeReadyFishDefinition(
		TEXT("RiskyFish"), ECatFishBodyClass::Standard, 1, 120.0, 120.0);
	UCatFishDefinition* Unsafe = CatFishCatalogSettingsTest::MakeReadyFishDefinition(
		TEXT("UnsafeFish"), ECatFishBodyClass::Standard, 1, 136.0, 100.0);
	Settings->Definitions = {Risky, Unsafe};
	const FCatFishSelectionContext Context = CatFishCatalogSettingsTest::MakeSelectionContext();
	const FCatFishSelectionResult RiskyResult = Settings->SelectRuntimeDefinition(Context);
	TestTrue(TEXT("fish above player capability but within safety ceiling remains selectable"),
		RiskyResult.bSelected);
	TestEqual(TEXT("unsafe fish is filtered while risky fish remains"), RiskyResult.FishDefinitionId,
		FName(TEXT("RiskyFish")));
	Settings->Definitions = {Unsafe};
	const FCatFishSelectionResult UnsafeResult = Settings->SelectRuntimeDefinition(Context);
	TestFalse(TEXT("fish beyond maximum challenge ratio is rejected"), UnsafeResult.bSelected);
	return !HasAnyErrors();
}

// 测试流程：三条鱼生态权重相同，通过把某个难度带权重设为 1，证明带分布独立于鱼种数量与 SpawnWeight。
bool FCatFishCatalogChallengeBandTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UCatFishCatalogSettings* Settings = NewObject<UCatFishCatalogSettings>(GetTransientPackage());
	CatFishCatalogSettingsTest::ConfigureReadySelection(*Settings);
	Settings->Definitions = {
		CatFishCatalogSettingsTest::MakeReadyFishDefinition(
			TEXT("ComfortFish"), ECatFishBodyClass::Standard, 1, 50.0, 50.0),
		CatFishCatalogSettingsTest::MakeReadyFishDefinition(
			TEXT("MatchedFish"), ECatFishBodyClass::Standard, 1, 90.0, 90.0),
		CatFishCatalogSettingsTest::MakeReadyFishDefinition(
			TEXT("RiskyFish"), ECatFishBodyClass::Standard, 1, 120.0, 120.0)};
	const FCatFishSelectionContext Context = CatFishCatalogSettingsTest::MakeSelectionContext();
	Settings->ComfortChallengeBandWeight = 0.0;
	Settings->MatchedChallengeBandWeight = 1.0;
	Settings->RiskyChallengeBandWeight = 0.0;
	TestEqual(TEXT("matched-only mix selects matched fish"),
		Settings->SelectRuntimeDefinition(Context).FishDefinitionId, FName(TEXT("MatchedFish")));
	Settings->ComfortChallengeBandWeight = 0.0;
	Settings->MatchedChallengeBandWeight = 0.0;
	Settings->RiskyChallengeBandWeight = 1.0;
	TestEqual(TEXT("risky-only mix selects risky fish"),
		Settings->SelectRuntimeDefinition(Context).FishDefinitionId, FName(TEXT("RiskyFish")));
	Settings->ComfortChallengeBandWeight = 1.0;
	Settings->MatchedChallengeBandWeight = 0.0;
	Settings->RiskyChallengeBandWeight = 0.0;
	TestEqual(TEXT("comfort-only mix selects comfort fish"),
		Settings->SelectRuntimeDefinition(Context).FishDefinitionId, FName(TEXT("ComfortFish")));
	return !HasAnyErrors();
}

bool FCatFishCatalogChallengeBandFallbackTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UCatFishCatalogSettings* Settings = NewObject<UCatFishCatalogSettings>(GetTransientPackage());
	CatFishCatalogSettingsTest::ConfigureReadySelection(*Settings);
	Settings->ComfortChallengeBandWeight = 0.0;
	Settings->MatchedChallengeBandWeight = 0.0;
	Settings->RiskyChallengeBandWeight = 1.0;
	Settings->Definitions = {CatFishCatalogSettingsTest::MakeReadyFishDefinition(
		TEXT("OnlyComfortFish"), ECatFishBodyClass::Standard, 1, 50.0, 50.0)};
	const FCatFishSelectionResult Result = Settings->SelectRuntimeDefinition(
		CatFishCatalogSettingsTest::MakeSelectionContext());
	TestTrue(TEXT("an available ecological candidate prevents an empty hook"), Result.bSelected);
	TestEqual(TEXT("selection falls back to the only available band"), Result.FishDefinitionId,
		FName(TEXT("OnlyComfortFish")));
	return !HasAnyErrors();
}

bool FCatFishCatalogChallengeAffinityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UCatFishCatalogSettings* Settings = NewObject<UCatFishCatalogSettings>(GetTransientPackage());
	CatFishCatalogSettingsTest::ConfigureReadySelection(*Settings);
	const FCatFishSelectionContext Context = CatFishCatalogSettingsTest::MakeSelectionContext();
	UCatFishDefinition* Target = CatFishCatalogSettingsTest::MakeReadyFishDefinition(
		TEXT("TargetFish"), ECatFishBodyClass::Standard, 1, 100.0, 100.0);
	UCatFishDefinition* Distant = CatFishCatalogSettingsTest::MakeReadyFishDefinition(
		TEXT("DistantFish"), ECatFishBodyClass::Standard, 1, 70.0, 70.0);
	Settings->Definitions = {Target};
	const FCatFishSelectionResult TargetResult = Settings->SelectRuntimeDefinition(Context);
	Settings->Definitions = {Distant};
	const FCatFishSelectionResult DistantResult = Settings->SelectRuntimeDefinition(Context);
	TestTrue(TEXT("both target and distant fish remain selectable"),
		TargetResult.bSelected && DistantResult.bSelected);
	TestTrue(TEXT("continuous challenge affinity gives the target ratio more weight"),
		TargetResult.SelectedFinalWeight > DistantResult.SelectedFinalWeight);
	return !HasAnyErrors();
}

// 资产合同：Showcase2 尚未把水域身份迁移为 River 前，旧兼容鱼必须能让当前默认 50 力量/60 体力角色进入搏斗链，而不是空钩。
bool FCatFishCatalogShowcaseCompatibilityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const UCatFishCatalogSettings* Settings = GetDefault<UCatFishCatalogSettings>();
	FCatFishSelectionContext Context;
	Context.WaterRegion.RegionId = TEXT("Showcase_River_01");
	Context.WaterRegion.GeometryRevision = 1;
	Context.ChumSample.bSucceeded = true;
	Context.ChumSample.WaterRegion = Context.WaterRegion;
	Context.TimeOfDay = ECatEnvironmentTimeOfDay::Day;
	Context.Weather = ECatEnvironmentWeather::Clear;
	Context.BaitDefinitionId = TEXT("BugBait");
	Context.ActivePlayerCount = 1;
	Context.CombinedFishingStrength = 50.0;
	Context.CombinedFightStamina = 60.0;
	Context.RandomSeed = 17;
	const FCatFishSelectionResult Result = Settings->SelectRuntimeDefinition(Context);
	TestTrue(TEXT("Showcase compatibility fish stays inside the 1.35 challenge ceiling"), Result.bSelected);
	const UCatFishDefinition* Definition = Settings->FindRuntimeDefinition(Result.FishDefinitionId);
	TestNotNull(TEXT("selected compatibility id resolves to a runtime fish"), Definition);
	const UCatFishingSettings* FishingSettings = GetDefault<UCatFishingSettings>();
	TestNotNull(TEXT("selected compatibility fish resolves its bite personality"), Definition
		? FishingSettings->FindBitePersonality(Definition->BitePersonalityId) : nullptr);
	TestNotNull(TEXT("selected compatibility fish resolves its fight personality"), Definition
		? FishingSettings->FindFightPersonality(Definition->FightPersonalityId) : nullptr);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
