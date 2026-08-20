#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Environment/CatChumSpotSubsystem.h"
#include "Environment/CatEnvironmentSettings.h"

namespace CatChumSpotTest
{
	// 命令构造流程：拼出投窝写口的最小正式命令；身份、RequestId、ExpectedRevision、落点与三轴全部由调用方决定，
	// 以便同一个 helper 覆盖首次提交、重放、载荷漂移、陈旧 Revision 和安全夹这几条互斥路径。
	static FCatAggregationCommand MakeChumCommand(const FString& StableNetId, const FGuid RequestId,
		const int64 ExpectedRevision, const FVector& DropLocation, const double Fishy,
		const double Fragrant = 0.0, const double Fermented = 0.0)
	{
		FCatAggregationCommand Command;
		Command.Context.StableNetId = StableNetId;
		Command.Context.RequestId = RequestId;
		Command.Context.ExpectedRevision = ExpectedRevision;
		Command.DropLocation = DropLocation;
		Command.Contribution.Fishy = Fishy;
		Command.Contribution.Fragrant = Fragrant;
		Command.Contribution.Fermented = Fermented;
		return Command;
	}

	// 前置流程：取出测试要用到的 authority 窝点子系统，并锁住本轮断言依赖的那几个项目默认窝料数值。
	// 这里刻意不改 CDO：这些默认值就是策划给的半径 5 米、每 30 秒乘 0.9、Total 低于 1 归零，
	// 断言它们本身也是一条不变量——数值被悄悄改掉时应该在这里就红，而不是等下游选鱼行为莫名其妙。
	static UCatChumSpotSubsystem* PrepareSubsystem(FAutomationTestBase& Test, UWorld* World)
	{
		UCatChumSpotSubsystem* Subsystem = World ? World->GetSubsystem<UCatChumSpotSubsystem>() : nullptr;
		Test.TestNotNull(TEXT("可取得窝点子系统"), Subsystem);
		const UCatEnvironmentSettings* Settings = GetDefault<UCatEnvironmentSettings>();
		Test.TestNotNull(TEXT("可读取 Environment 设置"), Settings);
		if (!Subsystem || !Settings)
		{
			return nullptr;
		}
		Test.TestTrue(TEXT("项目默认窝料配置可运行"), Settings->IsChumRuntimeReady());
		Test.TestEqual(TEXT("项目默认窝点半径为 5 米"), Settings->ChumSpotRadius, 500.0);
		Test.TestEqual(TEXT("项目默认衰减周期为 30 秒"), Settings->ChumDecayIntervalSeconds, 30.0);
		Test.TestEqual(TEXT("项目默认衰减比例为 0.9"), Settings->ChumDecayFactor, 0.9);
		Test.TestEqual(TEXT("项目默认消散地板 Total 为 1"), Settings->ChumDecayFloorTotal, 1.0);
		return Subsystem;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatChumSpotGeometryTest,
	"Catfishing.Unit.Environment.ChumSpot.DropGeometryMergesByCircleAndResolvesNearestCenter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatChumSpotDecayTest,
	"Catfishing.Unit.Environment.ChumSpot.GlobalDecayConvergesAndTopUpKeepsCadence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatChumSpotTransactionTest,
	"Catfishing.Unit.Environment.ChumSpot.ContributionReplayRevisionAndSafetyCapContracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatChumSpotAggregationCooldownTest,
	"Catfishing.Unit.Environment.ChumSpot.AggregationMomentCooldownIsSharedByBothSources",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：用一串落点覆盖窝点几何的全部三条规则——落在已有窝圆内并池且沿用原圆心半径、落在圆外新建窝、
// 一个点同时落进多个相交窝时取圆心最近的那个；顺带锁住三轴占比访问器的取值。
bool FCatChumSpotGeometryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建窝点几何测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	UCatChumSpotSubsystem* Chum = CatChumSpotTest::PrepareSubsystem(*this, World);
	if (!Chum)
	{
		return false;
	}

	const FCatAggregationResult FirstResult = Chum->ContributeChum(CatChumSpotTest::MakeChumCommand(
		TEXT("PlayerA"), FGuid::NewGuid(), Chum->GetAggregationRevision(), FVector::ZeroVector, 100.0));
	TestTrue(TEXT("首次投窝提交成功"), FirstResult.Command.bCommitted);
	TestTrue(TEXT("首次投窝建出窝点"), FirstResult.Spot.bHasSpot);
	TestEqual(TEXT("新建窝点以落点为圆心"), FirstResult.Spot.Center, FVector::ZeroVector);
	TestEqual(TEXT("新建窝点使用半径快照"), FirstResult.Spot.Radius, 500.0);
	TestEqual(TEXT("新建窝点池等于投掷向量"), FirstResult.Spot.Pool.Fishy, 100.0);

	TestTrue(TEXT("圆内坐标命中窝点"), Chum->QueryChumSpot(FVector(400.0, 0.0, 0.0)).bHasSpot);
	const FCatChumSpotSnapshot OutsideSnapshot = Chum->QueryChumSpot(FVector(600.0, 0.0, 0.0));
	TestFalse(TEXT("圆外坐标不命中窝点"), OutsideSnapshot.bHasSpot);
	TestEqual(TEXT("未命中时 Total 为零"), OutsideSnapshot.Pool.Total(), 0.0);

	const FCatAggregationResult MergeResult = Chum->ContributeChum(CatChumSpotTest::MakeChumCommand(
		TEXT("PlayerB"), FGuid::NewGuid(), Chum->GetAggregationRevision(), FVector(400.0, 0.0, 0.0), 0.0, 50.0));
	TestTrue(TEXT("圆内落点投窝提交成功"), MergeResult.Command.bCommitted);
	TestEqual(TEXT("并池沿用原圆心"), MergeResult.Spot.Center, FVector::ZeroVector);
	TestEqual(TEXT("并池沿用原半径"), MergeResult.Spot.Radius, 500.0);
	TestEqual(TEXT("并池保留原腥轴"), MergeResult.Spot.Pool.Fishy, 100.0);
	TestEqual(TEXT("并池累加新香轴"), MergeResult.Spot.Pool.Fragrant, 50.0);
	TestEqual(TEXT("并池后 Total 为两次之和"), MergeResult.Spot.Pool.Total(), 150.0);
	// 若并池写成了"新建一个以 (400,0,0) 为圆心的窝"，(880,0,0) 会落进那个新窝；这里断言它仍然没窝，等价于断言窝点数没增加。
	TestFalse(TEXT("并池没有额外新建窝点"), Chum->QueryChumSpot(FVector(880.0, 0.0, 0.0)).bHasSpot);
	TestEqual(TEXT("并池后腥占比为三分之二"), MergeResult.Spot.Pool.FishyShare(), 2.0 / 3.0, 1e-9);
	TestEqual(TEXT("并池后香占比为三分之一"), MergeResult.Spot.Pool.FragrantShare(), 1.0 / 3.0, 1e-9);
	TestEqual(TEXT("并池后酵占比为零"), MergeResult.Spot.Pool.FermentedShare(), 0.0);

	const FCatAggregationResult FarResult = Chum->ContributeChum(CatChumSpotTest::MakeChumCommand(
		TEXT("PlayerA"), FGuid::NewGuid(), Chum->GetAggregationRevision(), FVector(2000.0, 0.0, 0.0), 30.0));
	TestTrue(TEXT("圆外落点投窝提交成功"), FarResult.Command.bCommitted);
	TestEqual(TEXT("圆外落点新建独立窝点"), FarResult.Spot.Center, FVector(2000.0, 0.0, 0.0));
	TestEqual(TEXT("新窝点只含本次投掷向量"), FarResult.Spot.Pool.Total(), 30.0);
	TestEqual(TEXT("新建窝点不影响原窝点"), Chum->QueryChumSpot(FVector::ZeroVector).Pool.Total(), 150.0);

	const FCatAggregationResult NeighbourResult = Chum->ContributeChum(CatChumSpotTest::MakeChumCommand(
		TEXT("PlayerA"), FGuid::NewGuid(), Chum->GetAggregationRevision(), FVector(800.0, 0.0, 0.0), 0.0, 0.0, 70.0));
	TestTrue(TEXT("相邻落点投窝提交成功"), NeighbourResult.Command.bCommitted);
	TestEqual(TEXT("相邻落点新建第二个窝点"), NeighbourResult.Spot.Center, FVector(800.0, 0.0, 0.0));
	// (450,0,0) 到两个圆心的距离分别是 450 和 350，两个窝都覆盖它；按圆心最近规则必须解析到 (800,0,0)。
	const FCatChumSpotSnapshot OverlapSnapshot = Chum->QueryChumSpot(FVector(450.0, 0.0, 0.0));
	TestTrue(TEXT("相交区域仍然命中窝点"), OverlapSnapshot.bHasSpot);
	TestEqual(TEXT("相交区域取圆心最近的窝点"), OverlapSnapshot.Center, FVector(800.0, 0.0, 0.0));
	TestEqual(TEXT("相交区域返回最近窝点的池"), OverlapSnapshot.Pool.Fermented, 70.0);
	TestEqual(TEXT("最近窝点酵占比为一"), OverlapSnapshot.Pool.FermentedShare(), 1.0);
	return !HasAnyErrors();
}

// 测试流程：先验证全局衰减按 30 秒一拍乘 0.9 单调收敛，再验证 Total 跨过地板时窝点被整体移除而不是留一个空壳；
// 最后单独验证补窝只加池、不重置衰减节拍——这是策划口径里最容易被实现成"每窝点独立计时"的一条。
bool FCatChumSpotDecayTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建窝点衰减测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	UCatChumSpotSubsystem* Chum = CatChumSpotTest::PrepareSubsystem(*this, World);
	if (!Chum)
	{
		return false;
	}

