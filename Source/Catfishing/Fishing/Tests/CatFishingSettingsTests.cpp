#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Animation/AnimSequenceBase.h"
#include "Engine/SkeletalMesh.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/Config/CatFishingFightBalanceDefinition.h"
#include "Fishing/Presentation/CatFishAnimInstance.h"
#include "Fishing/Presentation/CatFishPresentationDefinition.h"
#include "StateTree.h"
#include "UObject/UnrealType.h"

#include <limits>

namespace
{
	void PopulateValidFightBalance(UCatFishingFightBalanceDefinition& Balance)
	{
		Balance.BalanceDefinitionId = TEXT("TestFishingFightBalance");
		Balance.bEnableRuntimeDefinition = true;
		Balance.StrengthPerKilogram = 10.0;
		Balance.AccelerationPerStrength = 5.0;
		Balance.DriveResponseSeconds = 1.0;
		Balance.ReelSpeedCentimetersPerSecond = 80.0;
		Balance.CatStaminaCostPerStrengthCentimeter = 0.002;
		Balance.FishStaminaCostPerStrengthCentimeter = 0.002;
		Balance.IsometricEffortMultiplier = 1.0;
		Balance.SlackStaminaRegenPerSecond = 3.0;
		Balance.FishExhaustionThreshold = 0.5;
		Balance.LowStaminaRestThreshold = 0.5;
		Balance.LowStaminaRestMultiplier = 1.5;
		Balance.TensionResponseRangeCentimeters = 10.0;
		Balance.EscapeSlackCentimeters = 100.0;
		Balance.StalemateRodWearPerFishStrength = 0.1;
		Balance.HeldRodMinimumLeverageMultiplier = 0.4;
		Balance.MaximumFishConstraintCorrectionSpeedCentimetersPerSecond = 160.0;
		Balance.MinimumCarrierAwaySpeedMultiplier = 0.15;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingSettingsRuntimeReadinessTest,
	"Catfishing.Unit.Fishing.Settings.RuntimeRequiresStateTreeAndPositiveWindows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishWeightVisualScaleTest,
	"Catfishing.Unit.Fishing.Settings.FishWeightUsesCubeRootUniformVisualScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatRodOperatorLayoutSettingsTest,
	"Catfishing.Unit.Fishing.Settings.RodOperatorLayoutIsBounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingFightBalanceDefinitionTest,
	"Catfishing.Unit.Fishing.Settings.FightBalanceIsValidatedAndDesignerReadable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFormalFishingFightBalanceAssetTest,
	"Catfishing.Unit.Fishing.Assets.FormalFightBalanceAllowsIndependentStaminaTuning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatStarterRodDurabilityBaselineTest,
	"Catfishing.Unit.Fishing.Assets.StarterRodPreservesMaximumDurabilityBaseline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFormalFishingFightBalanceAssetTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	const UCatFishingFightBalanceDefinition* Balance = Settings
		? Settings->LoadFightBalanceDefinition() : nullptr;
	if (!TestNotNull(TEXT("默认配置可加载正式搏斗平衡资产"), Balance)) return false;

	TestEqual(TEXT("正式平衡资产 ID 稳定"), Balance->BalanceDefinitionId,
		FName(TEXT("DefaultFishingFightBalance")));
	TestEqual(TEXT("保留每公斤十点力量基线"), Balance->StrengthPerKilogram, 10.0);
	TestEqual(TEXT("保留每点力量五厘米每平方秒加速度基线"),
		Balance->AccelerationPerStrength, 5.0);
	TestTrue(TEXT("正式资产的独立体力调参通过统一运行校验"), Balance->IsRuntimeDefinitionReady());
	return !HasAnyErrors();
}

bool FCatStarterRodDurabilityBaselineTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const UCatEquipmentSettings* EquipmentSettings = GetDefault<UCatEquipmentSettings>();
	const UCatEquipmentDefinition* StarterRod = EquipmentSettings
		? EquipmentSettings->FindRuntimeDefinition(TEXT("StarterRodT1")) : nullptr;
	if (!TestNotNull(TEXT("正式装备目录可加载初级鱼竿"), StarterRod)) return false;
	TestEqual(TEXT("初级鱼竿定义 ID 稳定"), StarterRod->EquipmentDefinitionId,
		FName(TEXT("StarterRodT1")));
	TestEqual(TEXT("初级鱼竿最大耐久为 150，开场读取实例剩余值"), StarterRod->MaximumRodDurability, 150.0);
	return !HasAnyErrors();
}

bool FCatFishingFightBalanceDefinitionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UCatFishingFightBalanceDefinition* Balance = NewObject<UCatFishingFightBalanceDefinition>(GetTransientPackage());
	if (!TestNotNull(TEXT("可创建瞬态搏斗平衡资产"), Balance)) return false;

	TestFalse(TEXT("未配置资产默认不可进入运行态"), Balance->IsRuntimeDefinitionReady());
	PopulateValidFightBalance(*Balance);
	TestTrue(TEXT("完整合法数值可进入运行态"), Balance->IsRuntimeDefinitionReady());
	TestEqual(TEXT("既有资产获得猫力竭外冲默认倍率"), Balance->ExhaustedCatEscapeSpeedMultiplier, 2.0);
	Balance->ExhaustedCatEscapeSpeedMultiplier = 3.0;
	TestTrue(TEXT("猫力竭外冲速度可独立调整"), Balance->IsRuntimeDefinitionReady());
	for (const double InvalidEscapeSpeed : {0.0, -1.0, std::numeric_limits<double>::quiet_NaN(),
		std::numeric_limits<double>::infinity()})
	{
		Balance->ExhaustedCatEscapeSpeedMultiplier = InvalidEscapeSpeed;
		TestFalse(TEXT("非法外冲倍率拒绝正式运行"), Balance->IsRuntimeDefinitionReady());
	}
	Balance->ExhaustedCatEscapeSpeedMultiplier = 2.0;
	TestNotNull(TEXT("资产生成脚本可调用统一运行校验"),
		UCatFishingFightBalanceDefinition::StaticClass()->FindFunctionByName(
			GET_FUNCTION_NAME_CHECKED(UCatFishingFightBalanceDefinition, IsRuntimeDefinitionReady)));

	Balance->CatStaminaCostPerStrengthCentimeter = 0.003;
	Balance->FishStaminaCostPerStrengthCentimeter = 0.001;
	TestTrue(TEXT("猫鱼可以独立配置不同体力价格"), Balance->IsRuntimeDefinitionReady());

	struct FStaminaMultiplierCase
	{
		const TCHAR* Name;
		double UCatFishingFightBalanceDefinition::* Field;
	};
	const FStaminaMultiplierCase MultiplierCases[] = {
		{ TEXT("猫移动"), &UCatFishingFightBalanceDefinition::CatMovementStaminaMultiplier },
		{ TEXT("猫收线"), &UCatFishingFightBalanceDefinition::CatReelStaminaMultiplier },
		{ TEXT("猫转杆"), &UCatFishingFightBalanceDefinition::CatRodStaminaMultiplier },
		{ TEXT("猫持竿"), &UCatFishingFightBalanceDefinition::CatHoldStaminaMultiplier },
		{ TEXT("猫负载"), &UCatFishingFightBalanceDefinition::CatLoadStaminaMultiplier },
		{ TEXT("鱼负载"), &UCatFishingFightBalanceDefinition::FishLoadStaminaMultiplier },
	};
	for (const FStaminaMultiplierCase& Case : MultiplierCases)
	{
		double& Multiplier = Balance->*Case.Field;
		TestEqual(FString::Printf(TEXT("%s体力倍率为旧资产提供默认值"), Case.Name), Multiplier, 1.0);
		Multiplier = 0.0;
		TestTrue(FString::Printf(TEXT("%s体力倍率允许关闭该项"), Case.Name), Balance->IsRuntimeDefinitionReady());
		Multiplier = 2.5;
		TestTrue(FString::Printf(TEXT("%s体力倍率允许独立调高"), Case.Name), Balance->IsRuntimeDefinitionReady());
		Multiplier = -0.1;
		TestFalse(FString::Printf(TEXT("%s负倍率阻止运行"), Case.Name), Balance->IsRuntimeDefinitionReady());
		Multiplier = std::numeric_limits<double>::quiet_NaN();
		TestFalse(FString::Printf(TEXT("%s非数值倍率阻止运行"), Case.Name), Balance->IsRuntimeDefinitionReady());
		Multiplier = std::numeric_limits<double>::infinity();
		TestFalse(FString::Printf(TEXT("%s无穷倍率阻止运行"), Case.Name), Balance->IsRuntimeDefinitionReady());
		Multiplier = 1.0;
	}

	const FProperty* StrengthProperty = FindFProperty<FProperty>(
		UCatFishingFightBalanceDefinition::StaticClass(),
		GET_MEMBER_NAME_CHECKED(UCatFishingFightBalanceDefinition, StrengthPerKilogram));
	TestNotNull(TEXT("每公斤力量字段可反射"), StrengthProperty);
