#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "TimerManager.h"
#include "Environment/CatChumFieldSubsystem.h"
#include "Environment/CatChumFieldReplicationComponent.h"
#include "Environment/Tests/CatWaterTestFixtures.h"
#include "Equipment/CatEquipmentItemDefinition.h"
#include "Equipment/Fragments/CatEquipmentFragment_Chum.h"
#include "Inventory/CatInventorySettings.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/CatFishingService.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingGameState.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatRunTransientCleanupTest,
	"Catfishing.Unit.Run.RunPhaseClearsSessionsTimersAndCurrentFields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatRunTransientCleanupTest::RunTest(const FString& Parameters)
{
	for (const auto TargetPhase : {ECatRunPhase::DayActive, ECatRunPhase::SuccessSettlementNight, ECatRunPhase::Ending})
	{
		FTestWorldWrapper Wrapper;
		if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
		Wrapper.ForwardErrorMessages(this);
		auto* World = Wrapper.GetTestWorld();
		FURL URL;
		URL.AddOption(TEXT("game=/Script/Catfishing.CatfishingGameModeBase"));
		if (!World->SetGameMode(URL)) return false;
		FCatWaterGeometryBuildInput Input;
		Input.RegionId = TEXT("Batch7AWater");
		Input.WaterPointVerticalToleranceCm = 100;
		Input.BankHeightToleranceCm = 50;
		Input.BoundaryToleranceCm = 1;
		Input.MaxLandingCorrectionCm = 100;
		Input.MinimumWaterInsetCm = 1;
		FCatWaterPolygonBuildInput Polygon;
		Polygon.BoundaryId = TEXT("Batch7ABoundary");
		Polygon.Vertices = {{-5000,-5000},{5000,-5000},{5000,5000},{-5000,5000}};
		Input.Boundaries.Add(Polygon);
		const auto Built = FCatWaterGeometry::Build(Input);
		if (!TestTrue(TEXT("water fixture baked"), Built.bSucceeded)) return false;
		auto* Region = World->SpawnActor<ACatWaterRegion>();
		FCatWaterRegionTestAccess::InjectBakedGeometry(*Region, Built.Cache);
		if (!Wrapper.BeginPlayInTestWorld()) return false;
		auto* Mode = World->GetAuthGameMode<ACatfishingGameModeBase>();
		auto* Fishing = World->GetSubsystem<UCatFishingService>();
		auto* Fields = World->GetSubsystem<UCatChumFieldSubsystem>();
		auto* Replication = World->GetGameState<ACatfishingGameState>()->GetChumFieldReplicationFromAuthority();
		const auto* Definition = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentItemDefinition>(5);
		if (!Mode || !Fishing || !Fields || !Replication || !Definition) return false;
		FCatPrepareChumFieldRequest Request;
		Request.StableNetId = TEXT("Batch7A");
		Request.Command.RequestId = FGuid::NewGuid();
		Request.Command.ExpectedWaterRegionHandle = Built.Cache.Handle;
		Request.Command.ChumItemId = Definition->ItemId;
		Request.Command.Quantity = 1;
		Request.Influence = Definition->FindFragment<UCatEquipmentFragment_Chum>()->ChumInfluence;
		Request.ServerTime = World->GetTimeSeconds();
		const auto Prepared = Fields->PrepareField(Request);
		if (!TestTrue(TEXT("field prepared"), Prepared.bPrepared)) return false;
		const auto Activated = Fields->ActivatePreparedFieldDeferred(Prepared.CommitToken);
		Fields->StoreTerminalResult(Request.StableNetId, Activated);
		Fields->PublishActivatedField(Activated.FieldId);
		TestTrue(TEXT("unexpired field replicated"), Activated.bCommitted && Replication->GetPublicFields().Num() == 1);
		Request.Command.RequestId = FGuid::NewGuid();
		const auto Pending = Fields->PrepareField(Request);
		TestTrue(TEXT("second field remains prepared"), Pending.bPrepared);
		auto* Waiting = World->SpawnActor<ACatFishingSession>();
		auto* Fighting = World->SpawnActor<ACatFishingSession>();
		Waiting->Snapshot.FishingSessionId = FGuid::NewGuid();
		Waiting->Snapshot.Phase = ECatFishingPhase::Waiting;
		Fighting->Snapshot.FishingSessionId = FGuid::NewGuid();
		Fighting->Snapshot.Phase = ECatFishingPhase::HookedFight;
		Fishing->Sessions.Add(Waiting->Snapshot.FishingSessionId, Waiting);
		Fishing->Sessions.Add(Fighting->Snapshot.FishingSessionId, Fighting);
		bool bYesterdayTimerFired = false;
		World->GetTimerManager().SetTimer(Waiting->ProbeTimerHandle, [&]() { bYesterdayTimerFired = true; }, 0.1f, false);
		TestTrue(TEXT("normal night entered"), Mode->EnterRunPhaseFromStateTree(ECatRunPhase::NormalNight, ECatRunTransitionReason::None).bApplied);
		TestFalse(TEXT("ordinary night preserves ongoing fight"), Fighting->IsTerminal());
		TestFalse(TEXT("ordinary night cancels waiting timer"), World->GetTimerManager().IsTimerActive(Waiting->ProbeTimerHandle));
		TestEqual(TEXT("ordinary night preserves field"), Replication->GetPublicFields().Num(), 1);
		TestTrue(TEXT("target phase applied"), Mode->EnterRunPhaseFromStateTree(TargetPhase, ECatRunTransitionReason::None).bApplied);
		TestTrue(TEXT("phase terminates both waiting and fighting"), Waiting->IsTerminal() && Fighting->IsTerminal());
		TestTrue(TEXT("service no longer exposes yesterday sessions"), !Fishing->FindSession(Waiting->Snapshot.FishingSessionId)
			&& !Fishing->FindSession(Fighting->Snapshot.FishingSessionId));
		TestEqual(TEXT("unexpired fields removed from replication"), Replication->GetPublicFields().Num(), 0);
		TestFalse(TEXT("old prepare token cannot activate after transition"), Fields->ActivatePreparedFieldDeferred(Pending.CommitToken).bCommitted);
		TestEqual(TEXT("repeat clear is idempotent"), Fields->ClearFieldsForRunTransition(), 0);
		for (int32 Tick = 0; Tick < 20; ++Tick) Wrapper.TickTestWorld(1.0f / 60.0f);
		TestFalse(TEXT("old waiting timer never resumes"), bYesterdayTimerFired);
	}
	return !HasAnyErrors();
}
#endif