	TestTrue(TEXT("衰减测试首次投窝成功"), Chum->ContributeChum(CatChumSpotTest::MakeChumCommand(
		TEXT("PlayerA"), FGuid::NewGuid(), Chum->GetAggregationRevision(), FVector::ZeroVector, 100.0)).Command.bCommitted);

	Chum->AdvanceDecay(30.0);
	TestEqual(TEXT("一个周期后池乘 0.9"), Chum->QueryChumSpot(FVector::ZeroVector).Pool.Total(), 90.0, 1e-9);
	Chum->AdvanceDecay(30.0);
	TestEqual(TEXT("两个周期后池再乘 0.9"), Chum->QueryChumSpot(FVector::ZeroVector).Pool.Total(), 81.0, 1e-9);

	double PreviousTotal = Chum->QueryChumSpot(FVector::ZeroVector).Pool.Total();
	for (int32 StepIndex = 0; StepIndex < 10; ++StepIndex)
	{
		Chum->AdvanceDecay(30.0);
		const double CurrentTotal = Chum->QueryChumSpot(FVector::ZeroVector).Pool.Total();
		TestTrue(TEXT("连续衰减后 Total 单调下降"), CurrentTotal < PreviousTotal);
		PreviousTotal = CurrentTotal;
	}

	// 起始 100、每拍乘 0.9，第 43 拍是 1.079 仍在地板之上，第 44 拍降到 0.971 越过地板。
	// 这里一次推进 31 个周期的时长，同时也验证累加器能把跨多个周期的时间补齐，而不是一次只衰减一拍。
	Chum->AdvanceDecay(30.0 * 31.0);
	const FCatChumSpotSnapshot BeforeFloorSnapshot = Chum->QueryChumSpot(FVector::ZeroVector);
	TestTrue(TEXT("跨地板前窝点仍然存在"), BeforeFloorSnapshot.bHasSpot);
	TestTrue(TEXT("跨地板前 Total 仍不低于地板"), BeforeFloorSnapshot.Pool.Total() >= 1.0);

