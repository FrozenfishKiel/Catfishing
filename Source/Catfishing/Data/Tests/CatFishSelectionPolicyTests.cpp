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
	FCatFishSelectionBasePoolFallbackTest,
	"Catfishing.Unit.Data.FishSelection.EmptyCandidateSetFallsBackToBasePool",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishSelectionPostFilterNormalizationTest,
	"Catfishing.Unit.Data.FishSelection.FiltersBeforeNormalizingRemainingCandidates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFormalFishSelectionWeightStrengthTest,
	"Catfishing.Unit.Data.FishSelection.FormalCatalogFreezesWeightDerivedStrength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatPerfectHookReductionRarityTierTest,
	"Catfishing.Unit.Data.FishSelection.PerfectHookReductionFollowsRarityTier",
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
		Definition->RarityTierId = TEXT("TestRarity");
		Definition->RegionIds = {TEXT("TestLake")};
		Definition->TimeOfDay = {ECatEnvironmentTimeOfDay::Morning};
		Definition->Weather = {ECatEnvironmentWeather::Clear};
		Definition->SpawnWeight = SpawnWeight;
		Definition->FishStrengthPerKilogram = 10.0; // 力量系数K 逐鱼配；力量 = 重量 x K。
		Definition->MinimumWeightKilograms = 1.0;
		Definition->MaximumWeightKilograms = 1.0;
		Definition->MinimumFightParticipants = 1;
		Definition->FishFightStaminaPerKilogram = 5.0;
		Definition->BitePersonalityId = TEXT("TestBite");
		Definition->FightPersonalityId = TEXT("TestFight");
		Definition->FoodSafety = ECatFishFoodSafety::Safe;
		Definition->EatingExperiencePerKilogram = 1.0;
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
	// 空数组＝这条鱼不受该轴约束。鱼表格现在还没有时段/天气两列，若把空数组读成"永不出现"，
	// 开关一打开整份目录会同时消失，"开关打开后链路是通的"就不成立。
	Definition->TimeOfDay.Empty();
	Definition->Weather.Empty();
	TestTrue(TEXT("enabled time gate treats an unauthored axis as unconstrained"),
		FCatFishEligibilityPolicy::PassesTimeOfDay(*Definition, ECatEnvironmentTimeOfDay::Dusk, true));
	TestTrue(TEXT("enabled weather gate treats an unauthored axis as unconstrained"),
		FCatFishEligibilityPolicy::PassesWeather(*Definition, ECatEnvironmentWeather::Rain, true));
	return !HasAnyErrors();
}

bool FCatFishSelectionBasePoolFallbackTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UCatFishCatalogSettings* Settings = NewObject<UCatFishCatalogSettings>(GetTransientPackage());
	UCurveFloat* SaturationCurve = NewObject<UCurveFloat>(GetTransientPackage());
	UCatFishPresentationDefinition* Presentation =
		CatFishSelectionPolicyTestsPrivate::MakePresentationDefinition();
	UCatFishDefinition* PoolFish = CatFishSelectionPolicyTestsPrivate::MakeFishDefinition(
		TEXT("PoolFish"), 1.0, Presentation);
	if (!TestNotNull(TEXT("creates transient catalog settings"), Settings)
		|| !TestNotNull(TEXT("creates transient saturation curve"), SaturationCurve)
		|| !TestNotNull(TEXT("creates transient presentation"), Presentation)
		|| !TestNotNull(TEXT("creates transient base pool fish"), PoolFish))
	{
		return false;
	}
	SaturationCurve->FloatCurve.AddKey(0.0f, 1.0f);
	SaturationCurve->FloatCurve.AddKey(1.0f, 3.0f);
	Settings->Definitions = {PoolFish};
	Settings->ChumSaturationCurve = SaturationCurve;
	Settings->ChumAffinityHalfSaturation = 10.0;
	Settings->MaximumChumModifier = 3.0;
	Settings->MaximumChallengeRatio = 1.35;

	FCatFishSelectionContext Context;
	Context.WaterRegion.RegionId = TEXT("TestLake");
	Context.WaterRegion.GeometryRevision = 1;
	Context.ChumSample.bSucceeded = true;
	Context.ChumSample.WaterRegion = Context.WaterRegion;
	Context.TimeOfDay = ECatEnvironmentTimeOfDay::Morning;
	Context.Weather = ECatEnvironmentWeather::Clear;
	Context.ActivePlayerCount = 1;
	// 队伍战力远低于这条鱼：挑战度超过 MaximumChallengeRatio，普通候选池会被筛空。
	// 基础池刻意不读挑战度，所以它仍然能把这条鱼抽出来——这正是「空窝不空钩」要保住的那条路。
	Context.CombinedFishingStrength = 1.0;
	Context.CombinedFightStamina = 1.0;
	Context.RandomSeed = 20260912;

	// 名册没填时保持未选中，但必须留下可查的原因，而不是静默空钩。
	AddExpectedMessage(TEXT("Event=fish_selection_base_pool_unavailable"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, 1);
	const FCatFishSelectionResult WithoutRoster = Settings->SelectRuntimeDefinition(Context);
	TestFalse(TEXT("an unauthored base pool still cannot invent a fish"), WithoutRoster.bSelected);
	TestTrue(TEXT("the unselected result still reports that the base pool path was taken"),
		WithoutRoster.bFromBasePool);

	// 名册填上之后，「候选为空」必须落基础池而不是空钩（2026-09-08 李前臻裁）。
	FCatFishBasePoolEntry& Entry = Settings->BasePool.AddDefaulted_GetRef();
	Entry.FishDefinitionId = TEXT("PoolFish");
	Entry.Probability = 1.0;
	const FCatFishSelectionResult FromPool = Settings->SelectRuntimeDefinition(Context);
	TestTrue(TEXT("an empty candidate set falls back to the base pool"), FromPool.bSelected);
	TestTrue(TEXT("the fallback marks itself as base pool sourced"), FromPool.bFromBasePool);
	TestEqual(TEXT("the fallback picks the only roster member"),
		FromPool.FishDefinitionId, FName(TEXT("PoolFish")));
	TestEqual(TEXT("the fallback still derives strength from weight times the fish's own coefficient"),
		FromPool.BaseFishStrength, FromPool.WeightKilograms * PoolFish->FishStrengthPerKilogram, 1e-6);
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
	Settings->MaximumChallengeRatio = 1.35;
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
	Context.RandomSeed = 20260901;
	// 跨原轻松/高风险带，仍必须用原始权重 1:3 一起归一化，不能先选带或乘挑战倍率。
	LightFish->FishStrengthPerKilogram = 5.0;
	HeavyFish->FishStrengthPerKilogram = 12.0;

	const FCatFishSelectionResult BypassedResult = Settings->SelectRuntimeDefinition(Context);
	TestTrue(TEXT("disabled time and weather filters leave valid candidates selectable"),
		BypassedResult.bSelected);
	TestEqual(TEXT("both fish remain after challenge and participant filters"),
		BypassedResult.EligibleCandidateCount, 2);
	TestEqual(TEXT("both fish participate in final weight normalization"),
		BypassedResult.PositiveWeightCandidateCount, 2);
	const double ExpectedProbability = BypassedResult.FishDefinitionId == TEXT("HeavyFish") ? 0.75 : 0.25;
	TestEqual(TEXT("reported probability is normalized only across remaining candidates"),
		BypassedResult.SelectedNormalizedProbability, ExpectedProbability, UE_DOUBLE_SMALL_NUMBER);
	TestEqual(TEXT("selected individual weight is frozen once"),
		BypassedResult.WeightKilograms, 1.0, UE_DOUBLE_SMALL_NUMBER);
	TestEqual(TEXT("fish strength is sampled weight times that fish's own strength coefficient"),
		BypassedResult.BaseFishStrength, BypassedResult.FishDefinitionId == TEXT("HeavyFish") ? 12.0 : 5.0,
		UE_DOUBLE_SMALL_NUMBER);
	TestEqual(TEXT("challenge no longer scales raw ecological weights"), BypassedResult.SelectedFinalWeight,
		BypassedResult.FishDefinitionId == TEXT("HeavyFish") ? 3.0 : 1.0, UE_DOUBLE_SMALL_NUMBER);
	Settings->MaximumChallengeRatio = 1.2;
	TestEqual(TEXT("hard safety ceiling equality stays eligible"), Settings->SelectRuntimeDefinition(Context).EligibleCandidateCount, 2);
	Settings->MaximumChallengeRatio = 1.19;
	const auto BelowCeiling = Settings->SelectRuntimeDefinition(Context);
	TestEqual(TEXT("hard safety ceiling still removes the excessive individual"), BelowCeiling.FishDefinitionId, FName(TEXT("LightFish")));
	TestEqual(TEXT("normalization excludes unsafe fish"), BelowCeiling.SelectedNormalizedProbability, 1.0, UE_DOUBLE_SMALL_NUMBER);
	Settings->MaximumChallengeRatio = 1.35;

	// 力量系数K 未配置的鱼直接退出候选，不回退任何全局系数，也不带着 0 力量混进抽取池。
	AddExpectedMessage(TEXT("Event=fish_selection_strength_coefficient_unset"), ELogVerbosity::Warning);
	// 候选被筛空之后会落基础池；本用例没配名册，所以兜底也拿不出鱼，这条是预期日志不是失败。
	// 次数取 -1（出现与否都不判定）：下面两次选鱼各会走一次兜底，用例关心的是选不出鱼，不是报了几次。
	AddExpectedMessage(TEXT("Event=fish_selection_base_pool_unavailable"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, -1);
	LightFish->FishStrengthPerKilogram = 0.0;
	HeavyFish->FishStrengthPerKilogram = 0.0;
	const FCatFishSelectionResult UnsetCoefficientResult = Settings->SelectRuntimeDefinition(Context);
	TestFalse(TEXT("fish without an authored strength coefficient cannot be selected"),
		UnsetCoefficientResult.bSelected);
	TestEqual(TEXT("unset strength coefficient removes the candidate before normalization"),
		UnsetCoefficientResult.EligibleCandidateCount, 0);
	LightFish->FishStrengthPerKilogram = 10.0;
	HeavyFish->FishStrengthPerKilogram = 10.0;

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
	Context.RandomSeed = 20260903;
	// 逐鱼「力量系数K」在鱼表格里，值要经编辑器落到 Fish_*.uasset；没落之前选鱼链 fail-closed，一条都选不出来。
	// 先确认这个内容缺口，避免把"资产待补值"报成选鱼逻辑错误；补值后本用例才继续校验 重量 x K 口径。
	bool bAnyStrengthCoefficientAuthored = false;
	for (const TSoftObjectPtr<UCatFishDefinition>& Entry : Settings->Definitions)
	{
		const UCatFishDefinition* Candidate = Entry.LoadSynchronous();
		if (Candidate != nullptr && Candidate->FishStrengthPerKilogram > 0.0)
		{
			bAnyStrengthCoefficientAuthored = true;
			break;
		}
	}
	if (!bAnyStrengthCoefficientAuthored)
	{
		AddWarning(TEXT("正式鱼表资产尚未填入鱼表格「力量系数K」列；选鱼链按 fail-closed 跳过全部候选，"
			"补值落到 Fish_*.uasset 后本用例才会校验「重量 x K」口径。"));
		return !HasAnyErrors();
	}
	// 正式目录里可能只有一部分鱼补了 K，未补的那些会各自记一条内容缺口警告，不是用例失败。
	AddExpectedMessage(TEXT("Event=fish_selection_strength_coefficient_unset"), ELogVerbosity::Warning);
	// Fish_*.uasset 的体力列还是 2026-09-08 之前的定额，取系数时会按重量中点折算并各报一次；
	// 报几条取决于目录里有多少条鱼，故取 -1 不判定次数。资产按终版鱼表重生成、ini 开关改成 False 之后，
	// 这条期待连同过渡逻辑一起删。
	AddExpectedMessage(TEXT("Event=fish_fight_stamina_legacy_flat_value_converted"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, -1);
	// 正式目录若被条件门筛空会落基础池；名册目前是空的，兜底同样会报。出现与否都不判定。
	AddExpectedMessage(TEXT("Event=fish_selection_base_pool_unavailable"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, -1);
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
	TestEqual(TEXT("formal runtime strength comes from that exact individual weight and the fish's own coefficient"),
		First.BaseFishStrength,
		Definition != nullptr ? First.WeightKilograms * Definition->FishStrengthPerKilogram : 0.0, 1e-6);
	return !HasAnyErrors();
}

bool FCatPerfectHookReductionRarityTierTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UCatFishCatalogSettings* Settings = NewObject<UCatFishCatalogSettings>(GetTransientPackage());
	UCatFishDefinition* CommonFish = NewObject<UCatFishDefinition>(GetTransientPackage());
	UCatFishDefinition* TopTierFish = NewObject<UCatFishDefinition>(GetTransientPackage());
	if (!TestNotNull(TEXT("creates transient catalog settings"), Settings)
		|| !TestNotNull(TEXT("creates transient common fish"), CommonFish)
		|| !TestNotNull(TEXT("creates transient top tier fish"), TopTierFish))
	{
		return false;
	}
	CommonFish->RarityTierId = TEXT("TestCommonTier");
	TopTierFish->RarityTierId = TEXT("TestTopTier");
	Settings->RarePerfectHookRarityTierIds = {TEXT("TestTopTier")};
	Settings->CommonPerfectFishStrengthMultiplier = 0.8;
	Settings->CommonPerfectFishStaminaMultiplier = 0.85;
	Settings->RarePerfectFishStrengthMultiplier = 0.85;
	Settings->RarePerfectFishStaminaMultiplier = 0.9;
	Settings->PerfectInitialLineLengthMultiplier = 0.5;

	const FCatPerfectHookReduction CommonReduction = Settings->ResolvePerfectHookReduction(*CommonFish);
	TestEqual(TEXT("非最高档按普通鱼取力量系数"),
		CommonReduction.FishStrengthMultiplier, 0.8, UE_DOUBLE_SMALL_NUMBER);
	TestEqual(TEXT("非最高档按普通鱼取体力系数"),
		CommonReduction.FishStaminaMultiplier, 0.85, UE_DOUBLE_SMALL_NUMBER);
	const FCatPerfectHookReduction TopTierReduction = Settings->ResolvePerfectHookReduction(*TopTierFish);
	TestEqual(TEXT("清单内的档按稀有鱼取力量系数"),
		TopTierReduction.FishStrengthMultiplier, 0.85, UE_DOUBLE_SMALL_NUMBER);
	TestEqual(TEXT("清单内的档按稀有鱼取体力系数"),
		TopTierReduction.FishStaminaMultiplier, 0.9, UE_DOUBLE_SMALL_NUMBER);
	TestEqual(TEXT("完美线长系数不按稀有度分档"),
		TopTierReduction.InitialLineLengthMultiplier, 0.5, UE_DOUBLE_SMALL_NUMBER);

	// 未配置、越界与负值都退回 1.0：完美只会不削减，绝不放大鱼，也不把本场值清零。
	Settings->CommonPerfectFishStrengthMultiplier = 0.0;
	Settings->RarePerfectFishStaminaMultiplier = 1.5;
	Settings->PerfectInitialLineLengthMultiplier = -1.0;
	TestEqual(TEXT("未配置的普通档系数退回 1.0"),
		Settings->ResolvePerfectHookReduction(*CommonFish).FishStrengthMultiplier, 1.0, UE_DOUBLE_SMALL_NUMBER);
	const FCatPerfectHookReduction SanitizedTopTier = Settings->ResolvePerfectHookReduction(*TopTierFish);
	TestEqual(TEXT("越界的稀有档系数退回 1.0"),
		SanitizedTopTier.FishStaminaMultiplier, 1.0, UE_DOUBLE_SMALL_NUMBER);
	TestEqual(TEXT("负的线长系数退回 1.0"),
		SanitizedTopTier.InitialLineLengthMultiplier, 1.0, UE_DOUBLE_SMALL_NUMBER);

	// 稀有档清单为空时全部按普通鱼取，与设计「其余档按普通鱼」一致。
	Settings->RarePerfectHookRarityTierIds.Reset();
	Settings->CommonPerfectFishStrengthMultiplier = 0.8;
	TestEqual(TEXT("稀有档清单为空时最高档也按普通鱼取"),
		Settings->ResolvePerfectHookReduction(*TopTierFish).FishStrengthMultiplier, 0.8, UE_DOUBLE_SMALL_NUMBER);
	// 2026-09-14 裁决⑤：读正式资产和正式配置，避免用测试 ID 自证配置已接通。
	const auto* FormalSettings = GetDefault<UCatFishCatalogSettings>();
	for (const TCHAR* Name : {TEXT("ForestLongtail"),TEXT("SilvermoonTrout"),TEXT("Blackfish"),TEXT("Pike"),TEXT("LakeGiantShadow")})
	{
		const FString Path = FString::Printf(TEXT("/Game/Catfishing/Data/Fish/Fish_%s.Fish_%s"),Name,Name);
		const auto* Fish = LoadObject<UCatFishDefinition>(nullptr,*Path);
		if (!TestNotNull(*Path,Fish)) return false;
		const bool bTop = FString(Name)!=TEXT("LakeGiantShadow");
		if (bTop) TestEqual(TEXT("四条珍稀的实际 ID 是 Rare"),Fish->RarityTierId,FName(TEXT("Rare")));
		const auto Reduction = FormalSettings->ResolvePerfectHookReduction(*Fish);
		TestEqual(*FString::Printf(TEXT("%s 正式完美力量系数"),Name),Reduction.FishStrengthMultiplier,bTop ? 0.85 : 0.8,1e-8);
		TestEqual(*FString::Printf(TEXT("%s 正式完美体力系数"),Name),Reduction.FishStaminaMultiplier,bTop ? 0.9 : 0.85,1e-8);
	}
	return !HasAnyErrors();
}

#endif
