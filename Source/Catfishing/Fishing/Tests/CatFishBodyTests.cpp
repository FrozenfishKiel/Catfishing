#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Fishing/Simulation/CatFishBodyModel.h"
#include "Fishing/Simulation/CatFishingFightSimulator.h"

namespace CatFishBodyTests
{
	FCatFightSimulationConfig MakeConfig()
	{
		FCatFightSimulationConfig Config;
		Config.FixedStepSeconds = 0.05;
		Config.PrimaryOperatorCatStrength = 40.0;
		Config.PrimaryOperatorMassKilograms = 10.0;
		Config.FishMassKilograms = 5.0;
		Config.FishStrength = 80.0;
		Config.CatStaminaMaximum = 1000.0;
		Config.ReelSpeedCentimetersPerSecond = 80.0;
		Config.FishFullEffortSpeedCentimetersPerSecond = 180.0;
		Config.MaximumLineLengthCentimeters = 2000.0;
		Config.RodDurability = 10000.0;
		Config.FishBody.Geometry.MouthLocalPositionCentimeters = FVector(35.0, 0.0, 0.0);
		Config.FishBody.Geometry.CenterOfMassLocalPositionCentimeters = FVector(-5.0, 0.0, 0.0);
		Config.FishBody.Geometry.YawRadiusOfGyrationCentimeters = 18.0;
		return Config;
	}

	FCatFightSimulationState MakeState()
	{
		FCatFightSimulationState State;
		State.CatStamina = 1000.0;
		State.FishStamina = 1000.0;
		State.FishWorldPosition = FVector(500.0, 0.0, 0.0);
		State.FishBody.Heading = FVector::ForwardVector;
		State.LineLengthCentimeters = 535.0;
		State.MotionIntent = ECatFishMotionIntent::StrugglingOutward;
		return State;
	}

