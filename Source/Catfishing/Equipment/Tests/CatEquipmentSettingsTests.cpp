#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"

namespace CatEquipmentSettingsTest
{
	// 构造流程：创建一条可被目录接受的通用一次性道具；它只服务目录合同测试，不表达旧草药恢复路径。
	static UCatEquipmentDefinition* MakeReadyUtilityDefinition(const FName DefinitionId)
	{
		UCatEquipmentDefinition* Definition = NewObject<UCatEquipmentDefinition>(GetTransientPackage());
		Definition->bEnableRuntimeDefinition = true;
		Definition->EquipmentDefinitionId = DefinitionId;
		Definition->Kind = ECatEquipmentKind::Utility;
		Definition->FunctionalRouteId = TEXT("UtilityRoute");
		Definition->bRunConsumable = true;
		return Definition;
	}

	// 构造流程：创建一条正式窝料定义；ContentHash 测试使用仍保留的 Chum 路径，而不是旧修竿浮木路径。
	static UCatEquipmentDefinition* MakeReadyChumDefinition(const FName DefinitionId)
	{
		UCatEquipmentDefinition* Definition = NewObject<UCatEquipmentDefinition>(GetTransientPackage());
		Definition->bEnableRuntimeDefinition = true;
		Definition->EquipmentDefinitionId = DefinitionId;
		Definition->Kind = ECatEquipmentKind::Chum;
		Definition->FunctionalRouteId = TEXT("ChumRoute");
		Definition->bRunConsumable = true;
		Definition->ChumContribution.Fishy = 1.0;
		return Definition;
	}


    // 构造流程：创建一条 starter 鱼竿定义；测试只使用 None UnlockId，验证服务器 starter 授权的最低合同。
    static UCatEquipmentDefinition* MakeReadyRodDefinition(const FName DefinitionId)
    {
        UCatEquipmentDefinition* Definition = NewObject<UCatEquipmentDefinition>(GetTransientPackage());
        Definition->bEnableRuntimeDefinition = true;
        Definition->EquipmentDefinitionId = DefinitionId;
        Definition->Kind = ECatEquipmentKind::Rod;
        Definition->LoadoutSlotId = TEXT("Rod");
        Definition->FunctionalRouteId = TEXT("StarterRod");
        Definition->MaximumRodDurability = 100.0;
        Definition->RodStrength = 25.0;
        Definition->MaximumLineLengthMeters = 60.0;
        return Definition;
    }

    // 构造流程：创建一条 starter 普通鱼饵定义；按飞书 §3.4 口径普通饵也是一局消耗品，所以必须显式声明 bRunConsumable，否则进不了运行目录。
    static UCatEquipmentDefinition* MakeReadyBaitDefinition(const FName DefinitionId)
    {
        UCatEquipmentDefinition* Definition = NewObject<UCatEquipmentDefinition>(GetTransientPackage());
        Definition->bEnableRuntimeDefinition = true;
        Definition->EquipmentDefinitionId = DefinitionId;
        Definition->Kind = ECatEquipmentKind::Bait;
        Definition->LoadoutSlotId = TEXT("Bait");
        Definition->FunctionalRouteId = TEXT("StarterBait");
        Definition->bRunConsumable = true;
        return Definition;
    }

    // 构造流程：创建一条 starter 鱼漂定义；鱼漂只表达功能路线，不携带等级或耐久。
    static UCatEquipmentDefinition* MakeReadyFloatDefinition(const FName DefinitionId)
    {
        UCatEquipmentDefinition* Definition = NewObject<UCatEquipmentDefinition>(GetTransientPackage());
        Definition->bEnableRuntimeDefinition = true;
        Definition->EquipmentDefinitionId = DefinitionId;
        Definition->Kind = ECatEquipmentKind::Float;
        Definition->LoadoutSlotId = TEXT("Float");
        Definition->FunctionalRouteId = TEXT("StarterFloat");
        // 射程与精准度偏移半径是 Float 的必填项（抛竿落点和 D₀ 都读它们），不给正值这条定义进不了运行目录。
        Definition->FloatCastRangeMeters = 3.0;
        Definition->FloatAccuracyOffsetRadiusMeters = 0.3;
        return Definition;
    }
	// 来源戳构造流程：测试用离线来源满足 WORK-01 可追源门禁，避免目录测试退回无来源手写常量。
	static FCatDataCatalogSourceStamp MakeEquipmentSourceStamp()
	{
		FCatDataCatalogSourceStamp Stamp;
		Stamp.SourceKind = TEXT("AutomationEquipmentDoc");
		Stamp.SourceNodeToken = TEXT("EquipmentDocNode");
		Stamp.SourceRevision = 652;
		return Stamp;
	}

