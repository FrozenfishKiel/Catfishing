#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "TimerManager.h"
#include "Environment/CatChumFieldSubsystem.h"
#include "Environment/Tests/CatWaterTestFixtures.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/Fragments/CatEquipmentFragment_Chum.h"
#include "Fishing/Actors/CatFishingHookActor.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/Presentation/CatFishingPresentationSettings.h"
#include "Fishing/Simulation/CatFishingBiteTimingModel.h"
#include "Inventory/CatInventorySettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingBiteTimingWorldTest,
	"Catfishing.Unit.Fishing.BiteTiming.WorldFieldsDriveFormalStateTreeAndBobber",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingBiteTimingWorldTest::RunTest(const FString& Parameters)
{
	const UCatEquipmentDefinition* ChumDefinition = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentDefinition>(TEXT("BugChum"));
	if (!TestNotNull(TEXT("正式窝料资产可加载"), ChumDefinition)) return false;
	const UCatEquipmentFragment_Chum* ChumFragment = ChumDefinition->FindFragment<UCatEquipmentFragment_Chum>();
	if (!TestNotNull(TEXT("正式窝料资产含水域影响片段"), ChumFragment)) return false;
	FCatFishingBiteTimingParameters Timing;
	if (!TestTrue(TEXT("正式等待配置可读取"), GetDefault<UCatFishingSettings>()->TryGetBiteTimingParameters(Timing))) return false;
	// 只准备抛竿事务的已冻结输入；后续采样、计时器、正式 StateTree 和正式 Hook BP 都走生产代码。
	for (const int32 Portions : {0, 1, 5})
	{
		FTestWorldWrapper Wrapper;
		if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
		Wrapper.ForwardErrorMessages(this);
		UWorld* World = Wrapper.GetTestWorld();
		FCatWaterGeometryBuildInput WaterInput;
		WaterInput.RegionId = TEXT("BiteTimingTestWater");
		WaterInput.WaterPointVerticalToleranceCm = 100.0;
		WaterInput.BankHeightToleranceCm = 50.0;
		WaterInput.BoundaryToleranceCm = 1.0;
		WaterInput.MaxLandingCorrectionCm = 100.0;
		WaterInput.MinimumWaterInsetCm = 1.0;
		FCatWaterPolygonBuildInput Polygon;
		Polygon.BoundaryId = TEXT("BiteTimingBoundary");
		Polygon.Vertices = {{-5000, -5000}, {5000, -5000}, {5000, 5000}, {-5000, 5000}};
		WaterInput.Boundaries.Add(Polygon);
		const auto Built = FCatWaterGeometry::Build(WaterInput);
		if (!TestTrue(TEXT("水域烘焙成功"), Built.bSucceeded)) return false;
		ACatWaterRegion* Region = World->SpawnActor<ACatWaterRegion>();
		if (!Region) return false;
		FCatWaterRegionTestAccess::InjectBakedGeometry(*Region, Built.Cache);
		if (!TestTrue(TEXT("水域注册并启动 World 生命周期"), Wrapper.BeginPlayInTestWorld())) return false;
		UCatChumFieldSubsystem* Chum = World->GetSubsystem<UCatChumFieldSubsystem>();
		if (!TestNotNull(TEXT("真实窝料子系统存在"), Chum)) return false;
		const double StartTime = World->GetTimeSeconds();
		for (int32 Portion = 0; Portion < Portions; ++Portion)
		{
			FCatPrepareChumFieldRequest Request;
			Request.StableNetId = TEXT("BiteTimingAutomation");
			Request.Command.RequestId = FGuid::NewGuid();
			Request.Command.ExpectedWaterRegionHandle = Built.Cache.Handle;
			Request.Command.ChumDefinitionId = ChumDefinition->EquipmentDefinitionId;
			Request.Command.Quantity = 1;
			Request.Influence = ChumFragment->ChumInfluence;
			Request.ServerTime = StartTime;
			const auto Prepared = Chum->PrepareField(Request);
			if (!TestTrue(TEXT("正式窝料准备成功"), Prepared.bPrepared)) return false;
			TestEqual(TEXT("正式资产保留三分钟持续时间"), Prepared.ExpireServerTime - Prepared.StartServerTime, 180.0);
			const auto Activated = Chum->ActivatePreparedFieldDeferred(Prepared.CommitToken);
			if (!TestTrue(TEXT("真实窝料场激活成功"), Activated.bCommitted)) return false;
			Chum->PublishActivatedField(Activated.FieldId);
		}
		const auto Sample = Chum->SampleChumAtPoint(FVector::ZeroVector, Built.Cache.Handle, StartTime);
		if (!TestTrue(TEXT("中心采样成功"), Sample.bSucceeded)) return false;
		const auto Total = [](const FCatChumSample& Value)
		{
			return Value.EffectiveChumVector.Fishy + Value.EffectiveChumVector.Fragrant + Value.EffectiveChumVector.Fermented;
		};
		TestEqual(TEXT("每份正式窝料贡献为1.7"), Total(Sample), Portions * 1.7, 1.e-7);
		TestEqual(TEXT("独立投放真实重叠"), Sample.ContributingFieldCount, Portions);
		if (Portions > 0)
		{
			const auto Aged = Chum->SampleChumAtPoint(FVector::ZeroVector, Built.Cache.Handle, StartTime + 90.0);
			const auto Edge = Chum->SampleChumAtPoint(FVector(800, 0, 0), Built.Cache.Handle, StartTime);
			const auto Expired = Chum->SampleChumAtPoint(FVector::ZeroVector, Built.Cache.Handle, StartTime + 180.0);
			TestTrue(TEXT("时间衰减降低有效贡献"), Aged.bSucceeded && Total(Aged) > 0.0 && Total(Aged) < Total(Sample));
			TestTrue(TEXT("扩大后的八米处仍在范围内且更淡"), Edge.bSucceeded && Total(Edge) > 0.0 && Total(Edge) < Total(Sample));
			TestTrue(TEXT("到期按零贡献返回"), Expired.bSucceeded && Total(Expired) == 0.0);
			FCatFishingBiteTimingDistribution FreshDistribution, AgedDistribution;
			FCatFishingBiteTimingModel::BuildDistribution(Timing, Total(Sample), 1.0, 1.0, FreshDistribution);
			FCatFishingBiteTimingModel::BuildDistribution(Timing, Total(Aged), 1.0, 1.0, AgedDistribution);
			TestTrue(TEXT("真实衰减采样使均值回升"), AgedDistribution.ExpectedMeanSeconds > FreshDistribution.ExpectedMeanSeconds);
		}
		UClass* HookClass = GetDefault<UCatFishingPresentationSettings>()->HookActorClass.LoadSynchronous();
		if (!HookClass) return false;
		ACatFishingHookActor* Hook = World->SpawnActor<ACatFishingHookActor>(HookClass);
		ACatFishingSession* Session = World->SpawnActor<ACatFishingSession>();
		if (!Hook || !Session) return false;
		Session->Snapshot.FishingSessionId = FGuid::NewGuid();
		Session->Snapshot.CastAttemptId = FGuid::NewGuid();
		Session->Snapshot.HookActor = Hook;
		Session->AttemptSnapshot.ServerRandomSeed = 78629;
		Session->AttemptSnapshot.BaitDefinitionId = TEXT("BugBait");
		Session->AttemptSnapshot.WaterRegion = Built.Cache.Handle;
		Session->AttemptSnapshot.ServerCorrectedLandingWorldPoint = FVector::ZeroVector;
		Session->bPrepared = true;
		Hook->InitializeAuthoritativeIdentity(Session->Snapshot.FishingSessionId, Session->Snapshot.CastAttemptId);
		Hook->SetActorLocation(FVector(-2000, 0, 100)); // 飞行起点在窝外，必须读冻结落点。
		if (!TestTrue(TEXT("真实抛竿飞行启动"), Hook->BeginAuthoritativeFlight(FVector::ZeroVector))) return false;
		if (!TestTrue(TEXT("正式会话 StateTree 进入等待"), Session->StartPreparedSessionLogicFromAuthority())) return false;
		TestEqual(TEXT("运行阶段为等待"), Session->GetSnapshot().Phase, ECatFishingPhase::Waiting);
		FCatFishingBiteTimingDistribution Distribution;
		FCatFishingBiteTimingModel::BuildDistribution(Timing, Total(Sample), 1.0, 1.0, Distribution);
		FRandomStream Random(static_cast<int32>(Session->CurrentBiteRandomSeed));
		double ExpectedWait = 0.0;
		Distribution.TrySample(Random.FRand(), ExpectedWait);
		const double FlightSeconds = Hook->GetPresentationState().CastTrajectory.DurationSeconds;
		const double BiteRemaining = World->GetTimerManager().GetTimerRemaining(Session->ProbeTimerHandle);
		const double WarningRemaining = World->GetTimerManager().GetTimerRemaining(Session->BiteWarningTimerHandle);
		TestEqual(TEXT("生产计时器消费冻结落点的新模型并加飞行时间"), BiteRemaining, ExpectedWait + FlightSeconds, 1.e-4);
		TestEqual(TEXT("计时器保证完整1.5秒预警"), BiteRemaining - WarningRemaining, 1.5, 1.e-4);
		const uint64 FirstSeed = Session->CurrentBiteRandomSeed;
		double ObservedWarningTime = -1.0;
		for (int32 Frame = 0; Frame < 5000 && Session->GetSnapshot().Phase != ECatFishingPhase::TrueBiteWindow; ++Frame)
		{
			Wrapper.TickTestWorld(0.01f);
			if (ObservedWarningTime < 0.0 && Hook->GetPresentationState().BobberMode == ECatFishingBobberPresentationMode::BiteWarning)
				ObservedWarningTime = World->GetTimeSeconds();
		}
		TestTrue(TEXT("运行时观察到预警模式"), ObservedWarningTime >= 0.0);
		TestEqual(TEXT("正式 StateTree 真正打开咬钩窗口"), Session->GetSnapshot().Phase, ECatFishingPhase::TrueBiteWindow);
		TestEqual(TEXT("浮漂真咬时下沉"), Hook->GetPresentationState().BobberMode, ECatFishingBobberPresentationMode::Sunk);
		TestEqual(TEXT("实际预警持续完整时段（帧量化容差）"), World->GetTimeSeconds() - ObservedWarningTime, 1.5, 0.04);
		TestNull(TEXT("未提竿不提前创建鱼"), Session->GetSnapshot().FishEncounterActor.Get());
		TestTrue(TEXT("未提竿不提前选鱼"), Session->GetSnapshot().FishDefinitionId.IsNone());
		for (int32 Frame = 0; Frame < 500 && Session->GetSnapshot().Phase != ECatFishingPhase::Waiting; ++Frame)
			Wrapper.TickTestWorld(0.01f);
		TestEqual(TEXT("错过窗口后正式回到等待"), Session->GetSnapshot().Phase, ECatFishingPhase::Waiting);
		TestEqual(TEXT("每个机会只调度一次"), Session->BiteOpportunitySequence, 2u);
		TestTrue(TEXT("下一机会使用新种子"), Session->CurrentBiteRandomSeed != FirstSeed);
		TestTrue(TEXT("下一等待计时器已启动"), World->GetTimerManager().IsTimerActive(Session->ProbeTimerHandle));
		Session->TerminateSession(ECatFishingOutcome::Cancelled, TEXT("Bite timing automation complete"));
		TestFalse(TEXT("退出清理预警"), World->GetTimerManager().IsTimerActive(Session->BiteWarningTimerHandle));
		TestFalse(TEXT("退出清理咬钩调度"), World->GetTimerManager().IsTimerActive(Session->ProbeTimerHandle));
		TestFalse(TEXT("退出清理响应窗口"), World->GetTimerManager().IsTimerActive(Session->TrueBiteTimerHandle));
	}
	return !HasAnyErrors();
}

#endif
