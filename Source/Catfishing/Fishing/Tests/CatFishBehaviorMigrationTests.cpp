#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Data/CatFishPersonalityDefinition.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishBehaviorLegacyMigrationTest,
	"Catfishing.Unit.Fishing.Behavior.LegacyPersonalityMigrationPreservesUnitsAndSeparatesEffort",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishBehaviorLegacyMigrationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	struct FLegacySpeedCase
	{
		const TCHAR* Name;
		double CalmCmPerSecond;
		double StruggleCmPerSecond;
	};
	// 来源是迁移前四类正式资产的速度快照；时长另用不同端点的夹具，专门检测单位或阶段交换。
	const FLegacySpeedCase Cases[] = {
		{ TEXT("Small"), 65.0, 110.0 }, { TEXT("Medium"), 80.0, 140.0 },
		{ TEXT("Large"), 95.0, 180.0 }, { TEXT("Giant"), 120.0, 240.0 }
	};
	const FVector2D LegacyEaseDuration(1.25, 2.75);
	const FVector2D LegacyOutwardDuration(3.25, 4.75);
	const FCatFishSteeringConfig NewDefaults;
	for (const FLegacySpeedCase& Case : Cases)
	{
		UCatFightPersonalityDefinition* Legacy = NewObject<UCatFightPersonalityDefinition>();
		Legacy->FightPersonalityId = FName(Case.Name);
		Legacy->CalmMovementSpeedCentimetersPerSecond = Case.CalmCmPerSecond;
		Legacy->StruggleMovementSpeedCentimetersPerSecond = Case.StruggleCmPerSecond;
		Legacy->CalmDurationRangeSeconds = LegacyEaseDuration;
		Legacy->StruggleDurationRangeSeconds = LegacyOutwardDuration;
		Legacy->DirectionRetargetDurationRangeSeconds = FVector2D(0.35, 0.85);
		Legacy->MaximumTurnRateDegreesPerSecond = 137.0;
		// 这些旧倍率不是无量纲出力参数；故意拉开它们，避免迁移暗中用旧价格构造新 u。
		Legacy->BaseDrainMultiplier = 37.0;
		Legacy->StruggleDrainMultiplier = 91.0;
		if (!TestTrue(FString::Printf(TEXT("%s旧版本执行一次迁移"), Case.Name), Legacy->MigrateLegacyMotionSettings())) return false;
		TestEqual(FString::Printf(TEXT("%s满力参考游速保留cm/s"), Case.Name),
			Legacy->FullEffortMovementSpeedCentimetersPerSecond, Case.StruggleCmPerSecond);
		TestTrue(TEXT("旧挣扎秒数映射为新外冲最长时长区间"),
			Legacy->AdaptiveSteeringConfig.OutwardDurationRangeSeconds.Equals(LegacyOutwardDuration, 1e-9));
		TestTrue(TEXT("旧平静秒数映射为新缓游最长时长区间"),
			Legacy->AdaptiveSteeringConfig.EaseOffDurationRangeSeconds.Equals(LegacyEaseDuration, 1e-9));
		TestEqual(TEXT("转向限制保留度每秒，不转成弧度"),
			Legacy->AdaptiveSteeringConfig.MaximumTurnRateDegreesPerSecond, 137.0);
		TestTrue(TEXT("方向重选仍使用秒区间"), Legacy->AdaptiveSteeringConfig.RetargetDurationRangeSeconds.Equals(
			FVector2D(0.35, 0.85), 1e-9));
		TestTrue(TEXT("新外冲出力不由旧速度比或耗体倍率推导"),
			Legacy->AdaptiveSteeringConfig.OutwardEffortRange.Equals(NewDefaults.OutwardEffortRange, 1e-9));
		TestTrue(TEXT("新横切出力不沿用旧挣扎耗体倍率"),
			Legacy->AdaptiveSteeringConfig.LateralEffortRange.Equals(NewDefaults.LateralEffortRange, 1e-9));
		TestTrue(TEXT("新缓游出力不沿用旧平静耗体倍率"),
			Legacy->AdaptiveSteeringConfig.EaseOffEffortRange.Equals(NewDefaults.EaseOffEffortRange, 1e-9));
		TestEqual(TEXT("成功迁移留下显式版本"), Legacy->AdaptiveMotionVersion, 1);
		TestTrue(TEXT("迁移后的完整性格可用于正式运行"), Legacy->IsRuntimeDefinitionReady());
		TestFalse(TEXT("重复迁移幂等"), Legacy->MigrateLegacyMotionSettings());
	}
	// 每厘米鱼价格在全局 FightBalance，不属于 Personality 迁移。这里不手工拼接费用公式，
	// 避免拿测试自造的接线冒充 Session->Simulator 的实际费用隔离；该证据由实际消费者回归提供。
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishBehaviorVersionedMigrationTest,
	"Catfishing.Unit.Fishing.Behavior.VersionedPersonalityPreservesDesignerSettingsAndRejectsInvalidNewSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishBehaviorVersionedMigrationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UCatFightPersonalityDefinition* Edited = NewObject<UCatFightPersonalityDefinition>();
	Edited->FightPersonalityId = TEXT("DesignerEdited");
	Edited->AdaptiveMotionVersion = 1;
	Edited->FullEffortMovementSpeedCentimetersPerSecond = 37.5;
	Edited->AdaptiveSteeringConfig.OutwardEffortRange = FVector2D(0.52, 0.74);
	Edited->AdaptiveSteeringConfig.EaseOffEffortRange = FVector2D(0.07, 0.18);
	Edited->AdaptiveSteeringConfig.OutwardDurationRangeSeconds = FVector2D(6.25, 7.5);
	Edited->AdaptiveSteeringConfig.EaseOffDurationRangeSeconds = FVector2D(2.5, 3.75);
	Edited->AdaptiveSteeringConfig.MaximumTurnRateDegreesPerSecond = 83.0;
	Edited->CalmMovementSpeedCentimetersPerSecond = 900.0;
	Edited->StruggleMovementSpeedCentimetersPerSecond = 999.0;
	Edited->CalmDurationRangeSeconds = FVector2D(90.0, 95.0);
	Edited->StruggleDurationRangeSeconds = FVector2D(99.0, 100.0);
	TestTrue(TEXT("策划的新参数本身合法"), Edited->IsRuntimeDefinitionReady());
	TestFalse(TEXT("版本1不再从残存旧载荷迁移"), Edited->MigrateLegacyMotionSettings());
	TestEqual(TEXT("策划满力参考游速不被旧999覆盖"), Edited->FullEffortMovementSpeedCentimetersPerSecond, 37.5);
	TestTrue(TEXT("策划缓游出力不被迁移默认值覆盖"),
		Edited->AdaptiveSteeringConfig.EaseOffEffortRange.Equals(FVector2D(0.07, 0.18), 1e-9));
	TestTrue(TEXT("策划外冲时长不被旧阶段秒数覆盖"),
		Edited->AdaptiveSteeringConfig.OutwardDurationRangeSeconds.Equals(FVector2D(6.25, 7.5), 1e-9));
	TestEqual(TEXT("策划转向参数保持原样"), Edited->AdaptiveSteeringConfig.MaximumTurnRateDegreesPerSecond, 83.0);

	Edited->FullEffortMovementSpeedCentimetersPerSecond = 0.0;
	TestFalse(TEXT("已迁移资产中的非法零速度不能触发旧模型回填"), Edited->MigrateLegacyMotionSettings());
	TestEqual(TEXT("非法新速度保留原值供诊断"), Edited->FullEffortMovementSpeedCentimetersPerSecond, 0.0);
	TestFalse(TEXT("新版本非法配置会拒绝正式运行"), Edited->IsRuntimeDefinitionReady());

	UCatFightPersonalityDefinition* MissingLegacy = NewObject<UCatFightPersonalityDefinition>();
	MissingLegacy->FightPersonalityId = TEXT("MissingLegacyData");
	TestFalse(TEXT("缺失合法旧游速不能伪装成迁移成功"), MissingLegacy->MigrateLegacyMotionSettings());
	TestEqual(TEXT("失败的旧资产仍保留未迁移版本"), MissingLegacy->AdaptiveMotionVersion, 0);
	TestFalse(TEXT("未迁移的空资产不能正式运行"), MissingLegacy->IsRuntimeDefinitionReady());
	return !HasAnyErrors();
}

#endif