	// 装备目录来源流程：每个正向目录测试都显式声明来源，让断言聚焦重复 ID、引用和 Hash 行为。
	static void MarkCatalogSource(UCatEquipmentSettings* Settings)
	{
		if (Settings)
		{
			Settings->SourceStamp = MakeEquipmentSourceStamp();
		}
	}

	// 测试夹具重置流程：Config 类的 NewObject 会继承项目默认值；目录单元测试必须先清空工程默认字段，再只验证本用例填入的目录内容。
	static void ResetCatalogForIsolatedTest(UCatEquipmentSettings* Settings)
	{
		if (!Settings)
		{
			return;
		}
		Settings->ContentSchemaVersion = UCatEquipmentSettings::CurrentContentSchemaVersion;
		Settings->DataRevision = 0;
		Settings->SourceStamp = FCatDataCatalogSourceStamp();
		Settings->Definitions.Reset();
		Settings->ProfileLoadoutTrustPolicy = ECatDomainPolicy::Unset;
		Settings->StarterRodDefinitionId = NAME_None;
		Settings->StarterBaitDefinitionId = NAME_None;
		Settings->StarterFloatDefinitionId = NAME_None;
		Settings->RodFailureDurabilityLoss = 0.0;
		Settings->DriftwoodDefinitionId = NAME_None;
	}

	// Issue 查询流程：按机器码、稳定 ID 和字段名定位问题；测试不解析 Message，避免中文说明变化破坏行为断言。
	static bool HasIssue(const FCatDataCatalogValidationResult& Result, const ECatDataCatalogIssueCode Code,
		const FName StableId = NAME_None, const FName FieldName = NAME_None)
	{
		for (const FCatDataCatalogIssue& Issue : Result.Issues)
		{
			if (Issue.Code == Code
				&& (StableId.IsNone() || Issue.StableId == StableId)
				&& (FieldName.IsNone() || Issue.FieldName == FieldName))
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentSettingsDuplicateDefinitionTest,
	"Catfishing.Unit.Equipment.Settings.DuplicateRuntimeDefinitionsFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentSettingsFindDefinitionTest,
	"Catfishing.Unit.Equipment.Settings.FindRuntimeDefinitionRequiresEnabledReadyDefinition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentSettingsValidationTest,
	"Catfishing.Unit.Equipment.Settings.ValidationReportsRevisionDuplicateAndLegacyRepairReference",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentSettingsContentHashTest,
	"Catfishing.Unit.Equipment.Settings.ContentHashIsStableAcrossOrderAndChangesWithRevision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCatEquipmentSettingsStarterLoadoutValidationTest,
    "Catfishing.Unit.Equipment.Settings.StarterLoadoutRequiresCompleteTypedRuntimeReferences",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：向瞬态装备目录放入两条同 ID 正式定义；查找接口必须拒绝重复 ID，避免装配或消耗命令读取不稳定条目。
bool FCatEquipmentSettingsDuplicateDefinitionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatEquipmentSettings* Settings = NewObject<UCatEquipmentSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 Equipment Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	CatEquipmentSettingsTest::ResetCatalogForIsolatedTest(Settings);
	Settings->DataRevision = 1;
	CatEquipmentSettingsTest::MarkCatalogSource(Settings);
	UCatEquipmentDefinition* FirstDefinition = CatEquipmentSettingsTest::MakeReadyUtilityDefinition(TEXT("UtilityA"));
	UCatEquipmentDefinition* SecondDefinition = CatEquipmentSettingsTest::MakeReadyUtilityDefinition(TEXT("UtilityA"));
	Settings->Definitions = {FirstDefinition, SecondDefinition};

	TestNull(TEXT("重复 EquipmentDefinitionId 返回空"), Settings->FindRuntimeDefinition(TEXT("UtilityA")));
	const FCatDataCatalogValidationResult Validation = Settings->ValidateRuntimeCatalog();
	TestFalse(TEXT("重复 ID 目录整体不可运行"), Validation.bValid);
	TestTrue(TEXT("重复 ID 暴露 DuplicateStableId"), CatEquipmentSettingsTest::HasIssue(
		Validation, ECatDataCatalogIssueCode::DuplicateStableId, TEXT("UtilityA"), TEXT("EquipmentDefinitionId")));
	return !HasAnyErrors();
}

// 测试流程：同一目录先放未启用定义，再替换为完整定义；公开查找只能返回已启用且 readiness 通过的条目。
bool FCatEquipmentSettingsFindDefinitionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatEquipmentSettings* Settings = NewObject<UCatEquipmentSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态 Equipment Settings 查找场景"), Settings);
	if (!Settings)
	{
		return false;
	}

	CatEquipmentSettingsTest::ResetCatalogForIsolatedTest(Settings);
	Settings->DataRevision = 1;
	CatEquipmentSettingsTest::MarkCatalogSource(Settings);
	UCatEquipmentDefinition* DisabledDefinition = CatEquipmentSettingsTest::MakeReadyUtilityDefinition(TEXT("UtilityA"));
	DisabledDefinition->bEnableRuntimeDefinition = false;
	Settings->Definitions = {DisabledDefinition};
	TestNull(TEXT("未启用定义不会被目录返回"), Settings->FindRuntimeDefinition(TEXT("UtilityA")));

	UCatEquipmentDefinition* ReadyDefinition = CatEquipmentSettingsTest::MakeReadyUtilityDefinition(TEXT("UtilityA"));
	Settings->Definitions = {ReadyDefinition};
	TestEqual(TEXT("唯一完整定义可被目录返回"), Settings->FindRuntimeDefinition(TEXT("UtilityA")), ReadyDefinition);
	return !HasAnyErrors();
}

// 测试流程：同时制造缺失 DataRevision、重复装备 ID 和旧修竿引用；目录校验必须逐项报告而不是让运行时任选一条继续。
bool FCatEquipmentSettingsValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatEquipmentSettings* Settings = NewObject<UCatEquipmentSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建装备目录校验 Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	CatEquipmentSettingsTest::ResetCatalogForIsolatedTest(Settings);
	CatEquipmentSettingsTest::MarkCatalogSource(Settings);
	UCatEquipmentDefinition* FirstDefinition = CatEquipmentSettingsTest::MakeReadyUtilityDefinition(TEXT("UtilityA"));
	UCatEquipmentDefinition* SecondDefinition = CatEquipmentSettingsTest::MakeReadyUtilityDefinition(TEXT("UtilityA"));
	Settings->Definitions = {FirstDefinition, SecondDefinition};
	Settings->DriftwoodDefinitionId = TEXT("MissingDriftwood");