	void CommitStep(FCatFightSimulationState& State, const FCatFightStepResult& Result)
	{
		State.FishWorldPosition = Result.ProposedFishWorldPosition;
		State.FishBody = Result.FishBodyTurn.State;
		State.FishVelocityCentimetersPerSecond = Result.ResolvedFishVelocityCentimetersPerSecond;
		State.LineLengthCentimeters = Result.LineLengthCentimeters;
		State.CatStamina -= Result.CatStaminaDrain;
		State.FishStamina -= Result.FishStaminaDrain;
		State.AbsoluteRodWear = Result.AbsoluteRodWear;
		State.StrongConfrontationBuildUpSeconds = Result.StrongConfrontationBuildUpSeconds;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishBodyGeometryTest,
	"Catfishing.Unit.Fishing.FishBody.CalibratedMouthAndCenterRespectYawAndScaleOrigin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishBodyGeometryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCatFishBodyGeometry Geometry;
	Geometry.MouthLocalPositionCentimeters = FVector(30.0, 8.0, 4.0);
	Geometry.CenterOfMassLocalPositionCentimeters = FVector(-10.0, 8.0, -2.0);
	Geometry.ScaleOriginLocalCentimeters = FVector(10.0, -4.0, 2.0);
	Geometry.YawRadiusOfGyrationCentimeters = 20.0;
	TestTrue(TEXT("标定几何具有有效嘴部力臂"), Geometry.IsValid() && Geometry.HasMouthLever());
	const FVector Root(100.0, 200.0, 300.0);
	TestTrue(TEXT("九十度鱼身转向同时旋转嘴部偏移"),
		FCatFishBodyModel::MouthPosition(Geometry, Root, FVector::RightVector).Equals(FVector(92.0, 230.0, 304.0), 1e-8));
	TestTrue(TEXT("质心使用自己的标定偏移，不能复用根点或嘴"),
		FCatFishBodyModel::CenterPosition(Geometry, Root, FVector::RightVector).Equals(FVector(92.0, 190.0, 298.0), 1e-8));
	const auto Scaled = Geometry.Scaled(2.0);
	TestTrue(TEXT("重量视觉缩放围绕标定网格原点缩放嘴"),
		Scaled.MouthLocalPositionCentimeters.Equals(FVector(50.0, 20.0, 6.0), 1e-8));
	TestTrue(TEXT("质心使用同一缩放原点"),
		Scaled.CenterOfMassLocalPositionCentimeters.Equals(FVector(-30.0, 20.0, -6.0), 1e-8));
	TestEqual(TEXT("回转半径按长度缩放"), Scaled.YawRadiusOfGyrationCentimeters, 40.0, 1e-8);
	TestFalse(TEXT("质点极限不冒充已标定的鱼嘴力臂"), FCatFishBodyGeometry().HasMouthLever());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishBodyTorqueTest,
	"Catfishing.Unit.Fishing.FishBody.SidePullTurnsTheHeadAgainstFiniteSwimTorque",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishBodyTorqueTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const auto Config = CatFishBodyTests::MakeConfig();
	const auto State = CatFishBodyTests::MakeState().FishBody;
	const auto Straight = FCatFishBodyModel::PredictTurn(Config.FishBody, State, FVector::ForwardVector,
		1.0, Config.FishMassKilograms, 80.0, FVector(-80.0, 0.0, 0.0), Config.FixedStepSeconds);
	TestEqual(TEXT("穿过嘴和质心的正拉没有偏航力矩"), Straight.LineTorqueNewtonMeters, 0.0, 1e-8);
	TestTrue(TEXT("正拉不伪造转身"), Straight.State.Heading.Equals(State.Heading, 1e-8));
	const auto Left = FCatFishBodyModel::PredictTurn(Config.FishBody, State, -FVector::RightVector,
		1.0, Config.FishMassKilograms, 80.0, FVector(0.0, 80.0, 0.0), Config.FixedStepSeconds);
	const auto Right = FCatFishBodyModel::PredictTurn(Config.FishBody, State, FVector::RightVector,
		1.0, Config.FishMassKilograms, 80.0, FVector(0.0, -80.0, 0.0), Config.FixedStepSeconds);
	TestEqual(TEXT("四十厘米力臂只转换一次到米"), Left.LineTorqueNewtonMeters, 32.0, 1e-8);
	TestTrue(TEXT("AI 确实试图向反方向抵抗"), Left.SwimTorqueNewtonMeters < 0.0);
	TestTrue(TEXT("AI 反向转矩有限，足够侧压仍能带偏鱼头"),
		FMath::Abs(Left.SwimTorqueNewtonMeters) < Left.LineTorqueNewtonMeters
		&& Left.State.Heading.Y > 0.0 && Left.State.AngularVelocityRadiansPerSecond > 0.0);
	TestTrue(TEXT("转向有惯量，不瞬间指向拉力"), Left.EffectiveInertiaKilogramMetersSquared > 0.0
		&& Left.State.Heading.X > 0.95);
	TestEqual(TEXT("左右侧压力矩镜像对称"), Left.State.AngularVelocityRadiansPerSecond,
		-Right.State.AngularVelocityRadiansPerSecond, 1e-8);
	TestTrue(TEXT("候选预测不改变输入的权威鱼身状态"), State.Heading.Equals(FVector::ForwardVector)
		&& State.AngularVelocityRadiansPerSecond == 0.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishBodyContinuityTest,
	"Catfishing.Unit.Fishing.FishBody.DirectionChangesAndLineReleasePreserveAngularMomentum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishBodyContinuityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const auto Config = CatFishBodyTests::MakeConfig();
	auto Body = CatFishBodyTests::MakeState().FishBody;
	Body.AngularVelocityRadiansPerSecond = 1.0;
	const auto Released = FCatFishBodyModel::PredictTurn(Config.FishBody, Body, Body.Heading,
		0.0, Config.FishMassKilograms, 80.0, FVector::ZeroVector, Config.FixedStepSeconds);
	TestTrue(TEXT("放线和撤掉主动出力后仍有同向转动，阻尼逐步减速"),
		Released.State.AngularVelocityRadiansPerSecond > 0.0
		&& Released.State.AngularVelocityRadiansPerSecond < Body.AngularVelocityRadiansPerSecond);
	const auto Retargeted = FCatFishBodyModel::PredictTurn(Config.FishBody, Body, -FVector::RightVector,
		0.3, Config.FishMassKilograms, 80.0, FVector::ZeroVector, Config.FixedStepSeconds);
	TestTrue(TEXT("状态切换成反向低出力目标不会重置或立即反转既有角速度"),
		Retargeted.State.AngularVelocityRadiansPerSecond > 0.0
		&& Retargeted.State.AngularVelocityRadiansPerSecond < Released.State.AngularVelocityRadiansPerSecond);
	TestTrue(TEXT("反向目标先制动，鱼身仍按连续角速度略向原侧转动"), Retargeted.State.Heading.Y > 0.0);
	for (int32 Step = 0; Step < 40; ++Step)
	{
		const auto Next = FCatFishBodyModel::PredictTurn(Config.FishBody, Body, Body.Heading,
			0.0, Config.FishMassKilograms, 80.0, FVector::ZeroVector, Config.FixedStepSeconds);
		TestTrue(TEXT("无力矩自由衰减不制造反向角速度"), Next.State.AngularVelocityRadiansPerSecond >= 0.0
			&& Next.State.AngularVelocityRadiansPerSecond <= Body.AngularVelocityRadiansPerSecond);
		Body = Next.State;
	}
	TestTrue(TEXT("持续放线后角速度自然衰减"), Body.AngularVelocityRadiansPerSecond < 0.01);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishBodyEndpointTest,
	"Catfishing.Unit.Fishing.FishBody.MouthConstrainsStaticPhysicalAndCMCEndpointCandidates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishBodyEndpointTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	auto Config = CatFishBodyTests::MakeConfig();
	Config.FishBody.Geometry.MouthLocalPositionCentimeters = FVector(30.0, 8.0, 4.0);
	Config.FishBody.Geometry.CenterOfMassLocalPositionCentimeters = FVector(-10.0, 8.0, -2.0);
	for (int32 EndpointKind = 0; EndpointKind < 3; ++EndpointKind)
	{
		auto State = CatFishBodyTests::MakeState();
		State.FishWorldPosition = FVector(400.0, 150.0, -10.0);
		State.FishBody.Heading = FVector(0.6, 0.8, 0.0);
		State.FishVelocityCentimetersPerSecond = State.FishBody.Heading * 180.0;
		FCatFightRodConstraintInput Rod;
		Rod.RodTipWorldPosition = FVector(0.0, 0.0, 110.0);
		const FVector Mouth0 = FCatFishBodyModel::MouthPosition(Config.FishBody.Geometry, State.FishWorldPosition, State.FishBody.Heading);
		State.LineLengthCentimeters = FVector::Distance(Mouth0, Rod.RodTipWorldPosition);
		Rod.RodForwardWorld = (Mouth0 - Rod.RodTipWorldPosition).GetSafeNormal();
		Rod.bRodHeld = true;
		Rod.bPhysicalRodEndpoint = EndpointKind != 0;
		Rod.PhysicsStepSeconds = Config.FixedStepSeconds;
		Rod.RodPointInverseMassX = FVector(0.025, 0.0, 0.0);
		Rod.RodPointInverseMassY = FVector(0.0, 0.025, 0.0);
		Rod.RodPointInverseMassZ = FVector(0.0, 0.0, 0.025);
		int32 CandidateCalls = 0;
		const FVector RodTip0 = Rod.RodTipWorldPosition;
		if (EndpointKind == 2)
		{
			Rod.PredictCMCEndpoint = [RodTip0, &CandidateCalls](const FCatFightCMCPredictionQuery& Query)
			{
				++CandidateCalls;
				FCatFightCMCPredictionResult Result;
				Result.RodTipWorldPosition = RodTip0 + Query.ForceNewtons * (100.0 * Query.Seconds * Query.Seconds / 40.0);
				Result.bSucceeded = true;
				return Result;
			};
		}
		const auto Result = FCatFishingFightSimulator::Step(Config, State, Rod, State.FishBody.Heading);
		if (!TestTrue(FString::Printf(TEXT("端点路径 %d 接受非零偏移和鱼身偏航"), EndpointKind), Result.bSucceeded)) return false;
		const FVector ExpectedMouth = FCatFishBodyModel::MouthPosition(Config.FishBody.Geometry,
			Result.ProposedFishWorldPosition, Result.FishBodyTurn.State.Heading);
		TestTrue(TEXT("最终鱼根和朝向唯一决定鱼嘴与鱼钩落点"), Result.ProposedMouthWorldPosition.Equals(ExpectedMouth, 1e-7));
		const FVector PredictedRodTip = EndpointKind == 0 ? RodTip0
			: RodTip0 + Result.RodLineForceNewtons * (100.0 * Config.FixedStepSeconds * Config.FixedStepSeconds / 40.0);
		TestTrue(TEXT("非零张力下嘴部距离满足同一个候选端点线长约束"), Result.LineTensionNewtons > 0.0
			&& FMath::Abs(FVector::Distance(Result.ProposedMouthWorldPosition, PredictedRodTip) - Result.LineLengthCentimeters) < 0.002);
		TestTrue(TEXT("该场景能区分嘴和根，不能退化为根点约束"),
			FMath::Abs(FVector::Distance(Result.ProposedFishWorldPosition, PredictedRodTip) - Result.LineLengthCentimeters) > 5.0);
		TestEqual(TEXT("观察到的起始距离也以嘴为端点"), Result.Trace.DistanceBeforeCentimeters,
			State.LineLengthCentimeters, 1e-8);
		const auto Repeated = FCatFishingFightSimulator::Step(Config, State, Rod, State.FishBody.Heading);
		TestTrue(TEXT("反复张力候选不累计旋转或修改原始位姿"), Repeated.bSucceeded
			&& Repeated.ProposedFishWorldPosition.Equals(Result.ProposedFishWorldPosition, 1e-8)
			&& Repeated.FishBodyTurn.State.Heading.Equals(Result.FishBodyTurn.State.Heading, 1e-8)
			&& State.FishBody.Heading.Equals(FVector(0.6, 0.8, 0.0), 1e-8));
		TestTrue(TEXT("CMC 夹具确实进入多次只读候选，其他路径不伪称 CMC"),
			EndpointKind == 2 ? CandidateCalls > 1 && Result.Trace.bCMCEndpointPredicted : !Result.Trace.bCMCEndpointPredicted);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishBodyThrustTest,
	"Catfishing.Unit.Fishing.FishBody.ThrustFollowsAchievedHeadingRatherThanDesiredHeading",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishBodyThrustTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const auto Config = CatFishBodyTests::MakeConfig();
	auto State = CatFishBodyTests::MakeState();
	State.LineLengthCentimeters = 1500.0;
	const FVector Center0 = FCatFishBodyModel::CenterPosition(Config.FishBody.Geometry, State.FishWorldPosition, State.FishBody.Heading);
	const auto Result = FCatFishingFightSimulator::Step(Config, State, FVector::ZeroVector, FVector::RightVector);
	if (!TestTrue(TEXT("余线中可朝新 AI 目标主动转向"), Result.bSucceeded)) return false;
	const FVector Center1 = FCatFishBodyModel::CenterPosition(Config.FishBody.Geometry, Result.ProposedFishWorldPosition, Result.FishBodyTurn.State.Heading);
	TestTrue(TEXT("本步推进使用步首实际鱼身朝向"), Result.FishThrustDirection.Equals(FVector::ForwardVector, 1e-8)
		&& Result.ResolvedFishVelocityCentimetersPerSecond.X > 0.0
		&& FMath::Abs(Result.ResolvedFishVelocityCentimetersPerSecond.Y) < 1e-8);
	TestTrue(TEXT("AI 意图仍保留目标方向，控头后的未兑现进展不能随推力一起改写"),
		Result.FishEffortDirection.Equals(FVector::RightVector, 1e-8));
	TestTrue(TEXT("身体只在有限转矩下开始转向，未瞬间侧漂到目标方向"),
		Result.FishBodyTurn.State.Heading.Y > 0.0 && Result.FishBodyTurn.State.Heading.X > 0.99
		&& FMath::Abs(Center1.Y - Center0.Y) < 1e-8);
	CatFishBodyTests::CommitStep(State, Result);
	const auto Next = FCatFishingFightSimulator::Step(Config, State, FVector::ZeroVector, FVector::RightVector);
	TestTrue(TEXT("下一步推进读回真正已转过的鱼身朝向"), Next.bSucceeded
		&& Next.FishThrustDirection.Equals(Result.FishBodyTurn.State.Heading, 1e-8)
		&& Next.ResolvedFishVelocityCentimetersPerSecond.Y > 0.0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishBodyCostTest,
	"Catfishing.Unit.Fishing.FishBody.RootMotionFromTurningCannotFalsifyCenterProgressOrStamina",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishBodyCostTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	auto Config = CatFishBodyTests::MakeConfig();
	Config.FishBody.Geometry.CenterOfMassLocalPositionCentimeters = FVector(-20.0, 0.0, 0.0);
	auto State = CatFishBodyTests::MakeState();
	State.LineLengthCentimeters = 1500.0;
	FCatFightRodConstraintInput Rod;
	auto Turned = FCatFishingFightSimulator::Step(Config, State, Rod, State.FishBody.Heading);
	if (!TestTrue(TEXT("建立可重算的权威费用结果"), Turned.bSucceeded)) return false;
	const FVector Center0 = FCatFishBodyModel::CenterPosition(Config.FishBody.Geometry, State.FishWorldPosition, State.FishBody.Heading);
	// 地形/姿态提交后重算费用的契约：质心没前进，只是鱼根绕质心换位置。
	Turned.FishBodyTurn.State.Heading = FVector::RightVector;
	Turned.ProposedFishWorldPosition = Center0 - FCatFishBodyModel::RotateLocal(
		Config.FishBody.Geometry.CenterOfMassLocalPositionCentimeters, Turned.FishBodyTurn.State.Heading);
	Turned.ProposedMouthWorldPosition = FCatFishBodyModel::MouthPosition(Config.FishBody.Geometry,
		Turned.ProposedFishWorldPosition, Turned.FishBodyTurn.State.Heading);
	Turned.ResolvedFishVelocityCentimetersPerSecond = FVector::ZeroVector;
	if (!TestTrue(TEXT("转身后的最终位置仍可幂等结算"), FCatFishingFightSimulator::FinalizeResolvedStep(Config, State, Rod, Turned))) return false;
	TestTrue(TEXT("夹具确有足以误计费的鱼根反向位移"), Turned.ProposedFishWorldPosition.X < State.FishWorldPosition.X - 10.0);
	TestEqual(TEXT("单纯转身不伪造质心正负进展"), Turned.FishActualIntentProgressCentimeters, 0.0, 1e-8);
	TestEqual(TEXT("僵持只欠本步主动九厘米，不叠加根点转动距离"), Turned.FishUnfulfilledDistanceCentimeters, 9.0, 1e-8);
	TestEqual(TEXT("费用仍由同一未兑现位移模型结算"), Turned.FishStaminaDrain,
		0.09 * Config.FishStaminaPerUnfulfilledMeter, 1e-8);
	auto Translated = Turned;
	Translated.ProposedFishWorldPosition += FVector(4.0, 0.0, 0.0);
	Translated.ProposedMouthWorldPosition += FVector(4.0, 0.0, 0.0);
	if (!TestTrue(TEXT("转身同时前进可以重算"), FCatFishingFightSimulator::FinalizeResolvedStep(Config, State, Rod, Translated))) return false;
	TestEqual(TEXT("转身同时发生的真实质心前进完整记账"), Translated.FishActualIntentProgressCentimeters, 4.0, 1e-8);
	TestEqual(TEXT("前进四厘米后只欠五厘米"), Translated.FishUnfulfilledDistanceCentimeters, 5.0, 1e-8);
	const double Once = Translated.FishStaminaDrain;
	TestTrue(TEXT("同一最终位姿重复结算不会双扣"), FCatFishingFightSimulator::FinalizeResolvedStep(Config, State, Rod, Translated));
	TestEqual(TEXT("重算保持同一费用"), Translated.FishStaminaDrain, Once, 1e-8);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishBodySideControlTest,
	"Catfishing.Unit.Fishing.FishBody.SustainedSidePressureOpensReelingAgainstStrongerFish",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishBodySideControlTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const auto Config = CatFishBodyTests::MakeConfig();
	double TotalReeled[3] = {0.0, 0.0, 0.0};
	FVector FinalPosition[3];
	for (int32 Variant = 0; Variant < 3; ++Variant)
	{
		auto State = CatFishBodyTests::MakeState();
		State.FishVelocityCentimetersPerSecond = FVector::ForwardVector * Config.FishFullEffortSpeedCentimetersPerSecond;
		State.CatAction = ECatFightCatAction::Pull;
		const double SideAngle = Variant == 0 ? 0.0 : Variant == 1 ? -45.0 : 45.0;
		const FVector InitialOutward = FRotator(0.0, SideAngle, 0.0).RotateVector(FVector::ForwardVector);
		FCatFightRodConstraintInput Rod;
		Rod.RodTipWorldPosition = FCatFishBodyModel::MouthPosition(Config.FishBody.Geometry,
			State.FishWorldPosition, State.FishBody.Heading) - InitialOutward * State.LineLengthCentimeters;
		Rod.RodForwardWorld = InitialOutward;
		Rod.bRodHeld = true;
		for (int32 Step = 0; Step < 80; ++Step)
		{
			Rod.RodForwardWorld = (FCatFishBodyModel::MouthPosition(Config.FishBody.Geometry,
				State.FishWorldPosition, State.FishBody.Heading) - Rod.RodTipWorldPosition).GetSafeNormal();
			const auto Result = FCatFishingFightSimulator::Step(Config, State, Rod, FVector::ForwardVector);
			if (!TestTrue(TEXT("持续控头的每步约束与费用均有效"), Result.bSucceeded)) return false;
			TestTrue(TEXT("连续控头保持鱼嘴在线长内"),
				FVector::Distance(Result.ProposedMouthWorldPosition, Rod.RodTipWorldPosition) <= Result.LineLengthCentimeters + 0.002);
			TestTrue(TEXT("控头改变鱼身朝向时，AI 仍保持原有外游意图"),
				Result.FishEffortDirection.Equals(FVector::ForwardVector, 1e-8));
			if (Step == 0) TestEqual(TEXT("更强的鱼在初始对抗时没有收线余力"), Result.ActualReelDistanceCentimeters, 0.0, 1e-6);
			TotalReeled[Variant] += Result.ActualReelDistanceCentimeters;
			CatFishBodyTests::CommitStep(State, Result);
		}
		FinalPosition[Variant] = State.FishWorldPosition;
		AddInfo(FString::Printf(TEXT("Event=fish_body_control_contract Variant=%d ReeledCm=%.6f FishPosition=%s Heading=%s"),
			Variant, TotalReeled[Variant], *State.FishWorldPosition.ToCompactString(), *State.FishBody.Heading.ToCompactString()));
	}
	TestEqual(TEXT("正面对抗不会凭空获得更大收线力量"), TotalReeled[0], 0.0, 1e-5);
	TestTrue(TEXT("持续侧压改变实际鱼身推力方向，产生可累计的收线窗口"), TotalReeled[1] > 5.0 && TotalReeled[2] > 5.0);
	TestEqual(TEXT("左右控头应有相同收益"), TotalReeled[1], TotalReeled[2], 1e-5);
	TestEqual(TEXT("左右鱼身轨迹沿湖面镜像"), FinalPosition[1].Y, -FinalPosition[2].Y, 1e-5);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishBodyNearTipBoundaryTest,
	"Catfishing.Unit.Fishing.FishBody.NearTipMouthConstraintWithFiniteLiveEffortAndExhaustion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishBodyNearTipBoundaryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	for (int32 GeometryKind = 0; GeometryKind < 2; ++GeometryKind)
	for (const double Dt : {0.05, 1.0 / 60.0})
	for (const double Height : {0.0, 1.0, 100.0})
	for (const double HorizontalDistance : {0.0, 1.0})
	for (int32 EndpointKind = 0; EndpointKind < 3; ++EndpointKind)
	for (const bool bExhausted : {true, false})
	for (const double AngularVelocity : {0.0, bExhausted ? FMath::DegreesToRadians(240.0) : 1.0})
	{
		auto Config = CatFishBodyTests::MakeConfig();
		Config.FixedStepSeconds = Dt;
		if (GeometryKind == 0)
		{
			// RiverPattern 参考姿态标定尺度的固定 contract 夹具，不依赖编辑器加载正式资产。
			Config.FishBody.Geometry.MouthLocalPositionCentimeters = FVector(10.330617266958864, 0.19778288735756266, -4.641465348386552);
			Config.FishBody.Geometry.CenterOfMassLocalPositionCentimeters = FVector(0.0, 0.0, -5.0);
			Config.FishBody.Geometry.ScaleOriginLocalCentimeters = FVector(0.0, 0.0, -5.0);
			Config.FishBody.Geometry.YawRadiusOfGyrationCentimeters = 6.6415506753894755;
		}
		else
		{
			Config.FishBody.Geometry.MouthLocalPositionCentimeters = FVector(35.0, 0.0, -5.0);
			Config.FishBody.Geometry.CenterOfMassLocalPositionCentimeters = FVector(-5.0, 0.0, -5.0);
			Config.FishBody.Geometry.ScaleOriginLocalCentimeters = FVector(0.0, 0.0, -5.0);
		}
		auto State = CatFishBodyTests::MakeState();
		State.bFishExhausted = bExhausted;
		State.FishStamina = bExhausted ? 0.0 : 1000.0;
		State.FishEffortRatio = bExhausted ? 0.0 : 0.35;
		State.MotionIntent = bExhausted ? ECatFishMotionIntent::AutoHauling : ECatFishMotionIntent::CalmOrInward;
		State.CatAction = ECatFightCatAction::Pull;
		State.FishBody.AngularVelocityRadiansPerSecond = AngularVelocity;
		State.FishWorldPosition = FVector(HorizontalDistance, 0.0, 0.0)
			- Config.FishBody.Geometry.MouthLocalPositionCentimeters;
		State.LineLengthCentimeters = FMath::Sqrt(Height * Height + HorizontalDistance * HorizontalDistance);
		const FVector InitialRoot = State.FishWorldPosition;
		FCatFightRodConstraintInput Rod;
		Rod.RodTipWorldPosition = FVector(0.0, 0.0, Height);
		Rod.bPhysicalRodEndpoint = EndpointKind != 0;
		Rod.PhysicsStepSeconds = Dt;
		Rod.RodPointInverseMassX = FVector(0.025, 0.0, 0.0);
		Rod.RodPointInverseMassY = FVector(0.0, 0.025, 0.0);
		Rod.RodPointInverseMassZ = FVector(0.0, 0.0, 0.025);
		const FVector RodTip0 = Rod.RodTipWorldPosition;
		if (EndpointKind == 2)
		{
			Rod.PredictCMCEndpoint = [RodTip0](const FCatFightCMCPredictionQuery& Query)
			{
				FCatFightCMCPredictionResult Predicted;
				Predicted.bSucceeded = true;
				Predicted.RodTipWorldPosition = RodTip0
					+ Query.ForceNewtons * (100.0 * Query.Seconds * Query.Seconds / 40.0);
				return Predicted;
			};
		}
		const FString Context = FString::Printf(TEXT("Geometry=%d Dt=%.9f HeightCm=%.3f HorizontalCm=%.3f Endpoint=%d Exhausted=%d OmegaRadS=%.9f"),
			GeometryKind, Dt, Height, HorizontalDistance, EndpointKind, bExhausted, AngularVelocity);
		const FVector DesiredHeading = bExhausted ? FVector::ZeroVector : FVector::ForwardVector;
		const auto Result = FCatFishingFightSimulator::Step(Config, State, Rod, DesiredHeading);
		if (!TestTrue(TEXT("鱼收近边界接受有效输入 ") + Context, Result.bSucceeded)) continue;
		const FVector CandidateRodTip = EndpointKind == 0 ? RodTip0
			: RodTip0 + Result.RodLineForceNewtons * (100.0 * Dt * Dt / 40.0);
		const double MouthDistance = FVector::Distance(Result.ProposedMouthWorldPosition, CandidateRodTip);
		const double Excess = MouthDistance - Result.LineLengthCentimeters;
		TestTrue(TEXT("嘴部约束不能把正交方向无解候选误当作已满足 ") + Context,
			FMath::IsFinite(MouthDistance) && Excess <= 0.01);
		TestTrue(TEXT("收近边界不产生非有限位置、速度、角速度或张力 ") + Context,
			!Result.ProposedFishWorldPosition.ContainsNaN() && !Result.ProposedMouthWorldPosition.ContainsNaN()
			&& !Result.ResolvedFishVelocityCentimetersPerSecond.ContainsNaN()
			&& FMath::IsFinite(Result.LineTensionNewtons) && Result.LineTensionNewtons >= 0.0
			&& FMath::IsFinite(Result.FishBodyTurn.State.AngularVelocityRadiansPerSecond));
		TestTrue(TEXT("鱼身角速度继续服从身体角速率上限 ") + Context,
			FMath::Abs(Result.FishBodyTurn.State.AngularVelocityRadiansPerSecond)
				<= FMath::DegreesToRadians(Config.FishBody.MaximumBodyTurnRateDegreesPerSecond) + 1e-8);
		if (bExhausted)
		{
			TestEqual(TEXT("残余转动不恢复鱼主动转矩 ") + Context, Result.FishBodyTurn.SwimTorqueNewtonMeters, 0.0, 1e-8);
			TestEqual(TEXT("力竭收尾不凭残余角速度收取猫体力 ") + Context, Result.CatStaminaDrain, 0.0, 1e-8);
			TestEqual(TEXT("力竭收尾没有鱼体力费用 ") + Context, Result.FishStaminaDrain, 0.0, 1e-8);
			auto WithoutFreeSpin = State;
			WithoutFreeSpin.FishBody.AngularVelocityRadiansPerSecond = 0.0;
			const auto NoSpin = FCatFishingFightSimulator::Step(Config, WithoutFreeSpin, Rod, DesiredHeading);
			TestTrue(TEXT("力竭入口清除自由自旋，同时保留当步线力被动转头 ") + Context,
				NoSpin.bSucceeded && NoSpin.ProposedMouthWorldPosition.Equals(Result.ProposedMouthWorldPosition, 1e-8)
				&& NoSpin.FishBodyTurn.State.Heading.Equals(Result.FishBodyTurn.State.Heading, 1e-8)
				&& FMath::IsNearlyEqual(NoSpin.LineTensionNewtons, Result.LineTensionNewtons, 1e-8));
		}
		const auto Repeated = FCatFishingFightSimulator::Step(Config, State, Rod, DesiredHeading);
		TestTrue(TEXT("极短线候选仍可重放且不推进原始状态 ") + Context,
			Repeated.bSucceeded && Repeated.ProposedMouthWorldPosition.Equals(Result.ProposedMouthWorldPosition, 1e-8)
			&& FMath::IsNearlyEqual(Repeated.LineTensionNewtons, Result.LineTensionNewtons, 1e-8)
			&& FMath::IsNearlyEqual(Repeated.FishBodyTurn.State.AngularVelocityRadiansPerSecond,
				Result.FishBodyTurn.State.AngularVelocityRadiansPerSecond, 1e-8)
			&& State.FishWorldPosition.Equals(InitialRoot, 1e-8)
			&& State.FishBody.AngularVelocityRadiansPerSecond == AngularVelocity);
		if (Excess > 0.01 || Result.LineTensionNewtons > 1000.0 || AngularVelocity > 0.0 && HorizontalDistance == 0.0)
		{
			// 高张力仅记录事实，不以未经设计确认的牛顿上限替代线长与有限性契约。
			AddInfo(FString::Printf(TEXT("Event=fish_body_near_tip_contract %s InitialRoot=%s MouthLocalCm=%s CenterLocalCm=%s YawRadiusCm=%.9f LineBeforeCm=%.9f LineAfterCm=%.9f Mouth=%s CandidateRodTip=%s MouthDistanceCm=%.9f ExcessCm=%.9f TensionN=%.9f FinalOmegaRadS=%.9f"),
				*Context, *InitialRoot.ToCompactString(), *Config.FishBody.Geometry.MouthLocalPositionCentimeters.ToCompactString(),
				*Config.FishBody.Geometry.CenterOfMassLocalPositionCentimeters.ToCompactString(),
				Config.FishBody.Geometry.YawRadiusOfGyrationCentimeters, State.LineLengthCentimeters, Result.LineLengthCentimeters,
				*Result.ProposedMouthWorldPosition.ToCompactString(), *CandidateRodTip.ToCompactString(), MouthDistance, Excess,
				Result.LineTensionNewtons, Result.FishBodyTurn.State.AngularVelocityRadiansPerSecond));
		}
	}
	return !HasAnyErrors();
}

#endif
