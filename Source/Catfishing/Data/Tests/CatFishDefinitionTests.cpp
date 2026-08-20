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
	FCatFishDefinitionCatchableWithoutFoodEffectTest,
	"Catfishing.Unit.Data.FishDefinition.CatchableFishDoesNotRequireFoodEffect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishDefinitionToxicFoodTest,
	"Catfishing.Unit.Data.FishDefinition.ToxicFishRequiresPositivePoisonIncrease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishDefinitionFeishuBackedFieldsTest,
	"Catfishing.Unit.Data.FishDefinition.ReadyFromFeishuBackedFieldsOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatFishDefinitionTest
{
	// 构造流程：只填鱼表格真实有列的字段——稳定 ID、体型、献祭额度、出没地点、喜爱窝料、重量区间、协作人数与体力，
	// 外加工程侧的显式启用 gate、由重量和系数派生的力量，以及食用结论。CaptureImprintEventId 保持 None，
	// 用于证明实物鱼与印记候选已解耦。
	static UCatFishDefinition* MakeReadyStandardFish()
	{
		UCatFishDefinition* Definition = NewObject<UCatFishDefinition>(GetTransientPackage());
		Definition->bEnableRuntimeDefinition = true;
		Definition->FishDefinitionId = TEXT("TestStandardFish");
		Definition->BodyClass = ECatFishBodyClass::Standard;
		Definition->SacrificeContribution = 3;
		Definition->RegionIds = {TEXT("River")};
		Definition->ChumAffinities = {ECatChumAffinity::Fermented};
		Definition->MinimumWeightKilograms = 1.0;
		Definition->MaximumWeightKilograms = 2.0;
		Definition->MinimumFightParticipants = 1;
		Definition->FishStrength = 1.0;
		Definition->FishFightStamina = 1.0;
		Definition->FoodSafety = ECatFishFoodSafety::Safe;
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
	TestTrue(TEXT("Safe 鱼在移除饥饿系统后仍可被食用写口消费"), Definition->HasRuntimeConsumptionEffect());
	return !HasAnyErrors();
}

// 测试流程：在保留钓起、献祭、Fishing 数值的前提下清空食用字段；鱼目录应允许捕获，吃鱼入口则继续 fail-closed。
bool FCatFishDefinitionCatchableWithoutFoodEffectTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatFishDefinition* Definition = CatFishDefinitionTest::MakeReadyStandardFish();
	TestNotNull(TEXT("可创建只完成捕获字段的鱼定义"), Definition);
	if (!Definition)
	{
		return false;
	}

	Definition->FoodSafety = ECatFishFoodSafety::Unset;
	Definition->PoisonIncrease = 0.0;
	TestTrue(TEXT("未决食用效果不阻止鱼种进入钓鱼目录"), Definition->IsRuntimeDefinitionReady());
	TestFalse(TEXT("未决食用效果不能被吃鱼写口消费"), Definition->HasRuntimeConsumptionEffect());
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
	TestTrue(TEXT("Toxic 鱼的食用数值未完成不影响捕获 readiness"), Definition->IsRuntimeDefinitionReady());
	TestFalse(TEXT("Toxic 鱼缺少 PoisonIncrease 时不能被食用"), Definition->HasRuntimeConsumptionEffect());
	Definition->PoisonIncrease = 1.5;
	TestTrue(TEXT("Toxic 鱼给出正 PoisonIncrease 后可被食用写口消费"), Definition->HasRuntimeConsumptionEffect());
	return !HasAnyErrors();
}

// 测试流程：只填鱼表格真实有列的字段就要求 readiness 通过，再清空其中一个真实字段要求 readiness 立刻 fail-closed。
// 锁定的不变量：readiness 的必填集合不能超出鱼表格真实列的集合。稀有度、试探性格、搏斗性格、时段、天气和出现权重
// 在鱼表格里都没有对应列（性格模板只在鱼册 v1.3 作为案 B 被提出，正文从未定义），所以这六个字段已从类型上删除；
// 如果哪天有人为了凑齐某个"完整分类"把它们加回必填，本用例会红，因为夹具不会去编造这些值。
// 后半段的力量字段用来划另一条边：真实存在的列缺失时不能放行，readiness 放宽的只是"飞书没有的东西"。
bool FCatFishDefinitionFeishuBackedFieldsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatFishDefinition* Definition = CatFishDefinitionTest::MakeReadyStandardFish();
	TestNotNull(TEXT("可创建仅含飞书真实字段的鱼定义"), Definition);
	if (!Definition)
	{
		return false;
	}

	TestTrue(TEXT("仅凭鱼表格真实存在的列，鱼即可进入捕获链"), Definition->IsRuntimeDefinitionReady());

	Definition->FishStrength = 0.0;
	TestFalse(TEXT("飞书真实存在的力量字段缺失时，鱼必须继续 fail-closed"), Definition->IsRuntimeDefinitionReady());
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