	const FCatDataCatalogValidationResult Validation = Settings->ValidateRuntimeCatalog();
	TestFalse(TEXT("非法装备目录不可运行"), Validation.bValid);
	TestEqual(TEXT("校验结果保留 DataRevision"), Validation.DataRevision, static_cast<int64>(0));
	TestEqual(TEXT("校验结果保留来源修订"), Validation.SourceStamp.SourceRevision, static_cast<int64>(652));
	TestFalse(TEXT("非法目录仍给出可诊断 ContentHash"), Validation.ContentHashHex.IsEmpty());
	TestTrue(TEXT("缺失 DataRevision 报告 InvalidDefinition"), CatEquipmentSettingsTest::HasIssue(
		Validation, ECatDataCatalogIssueCode::InvalidDefinition, NAME_None, TEXT("DataRevision")));
	TestTrue(TEXT("重复装备报告 DuplicateStableId"), CatEquipmentSettingsTest::HasIssue(
		Validation, ECatDataCatalogIssueCode::DuplicateStableId, TEXT("UtilityA"), TEXT("EquipmentDefinitionId")));
	TestTrue(TEXT("旧修竿引用报告 InvalidReference"), CatEquipmentSettingsTest::HasIssue(
		Validation, ECatDataCatalogIssueCode::InvalidReference, TEXT("MissingDriftwood"), TEXT("DriftwoodDefinitionId")));
	return !HasAnyErrors();
}