#if WITH_EDITOR
	if (StrengthProperty)
	{
		TestEqual(TEXT("策划界面使用中文字段名"),
			StrengthProperty->GetDisplayNameText().ToString(), FString(TEXT("每公斤力量")));
	}
#endif

	Balance->AccelerationPerStrength = 0.0;
	TestFalse(TEXT("非法加速度系数阻止运行"), Balance->IsRuntimeDefinitionReady());
	return !HasAnyErrors();
}

bool FCatRodOperatorLayoutSettingsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UCatFishingSettings* Settings = NewObject<UCatFishingSettings>(GetTransientPackage());
	if (!TestNotNull(TEXT("creates transient fishing settings"), Settings)) return false;
	Settings->MaximumRodOperatorSlots = 2;
	Settings->RodOperatorSlotSpacingCentimeters = 140.0;
	int32 Slots = 99;
	double Spacing = 99.0;
	TestTrue(TEXT("two-slot layout is accepted"), Settings->TryGetRodOperatorLayout(Slots, Spacing));
	TestEqual(TEXT("layout returns two slots"), Slots, 2);
	TestEqual(TEXT("layout returns configured spacing"), Spacing, 140.0);
	Settings->MaximumRodOperatorSlots = 9;
	TestFalse(TEXT("unbounded replicated slot count is rejected"), Settings->TryGetRodOperatorLayout(Slots, Spacing));
	TestEqual(TEXT("rejected layout clears slot count"), Slots, 0);
	TestEqual(TEXT("rejected layout clears spacing"), Spacing, 0.0);
	Settings->MaximumRodOperatorSlots = 2;
	Settings->RodOperatorSlotSpacingCentimeters = -1.0;
	TestFalse(TEXT("negative spacing is rejected"), Settings->TryGetRodOperatorLayout(Slots, Spacing));
	return !HasAnyErrors();
}

bool FCatFishWeightVisualScaleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UCatFishPresentationDefinition* Settings = NewObject<UCatFishPresentationDefinition>(GetTransientPackage());
	if (!TestNotNull(TEXT("creates transient presentation settings"), Settings))
	{
		return false;
	}
	Settings->SkeletalMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/Test/FishMesh.FishMesh")));
	Settings->AnimInstanceClass = TSoftClassPtr<UCatFishAnimInstance>(FSoftObjectPath(TEXT("/Script/Catfishing.CatFishAnimInstance")));
	Settings->CalmAnimation = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(TEXT("/Game/Test/Calm.Calm")));
	Settings->StruggleAnimation = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(TEXT("/Game/Test/Struggle.Struggle")));
	Settings->ExhaustedAnimation = TSoftObjectPtr<UAnimSequenceBase>(FSoftObjectPath(TEXT("/Game/Test/Exhausted.Exhausted")));
	Settings->LandedAnimation = Settings->ExhaustedAnimation;
	Settings->MeshReferenceWeightKilograms = 1.0;
	Settings->MinimumUniformScale = 0.5;
	Settings->MaximumUniformScale = 2.0;
	TestTrue(TEXT("complete fish presentation is runtime-ready"), Settings->IsRuntimeDefinitionReady());
	TestEqual(TEXT("reference weight keeps unit scale"), Settings->ComputeUniformVisualScale(1.0), 1.0);
	TestEqual(TEXT("eight times weight doubles linear size"), Settings->ComputeUniformVisualScale(8.0), 2.0);
	TestEqual(TEXT("one eighth weight halves linear size"), Settings->ComputeUniformVisualScale(0.125), 0.5);
	TestEqual(TEXT("large values clamp to configured maximum"), Settings->ComputeUniformVisualScale(64.0), 2.0);
	TestEqual(TEXT("invalid weight safely falls back to unit scale"), Settings->ComputeUniformVisualScale(0.0), 1.0);
	return !HasAnyErrors();
}