	Chum->AdvanceDecay(30.0);
	const FCatChumSpotSnapshot AfterFloorSnapshot = Chum->QueryChumSpot(FVector::ZeroVector);
	TestFalse(TEXT("Total 低于 1 后窝点被移除"), AfterFloorSnapshot.bHasSpot);
	TestEqual(TEXT("窝点消散后腥轴归零"), AfterFloorSnapshot.Pool.Fishy, 0.0);
	TestEqual(TEXT("窝点消散后香轴归零"), AfterFloorSnapshot.Pool.Fragrant, 0.0);
	TestEqual(TEXT("窝点消散后酵轴归零"), AfterFloorSnapshot.Pool.Fermented, 0.0);

	FTestWorldWrapper CadenceWorldWrapper;
	TestTrue(TEXT("创建补窝节拍测试 World"), CadenceWorldWrapper.CreateTestWorld(EWorldType::Game));
	CadenceWorldWrapper.ForwardErrorMessages(this);
	UWorld* CadenceWorld = CadenceWorldWrapper.GetTestWorld();
	UCatChumSpotSubsystem* CadenceChum = CadenceWorld ? CadenceWorld->GetSubsystem<UCatChumSpotSubsystem>() : nullptr;
	TestNotNull(TEXT("可取得补窝节拍窝点子系统"), CadenceChum);
	if (!CadenceChum)
	{
		return false;
	}

