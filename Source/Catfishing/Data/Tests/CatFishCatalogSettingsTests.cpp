#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Data/CatDataCatalogValidation.h"
#include "Data/CatDataCatalogValidationCommandlet.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"

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
		Definition->RegionIds = {TEXT("LakeA")};
		Definition->ChumAffinities = {ECatChumAffinity::Fishy};
		Definition->MinimumWeightKilograms = 1.0;
		Definition->MaximumWeightKilograms = 2.0;
		Definition->MinimumFightParticipants = MinimumFightParticipants;
		Definition->FishStrength = FishStrength;
		Definition->FishFightStamina = FishFightStamina;
		Definition->FoodSafety = ECatFishFoodSafety::Safe;
		return Definition;
	}

	// 构造流程：创建一条可被 EquipmentCatalog 接受的特殊饵定义；鱼目录引用校验只关心稳定 ID、类别和特殊饵标记。
	static UCatEquipmentDefinition* MakeReadySpecialBaitDefinition(const FName DefinitionId)
	{
		UCatEquipmentDefinition* Definition = NewObject<UCatEquipmentDefinition>(GetTransientPackage());
		Definition->bEnableRuntimeDefinition = true;
		Definition->EquipmentDefinitionId = DefinitionId;
		Definition->Kind = ECatEquipmentKind::Bait;
		Definition->LoadoutSlotId = TEXT("BaitSlot");
		Definition->FunctionalRouteId = TEXT("SpecialBait");
		Definition->bRunConsumable = true;
		Definition->bSpecialBait = true;
		return Definition;
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

	// 来源戳构造流程：测试用显式 Automation 来源满足 WORK-01 可追源门禁，不把真实飞书读取带进运行路径。
	static FCatDataCatalogSourceStamp MakeSourceStamp(const FName SourceKind, const FString& SourceNodeToken,
		const int64 SourceRevision, const FString& SourceSliceName = FString())
	{
		FCatDataCatalogSourceStamp Stamp;
		Stamp.SourceKind = SourceKind;
		Stamp.SourceNodeToken = SourceNodeToken;
		Stamp.SourceRevision = SourceRevision;
		Stamp.SourceSliceName = SourceSliceName;
		return Stamp;
	}

	// 鱼目录来源流程：把测试鱼目录标记成已由离线落盘流程确认，避免缺来源门禁掩盖当前用例真正要测的鱼表行为。
	static void MarkFishCatalogSource(UCatFishCatalogSettings* Settings)
	{
		if (Settings)
		{
			Settings->SourceStamp = MakeSourceStamp(TEXT("AutomationFishSheet"), TEXT("FishSheetNode"), 547, TEXT("第一版"));
		}
	}

	// 装备目录来源流程：测试里的临时装备目录只验证当前用例声明的装备关系，所以要同时标记离线来源并清空项目默认
	// starter/维修入口，避免 DefaultGame.ini 的真实项目配置污染 scratch catalog。
	static void MarkEquipmentCatalogSource(UCatEquipmentSettings* Settings)
	{
		if (Settings)
		{
			Settings->SourceStamp = MakeSourceStamp(TEXT("AutomationEquipmentDoc"), TEXT("EquipmentDocNode"), 652);
			Settings->ProfileLoadoutTrustPolicy = ECatDomainPolicy::Unset;
			Settings->StarterRodDefinitionId = NAME_None;
			Settings->StarterBaitDefinitionId = NAME_None;
			Settings->StarterFloatDefinitionId = NAME_None;
			Settings->DriftwoodDefinitionId = NAME_None;
		}
	}

	// 合并报告 Issue 查询流程：按机器码和字段定位跨目录问题；测试只依赖结构化 code，不解析人工消息。
	static bool HasReportIssue(const FCatDataCatalogValidationReport& Report, const ECatDataCatalogIssueCode Code,
		const FName StableId = NAME_None, const FName FieldName = NAME_None)
	{
		for (const FCatDataCatalogIssue& Issue : Report.Issues)
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

	// 请求构造流程：把 encounter 选择的全部外部事实压成纯数据，避免测试绕过公开选择接口去检查内部候选数组。
	static FCatFishEncounterSelectionRequest MakeSelectionRequest()
	{
		FCatFishEncounterSelectionRequest Request;
		Request.RegionId = TEXT("LakeA");
		Request.TimeOfDay = ECatEnvironmentTimeOfDay::Day;
		Request.Weather = ECatEnvironmentWeather::Clear;
		Request.ActivePlayerCount = 2;
		Request.CombinedFishingStrength = 12.0;
		Request.CombinedFightStamina = 10.0;
		Request.SelectionSeed = 12345;
		return Request;
	}
	/** 默认目录覆写夹具保存项目当前 Fish/Equipment 配置；测试只在内存里制造非法目录，析构时恢复，避免未来正式内容落盘后被该用例污染。 */
	struct FInvalidDefaultCatalogOverride
	{
		/** 进入测试前的 Fish SchemaVersion；恢复时原样写回，避免影响同进程后续目录用例。 */
		int32 SavedFishContentSchemaVersion = UCatFishCatalogSettings::CurrentContentSchemaVersion;

		/** 进入测试前的 Fish DataRevision；恢复时保持项目默认内容身份。 */
		int64 SavedFishDataRevision = 0;

		/** 进入测试前的 Fish 来源戳；恢复时保持飞书/离线落盘证据。 */
		FCatDataCatalogSourceStamp SavedFishSourceStamp;

		/** 进入测试前的 Fish 显式资产清单；恢复时不丢失未来正式内容配置。 */
		TArray<TSoftObjectPtr<UCatFishDefinition>> SavedFishDefinitions;

		/** 进入测试前的 Equipment SchemaVersion；恢复时原样写回，避免影响装备目录测试。 */
		int32 SavedEquipmentContentSchemaVersion = UCatEquipmentSettings::CurrentContentSchemaVersion;

		/** 进入测试前的 Equipment DataRevision；恢复时保持项目默认内容身份。 */
		int64 SavedEquipmentDataRevision = 0;

		/** 进入测试前的 Equipment 来源戳；恢复时保持飞书/离线落盘证据。 */
		FCatDataCatalogSourceStamp SavedEquipmentSourceStamp;

		/** 进入测试前的 Equipment 显式资产清单；恢复时不丢失未来正式装备配置。 */
		TArray<TSoftObjectPtr<UCatEquipmentDefinition>> SavedEquipmentDefinitions;

		/** 进入测试前的默认鱼竿 ID；测试会清空 starter 三件套，避免未来默认配置给失败路径增加额外引用噪声。 */
		FName SavedStarterRodDefinitionId = NAME_None;

		/** 进入测试前的默认鱼饵 ID；恢复时保持玩家入局默认装备配置。 */
		FName SavedStarterBaitDefinitionId = NAME_None;

		/** 进入测试前的默认鱼漂 ID；恢复时保持玩家入局默认装备配置。 */
		FName SavedStarterFloatDefinitionId = NAME_None;

		/** 进入测试前的维修浮木 ID；测试会清空它，避免非法目录夹具混入非本用例关注的维修引用。 */
		FName SavedDriftwoodDefinitionId = NAME_None;

		/** 覆写流程保存两个默认目录的关键字段，再清空来源、修订和显式清单，以稳定触发 commandlet 的 CI 阻断路径。 */
		FInvalidDefaultCatalogOverride()
		{
			if (UCatFishCatalogSettings* FishSettings = GetMutableDefault<UCatFishCatalogSettings>())
			{
				SavedFishContentSchemaVersion = FishSettings->ContentSchemaVersion;
				SavedFishDataRevision = FishSettings->DataRevision;
				SavedFishSourceStamp = FishSettings->SourceStamp;
				SavedFishDefinitions = FishSettings->Definitions;
				FishSettings->ContentSchemaVersion = UCatFishCatalogSettings::CurrentContentSchemaVersion;
				FishSettings->DataRevision = 0;
				FishSettings->SourceStamp = FCatDataCatalogSourceStamp();
				FishSettings->Definitions.Reset();
			}
			if (UCatEquipmentSettings* EquipmentSettings = GetMutableDefault<UCatEquipmentSettings>())
			{
				SavedEquipmentContentSchemaVersion = EquipmentSettings->ContentSchemaVersion;
				SavedEquipmentDataRevision = EquipmentSettings->DataRevision;
				SavedEquipmentSourceStamp = EquipmentSettings->SourceStamp;
				SavedEquipmentDefinitions = EquipmentSettings->Definitions;
				SavedStarterRodDefinitionId = EquipmentSettings->StarterRodDefinitionId;
				SavedStarterBaitDefinitionId = EquipmentSettings->StarterBaitDefinitionId;
				SavedStarterFloatDefinitionId = EquipmentSettings->StarterFloatDefinitionId;
				SavedDriftwoodDefinitionId = EquipmentSettings->DriftwoodDefinitionId;
				EquipmentSettings->ContentSchemaVersion = UCatEquipmentSettings::CurrentContentSchemaVersion;
				EquipmentSettings->DataRevision = 0;
				EquipmentSettings->SourceStamp = FCatDataCatalogSourceStamp();
				EquipmentSettings->Definitions.Reset();
				EquipmentSettings->StarterRodDefinitionId = NAME_None;
				EquipmentSettings->StarterBaitDefinitionId = NAME_None;
				EquipmentSettings->StarterFloatDefinitionId = NAME_None;
				EquipmentSettings->DriftwoodDefinitionId = NAME_None;
			}
		}

		/** 恢复流程只写回本夹具改过的字段；不保存配置文件，不影响用户正在编辑的正式内容资产或 ini。 */
		~FInvalidDefaultCatalogOverride()
		{
			if (UCatFishCatalogSettings* FishSettings = GetMutableDefault<UCatFishCatalogSettings>())
			{
				FishSettings->ContentSchemaVersion = SavedFishContentSchemaVersion;
				FishSettings->DataRevision = SavedFishDataRevision;
				FishSettings->SourceStamp = SavedFishSourceStamp;
				FishSettings->Definitions = SavedFishDefinitions;
			}
			if (UCatEquipmentSettings* EquipmentSettings = GetMutableDefault<UCatEquipmentSettings>())
			{
				EquipmentSettings->ContentSchemaVersion = SavedEquipmentContentSchemaVersion;
				EquipmentSettings->DataRevision = SavedEquipmentDataRevision;
				EquipmentSettings->SourceStamp = SavedEquipmentSourceStamp;
				EquipmentSettings->Definitions = SavedEquipmentDefinitions;
				EquipmentSettings->StarterRodDefinitionId = SavedStarterRodDefinitionId;
				EquipmentSettings->StarterBaitDefinitionId = SavedStarterBaitDefinitionId;
				EquipmentSettings->StarterFloatDefinitionId = SavedStarterFloatDefinitionId;
				EquipmentSettings->DriftwoodDefinitionId = SavedDriftwoodDefinitionId;
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishCatalogDuplicateIdTest,
	"Catfishing.Unit.Data.FishCatalog.DuplicateRuntimeDefinitionsFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishCatalogSelectionCapabilityTest,
	"Catfishing.Unit.Data.FishCatalog.SelectionRequiresEnvironmentAndFightCapability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishCatalogSinglePlayerSelectionTest,
	"Catfishing.Unit.Data.FishCatalog.SinglePlayerSelectionIgnoresStaminaAndStopsAtStrength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishCatalogValidationTest,
	"Catfishing.Unit.Data.FishCatalog.ValidationReportsSchemaDuplicateAndInvalidReference",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishCatalogMalformedRegionIdsTest,
	"Catfishing.Unit.Data.FishCatalog.ValidationRejectsMalformedRegionIds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishCatalogMalformedChumAffinitiesTest,
	"Catfishing.Unit.Data.FishCatalog.ValidationRejectsMalformedChumAffinities",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishCatalogMalformedPreferredBaitIdsTest,
	"Catfishing.Unit.Data.FishCatalog.ValidationRejectsMalformedPreferredSpecialBaitIds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishCatalogInvalidSpecialBaitReferenceSelectionTest,
	"Catfishing.Unit.Data.FishCatalog.SelectionRejectsInvalidSpecialBaitReference",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishCatalogDeterministicEncounterTest,
	"Catfishing.Unit.Data.FishCatalog.EncounterSelectionIsDeterministicAndOrderIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatDataCatalogUnifiedValidationTest,
	"Catfishing.Unit.Data.CatalogValidation.UnifiedReportRequiresFishAndEquipmentSources",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatDataCatalogValidationCommandletTest,
	"Catfishing.Unit.Data.CatalogValidation.CommandletFailsInvalidDefaultCatalogs",
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

	Settings->DataRevision = 1;
	CatFishCatalogSettingsTest::MarkFishCatalogSource(Settings);
	UCatFishDefinition* FirstDefinition = CatFishCatalogSettingsTest::MakeReadyFishDefinition(
		TEXT("DuplicateFish"), ECatFishBodyClass::Standard, 1, 1.0, 1.0);
	UCatFishDefinition* SecondDefinition = CatFishCatalogSettingsTest::MakeReadyFishDefinition(
		TEXT("DuplicateFish"), ECatFishBodyClass::Standard, 1, 1.0, 1.0);
	Settings->Definitions = {FirstDefinition, SecondDefinition};

	TestNull(TEXT("重复 FishDefinitionId 返回空而不是任选一条"),
		Settings->FindRuntimeDefinition(TEXT("DuplicateFish")));
	const FCatDataCatalogValidationResult Validation = Settings->ValidateRuntimeCatalog();
	TestFalse(TEXT("重复 ID 目录整体不可运行"), Validation.bValid);
	TestTrue(TEXT("重复 ID 暴露 DuplicateStableId"), CatFishCatalogSettingsTest::HasIssue(
		Validation, ECatDataCatalogIssueCode::DuplicateStableId, TEXT("DuplicateFish"), TEXT("FishDefinitionId")));
	return !HasAnyErrors();
}

// 测试流程：只放入一条巨鱼定义，先用单人/低能力查询，再用足额协作能力查询；选择器必须先按地点和协作能力快照
// （在场人数、合计力量、合计体力）筛掉够不着的定义，剩下的候选集内部才轮到均匀抽取。
bool FCatFishCatalogSelectionCapabilityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatFishCatalogSettings* Settings = NewObject<UCatFishCatalogSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建瞬态鱼目录选择场景"), Settings);
	if (!Settings)
	{
		return false;
	}

	Settings->DataRevision = 1;
	CatFishCatalogSettingsTest::MarkFishCatalogSource(Settings);
	UCatFishDefinition* GiantDefinition = CatFishCatalogSettingsTest::MakeReadyFishDefinition(
		TEXT("GiantFish"), ECatFishBodyClass::Giant, 2, 10.0, 9.0);
	Settings->Definitions = {GiantDefinition};

	FCatFishEncounterSelectionRequest LowCapabilityRequest = CatFishCatalogSettingsTest::MakeSelectionRequest();
	LowCapabilityRequest.ActivePlayerCount = 1;
	LowCapabilityRequest.CombinedFishingStrength = 5.0;
	LowCapabilityRequest.CombinedFightStamina = 5.0;
	const FCatFishEncounterSelectionResult LowCapabilitySelection = Settings->GenerateEncounterSelection(
		LowCapabilityRequest, nullptr);
	TestFalse(TEXT("协作人数和能力不足时不生成巨鱼 Encounter"), LowCapabilitySelection.bSelected);
	TestTrue(TEXT("选择失败时不伪造鱼种"), LowCapabilitySelection.FishDefinitionId.IsNone());
	TestEqual(TEXT("选择失败时重量清零"), LowCapabilitySelection.FishWeightKilograms, 0.0);

	const FCatFishEncounterSelectionResult Selection = Settings->GenerateEncounterSelection(
		CatFishCatalogSettingsTest::MakeSelectionRequest(), nullptr);
	TestTrue(TEXT("足额协作能力生成 Encounter"), Selection.bSelected);
	TestEqual(TEXT("足额协作能力选择巨鱼稳定 ID"), Selection.FishDefinitionId, GiantDefinition->FishDefinitionId);
	TestTrue(TEXT("服务器重量落在定义范围内"), Selection.FishWeightKilograms >= 1.0 && Selection.FishWeightKilograms <= 2.0);
	TestFalse(TEXT("成功 Encounter 记录内容 Hash"), Selection.ContentHashHex.IsEmpty());
	return !HasAnyErrors();
}

// 测试流程：用单人快照（1 人、合计力量 50、合计体力 100）对两条鱼各查一次。两条鱼的体力都取 260——正好是落盘湖心巨影的体力，
// 远高于飞书拍定的猫体力上限 100，用来锁住"体力不再参与可选性判定"这一条：只要体力还在判据里，两条鱼都会被筛掉。
// 差异只在力量和最低协作人数：45 那条对应落盘的河口鲈（5.0kg × 9.0），单人必须选得出；112 那条对应湖心巨影（40.0kg × 2.8）
// 且要求 2 人，单人必须选不出。后者同时覆盖力量和人数两道闸，正是飞书「单人局不出超出能力档位的鱼」要的效果。
bool FCatFishCatalogSinglePlayerSelectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatFishCatalogSettings* Settings = NewObject<UCatFishCatalogSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建单人选鱼场景鱼目录"), Settings);
	if (!Settings)
	{
		return false;
	}

	Settings->DataRevision = 1;
	CatFishCatalogSettingsTest::MarkFishCatalogSource(Settings);
	UCatFishDefinition* SinglePlayerFish = CatFishCatalogSettingsTest::MakeReadyFishDefinition(
		TEXT("SinglePlayerReachableFish"), ECatFishBodyClass::Standard, 1, 45.0, 260.0);
	UCatFishDefinition* GiantFish = CatFishCatalogSettingsTest::MakeReadyFishDefinition(
		TEXT("SinglePlayerUnreachableFish"), ECatFishBodyClass::Giant, 2, 112.0, 260.0);

	FCatFishEncounterSelectionRequest SinglePlayerRequest = CatFishCatalogSettingsTest::MakeSelectionRequest();
	SinglePlayerRequest.ActivePlayerCount = 1;
	SinglePlayerRequest.CombinedFishingStrength = 50.0;
	SinglePlayerRequest.CombinedFightStamina = 100.0;

	Settings->Definitions = {SinglePlayerFish};
	const FCatFishEncounterSelectionResult ReachableSelection = Settings->GenerateEncounterSelection(
		SinglePlayerRequest, nullptr);
	TestTrue(TEXT("单人能选出力量够得着的鱼"), ReachableSelection.bSelected);
	TestEqual(TEXT("单人选出的正是那条鱼"), ReachableSelection.FishDefinitionId, SinglePlayerFish->FishDefinitionId);
	TestEqual(TEXT("鱼体力远高于合计体力也照样进候选"), ReachableSelection.FishFightStamina, 260.0);
	TestEqual(TEXT("结果原样带回本次合计体力"), ReachableSelection.CombinedFightStamina, 100.0);

	Settings->Definitions = {GiantFish};
	const FCatFishEncounterSelectionResult UnreachableSelection = Settings->GenerateEncounterSelection(
		SinglePlayerRequest, nullptr);
	TestFalse(TEXT("单人选不出力量与人数都够不着的巨鱼"), UnreachableSelection.bSelected);
	TestTrue(TEXT("选不中时不伪造鱼种"), UnreachableSelection.FishDefinitionId.IsNone());
	return !HasAnyErrors();
}

// 测试流程：同时制造不支持版本、重复鱼种 ID 和缺失特殊饵引用；校验结果必须逐项报告，不能只返回一个模糊失败。
bool FCatFishCatalogValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatFishCatalogSettings* FishCatalog = NewObject<UCatFishCatalogSettings>(GetTransientPackage());
	UCatEquipmentSettings* EquipmentCatalog = NewObject<UCatEquipmentSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建鱼目录校验 Settings"), FishCatalog);
	TestNotNull(TEXT("可创建装备目录校验 Settings"), EquipmentCatalog);
	if (!FishCatalog || !EquipmentCatalog)
	{
		return false;
	}

	FishCatalog->ContentSchemaVersion = 99;
	FishCatalog->DataRevision = 7;
	CatFishCatalogSettingsTest::MarkFishCatalogSource(FishCatalog);
	UCatFishDefinition* FirstDefinition = CatFishCatalogSettingsTest::MakeReadyFishDefinition(
		TEXT("DuplicateFish"), ECatFishBodyClass::Standard, 1, 1.0, 1.0);
	UCatFishDefinition* SecondDefinition = CatFishCatalogSettingsTest::MakeReadyFishDefinition(
		TEXT("DuplicateFish"), ECatFishBodyClass::Standard, 1, 1.0, 1.0);
	FirstDefinition->PreferredSpecialBaitIds = {TEXT("MissingSpecialBait")};
	FishCatalog->Definitions = {FirstDefinition, SecondDefinition};

	EquipmentCatalog->DataRevision = 1;
	CatFishCatalogSettingsTest::MarkEquipmentCatalogSource(EquipmentCatalog);
	EquipmentCatalog->Definitions = {
		CatFishCatalogSettingsTest::MakeReadySpecialBaitDefinition(TEXT("OtherSpecialBait"))};

	const FCatDataCatalogValidationResult Validation = FishCatalog->ValidateRuntimeCatalog(EquipmentCatalog);
	TestFalse(TEXT("非法鱼目录不可运行"), Validation.bValid);
	TestEqual(TEXT("校验结果保留目录 SchemaVersion"), Validation.SchemaVersion, 99);
	TestEqual(TEXT("校验结果保留目录 DataRevision"), Validation.DataRevision, static_cast<int64>(7));
	TestEqual(TEXT("校验结果保留来源修订"), Validation.SourceStamp.SourceRevision, static_cast<int64>(547));
	TestFalse(TEXT("非法目录仍给出可诊断 ContentHash"), Validation.ContentHashHex.IsEmpty());
	TestTrue(TEXT("不支持版本报告 UnsupportedSchemaVersion"), CatFishCatalogSettingsTest::HasIssue(
		Validation, ECatDataCatalogIssueCode::UnsupportedSchemaVersion, NAME_None, TEXT("ContentSchemaVersion")));
	TestTrue(TEXT("重复鱼种报告 DuplicateStableId"), CatFishCatalogSettingsTest::HasIssue(
		Validation, ECatDataCatalogIssueCode::DuplicateStableId, TEXT("DuplicateFish"), TEXT("FishDefinitionId")));
	TestTrue(TEXT("缺失特殊饵报告 InvalidReference"), CatFishCatalogSettingsTest::HasIssue(
		Validation, ECatDataCatalogIssueCode::InvalidReference, TEXT("DuplicateFish"), TEXT("PreferredSpecialBaitIds")));
	return !HasAnyErrors();
}

// 测试流程：先给一条本来完整的鱼塞入空地点名，再改成同一地点写两遍；两种写法都要被目录校验拦成 RegionIds 的 InvalidDefinition。
// 锁定的不变量：readiness 只数了 RegionIds 的长度，数组里装的是什么必须由目录校验负责，空名和重复项不能靠"反正筛不中"蒙混过去。
bool FCatFishCatalogMalformedRegionIdsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatFishCatalogSettings* FishCatalog = NewObject<UCatFishCatalogSettings>(GetTransientPackage());
	UCatFishDefinition* FishDefinition = CatFishCatalogSettingsTest::MakeReadyFishDefinition(
		TEXT("FishWithBadRegions"), ECatFishBodyClass::Standard, 1, 1.0, 1.0);
	TestNotNull(TEXT("可创建地点非法场景鱼目录"), FishCatalog);
	TestNotNull(TEXT("可创建地点非法场景鱼定义"), FishDefinition);
	if (!FishCatalog || !FishDefinition)
	{
		return false;
	}

	FishCatalog->DataRevision = 31;
	CatFishCatalogSettingsTest::MarkFishCatalogSource(FishCatalog);
	FishCatalog->Definitions = {FishDefinition};

	FishDefinition->RegionIds = {TEXT("River"), NAME_None};
	const FCatDataCatalogValidationResult EmptyNameValidation = FishCatalog->ValidateRuntimeCatalog();
	TestFalse(TEXT("地点数组含空名时目录不可运行"), EmptyNameValidation.bValid);
	TestTrue(TEXT("空地点名报告 RegionIds 的 InvalidDefinition"), CatFishCatalogSettingsTest::HasIssue(
		EmptyNameValidation, ECatDataCatalogIssueCode::InvalidDefinition, TEXT("FishWithBadRegions"), TEXT("RegionIds")));

	FishDefinition->RegionIds = {TEXT("River"), TEXT("River")};
	const FCatDataCatalogValidationResult DuplicateValidation = FishCatalog->ValidateRuntimeCatalog();
	TestFalse(TEXT("地点数组含重复项时目录不可运行"), DuplicateValidation.bValid);
	TestTrue(TEXT("重复地点报告 RegionIds 的 InvalidDefinition"), CatFishCatalogSettingsTest::HasIssue(
		DuplicateValidation, ECatDataCatalogIssueCode::InvalidDefinition, TEXT("FishWithBadRegions"), TEXT("RegionIds")));

	FishDefinition->RegionIds = {TEXT("River"), TEXT("ForestLake")};
	TestTrue(TEXT("同一条鱼写两个不同地点是合法配置"), FishCatalog->ValidateRuntimeCatalog().bValid);
	return !HasAnyErrors();
}

