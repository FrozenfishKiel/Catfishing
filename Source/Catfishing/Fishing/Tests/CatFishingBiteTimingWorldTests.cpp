#include "Inventory/CatInventorySettings.h"
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "TimerManager.h"
#include "Environment/CatChumFieldSubsystem.h"
#include "Environment/Tests/CatWaterTestFixtures.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/Fragments/CatEquipmentFragment_Chum.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Fishing/Actors/CatFishingHookActor.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/CatFishingService.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingGameState.h"
#include "Fishing/CatFishingGameplayTags.h"
#include "Components/StateTreeComponent.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/Presentation/CatFishingPresentationSettings.h"
#include "Fishing/Simulation/CatFishingBiteTimingModel.h"
#include "Character/CatCharacter.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Inventory/CatInventoryComponent.h"
#include "Equipment/Fragments/CatEquipmentFragment_Rod.h"
#include "Fishing/Actors/CatFishEncounterActor.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingBiteTimingWorldTest,
	"Catfishing.Unit.Fishing.BiteTiming.WorldFieldsDriveFormalStateTreeAndBobber",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingBiteTimingWorldTest::RunTest(const FString& Parameters)
{
	const UCatEquipmentDefinition* ChumDefinition = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentDefinition>(TEXT("BugChum"));
	if (!TestNotNull(TEXT("正式窝料资产可加载"), ChumDefinition)) return false;
	FCatFishingBiteTimingParameters Timing;
	if (!TestTrue(TEXT("正式等待配置可读取"), GetDefault<UCatFishingSettings>()->TryGetBiteTimingParameters(Timing))) return false;
	// 只准备抛竿事务的已冻结输入；后续采样、计时器、正式 StateTree 和正式 Hook BP 都走生产代码。
	for (const int32 Portions : {0, 1, 5})
	{
		FTestWorldWrapper Wrapper;
		if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
		Wrapper.ForwardErrorMessages(this);
		UWorld* World = Wrapper.GetTestWorld();
		FURL URL;
		URL.AddOption(TEXT("game=/Script/Catfishing.CatfishingGameModeBase"));
		if (!TestTrue(TEXT("使用正式昼夜准入宿主"), World->SetGameMode(URL))) return false;
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
		auto* Mode = World->GetAuthGameMode<ACatfishingGameModeBase>();
		Mode->bRunCommandsOpen = true;
		Mode->RunPublicState.Phase.Phase = ECatRunPhase::DayActive;
		Mode->RunPublicState.Phase.bNewFishingBitesAllowed = true;
		auto* Fishing = World->GetSubsystem<UCatFishingService>();
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
			Request.Influence = ChumDefinition->FindFragment<UCatEquipmentFragment_Chum>()->ChumInfluence;
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
		auto* BaitCharacter = World->SpawnActor<ACatCharacter>();
		auto* BaitPlayer = World->SpawnActor<ACatfishingPlayerState>();
		auto* BaitRod = World->SpawnActor<ACatFishingRodActor>();
		if (!BaitCharacter || !BaitPlayer || !BaitRod) return false;
		BaitCharacter->SetPlayerState(BaitPlayer);
		// 本夹具只验证计时和库存；没有角色地板，禁止无控制器角色自由落出世界。
		BaitCharacter->GetCharacterMovement()->SetComponentTickEnabled(false);
		auto* BaitEquipment = BaitCharacter->GetEquipmentComponent();
		for (const FName Definition : {FName(TEXT("StarterRodT1")), FName(TEXT("FeatherFloat"))})
			if (!BaitEquipment->GrantEquipmentFromAuthority(FGuid::NewGuid(), BaitEquipment->GetSnapshot().Revision, Definition).bCommitted) return false;
		if (!BaitEquipment->GrantInventoryQuantityFromAuthority(FGuid::NewGuid(), BaitEquipment->GetSnapshot().Revision, TEXT("BugBait"), 1).bCommitted) return false;
		if (!BaitEquipment->Use(FGuid::NewGuid(), BaitEquipment->GetSnapshot().Revision, BaitEquipment->GetSnapshot().RodItemInstanceId).bCommitted) return false;
		const auto BaitLoadout = BaitEquipment->GetSnapshot();
		if (!BaitEquipment->BeginFishingUse(Session->Snapshot.FishingSessionId, BaitLoadout.RodItemInstanceId,
			BaitLoadout.BaitItemInstanceId, BaitLoadout.FloatItemInstanceId, BaitLoadout.RodDefinitionId,
			BaitLoadout.BaitDefinitionId, BaitLoadout.FloatDefinitionId, BaitLoadout.Revision).bUseAccepted) return false;
		if (!BaitRod->InitializeAuthoritativeIdentity(FGuid::NewGuid(), BaitLoadout.RodItemInstanceId,
			BaitLoadout.RodDefinitionId, NAME_None, BaitPlayer, nullptr, true, false)) return false;
		Session->Snapshot.RodActor = BaitRod;
		Session->FisherCharacter = BaitCharacter;
		Session->AttemptSnapshot.RodActor = BaitRod;
		Session->AttemptSnapshot.RodDefinitionId = BaitLoadout.RodDefinitionId;
		BaitCharacter->SetActorLocation(FVector(-500, 0, 0));
		Session->CastEquipment = BaitEquipment;
		Session->Snapshot.CastAttemptId = FGuid::NewGuid();
		Session->Snapshot.HookActor = Hook;
		Session->AttemptSnapshot.ServerRandomSeed = 78629;
		Session->AttemptSnapshot.BaitDefinitionId = TEXT("BugBait");
		Session->AttemptSnapshot.WaterRegion = Built.Cache.Handle;
		Session->AttemptSnapshot.ServerCorrectedLandingWorldPoint = FVector::ZeroVector;
		Session->bPrepared = true;
		Fishing->Sessions.Add(Session->Snapshot.FishingSessionId, Session);
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
		double ObservedWarningTime = -1.0;
		for (int32 Frame = 0; Frame < 5000 && Session->GetSnapshot().Phase != ECatFishingPhase::TrueBiteWindow; ++Frame)
		{
			Wrapper.TickTestWorld(0.01f);
			if (ObservedWarningTime < 0.0 && Hook->GetPresentationState().BobberMode == ECatFishingBobberPresentationMode::BiteWarning)
			{
				ObservedWarningTime = World->GetTimeSeconds();
				TestEqual(TEXT("fast warning preserves the real bait quantity"),
					BaitCharacter->GetInventoryComponent()->CountVisibleInventoryQuantityByDefinitionId(TEXT("BugBait")), 1);
			}
		}
		TestTrue(TEXT("运行时观察到预警模式"), ObservedWarningTime >= 0.0);
		TestEqual(TEXT("正式 StateTree 真正打开咬钩窗口"), Session->GetSnapshot().Phase, ECatFishingPhase::TrueBiteWindow);
		TestEqual(TEXT("真咬才确认鱼饵消耗"), BaitEquipment->CommitFishingBaitDeferred(Session->Snapshot.FishingSessionId).Error,
			ECatDomainCommandError::AlreadyResolved);
		TestEqual(TEXT("浮漂真咬时下沉"), Hook->GetPresentationState().BobberMode, ECatFishingBobberPresentationMode::Sunk);
		TestEqual(TEXT("实际预警持续完整时段（帧量化容差）"), World->GetTimeSeconds() - ObservedWarningTime, 1.5, 0.04);
		TestEqual(TEXT("true bite consumes exactly one actual bait"), BaitCharacter->GetInventoryComponent()->CountVisibleInventoryQuantityByDefinitionId(TEXT("BugBait")), 0);
		const double D0 = Session->TrueBiteDistanceCentimeters;
		TestEqual(TEXT("D0 uses fish/cat distance at true bite"), D0, FVector::Distance(BaitCharacter->GetActorLocation(), Hook->GetActorLocation()), 0.01);
		BaitCharacter->SetActorLocation(FVector(-1200, 0, 0));
		TestEqual(TEXT("response-window movement does not recalculate D0"), Session->TrueBiteDistanceCentimeters, D0);
		TestNull(TEXT("未提竿不提前创建鱼"), Session->GetSnapshot().FishEncounterActor.Get());
		TestTrue(TEXT("未提竿不提前选鱼"), Session->GetSnapshot().FishDefinitionId.IsNone());
		// 正式 Morning/Day/Dusk timer 在等待期间会刷新鱼情，必须捕获实际真咬时的环境。
		const FCatEnvironmentSnapshot BiteEnvironment = World->GetGameState<ACatfishingGameState>()->GetRunPublicState().Environment;
		TestTrue(TEXT("真咬发生于正式可用的白天鱼情"), BiteEnvironment.TimeOfDay != ECatEnvironmentTimeOfDay::Unknown);
		Mode->RunPublicState.Phase.RunId = FGuid::NewGuid();
		Mode->bRunStartupInProgress = true;
		TestTrue(TEXT("正式 Run 入夜入口成功"), Mode->EnterRunPhaseFromStateTree(
			ECatRunPhase::NormalNight, ECatRunTransitionReason::DayEnded).bApplied);
		Mode->bRunStartupInProgress = false;
		TestEqual(TEXT("入夜不打断已经成立的真咬窗口"), Session->GetSnapshot().Phase, ECatFishingPhase::TrueBiteWindow);
		TestTrue(TEXT("入夜保留既有提竿响应计时"), World->GetTimerManager().IsTimerActive(Session->TrueBiteTimerHandle));
		TestEqual(TEXT("入夜后世界鱼情已没有白天时段"), World->GetGameState<ACatfishingGameState>()->GetRunPublicState().Environment.TimeOfDay,
			ECatEnvironmentTimeOfDay::Unknown);
		TestEqual(TEXT("跨夜真咬保留发生时的时段"), Session->BiteTimeOfDay, BiteEnvironment.TimeOfDay);
		TestEqual(TEXT("跨夜真咬保留发生时的天气"), Session->BiteWeather, BiteEnvironment.Weather);
		AddExpectedErrorPlain(TEXT("Outcome=ECatFishingOutcome::HookWindowExpired"), EAutomationExpectedErrorFlags::Contains, 1);
		for (int32 Frame = 0; Frame < 500 && !Session->IsTerminal(); ++Frame) Wrapper.TickTestWorld(0.01f);
		TestEqual(TEXT("true bite timeout ends this cast"), Session->GetSnapshot().Outcome, ECatFishingOutcome::HookWindowExpired);
		TestFalse(TEXT("timeout clears future bite scheduling"), World->GetTimerManager().IsTimerActive(Session->ProbeTimerHandle));
		TestFalse(TEXT("timeout closes equipment use"), BaitEquipment->HasActiveFishingUse());
		TestEqual(TEXT("timeout never refunds bait"), BaitCharacter->GetInventoryComponent()->CountVisibleInventoryQuantityByDefinitionId(TEXT("BugBait")), 0);
		TestFalse(TEXT("exit clears warning timer"), World->GetTimerManager().IsTimerActive(Session->BiteWarningTimerHandle));
		TestFalse(TEXT("exit clears response timer"), World->GetTimerManager().IsTimerActive(Session->TrueBiteTimerHandle));
		auto* NightSession = World->SpawnActor<ACatFishingSession>();
		auto* NightHook = World->SpawnActor<ACatFishingHookActor>(HookClass);
		if (!NightSession || !NightHook) return false;
		NightSession->Snapshot.FishingSessionId = FGuid::NewGuid();
		NightSession->Snapshot.CastAttemptId = FGuid::NewGuid();
		NightSession->Snapshot.HookActor = NightHook;
		NightSession->AttemptSnapshot = Session->AttemptSnapshot;
		NightSession->bPrepared = true;
		NightHook->InitializeAuthoritativeIdentity(NightSession->Snapshot.FishingSessionId, NightSession->Snapshot.CastAttemptId);
		TestTrue(TEXT("夜间新抛竿的正式 StateTree 正常启动"), NightSession->StartPreparedSessionLogicFromAuthority());
		TestEqual(TEXT("夜间新会话保持等待"), NightSession->GetSnapshot().Phase, ECatFishingPhase::Waiting);
		TestFalse(TEXT("夜间新抛竿没有咬钩计时"), World->GetTimerManager().IsTimerActive(NightSession->ProbeTimerHandle));
		TestTrue(TEXT("夜间取消正常结束新会话"), NightSession->CancelFromAuthority(FGuid::NewGuid()).bCommitted);
		Mode->RunPublicState.Phase.Phase = ECatRunPhase::DayActive;
		Mode->RunPublicState.Phase.bNewFishingBitesAllowed = true;
		const auto* RodDefinition = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentDefinition>(BaitLoadout.RodDefinitionId);
		const double Lmax = RodDefinition->FindFragment<UCatEquipmentFragment_Rod>()->MaximumLineLengthCentimeters;
		for (const double Distance : {Lmax, Lmax + 1.0})
		{
			auto* BoundarySession = World->SpawnActor<ACatFishingSession>();
			auto* BoundaryHook = World->SpawnActor<ACatFishingHookActor>(HookClass);
			BoundarySession->Snapshot.FishingSessionId = FGuid::NewGuid();
			BoundarySession->Snapshot.CastAttemptId = FGuid::NewGuid();
			BoundarySession->Snapshot.HookActor = BoundaryHook;
			BoundarySession->Snapshot.RodActor = BaitRod;
			BoundarySession->Snapshot.Phase = ECatFishingPhase::Probe;
			BoundarySession->AttemptSnapshot.RodDefinitionId = BaitLoadout.RodDefinitionId;
			BoundarySession->CastEquipment = BaitEquipment;
			BoundarySession->FisherCharacter = BaitCharacter;
			BoundarySession->bStartupInProgress = true;
			BaitCharacter->SetActorLocation(FVector(-Distance, 0, 0));
			BoundaryHook->InitializeAuthoritativeIdentity(BoundarySession->Snapshot.FishingSessionId, BoundarySession->Snapshot.CastAttemptId);
			BoundaryHook->SetActorLocation(FVector::ZeroVector);
			if (!BaitEquipment->GrantInventoryQuantityFromAuthority(FGuid::NewGuid(), BaitEquipment->GetSnapshot().Revision, TEXT("BugBait"), 1).bCommitted) return false;
			const auto Loadout = BaitEquipment->GetSnapshot();
			if (!BaitEquipment->BeginFishingUse(BoundarySession->Snapshot.FishingSessionId, Loadout.RodItemInstanceId,
				Loadout.BaitItemInstanceId, Loadout.FloatItemInstanceId, Loadout.RodDefinitionId, Loadout.BaitDefinitionId,
				Loadout.FloatDefinitionId, Loadout.Revision).bUseAccepted) return false;
			if (Distance > Lmax) AddExpectedErrorPlain(TEXT("Outcome=ECatFishingOutcome::Escaped"), EAutomationExpectedErrorFlags::Contains, 1);
			TestEqual(TEXT("true bite admits exact Lmax and rejects over-limit distance"), BoundarySession->OpenTrueBiteWindowFromStateTree(), Distance <= Lmax);
			TestEqual(TEXT("both boundary outcomes pay one bait"), BaitCharacter->GetInventoryComponent()->CountVisibleInventoryQuantityByDefinitionId(TEXT("BugBait")), 0);
			TestEqual(TEXT("the frozen distance is not clamped"), BoundarySession->TrueBiteDistanceCentimeters, Distance);
			TestTrue(TEXT("float inventory survives the distance decision"), BaitEquipment->GetSnapshot().FloatItemInstanceId.IsValid());
			if (Distance > Lmax)
			{
				TestEqual(TEXT("over-limit true bite ends as escaped before hook"), BoundarySession->GetSnapshot().Outcome, ECatFishingOutcome::Escaped);
				TestFalse(TEXT("over-limit session cannot start a fight"), BoundarySession->RequestHookFromAuthority(FGuid::NewGuid()).bCommitted);
			}
			else
			{
				AddExpectedErrorPlain(TEXT("Outcome=ECatFishingOutcome::HookWindowExpired"), EAutomationExpectedErrorFlags::Contains, 1);
				BoundarySession->HandleTrueBiteWindowExpired();
			}
		}

	}
	return !HasAnyErrors();
}

#endif