	CadenceChum->ContributeChum(CatChumSpotTest::MakeChumCommand(
		TEXT("PlayerA"), FGuid::NewGuid(), CadenceChum->GetAggregationRevision(), FVector::ZeroVector, 100.0));
	CadenceChum->AdvanceDecay(20.0);
	TestEqual(TEXT("不足一个周期不衰减"), CadenceChum->QueryChumSpot(FVector::ZeroVector).Pool.Total(), 100.0, 1e-9);
	CadenceChum->ContributeChum(CatChumSpotTest::MakeChumCommand(
		TEXT("PlayerA"), FGuid::NewGuid(), CadenceChum->GetAggregationRevision(), FVector::ZeroVector, 50.0));
	TestEqual(TEXT("补窝直接累加进同一窝点"), CadenceChum->QueryChumSpot(FVector::ZeroVector).Pool.Total(), 150.0, 1e-9);
	// 补窝若重置了衰减计时，再走 10 秒只累计到 10 秒，池会停在 150；这里必须看到 150 乘 0.9。
	CadenceChum->AdvanceDecay(10.0);
	TestEqual(TEXT("补窝不重置衰减计时器"), CadenceChum->QueryChumSpot(FVector::ZeroVector).Pool.Total(), 135.0, 1e-9);
	return !HasAnyErrors();
}

// 测试流程：把从 WaterRegion 迁过来的整套事务语义在新宿主上重跑一遍——首次提交推进 Revision，
// 同 RequestId 换载荷判 InvalidPayload，同 RequestId 同载荷重放判 AlreadyResolved，陈旧 Revision 判 RevisionConflict，
// 越过数值安全夹判 CapacityExceeded，局末清空后旧请求既不命中缓存也不复活旧池。
bool FCatChumSpotTransactionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建窝点事务测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	UCatChumSpotSubsystem* Chum = CatChumSpotTest::PrepareSubsystem(*this, World);
	if (!Chum)
	{
		return false;
	}

	const int64 InitialRevision = Chum->GetAggregationRevision();
	const FGuid RequestId = FGuid::NewGuid();
	const FCatAggregationResult FirstResult = Chum->ContributeChum(CatChumSpotTest::MakeChumCommand(
		TEXT("PlayerA"), RequestId, InitialRevision, FVector::ZeroVector, 10.0));
	TestTrue(TEXT("首次投窝提交成功"), FirstResult.Command.bCommitted);
	TestEqual(TEXT("首次投窝返回 None"), FirstResult.Command.Error, ECatDomainCommandError::None);
	TestEqual(TEXT("首次投窝推进集合 Revision"), FirstResult.Command.Revision, InitialRevision + 1);
	TestEqual(TEXT("首次投窝写进窝点池"), FirstResult.Spot.Pool.Fishy, 10.0);

	const FCatAggregationResult DriftResult = Chum->ContributeChum(CatChumSpotTest::MakeChumCommand(
		TEXT("PlayerA"), RequestId, InitialRevision, FVector::ZeroVector, 5.0));
	TestFalse(TEXT("同 RequestId 更换贡献不提交"), DriftResult.Command.bCommitted);
	TestEqual(TEXT("载荷漂移返回 InvalidPayload"), DriftResult.Command.Error, ECatDomainCommandError::InvalidPayload);
	TestEqual(TEXT("载荷漂移不改变池"), DriftResult.Spot.Pool.Fishy, 10.0);

	const FCatAggregationResult ReplayResult = Chum->ContributeChum(CatChumSpotTest::MakeChumCommand(
		TEXT("PlayerA"), RequestId, InitialRevision, FVector::ZeroVector, 10.0));
	TestFalse(TEXT("重放不再次提交"), ReplayResult.Command.bCommitted);
	TestEqual(TEXT("重放返回 AlreadyResolved"), ReplayResult.Command.Error, ECatDomainCommandError::AlreadyResolved);
	TestEqual(TEXT("重放不二次加池"), ReplayResult.Spot.Pool.Fishy, 10.0);

	const FCatAggregationResult StaleResult = Chum->ContributeChum(CatChumSpotTest::MakeChumCommand(
		TEXT("PlayerA"), FGuid::NewGuid(), InitialRevision, FVector::ZeroVector, 10.0));
	TestFalse(TEXT("陈旧 Revision 不提交"), StaleResult.Command.bCommitted);
	TestEqual(TEXT("陈旧 Revision 返回 RevisionConflict"), StaleResult.Command.Error,
		ECatDomainCommandError::RevisionConflict);
	TestEqual(TEXT("陈旧 Revision 不改变池"), StaleResult.Spot.Pool.Fishy, 10.0);

	// 安全夹只是数值异常保险丝，不是玩法上限：这里用 2e9 这种正常游玩绝不可能出现的量级去触发它。
	const FCatAggregationResult SafetyCapResult = Chum->ContributeChum(CatChumSpotTest::MakeChumCommand(
		TEXT("PlayerA"), FGuid::NewGuid(), Chum->GetAggregationRevision(), FVector::ZeroVector, 2.0e9));
	TestFalse(TEXT("越过数值安全夹不提交"), SafetyCapResult.Command.bCommitted);
	TestEqual(TEXT("越过数值安全夹返回 CapacityExceeded"), SafetyCapResult.Command.Error,
		ECatDomainCommandError::CapacityExceeded);
	TestEqual(TEXT("越过数值安全夹不改变池"), SafetyCapResult.Spot.Pool.Fishy, 10.0);

	Chum->ResetRuntimeChumFromAuthority();
	TestFalse(TEXT("局末清空后不再有窝点"), Chum->QueryChumSpot(FVector::ZeroVector).bHasSpot);
	TestEqual(TEXT("局末清空推进集合 Revision"), Chum->GetAggregationRevision(), InitialRevision + 2);

	const FCatAggregationResult ReplayAfterReset = Chum->ContributeChum(CatChumSpotTest::MakeChumCommand(
		TEXT("PlayerA"), RequestId, InitialRevision, FVector::ZeroVector, 10.0));
	TestFalse(TEXT("清空后旧 Request 不再命中终态缓存"), ReplayAfterReset.Command.bCommitted);
	TestEqual(TEXT("清空后旧 Request 因 Revision 冲突拒绝"), ReplayAfterReset.Command.Error,
		ECatDomainCommandError::RevisionConflict);
	TestFalse(TEXT("清空后旧 Request 不复活旧窝点"), ReplayAfterReset.Spot.bHasSpot);
	return !HasAnyErrors();
}