// 测试流程：把窝料归属分别写成 Unknown 和同一味重复两次，再回到"香+酵"这种飞书真实存在的多味配置。
// 锁定的不变量：Unknown 只代表鱼表格该行没填，它不能作为一个"第四味"混进正式目录；而一条鱼吃两味必须继续被接受。
bool FCatFishCatalogMalformedChumAffinitiesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatFishCatalogSettings* FishCatalog = NewObject<UCatFishCatalogSettings>(GetTransientPackage());
	UCatFishDefinition* FishDefinition = CatFishCatalogSettingsTest::MakeReadyFishDefinition(
		TEXT("FishWithBadChum"), ECatFishBodyClass::Standard, 1, 1.0, 1.0);
	TestNotNull(TEXT("可创建窝料归属非法场景鱼目录"), FishCatalog);
	TestNotNull(TEXT("可创建窝料归属非法场景鱼定义"), FishDefinition);
	if (!FishCatalog || !FishDefinition)
	{
		return false;
	}

	FishCatalog->DataRevision = 32;
	CatFishCatalogSettingsTest::MarkFishCatalogSource(FishCatalog);
	FishCatalog->Definitions = {FishDefinition};

	FishDefinition->ChumAffinities = {ECatChumAffinity::Fragrant, ECatChumAffinity::Unknown};
	const FCatDataCatalogValidationResult UnknownValidation = FishCatalog->ValidateRuntimeCatalog();
	TestFalse(TEXT("窝料归属含 Unknown 时目录不可运行"), UnknownValidation.bValid);
	TestTrue(TEXT("Unknown 窝料归属报告 ChumAffinities 的 InvalidDefinition"), CatFishCatalogSettingsTest::HasIssue(
		UnknownValidation, ECatDataCatalogIssueCode::InvalidDefinition, TEXT("FishWithBadChum"), TEXT("ChumAffinities")));

	FishDefinition->ChumAffinities = {ECatChumAffinity::Fragrant, ECatChumAffinity::Fragrant};
	const FCatDataCatalogValidationResult DuplicateValidation = FishCatalog->ValidateRuntimeCatalog();
	TestFalse(TEXT("窝料归属含重复味时目录不可运行"), DuplicateValidation.bValid);
	TestTrue(TEXT("重复窝料归属报告 ChumAffinities 的 InvalidDefinition"), CatFishCatalogSettingsTest::HasIssue(
		DuplicateValidation, ECatDataCatalogIssueCode::InvalidDefinition, TEXT("FishWithBadChum"), TEXT("ChumAffinities")));

	FishDefinition->ChumAffinities = {ECatChumAffinity::Fragrant, ECatChumAffinity::Fermented};
	TestTrue(TEXT("一条鱼同时吃香与酵是合法配置"), FishCatalog->ValidateRuntimeCatalog().bValid);
	return !HasAnyErrors();
}

