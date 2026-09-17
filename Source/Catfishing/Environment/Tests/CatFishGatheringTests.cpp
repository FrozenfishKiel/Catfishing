#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Environment/CatFishGatheringActor.h"
#include "Environment/CatChumFieldSettings.h"
#include "Environment/CatChumFieldSubsystem.h"
#include "Environment/CatChumFieldReplicationComponent.h"
#include "Environment/Tests/CatWaterTestFixtures.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/Fragments/CatEquipmentFragment_Chum.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Actors/CatFishingHookActor.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingGameState.h"
#include "EngineUtils.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "TimerManager.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishGatheringThresholdTest,
	"Catfishing.Unit.Environment.Gathering.ThreeAxesAndSpatialBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatFishGatheringThresholdTest::RunTest(const FString& Parameters)
{
	auto* Settings = NewObject<UCatChumFieldSettings>();
	Settings->GatheringConcentrationThreshold = {6.0, 3.0, 1.2};
	TestTrue(TEXT("exact three-axis threshold qualifies"), Settings->MeetsGatheringThreshold({6.0, 3.0, 1.2}));
	TestFalse(TEXT("high total cannot replace missing fermented concentration"), Settings->MeetsGatheringThreshold({100.0, 100.0, 1.19}));
	TestFalse(TEXT("fishy independently gates"), Settings->MeetsGatheringThreshold({5.9, 100.0, 100.0}));
	TestFalse(TEXT("fragrant independently gates"), Settings->MeetsGatheringThreshold({100.0, 2.9, 100.0}));
	TestFalse(TEXT("nonfinite contribution rejected"), Settings->MeetsGatheringThreshold({std::numeric_limits<double>::infinity(), 3.0, 1.2}));
	Settings->GatheringTriggerProbability = 1.01;
	TestFalse(TEXT("invalid probability rejected"), Settings->IsGatheringConfigurationValid());
	Settings->GatheringTriggerProbability = 0;
	TestTrue(TEXT("zero probability valid for explicit no-hit configuration"), Settings->IsGatheringConfigurationValid());
	Settings->GatheringConcentrationThreshold.Fermented = 0;
	TestFalse(TEXT("three positive thresholds required"), Settings->IsGatheringConfigurationValid());
	FCatFishGatheringState State;
	State.EventId = FGuid::NewGuid(); State.WaterRegion.RegionId = TEXT("TestLake"); State.WaterRegion.GeometryRevision = 1;
	State.RadiusCentimeters = 100; State.StartedServerTime = 10; State.EndsServerTime = 55; State.BiteSpeedMultiplier = 2.5;
	TestTrue(TEXT("circle edge inclusive and start inclusive"), State.Contains(FVector(100,0,0), State.WaterRegion, 10));
	TestFalse(TEXT("outside circle excluded"), State.Contains(FVector(100.01,0,0), State.WaterRegion, 10));
	TestFalse(TEXT("end time exclusive even before destruction callback"), State.Contains(FVector::ZeroVector, State.WaterRegion, 55));
	auto OtherRegion = State.WaterRegion; OtherRegion.GeometryRevision++;
	TestFalse(TEXT("stale geometry cannot receive speed bonus"), State.Contains(FVector::ZeroVector, OtherRegion, 11));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishGatheringFormalRecipeTest,
	"Catfishing.Unit.Environment.Gathering.FormalRecipesAndQuantityScaling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatFishGatheringFormalRecipeTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* Name : {TEXT("Bug"), TEXT("FermentedGrain"), TEXT("FruitFragrance"), TEXT("HolyLight")})
	{
		const FString Path = FString::Printf(TEXT("/Game/Catfishing/Data/Equipment/Equip_Chum_%s.Equip_Chum_%s"), Name, Name);
		const auto* Definition = LoadObject<UCatEquipmentDefinition>(nullptr, *Path);
		if (!TestNotNull(TEXT("formal chum asset loads"), Definition)) return false;
		const auto* Fragment = Definition->FindFragment<UCatEquipmentFragment_Chum>();
		if (!TestNotNull(TEXT("formal recipe fragment exists"), Fragment)) return false;
		const auto& Spec = Fragment->ChumInfluence;
		FCatChumRuntimeInfluence Runtime;
		if (!TestTrue(TEXT("formal recipe compiles at one portion"), Spec.BuildRuntimeInfluence(1, Runtime))) return false;
		AddInfo(FString::Printf(TEXT("Event=gathering_recipe_audit ItemId=%d Asset=%s Fishy=%.6f Fragrant=%.6f Fermented=%.6f Duration=%.3f Radius=%.3f MaxQuantity=%d"),
			Definition->ItemId, *Path, Runtime.BaseContribution.Fishy, Runtime.BaseContribution.Fragrant, Runtime.BaseContribution.Fermented,
			Runtime.DurationSeconds, Runtime.RadiusCentimeters, Spec.MaximumQuantityPerPlacement));
		TestEqual(TEXT("runtime retains fishy recipe"), Runtime.BaseContribution.Fishy, Spec.BaseContribution.Fishy);
		FCatChumInfluenceSpec Multiple = Spec;
		Multiple.MaximumQuantityPerPlacement = 3;
		TestTrue(TEXT("quantity scales the recipe"), Multiple.BuildRuntimeInfluence(3, Runtime));
		TestEqual(TEXT("three portions multiply fermented contribution"), Runtime.BaseContribution.Fermented, Spec.BaseContribution.Fermented * 3.0);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishGatheringWorldTest,
	"Catfishing.Unit.Environment.Gathering.WorldPublicationTimersAndCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatFishGatheringWorldTest::RunTest(const FString& Parameters)
{
	auto* Settings = GetMutableDefault<UCatChumFieldSettings>();
	TGuardValue<bool> Enable(Settings->bEnableFishGathering, true);
	TGuardValue<double> Probability(Settings->GatheringTriggerProbability, 1.0);
	TGuardValue<double> Duration(Settings->GatheringDurationSeconds, 1.0);
	TGuardValue<FCatChumVector> Threshold(Settings->GatheringConcentrationThreshold, FCatChumVector{0.9, 0.4, 0.1});
	FTestWorldWrapper Wrapper;
	if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
	Wrapper.ForwardErrorMessages(this);
	auto* World = Wrapper.GetTestWorld();
	FURL URL; URL.AddOption(TEXT("game=/Script/Catfishing.CatfishingGameModeBase"));
	if (!World->SetGameMode(URL)) return false;
	FCatWaterGeometryBuildInput Input;
	Input.RegionId = TEXT("GatheringTestLake"); Input.WaterPointVerticalToleranceCm = 100;
	Input.BankHeightToleranceCm = 50; Input.BoundaryToleranceCm = 1; Input.MaxLandingCorrectionCm = 100; Input.MinimumWaterInsetCm = 1;
	FCatWaterPolygonBuildInput Polygon; Polygon.BoundaryId = TEXT("GatheringBoundary");
	Polygon.Vertices = {{-10000,-10000},{10000,-10000},{10000,10000},{-10000,10000}};
	Input.Boundaries.Add(Polygon);
	const auto Built = FCatWaterGeometry::Build(Input);
	if (!TestTrue(TEXT("water geometry builds"), Built.bSucceeded)) return false;
	auto* Region = World->SpawnActor<ACatWaterRegion>();
	if (!Region) return false;
	FCatWaterRegionTestAccess::InjectBakedGeometry(*Region, Built.Cache);
	if (!Wrapper.BeginPlayInTestWorld()) return false;
	auto* Mode = World->GetAuthGameMode<ACatfishingGameModeBase>();
	Mode->bRunCommandsOpen = true; Mode->RunPublicState.Phase.Phase = ECatRunPhase::DayActive;
	Mode->RunPublicState.Phase.bNewFishingBitesAllowed = true;
	auto* Fields = World->GetSubsystem<UCatChumFieldSubsystem>();
	const auto* Definition = LoadObject<UCatEquipmentDefinition>(nullptr, TEXT("/Game/Catfishing/Data/Equipment/Equip_Chum_Bug.Equip_Chum_Bug"));
	if (!Fields || !Definition || !Definition->FindFragment<UCatEquipmentFragment_Chum>()) return false;
	const auto Spec = Definition->FindFragment<UCatEquipmentFragment_Chum>()->ChumInfluence;
	const auto CountEvents = [&]() { int32 Count=0; for (TActorIterator<ACatFishGatheringActor> It(World); It; ++It) if (IsValid(*It)) ++Count; return Count; };
	const auto Place = [&](ECatChumFieldSource Source, FVector Point, bool bPublish = true, double Lifetime = 180.0)
	{
		FCatPrepareChumFieldRequest Request;
		Request.StableNetId = TEXT("GatheringAutomation"); Request.Command.RequestId = FGuid::NewGuid();
		Request.Command.ExpectedWaterRegionHandle = Built.Cache.Handle; Request.Command.ChumItemId = Definition->ItemId;
		Request.Command.Quantity = 1; Request.ServerCorrectedCenter = Point; Request.ServerTime = World->GetTimeSeconds();
		Request.Source = Source; Request.Influence = Spec; Request.Influence.DurationSeconds = Lifetime;
		const auto Prepared = Fields->PrepareField(Request);
		if (!TestTrue(TEXT("placement prepared"), Prepared.bPrepared)) return FCatPlaceChumResult{};
		const auto Result = Fields->ActivatePreparedFieldDeferred(Prepared.CommitToken);
		TestTrue(TEXT("placement committed"), Result.bCommitted);
		if (bPublish) { Fields->StoreTerminalResult(Request.StableNetId, Result); Fields->PublishActivatedField(Result.FieldId); }
		return Result;
	};
	Place(ECatChumFieldSource::NaturalEvent, FVector::ZeroVector);
	TestEqual(TEXT("natural source never triggers"), CountEvents(), 0);
	const auto Pending = Place(ECatChumFieldSource::Player, FVector::ZeroVector, false);
	Fields->PublishActivatedField(Pending.FieldId);
	TestEqual(TEXT("without terminal receipt publication cannot trigger"), CountEvents(), 0);
	Fields->ClearFieldsForRunTransition();
	Settings->GatheringTriggerProbability = 0.0;
	Place(ECatChumFieldSource::Player, FVector::ZeroVector);
	TestEqual(TEXT("zero probability never starts event"), CountEvents(), 0);
	Fields->ClearFieldsForRunTransition();
	Settings->GatheringTriggerProbability = 1.0;
	Settings->GatheringConcentrationThreshold = {0.9,0.4,100.0};
	Place(ECatChumFieldSource::Player, FVector::ZeroVector);
	TestEqual(TEXT("missing one class prevents start"), CountEvents(), 0);
	Fields->ClearFieldsForRunTransition();
	Settings->GatheringConcentrationThreshold = {0.9,0.4,0.1};

	// 使用生产 Session 的等待入口与事件订阅，检验补窝和事件消失都保留半程进度。
	auto* Session = World->SpawnActor<ACatFishingSession>();
	auto* Hook = World->SpawnActor<ACatFishingHookActor>();
	if (!Session || !Hook) return false;
	Session->Snapshot.FishingSessionId = FGuid::NewGuid(); Session->Snapshot.CastAttemptId = FGuid::NewGuid();
	Session->Snapshot.Phase = ECatFishingPhase::Waiting; Session->Snapshot.HookActor = Hook;
	Session->AttemptSnapshot.WaterRegion = Built.Cache.Handle;
	Session->AttemptSnapshot.ServerCorrectedLandingWorldPoint = FVector::ZeroVector;
	Session->bBiteWaitActive = true; Session->BiteWaitMultiplier = 1.0;
	Session->BiteWaitProgress.LastServerTime = World->GetTimeSeconds();
	Session->BiteWaitProgress.IntervalSeconds = 120; Session->BiteWaitProgress.RemainingFraction = 0.5;
	Fields->OnGatheringChanged.AddUObject(Session, &ACatFishingSession::HandleWaitingChumChanged);
	Fields->OnFieldActivated.AddUObject(Session, &ACatFishingSession::HandleWaitingChumChanged);
	const auto First = Place(ECatChumFieldSource::Player, FVector::ZeroVector);
	TestEqual(TEXT("qualifying committed player placement triggers once"), CountEvents(), 1);
	TestEqual(TEXT("event preserves waiting fraction"), Session->BiteWaitProgress.RemainingFraction, 0.5);
	TestEqual(TEXT("event divides actual interval by 2.5"), Session->BiteWaitProgress.IntervalSeconds, (150.0/13.0)/2.5, 1.e-6);
	TestEqual(TEXT("production probe timer uses remaining half"), double(World->GetTimerManager().GetTimerRemaining(Session->ProbeTimerHandle)), (75.0/13.0)/2.5, 1.e-5);
	const auto Sample = Fields->SampleChumAtPoint(FVector::ZeroVector, Built.Cache.Handle, World->GetTimeSeconds());
	TestEqual(TEXT("event does not add concentration"), Sample.EffectiveChumVector.Fishy, 1.0, 1.e-6);
	TestEqual(TEXT("sample carries event identity"), Sample.GatheringEventId, First.FieldId);
	Fields->PublishActivatedField(First.FieldId);
	TestEqual(TEXT("replayed publication has no second event"), CountEvents(), 1);
	for (TActorIterator<ACatFishGatheringActor> It(World); It; ++It)
	{
		TestTrue(TEXT("event is replicated and always relevant"), It->GetIsReplicated() && It->bAlwaysRelevant);
		TArray<UInstancedStaticMeshComponent*> Meshes; It->GetComponents(Meshes);
		TestTrue(TEXT("native presentation has range and denser moving fish shadows"), Meshes.Num() == 2 && Meshes[0]->GetInstanceCount() > 0 && Meshes[1]->GetInstanceCount() > 0);
	}
	Place(ECatChumFieldSource::Player, FVector(100,0,0));
	TestEqual(TEXT("overlapping active range cannot retrigger"), CountEvents(), 1);
	Place(ECatChumFieldSource::Player, FVector(4000,0,0));
	TestEqual(TEXT("independent distant range may trigger"), CountEvents(), 2);
	TestEqual(TEXT("outside all event circles receives no speed"), Fields->SampleChumAtPoint(FVector(2000,0,0), Built.Cache.Handle, World->GetTimeSeconds()).GatheringBiteSpeedMultiplier, 1.0);
	for (int32 Frame=0; Frame<110; ++Frame) Wrapper.TickTestWorld(0.01f);
	TestEqual(TEXT("events expire on timer"), CountEvents(), 0);
	TestTrue(TEXT("remaining progress survives event end"), Session->BiteWaitProgress.RemainingFraction > 0 && Session->BiteWaitProgress.RemainingFraction < 0.5);
	const auto EndSample = Fields->SampleChumAtPoint(FVector::ZeroVector, Built.Cache.Handle, World->GetTimeSeconds());
	double NormalInterval = 0;
	FCatFishingBiteTimingModel::TryComputeInterval({}, EndSample.EffectiveChumVector.Fishy + EndSample.EffectiveChumVector.Fragrant + EndSample.EffectiveChumVector.Fermented, 1.0, NormalInterval);
	Session->RefreshWaitingBiteClock();
	TestEqual(TEXT("event end restores actual normal interval"), Session->BiteWaitProgress.IntervalSeconds, NormalInterval, 1.e-6);
	Place(ECatChumFieldSource::Player, FVector::ZeroVector);
	TestEqual(TEXT("next qualified placement immediately retriggers without cooldown"), CountEvents(), 1);
	Session->StopWaitingBiteClock();
	Fields->ClearFieldsForRunTransition();
	TestEqual(TEXT("run transition clears gathering"), CountEvents(), 0);
	TestEqual(TEXT("run transition removes all concentration"), Fields->SampleChumAtPoint(FVector::ZeroVector, Built.Cache.Handle, World->GetTimeSeconds()).ContributingFieldCount, 0);
	Place(ECatChumFieldSource::Player, FVector::ZeroVector, true, 0.2);
	for (int32 Frame=0; Frame<30; ++Frame) Wrapper.TickTestWorld(0.01f);
	Fields->CleanupExpiredFields(World->GetTimeSeconds());
	TestEqual(TEXT("source field expiration does not shorten event"), CountEvents(), 1);
	const auto EmptyActive = Fields->SampleChumAtPoint(FVector::ZeroVector, Built.Cache.Handle, World->GetTimeSeconds());
	TestEqual(TEXT("event leaves expired concentration at zero"), EmptyActive.EffectiveChumVector.Fishy, 0.0);
	TestEqual(TEXT("event keeps speed until fixed end"), EmptyActive.GatheringBiteSpeedMultiplier, 2.5);
	Fields->ClearFieldsForRunTransition();
	bool bClearedDuringNotification = false;
	const auto Reentry = Fields->OnGatheringChanged.AddLambda([&](FGuid)
	{
		if (!bClearedDuringNotification) { bClearedDuringNotification = true; Fields->ClearFieldsForRunTransition(); }
	});
	Place(ECatChumFieldSource::Player, FVector::ZeroVector);
	Fields->OnGatheringChanged.Remove(Reentry);
	TestTrue(TEXT("consumer may end run during gathering notification"), bClearedDuringNotification);
	TestEqual(TEXT("reentrant cleanup leaves no gathering"), CountEvents(), 0);
	const auto* PublicFields = World->GetGameState<ACatfishingGameState>()->GetChumFieldReplicationFromAuthority();
	TestTrue(TEXT("publication cannot resurrect cleared field presentation"), PublicFields && PublicFields->GetPublicFields().IsEmpty());
	return !HasAnyErrors();
}

#endif
