#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Data/CatFishDefinition.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishDefinitionDefaultsTest,
	"Catfishing.Unit.Data.FishDefinition.DefaultsAreNotRuntimeReady",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishDefinitionStandardReadyTest,
	"Catfishing.Unit.Data.FishDefinition.StandardFishReadyWithoutCaptureImprintEvent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishDefinitionToxicFoodTest,
	"Catfishing.Unit.Data.FishDefinition.ToxicFishRequiresPositivePoisonIncrease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatFishDefinitionTest
{
	// 构造流程：只填运行 readiness 所需的公开字段，CaptureImprintEventId 保持 None，用于证明实物鱼与印记候选已解耦。
	static UCatFishDefinition* MakeReadyStandardFish()
	{
		UCatFishDefinition* Definition = NewObject<UCatFishDefinition>(GetTransientPackage());
		Definition->bEnableRuntimeDefinition = true;
		Definition->FishDefinitionId = TEXT("TestStandardFish");
		Definition->BodyClass = ECatFishBodyClass::Standard;
		Definition->SacrificeContribution = 3;
		Definition->RarityTierId = TEXT("Common");
		Definition->RegionIds = {TEXT("LakeA")};
		Definition->TimeOfDay = {ECatEnvironmentTimeOfDay::Morning};
		Definition->Weather = {ECatEnvironmentWeather::Clear};
		Definition->SpawnWeight = 1.0;
		Definition->MinimumWeightKilograms = 1.0;
		Definition->MaximumWeightKilograms = 2.0;
		Definition->MinimumFightParticipants = 1;
		Definition->FishStrength = 1.0;
		Definition->FishFightStamina = 1.0;
		Definition->BitePersonalityId = TEXT("Nibble");
		Definition->FightPersonalityId = TEXT("Steady");
		Definition->FoodSafety = ECatFishFoodSafety::Safe;
		Definition->HungerRelief = 2.0;
		Definition->PoisonIncrease = 0.0;
		return Definition;
	}
}

// 测试流程：直接读取默认 DataAsset 的正式 readiness；默认对象不能进入鱼池或事务。
bool FCatFishDefinitionDefaultsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatFishDefinition* Definition = NewObject<UCatFishDefinition>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态鱼定义"), Definition);
	if (!Definition)
	{
		return false;
	}

	TestFalse(TEXT("默认鱼定义不可运行"), Definition->IsRuntimeDefinitionReady());
	return !HasAnyErrors();
}

// 测试流程：构造完整普通鱼定义并保持 CaptureImprintEventId=None；readiness 必须通过，证明无印记事件不会阻止实物鱼创建。
bool FCatFishDefinitionStandardReadyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatFishDefinition* Definition = CatFishDefinitionTest::MakeReadyStandardFish();
	TestNotNull(TEXT("可创建完整普通鱼定义"), Definition);
	if (!Definition)
	{
		return false;
	}

	TestTrue(TEXT("普通鱼完整配置可运行"), Definition->IsRuntimeDefinitionReady());
	TestTrue(TEXT("捕获印记事件为空不影响实物鱼 readiness"), Definition->CaptureImprintEventId.IsNone());
	return !HasAnyErrors();
}

// 测试流程：从完整普通鱼切换为 Toxic 食用结论，先不给 PoisonIncrease 再补正值，核对食用安全合同不会半配置通过。
bool FCatFishDefinitionToxicFoodTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatFishDefinition* Definition = CatFishDefinitionTest::MakeReadyStandardFish();
	TestNotNull(TEXT("可创建 Toxic 场景鱼定义"), Definition);
	if (!Definition)
	{
		return false;
	}

	Definition->FoodSafety = ECatFishFoodSafety::Toxic;
	Definition->PoisonIncrease = 0.0;
	TestFalse(TEXT("Toxic 鱼缺少 PoisonIncrease 时不可运行"), Definition->IsRuntimeDefinitionReady());
	Definition->PoisonIncrease = 1.5;
	TestTrue(TEXT("Toxic 鱼给出正 PoisonIncrease 后可运行"), Definition->IsRuntimeDefinitionReady());
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