// 测试流程：在有 Equipment 目录的前提下，把偏好特殊饵写成空 ID 或重复 ID，检查报出来的是哪一类问题。
// 锁定的不变量："这行没填饵"必须报成 InvalidDefinition，不能落进跨目录引用分支被报成 InvalidReference——
// 后者会把配置遗漏说成"引用了不存在的饵"，两者的修法完全不同。
bool FCatFishCatalogMalformedPreferredBaitIdsTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatFishCatalogSettings* FishCatalog = NewObject<UCatFishCatalogSettings>(GetTransientPackage());
	UCatEquipmentSettings* EquipmentCatalog = NewObject<UCatEquipmentSettings>(GetTransientPackage());
	UCatFishDefinition* FishDefinition = CatFishCatalogSettingsTest::MakeReadyFishDefinition(
		TEXT("FishWithBadBaitIds"), ECatFishBodyClass::Standard, 1, 1.0, 1.0);
	TestNotNull(TEXT("可创建偏好饵非法场景鱼目录"), FishCatalog);
	TestNotNull(TEXT("可创建偏好饵非法场景装备目录"), EquipmentCatalog);
	TestNotNull(TEXT("可创建偏好饵非法场景鱼定义"), FishDefinition);
	if (!FishCatalog || !EquipmentCatalog || !FishDefinition)
	{
		return false;
	}

	EquipmentCatalog->DataRevision = 33;
	CatFishCatalogSettingsTest::MarkEquipmentCatalogSource(EquipmentCatalog);
	EquipmentCatalog->Definitions = {
		CatFishCatalogSettingsTest::MakeReadySpecialBaitDefinition(TEXT("MoonlightBait"))};

	FishCatalog->DataRevision = 33;
	CatFishCatalogSettingsTest::MarkFishCatalogSource(FishCatalog);
	FishCatalog->Definitions = {FishDefinition};

	FishDefinition->PreferredSpecialBaitIds = {TEXT("MoonlightBait"), NAME_None};
	const FCatDataCatalogValidationResult EmptyIdValidation = FishCatalog->ValidateRuntimeCatalog(EquipmentCatalog);
	TestFalse(TEXT("偏好饵含空 ID 时目录不可运行"), EmptyIdValidation.bValid);
	TestTrue(TEXT("空偏好饵 ID 报告 PreferredSpecialBaitIds 的 InvalidDefinition"), CatFishCatalogSettingsTest::HasIssue(
		EmptyIdValidation, ECatDataCatalogIssueCode::InvalidDefinition, TEXT("FishWithBadBaitIds"), TEXT("PreferredSpecialBaitIds")));
	TestFalse(TEXT("空偏好饵 ID 不被误报成引用了不存在的饵"), CatFishCatalogSettingsTest::HasIssue(
		EmptyIdValidation, ECatDataCatalogIssueCode::InvalidReference, TEXT("FishWithBadBaitIds"), TEXT("PreferredSpecialBaitIds")));

	FishDefinition->PreferredSpecialBaitIds = {TEXT("MoonlightBait"), TEXT("MoonlightBait")};
	const FCatDataCatalogValidationResult DuplicateValidation = FishCatalog->ValidateRuntimeCatalog(EquipmentCatalog);
	TestFalse(TEXT("偏好饵含重复 ID 时目录不可运行"), DuplicateValidation.bValid);
	TestTrue(TEXT("重复偏好饵 ID 报告 PreferredSpecialBaitIds 的 InvalidDefinition"), CatFishCatalogSettingsTest::HasIssue(
		DuplicateValidation, ECatDataCatalogIssueCode::InvalidDefinition, TEXT("FishWithBadBaitIds"), TEXT("PreferredSpecialBaitIds")));

	FishDefinition->PreferredSpecialBaitIds = {TEXT("MoonlightBait")};
	TestTrue(TEXT("指向真实特殊饵的单条偏好是合法配置"),
		FishCatalog->ValidateRuntimeCatalog(EquipmentCatalog).bValid);
	return !HasAnyErrors();
}

