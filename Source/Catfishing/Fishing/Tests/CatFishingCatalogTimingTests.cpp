#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Data/CatFishCatalogSettings.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/Config/CatFishingFightBalanceDefinition.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingCatalogTimingDefaultsTest,
	"Catfishing.Unit.Fishing.BiteTiming.FormalCatalogResolvesAllSixteenWithoutLegacyFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatFishingCatalogTimingDefaultsTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
	auto* Session = Wrapper.GetTestWorld()->SpawnActor<ACatFishingSession>();
	const auto* Catalog = GetDefault<UCatFishCatalogSettings>();
	const TMap<FName, FVector2D> Expected = {{TEXT("Common"), {1.75, 10.0}}, {TEXT("Uncommon"), {2.5, 13.0}},
		{TEXT("Rare"), {3.0, 15.0}}, {TEXT("Event"), {2.5, 13.0}}};
	TestEqual(TEXT("正式配置四键齐全"), Catalog->BiteTimingDefaultsByRarityTier.Num(), 4);
	TestEqual(TEXT("正式鱼目录包含16条"), Catalog->Definitions.Num(), 16);
	TSet<FName> SeenTiers;
	for (const auto& Ref : Catalog->Definitions)
	{
		auto* Fish = Ref.LoadSynchronous();
		if (!TestNotNull(TEXT("正式鱼资产只读加载"), Fish)) return false;
		const auto* Timing = Expected.Find(Fish->RarityTierId);
		if (!TestNotNull(TEXT("正式资产的字面档位有默认值"), Timing)) return false;
		SeenTiers.Add(Fish->RarityTierId);
		Session->FishDefinition = Fish;
		double Probe = 0.0, Response = 0.0;
		const TCHAR* ProbeSource = nullptr;
		const TCHAR* ResponseSource = nullptr;
		TestTrue(TEXT("生产试探解析器成功"), Session->TryResolveProbeDurationSeconds(Probe, &ProbeSource));
		TestTrue(TEXT("生产响应解析器成功"), Session->TryResolveTrueBiteWindowSeconds(Response, &ResponseSource));
		TestEqual(TEXT("试探优先逐鱼，否则正式档位"), Probe, Fish->ProbeDurationSeconds > 0.0 ? Fish->ProbeDurationSeconds : Timing->X);
		TestEqual(TEXT("响应优先逐鱼，否则正式档位"), Response, Fish->TrueBiteWindowSeconds > 0.0 ? Fish->TrueBiteWindowSeconds : Timing->Y);
		TestTrue(TEXT("正式16条均不走任何旧兜底"), FString(ProbeSource) != TEXT("LegacyFallback") && FString(ResponseSource) != TEXT("LegacyFallback"));
		TestTrue(TEXT("响应在8到15秒内，绝不落全局3秒"), Response >= 8.0 && Response <= 15.0);
		AddInfo(FString::Printf(TEXT("Event=formal_fish_timing_verified Fish=%s RarityTierId=%s ProbeSeconds=%.3f ProbeSource=%s ResponseSeconds=%.3f ResponseSource=%s"),
			*Fish->FishDefinitionId.ToString(), *Fish->RarityTierId.ToString(), Probe, ProbeSource, Response, ResponseSource));
	}
	TestEqual(TEXT("正式加载覆盖Event等全部四键"), SeenTiers.Num(), 4);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingCatalogTimingOverridesTest,
	"Catfishing.Unit.Fishing.BiteTiming.AssetFieldsIndependentlyOverrideRarityDefaultsAndRejectInvalidValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatFishingCatalogTimingOverridesTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
	auto* Session = Wrapper.GetTestWorld()->SpawnActor<ACatFishingSession>();
	auto* Fish = NewObject<UCatFishDefinition>();
	Session->FishDefinition = Fish;
	auto* Catalog = GetMutableDefault<UCatFishCatalogSettings>();
	TGuardValue<TMap<FName, FCatFishBiteTimingDefaults>> RestoreDefaults(Catalog->BiteTimingDefaultsByRarityTier, Catalog->BiteTimingDefaultsByRarityTier);
	Fish->RarityTierId = TEXT("Common");
	double Probe = 0.0, Response = 0.0;
	Fish->ProbeDurationSeconds = 6.25;
	TestTrue(TEXT("逐鱼试探覆盖默认"), Session->TryResolveProbeDurationSeconds(Probe));
	TestEqual(TEXT("试探不钳成默认值"), Probe, 6.25);
	TestTrue(TEXT("另一字段独立使用配置"), Session->TryResolveTrueBiteWindowSeconds(Response));
	TestEqual(TEXT("响应保持Common默认"), Response, 10.0);
	Fish->ProbeDurationSeconds = 0.0;
	Fish->TrueBiteWindowSeconds = 12.0;
	TestTrue(TEXT("逐鱼响应覆盖默认"), Session->TryResolveTrueBiteWindowSeconds(Response));
	TestEqual(TEXT("响应为逐鱼12秒"), Response, 12.0);
	TestTrue(TEXT("试探独立回到配置"), Session->TryResolveProbeDurationSeconds(Probe));
	TestEqual(TEXT("试探为Common默认"), Probe, 1.75);
	for (const double Invalid : {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
	{
		Fish->ProbeDurationSeconds = Fish->TrueBiteWindowSeconds = Invalid;
		TestFalse(TEXT("非法资产试探不能被有效配置掩盖"), Session->TryResolveProbeDurationSeconds(Probe));
		TestFalse(TEXT("非法资产响应不能被有效配置掩盖"), Session->TryResolveTrueBiteWindowSeconds(Response));
		Fish->ProbeDurationSeconds = Fish->TrueBiteWindowSeconds = 0.0;
		auto& Defaults = Catalog->BiteTimingDefaultsByRarityTier.FindChecked(TEXT("Common"));
		Defaults.ProbeDurationSeconds = Defaults.TrueBiteWindowSeconds = Invalid;
		TestFalse(TEXT("非法配置试探不走随机兜底"), Session->TryResolveProbeDurationSeconds(Probe));
		TestFalse(TEXT("非法配置响应不走全局兜底"), Session->TryResolveTrueBiteWindowSeconds(Response));
	}
	for (const double Invalid : {7.99, 15.01})
	{
		Fish->TrueBiteWindowSeconds = Invalid;
		TestFalse(TEXT("逐鱼响应遵守8到15边界"), Session->TryResolveTrueBiteWindowSeconds(Response));
		Fish->TrueBiteWindowSeconds = 0.0;
		Catalog->BiteTimingDefaultsByRarityTier.FindChecked(TEXT("Common")).TrueBiteWindowSeconds = Invalid;
		TestFalse(TEXT("配置响应也遵守8到15边界"), Session->TryResolveTrueBiteWindowSeconds(Response));
	}
	for (const double Boundary : {8.0, 15.0})
	{
		Fish->TrueBiteWindowSeconds = Boundary;
		TestTrue(TEXT("资产有效边界盖过非法配置"), Session->TryResolveTrueBiteWindowSeconds(Response));
		TestEqual(TEXT("边界原值保留"), Response, Boundary);
	}
	Fish->ProbeDurationSeconds = Fish->TrueBiteWindowSeconds = 0.0;
	Fish->RarityTierId = TEXT("UnmigratedUnknownTier");
	const TCHAR* Source = nullptr;
	TestTrue(TEXT("未知档位保留试探保险"), Session->TryResolveProbeDurationSeconds(Probe, &Source));
	TestEqual(TEXT("缺配来源明确"), FString(Source), FString(TEXT("LegacyFallback")));
	TestTrue(TEXT("随机保险区间不变"), Probe >= 2.0 && Probe <= 4.0);
	TestTrue(TEXT("未知档位保留旧响应保险"), Session->TryResolveTrueBiteWindowSeconds(Response, &Source));
	TestEqual(TEXT("仅未知档位使用旧全局值"), Response, GetDefault<UCatFishingSettings>()->TrueBiteWindowSeconds);
	TestEqual(TEXT("响应缺配来源明确"), FString(Source), FString(TEXT("LegacyFallback")));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishFormalBasePoolTest,
	"Catfishing.Unit.Data.FishSelection.FormalEmptyChumSelectsAllFourBasePoolFish",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatFishFormalBasePoolTest::RunTest(const FString& Parameters)
{
	const auto* Catalog = GetDefault<UCatFishCatalogSettings>();
	TestEqual(TEXT("正式基础池四条"), Catalog->BasePool.Num(), 4);
	const auto* Balance = GetDefault<UCatFishingSettings>()->LoadFightBalanceDefinition();
	if (!TestNotNull(TEXT("正式力学配置"), Balance)) return false;
	FCatFishSelectionContext Context;
	Context.WaterRegion.RegionId = TEXT("River");
	Context.WaterRegion.GeometryRevision = 1;
	Context.ChumSample.bSucceeded = true;
	Context.ChumSample.WaterRegion = Context.WaterRegion;
	Context.ActivePlayerCount = 1;
	Context.CombinedFishingStrength = Context.CombinedFightStamina = 1000000.0;
	Context.StrengthPerKilogram = Balance->StrengthPerKilogram;
	Context.BaitDefinitionId = TEXT("BugBait");
	TSet<FName> SeenFish;
	for (int32 Seed = 0; Seed < 64; ++Seed)
	{
		Context.RandomSeed = Seed;
		const auto Result = Catalog->SelectRuntimeDefinition(Context);
		if (!TestTrue(TEXT("零窝料走正式基础池且确实选中"), Result.bSelected && Result.bFromBasePool)) return false;
		const auto* Entry = Catalog->BasePool.FindByPredicate([&](const auto& Candidate) { return Candidate.FishDefinitionId == Result.FishDefinitionId; });
		if (!TestNotNull(TEXT("只抽正式四条名册成员"), Entry)) return false;
		TestEqual(TEXT("正式概率和为1，结果保留名册概率"), Result.SelectedNormalizedProbability, Entry->Probability, 1e-6);
		TestTrue(TEXT("抽中鱼拥有实际正重量与力量"), Result.WeightKilograms > 0.0 && Result.BaseFishStrength > 0.0);
		SeenFish.Add(Result.FishDefinitionId);
	}
	TestEqual(TEXT("固定64种子实际抽到全部四鱼"), SeenFish.Num(), 4);
	for (const auto& Id : SeenFish) AddInfo(FString::Printf(TEXT("Event=formal_empty_chum_verified Fish=%s"), *Id.ToString()));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishBasePoolStrengthMigrationTest,
	"Catfishing.Unit.Data.FishSelection.BaseAndWeightedPoolsShareAuthoredStrengthAndMigrationFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatFishBasePoolStrengthMigrationTest::RunTest(const FString& Parameters)
{
	const auto* Formal = GetDefault<UCatFishCatalogSettings>();
	auto* Source = Formal->FindRuntimeDefinition(TEXT("RiverPatternFish"));
	if (!TestNotNull(TEXT("正式鱼模板可只读加载"), Source)) return false;
	auto* Fish = DuplicateObject<UCatFishDefinition>(Source, GetTransientPackage());
	auto* Catalog = NewObject<UCatFishCatalogSettings>();
	Catalog->Definitions = {Fish};
	Catalog->BasePool.Reset();
	auto& Entry = Catalog->BasePool.AddDefaulted_GetRef();
	Entry.FishDefinitionId = Fish->FishDefinitionId;
	Entry.Probability = 1.0;
	FCatFishSelectionContext Context;
	Context.WaterRegion.RegionId = TEXT("River");
	Context.WaterRegion.GeometryRevision = 1;
	Context.ChumSample.bSucceeded = true;
	Context.ChumSample.WaterRegion = Context.WaterRegion;
	Context.ActivePlayerCount = 1;
	Context.CombinedFishingStrength = Context.CombinedFightStamina = 1000000.0;
	Context.RandomSeed = 42;
	for (const double AssetK : {6.0, 0.0})
	{
		Fish->FishStrengthPerKilogram = AssetK; // 仅改瞬态副本，模拟将来的资产迁移。
		Context.StrengthPerKilogram = 10.0;
		for (const bool bEmpty : {true, false})
		{
			Context.ChumSample.EffectiveChumVector.Fishy = bEmpty ? 0.0 : 1.0;
			const auto Result = Catalog->SelectRuntimeDefinition(Context);
			TestTrue(TEXT("两种池均能选中已配置或待迁移鱼"), Result.bSelected);
			TestEqual(TEXT("空窝与非空窝进入各自生产分支"), Result.bFromBasePool, bEmpty);
			TestEqual(TEXT("逐鱼K优先，缺配才沿用既有全局K"), Result.BaseFishStrength,
				Result.WeightKilograms * (AssetK > 0.0 ? AssetK : 10.0), 1e-6);
		}
	}
	Context.StrengthPerKilogram = 0.0;
	for (const bool bEmpty : {true, false})
	{
		Context.ChumSample.EffectiveChumVector.Fishy = bEmpty ? 0.0 : 1.0;
		TestFalse(TEXT("逐鱼与全局K都缺配时仍拒绝，不编造鱼力量"), Catalog->SelectRuntimeDefinition(Context).bSelected);
	}
	return !HasAnyErrors();
}

#endif