// 测试流程：两份目录只改变编辑器列表顺序，ContentHash 必须一致；同一内容的 DataRevision 改变时，Hash 必须跟着改变。
bool FCatEquipmentSettingsContentHashTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatEquipmentDefinition* UtilityDefinition = CatEquipmentSettingsTest::MakeReadyUtilityDefinition(TEXT("UtilityA"));
	UCatEquipmentDefinition* ChumDefinition = CatEquipmentSettingsTest::MakeReadyChumDefinition(TEXT("ChumA"));

	UCatEquipmentSettings* FirstSettings = NewObject<UCatEquipmentSettings>(GetTransientPackage());
	UCatEquipmentSettings* ReorderedSettings = NewObject<UCatEquipmentSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建第一份装备目录"), FirstSettings);
	TestNotNull(TEXT("可创建重排后的装备目录"), ReorderedSettings);
	if (!FirstSettings || !ReorderedSettings)
	{
		return false;
	}

	CatEquipmentSettingsTest::ResetCatalogForIsolatedTest(FirstSettings);
	CatEquipmentSettingsTest::ResetCatalogForIsolatedTest(ReorderedSettings);
	FirstSettings->DataRevision = 4;
	CatEquipmentSettingsTest::MarkCatalogSource(FirstSettings);
	FirstSettings->Definitions = {UtilityDefinition, ChumDefinition};
	ReorderedSettings->DataRevision = 4;
	CatEquipmentSettingsTest::MarkCatalogSource(ReorderedSettings);
	ReorderedSettings->Definitions = {ChumDefinition, UtilityDefinition};

	const FCatDataCatalogValidationResult FirstValidation = FirstSettings->ValidateRuntimeCatalog();
	const FCatDataCatalogValidationResult ReorderedValidation = ReorderedSettings->ValidateRuntimeCatalog();
	TestTrue(TEXT("第一份装备目录可运行"), FirstValidation.bValid);
	TestTrue(TEXT("重排装备目录可运行"), ReorderedValidation.bValid);
	TestFalse(TEXT("装备目录 ContentHash 非空"), FirstValidation.ContentHashHex.IsEmpty());
	TestEqual(TEXT("ContentHash 不受定义列表顺序影响"), FirstValidation.ContentHashHex, ReorderedValidation.ContentHashHex);

	UtilityDefinition->bEnableRuntimeDefinition = false;
	TestNotEqual(TEXT("Definition 启用 gate 改变会改变 ContentHash"),
		FirstValidation.ContentHashHex,
		FirstSettings->ComputeContentHashHex());
	UtilityDefinition->bEnableRuntimeDefinition = true;

	ReorderedSettings->DataRevision = 5;
	TestNotEqual(TEXT("DataRevision 改变会改变 ContentHash"),
		FirstValidation.ContentHashHex,
		ReorderedSettings->ComputeContentHashHex());
	return !HasAnyErrors();
}

// 测试流程：用三条正式 starter 定义验证初始装配配置；半套字段必须暴露引用问题，完整三件套才可读，引用变化必须进入内容摘要。
bool FCatEquipmentSettingsStarterLoadoutValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatEquipmentSettings* Settings = NewObject<UCatEquipmentSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建 starter 装备目录 Settings"), Settings);
	if (!Settings)
	{
		return false;
	}

	CatEquipmentSettingsTest::ResetCatalogForIsolatedTest(Settings);

	UCatEquipmentDefinition* RodDefinition = CatEquipmentSettingsTest::MakeReadyRodDefinition(TEXT("StarterRodA"));
	UCatEquipmentDefinition* BaitDefinition = CatEquipmentSettingsTest::MakeReadyBaitDefinition(TEXT("StarterBaitA"));
	UCatEquipmentDefinition* FloatDefinition = CatEquipmentSettingsTest::MakeReadyFloatDefinition(TEXT("StarterFloatA"));
	Settings->DataRevision = 8;
	CatEquipmentSettingsTest::MarkCatalogSource(Settings);
	Settings->Definitions = {RodDefinition, BaitDefinition, FloatDefinition};

	FName Rod = NAME_None;
	FName Bait = NAME_None;
	FName Float = NAME_None;
	TestFalse(TEXT("空 starter 三件套不可读"), Settings->TryGetStarterLoadout(Rod, Bait, Float));
	Settings->StarterRodDefinitionId = TEXT("StarterRodA");
	Settings->StarterBaitDefinitionId = TEXT("StarterBaitA");
	const FCatDataCatalogValidationResult PartialValidation = Settings->ValidateRuntimeCatalog();
	TestFalse(TEXT("半套 starter 目录不可运行"), PartialValidation.bValid);
	TestTrue(TEXT("半套 starter 暴露缺少鱼漂引用"), CatEquipmentSettingsTest::HasIssue(
		PartialValidation, ECatDataCatalogIssueCode::InvalidReference, NAME_None, TEXT("StarterFloatDefinitionId")));

	Settings->StarterFloatDefinitionId = TEXT("StarterFloatA");
	const FCatDataCatalogValidationResult ValidValidation = Settings->ValidateRuntimeCatalog();
	TestTrue(TEXT("完整 starter 三件套目录可运行"), ValidValidation.bValid);
	TestTrue(TEXT("完整 starter 三件套可读"), Settings->TryGetStarterLoadout(Rod, Bait, Float));
	TestEqual(TEXT("starter 鱼竿 ID 可读"), Rod, TEXT("StarterRodA"));
	TestEqual(TEXT("starter 鱼饵 ID 可读"), Bait, TEXT("StarterBaitA"));
	TestEqual(TEXT("starter 鱼漂 ID 可读"), Float, TEXT("StarterFloatA"));
	const FString ValidHash = ValidValidation.ContentHashHex;

	Settings->StarterFloatDefinitionId = TEXT("MissingFloat");
	const FCatDataCatalogValidationResult MissingFloatValidation = Settings->ValidateRuntimeCatalog();
	TestFalse(TEXT("缺失 starter 鱼漂引用目录不可运行"), MissingFloatValidation.bValid);
	TestTrue(TEXT("缺失 starter 鱼漂引用有字段化问题"), CatEquipmentSettingsTest::HasIssue(
		MissingFloatValidation, ECatDataCatalogIssueCode::InvalidReference, TEXT("MissingFloat"), TEXT("StarterFloatDefinitionId")));
	TestNotEqual(TEXT("starter 引用变化会改变 ContentHash"), ValidHash, MissingFloatValidation.ContentHashHex);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatEquipmentSettingsProjectFloatCatalogTest,
	"Catfishing.Unit.Equipment.Settings.ProjectFloatsCarryFeishuRangesAndPositiveAccuracy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：从项目正式装备目录取三条鱼漂，逐条核对射程等于飞书装备册拍定的 3／5／7 米，精准度偏移半径为正且随射程变大