// 测试流程：鱼定义引用缺失特殊饵时，通过生产选择接口传入 Equipment 目录；选择必须 fail-closed，不能只让独立校验函数报告问题。
bool FCatFishCatalogInvalidSpecialBaitReferenceSelectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatFishCatalogSettings* FishCatalog = NewObject<UCatFishCatalogSettings>(GetTransientPackage());
	UCatEquipmentSettings* EquipmentCatalog = NewObject<UCatEquipmentSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建鱼目录特殊饵引用场景"), FishCatalog);
	TestNotNull(TEXT("可创建装备目录特殊饵引用场景"), EquipmentCatalog);
	if (!FishCatalog || !EquipmentCatalog)
	{
		return false;
	}

	UCatFishDefinition* FishDefinition = CatFishCatalogSettingsTest::MakeReadyFishDefinition(
		TEXT("FishNeedsSpecialBait"), ECatFishBodyClass::Standard, 1, 1.0, 1.0);
	FishDefinition->PreferredSpecialBaitIds = {TEXT("MissingSpecialBait")};
	FishCatalog->DataRevision = 8;
	CatFishCatalogSettingsTest::MarkFishCatalogSource(FishCatalog);
	FishCatalog->Definitions = {FishDefinition};

	EquipmentCatalog->DataRevision = 1;
	CatFishCatalogSettingsTest::MarkEquipmentCatalogSource(EquipmentCatalog);
	EquipmentCatalog->Definitions = {
		CatFishCatalogSettingsTest::MakeReadySpecialBaitDefinition(TEXT("OtherSpecialBait"))};

	const FCatDataCatalogValidationResult Validation = FishCatalog->ValidateRuntimeCatalog(EquipmentCatalog);
	TestFalse(TEXT("坏特殊饵引用让鱼目录不可运行"), Validation.bValid);
	TestTrue(TEXT("坏特殊饵引用报告 InvalidReference"), CatFishCatalogSettingsTest::HasIssue(
		Validation, ECatDataCatalogIssueCode::InvalidReference, TEXT("FishNeedsSpecialBait"), TEXT("PreferredSpecialBaitIds")));

	const FCatFishEncounterSelectionResult Selection = FishCatalog->GenerateEncounterSelection(
		CatFishCatalogSettingsTest::MakeSelectionRequest(),
		EquipmentCatalog);
	TestFalse(TEXT("生产选择接口传入 Equipment 目录后不生成 Encounter"), Selection.bSelected);
	TestTrue(TEXT("坏引用选择失败时不伪造鱼种"), Selection.FishDefinitionId.IsNone());

	TestNull(TEXT("ID 查找入口同样不能绕过特殊饵引用校验"),
		FishCatalog->FindRuntimeDefinition(TEXT("FishNeedsSpecialBait")));

	return !HasAnyErrors();
}

