#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Curves/CurveFloat.h"

namespace CatFishCatalogSettingsTest
{
	// 构造流程：创建一条只满足目录筛选所需公开字段的鱼定义；调用方通过人数、力量和体力参数表达标准鱼与巨鱼的可达差异。
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
		Definition->HungerRelief = 1.0;
		return Definition;
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
	UCurveFloat* Saturation = NewObject<UCurveFloat>(GetTransientPackage());
	Saturation->FloatCurve.AddKey(0.0f, 1.0f);
	Saturation->FloatCurve.AddKey(1.0f, 1.0f);
	Settings->ChumSaturationCurve = Saturation;
	Settings->ChumAffinityHalfSaturation = 1.0;
	Settings->MaximumChumModifier = 1.0;
	FCatFishSelectionContext Context;
	Context.WaterRegion.RegionId = TEXT("LakeA");
	Context.WaterRegion.GeometryRevision = 1;
	Context.ChumSample.bSucceeded = true;
	Context.ChumSample.WaterRegion = Context.WaterRegion;
	Context.TimeOfDay = ECatEnvironmentTimeOfDay::Day;
	Context.Weather = ECatEnvironmentWeather::Clear;
	Context.BaitDefinitionId = TEXT("UnlistedBait");
	Context.ActivePlayerCount = 1;
	Context.CombinedFishingStrength = 1.0;
	Context.CombinedFightStamina = 1.0;
	Context.RandomSeed = 17;
	const FCatFishSelectionResult Result = Settings->SelectRuntimeDefinition(Context);
	TestTrue(TEXT("zero local chum and unlisted bait remain neutral"), Result.bSelected);
	TestEqual(TEXT("neutral final weight is spawn weight"), Result.SelectedFinalWeight, 1.0);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