// （远漂更飘是装备册的定性口径，具体数值仍是工程暂定）。抛竿落点与遛鱼开局 D₀ 都读这两列，所以它们错了整条钓鱼链都会跟着错。
bool FCatEquipmentSettingsProjectFloatCatalogTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
	TestNotNull(TEXT("项目默认 Equipment Settings 可读取"), Settings);
	if (!Settings)
	{
		return false;
	}
	TestTrue(TEXT("项目正式装备目录可运行"), Settings->ValidateRuntimeCatalog().bValid);

	const TCHAR* const FloatIds[] = { TEXT("FeatherFloat"), TEXT("YarnBallFloat"), TEXT("BellFloat") };
	const double ExpectedRanges[] = { 3.0, 5.0, 7.0 };
	double PreviousAccuracyRadius = 0.0;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(FloatIds); ++Index)
	{
		const UCatEquipmentDefinition* Definition = Settings->FindRuntimeDefinition(FName(FloatIds[Index]));
		TestNotNull(*FString::Printf(TEXT("项目目录里能找到鱼漂 %s"), FloatIds[Index]), Definition);
		if (!Definition)
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s 的射程与飞书装备册一致"), FloatIds[Index]),
			Definition->FloatCastRangeMeters, ExpectedRanges[Index], 1e-9);
		TestTrue(*FString::Printf(TEXT("%s 的精准度偏移半径为正"), FloatIds[Index]),
			Definition->FloatAccuracyOffsetRadiusMeters > 0.0);
		TestTrue(*FString::Printf(TEXT("%s 比上一档漂更飘"), FloatIds[Index]),
			Definition->FloatAccuracyOffsetRadiusMeters > PreviousAccuracyRadius);
		PreviousAccuracyRadius = Definition->FloatAccuracyOffsetRadiusMeters;
	}

	// 竿不是漂，两个漂字段必须保持 0；否则目录校验会把它判成半配置定义。
	const UCatEquipmentDefinition* StarterRod = Settings->FindRuntimeDefinition(TEXT("StarterRodT1"));
	TestNotNull(TEXT("项目目录里能找到 starter 鱼竿"), StarterRod);
	if (StarterRod)
	{
		TestEqual(TEXT("鱼竿的漂射程为 0"), StarterRod->FloatCastRangeMeters, 0.0);
		TestEqual(TEXT("鱼竿的漂精准度半径为 0"), StarterRod->FloatAccuracyOffsetRadiusMeters, 0.0);
	}
	return !HasAnyErrors();
}
#endif // WITH_DEV_AUTOMATION_TESTS