// 测试流程：两份目录只改变编辑器列表顺序，用同一冻结输入和 seed 生成 encounter；结果与 ContentHash 必须一致，
// Revision 与启用 gate 改变时 Hash 必须改变。
bool FCatFishCatalogDeterministicEncounterTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatFishDefinition* AlphaDefinition = CatFishCatalogSettingsTest::MakeReadyFishDefinition(
		TEXT("FishAlpha"), ECatFishBodyClass::Standard, 1, 1.0, 1.0);
	UCatFishDefinition* BetaDefinition = CatFishCatalogSettingsTest::MakeReadyFishDefinition(
		TEXT("FishBeta"), ECatFishBodyClass::Giant, 2, 10.0, 9.0);

	UCatFishCatalogSettings* FirstCatalog = NewObject<UCatFishCatalogSettings>(GetTransientPackage());
	UCatFishCatalogSettings* ReorderedCatalog = NewObject<UCatFishCatalogSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建第一份确定性鱼目录"), FirstCatalog);
	TestNotNull(TEXT("可创建重排后的确定性鱼目录"), ReorderedCatalog);
	if (!FirstCatalog || !ReorderedCatalog)
	{
		return false;
	}

	FirstCatalog->DataRevision = 12;
	CatFishCatalogSettingsTest::MarkFishCatalogSource(FirstCatalog);
	FirstCatalog->Definitions = {BetaDefinition, AlphaDefinition};
	ReorderedCatalog->DataRevision = 12;
	CatFishCatalogSettingsTest::MarkFishCatalogSource(ReorderedCatalog);
	ReorderedCatalog->Definitions = {AlphaDefinition, BetaDefinition};

	const FCatFishEncounterSelectionRequest Request = CatFishCatalogSettingsTest::MakeSelectionRequest();
	const FCatFishEncounterSelectionResult First = FirstCatalog->GenerateEncounterSelection(Request, nullptr);
	const FCatFishEncounterSelectionResult Replay = FirstCatalog->GenerateEncounterSelection(Request, nullptr);
	const FCatFishEncounterSelectionResult Reordered = ReorderedCatalog->GenerateEncounterSelection(Request, nullptr);

	TestTrue(TEXT("第一份目录可生成 Encounter"), First.bSelected);
	TestTrue(TEXT("同目录重放可生成 Encounter"), Replay.bSelected);
	TestTrue(TEXT("重排目录可生成 Encounter"), Reordered.bSelected);
	TestEqual(TEXT("Encounter 记录 SchemaVersion"), First.SchemaVersion, UCatFishCatalogSettings::CurrentContentSchemaVersion);
	TestEqual(TEXT("Encounter 记录 DataRevision"), First.DataRevision, static_cast<int64>(12));
	TestFalse(TEXT("Encounter 记录 ContentHash"), First.ContentHashHex.IsEmpty());
	TestEqual(TEXT("Encounter 记录选择种子"), First.SelectionSeed, Request.SelectionSeed);
	TestEqual(TEXT("同目录同输入选择同一鱼种"), First.FishDefinitionId, Replay.FishDefinitionId);
	TestTrue(TEXT("同目录同输入生成同一重量"), FMath::IsNearlyEqual(First.FishWeightKilograms, Replay.FishWeightKilograms));
	TestEqual(TEXT("重排目录仍选择同一鱼种"), First.FishDefinitionId, Reordered.FishDefinitionId);
	TestTrue(TEXT("重排目录仍生成同一重量"), FMath::IsNearlyEqual(First.FishWeightKilograms, Reordered.FishWeightKilograms));
	TestEqual(TEXT("ContentHash 不受定义列表顺序影响"), First.ContentHashHex, Reordered.ContentHashHex);

	AlphaDefinition->bEnableRuntimeDefinition = false;
	TestNotEqual(TEXT("Definition 启用 gate 改变会改变 ContentHash"),
		First.ContentHashHex,
		FirstCatalog->ComputeContentHashHex());
	AlphaDefinition->bEnableRuntimeDefinition = true;

	ReorderedCatalog->DataRevision = 13;
	TestNotEqual(TEXT("DataRevision 改变会改变 ContentHash"),
		First.ContentHashHex,
		ReorderedCatalog->ComputeContentHashHex());
	return !HasAnyErrors();
}