// 测试流程：验证"聚鱼时刻"只有一本账——玩家投窝和自然事件投窝都会重置同一个共享冷却，冷却按显式推进的秒数走完后才解除。
// 这里全程用手动推进而不是等真实时间，所以断言与机器速度无关；同时确认冷却秒数没配时不会把自然涌现永久压住。
bool FCatChumSpotAggregationCooldownTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建聚鱼时刻冷却测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	UCatChumSpotSubsystem* Chum = CatChumSpotTest::PrepareSubsystem(*this, World);
	if (!Chum)
	{
		return false;
	}

	constexpr double CooldownSeconds = 300.0;
	TestFalse(TEXT("本局一次都没投过窝时不在冷却里"), Chum->IsAggregationMomentOnCooldown(CooldownSeconds));

	FCatAggregationCommand PlayerCommand = CatChumSpotTest::MakeChumCommand(
		TEXT("PlayerA"), FGuid::NewGuid(), Chum->GetAggregationRevision(), FVector(3000.0, 0.0, 0.0), 10.0);
	TestTrue(TEXT("玩家投窝提交成功"), Chum->ContributeChum(PlayerCommand).Command.bCommitted);
	TestTrue(TEXT("玩家投窝后聚鱼时刻进入冷却"), Chum->IsAggregationMomentOnCooldown(CooldownSeconds));

	Chum->AdvanceDecay(299.0);
	TestTrue(TEXT("冷却没走完时仍在冷却里"), Chum->IsAggregationMomentOnCooldown(CooldownSeconds));
	Chum->AdvanceDecay(2.0);
	TestFalse(TEXT("冷却走完后解除"), Chum->IsAggregationMomentOnCooldown(CooldownSeconds));

	// 自然事件用的是同一个写口，所以它同样会把这本账重新计时——两个触发源不会各记各的。
	FCatAggregationCommand NaturalCommand = CatChumSpotTest::MakeChumCommand(
		TEXT("Environment"), FGuid::NewGuid(), Chum->GetAggregationRevision(), FVector(-3000.0, 0.0, 0.0), 15.0, 15.0, 15.0);
	NaturalCommand.Source = ECatAggregationSource::NaturalEvent;
	TestTrue(TEXT("自然事件投窝提交成功"), Chum->ContributeChum(NaturalCommand).Command.bCommitted);
	TestTrue(TEXT("自然事件投窝后同一个冷却重新开始"), Chum->IsAggregationMomentOnCooldown(CooldownSeconds));

	TestFalse(TEXT("没配冷却秒数时不设冷却"), Chum->IsAggregationMomentOnCooldown(0.0));

	Chum->ResetRuntimeChumFromAuthority();
	TestFalse(TEXT("局末清空后冷却也一并清掉"), Chum->IsAggregationMomentOnCooldown(CooldownSeconds));
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