// 测试流程：构造一份只在内存存在的 Fishing Settings，先显式关闭运行态，再逐步补齐 StateTree、真咬窗口、近岸验证、抢抄距离和终态复制窗口；读取接口必须随 runtime readiness 同步 fail-closed。
bool FCatFishingSettingsRuntimeReadinessTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatFishingSettings* Settings = NewObject<UCatFishingSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 Fishing Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	// UDeveloperSettings 会从 DefaultGame.ini 装载项目配置；这里显式清空 runtime 必填项，
	// 才能稳定验证 fail-closed 行为，而不受当前项目默认配置影响。
	Settings->bEnableFishingRuntime = false;
	Settings->FishingSessionStateTree.Reset();
	Settings->FishBehaviorStateTree.Reset();
	Settings->FightBalanceDefinition.Reset();
	Settings->TrueBiteWindowSeconds = 0.0;
	Settings->bEnableNearShoreValidation = false;
	Settings->ScoopReachCentimeters = 0.0;
	Settings->TerminalReplicationWindowSeconds = 0.0;

	double ScoopReach = 99.0;
	double ScoopCooldown = 99.0;
	double BiteWarning = 99.0;
	double TerminalWindow = 99.0;
	TestFalse(TEXT("显式关闭的 Fishing runtime 不可运行"), Settings->IsRuntimeReady());
	TestFalse(TEXT("显式关闭时抢抄距离读取失败"), Settings->TryGetScoopReach(ScoopReach));
	TestEqual(TEXT("失败时抢抄距离清零"), ScoopReach, 0.0);
	TestTrue(TEXT("默认抄网冷却可读取"), Settings->TryGetScoopCooldown(ScoopCooldown));
	TestEqual(TEXT("默认抄网冷却为三秒"), ScoopCooldown, 3.0);
	Settings->ScoopCooldownSeconds = 0.0;
	TestFalse(TEXT("非正抄网冷却配置被拒绝"), Settings->TryGetScoopCooldown(ScoopCooldown));
	TestEqual(TEXT("冷却读取失败时输出清零"), ScoopCooldown, 0.0);
	Settings->ScoopCooldownSeconds = 3.0;
	TestTrue(TEXT("默认真咬预警可读取"), Settings->TryGetBiteWarning(BiteWarning));
	TestEqual(TEXT("默认真咬预警为1.5秒"), BiteWarning, 1.5);
	Settings->BiteWarningSeconds = 0.0;
	TestFalse(TEXT("非正真咬预警配置被拒绝"), Settings->TryGetBiteWarning(BiteWarning));
	TestEqual(TEXT("预警读取失败时输出清零"), BiteWarning, 0.0);
	Settings->BiteWarningSeconds = 3.0;
	Settings->MaximumBiteDelaySeconds = 7.0;
	TestFalse(TEXT("总时间上限不足以容纳慢浮下限和完整预警时拒绝"),
		Settings->TryGetBiteWarning(BiteWarning));
	Settings->MaximumBiteDelaySeconds = 8.0;
	TestTrue(TEXT("总时间上限恰好容纳五秒慢浮与三秒预警"), Settings->TryGetBiteWarning(BiteWarning));
	Settings->MaximumBiteDelaySeconds = 15.0;
	TestTrue(TEXT("恢复合法咬钩延迟后预警可读取"), Settings->TryGetBiteWarning(BiteWarning));
	TestFalse(TEXT("显式关闭时终态复制窗口读取失败"), Settings->TryGetTerminalReplicationWindow(TerminalWindow));
	TestEqual(TEXT("失败时终态复制窗口清零"), TerminalWindow, 0.0);

	Settings->bEnableFishingRuntime = true;
	Settings->FishingSessionStateTree = NewObject<UStateTree>(GetTransientPackage());
	Settings->FishBehaviorStateTree = NewObject<UStateTree>(GetTransientPackage());
	Settings->TrueBiteWindowSeconds = 1.25;
	Settings->bEnableNearShoreValidation = true;
	Settings->ScoopReachCentimeters = 250.0;
	TestFalse(TEXT("缺少终态复制窗口时仍不可运行"), Settings->IsRuntimeReady());
	Settings->TerminalReplicationWindowSeconds = 5.0;
	TestFalse(TEXT("缺少搏斗平衡资产时仍不可运行"), Settings->IsRuntimeReady());
	UCatFishingFightBalanceDefinition* FightBalance = NewObject<UCatFishingFightBalanceDefinition>(Settings);
	PopulateValidFightBalance(*FightBalance);
	Settings->FightBalanceDefinition = FightBalance;
	TestTrue(TEXT("完整 Fishing 配置可运行"), Settings->IsRuntimeReady());
	TestTrue(TEXT("完整配置可读取抢抄距离"), Settings->TryGetScoopReach(ScoopReach));
	TestEqual(TEXT("抢抄距离保持配置值"), ScoopReach, 250.0);
	TestTrue(TEXT("完整配置可读取终态复制窗口"), Settings->TryGetTerminalReplicationWindow(TerminalWindow));
	TestEqual(TEXT("终态复制窗口保持配置值"), TerminalWindow, 5.0);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