// 测试流程：构造一份互相引用的 Fish+Equipment 内容包；合并校验必须一次性给出包级 Hash，并在来源戳损坏时 fail-closed。
bool FCatDataCatalogUnifiedValidationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UCatFishCatalogSettings* FishCatalog = NewObject<UCatFishCatalogSettings>(GetTransientPackage());
	UCatEquipmentSettings* EquipmentCatalog = NewObject<UCatEquipmentSettings>(GetTransientPackage());
	TestNotNull(TEXT("可创建统一校验鱼目录"), FishCatalog);
	TestNotNull(TEXT("可创建统一校验装备目录"), EquipmentCatalog);
	if (!FishCatalog || !EquipmentCatalog)
	{
		return false;
	}

	UCatEquipmentDefinition* SpecialBait = CatFishCatalogSettingsTest::MakeReadySpecialBaitDefinition(TEXT("SpecialBaitA"));
	EquipmentCatalog->DataRevision = 21;
	CatFishCatalogSettingsTest::MarkEquipmentCatalogSource(EquipmentCatalog);
	EquipmentCatalog->Definitions = {SpecialBait};

	UCatFishDefinition* FishDefinition = CatFishCatalogSettingsTest::MakeReadyFishDefinition(
		TEXT("FishWithBait"), ECatFishBodyClass::Standard, 1, 1.0, 1.0);
	FishDefinition->PreferredSpecialBaitIds = {TEXT("SpecialBaitA")};
	FishCatalog->DataRevision = 22;
	CatFishCatalogSettingsTest::MarkFishCatalogSource(FishCatalog);
	FishCatalog->Definitions = {FishDefinition};

	const FCatDataCatalogValidationReport ValidReport = FCatDataCatalogValidator::ValidateRuntimeCatalogs(
		FishCatalog,
		EquipmentCatalog);
	TestTrue(TEXT("完整 Fish+Equipment 内容包可运行"), ValidReport.bValid);
	TestEqual(TEXT("合并报告包含两个目录结果"), ValidReport.Catalogs.Num(), 2);
	TestFalse(TEXT("合并报告给出内容包 Hash"), ValidReport.ContentHashHex.IsEmpty());
	TestEqual(TEXT("完整内容包没有结构化问题"), ValidReport.Issues.Num(), 0);

	EquipmentCatalog->SourceStamp.SourceRevision = 0;
	const FCatDataCatalogValidationReport MissingSourceReport = FCatDataCatalogValidator::ValidateRuntimeCatalogs(
		FishCatalog,
		EquipmentCatalog);
	TestFalse(TEXT("缺少装备来源戳时内容包不可运行"), MissingSourceReport.bValid);
	TestTrue(TEXT("缺来源报告 MissingSource"), CatFishCatalogSettingsTest::HasReportIssue(
		MissingSourceReport, ECatDataCatalogIssueCode::MissingSource, NAME_None, TEXT("SourceStamp")));

	return !HasAnyErrors();
}



// 测试流程：在内存中把默认 Fish/Equipment 目录改成缺来源、缺修订、缺显式清单的非法状态；Commandlet 必须返回失败码，让
// CI/Cook 能挡住未落正式内容的项目。
bool FCatDataCatalogValidationCommandletTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatFishCatalogSettingsTest::FInvalidDefaultCatalogOverride InvalidDefaults;
	AddExpectedErrorPlain(TEXT("Event=data_catalog_validation_failed"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedMessagePlain(TEXT("Event=data_catalog_validation_issue"), EAutomationExpectedErrorFlags::Contains, 6);
	UCatDataCatalogValidationCommandlet* Commandlet = NewObject<UCatDataCatalogValidationCommandlet>(
		GetTransientPackage());
	TestNotNull(TEXT("可创建 DataCatalog 验证命令入口"), Commandlet);
	if (!Commandlet)
	{
		return false;
	}

	TestEqual(TEXT("非法默认目录让命令入口返回 CI 失败码"), Commandlet->Main(TEXT("")), 1);
	return !HasAnyErrors();
}
#endif // WITH_DEV_AUTOMATION_TESTS
