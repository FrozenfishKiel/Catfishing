#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Environment/CatWaterTypes.h"
#include "Fishing/Integration/CatFishingCommandTypes.h"
#include "Logging/CatLogContext.h"
#include "Character/CatCharacter.h"
#include "Fishing/Integration/CatFishingPhysicalRodComponent.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Simulation/CatFishingFightRunner.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopeLock.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingDiagnosticLogContractTest,
	"Catfishing.Unit.Fishing.Diagnostics.StructuredContextPreservesFailureFacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingDiagnosticLogContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCatWaterSpatialResult Spatial;
	Spatial.bSucceeded = false;
	Spatial.Error = ECatWaterQueryError::HeightOutOfTolerance;
	Spatial.Containment = ECatWaterContainment::Boundary;
	Spatial.WaterRegion.RegionId = TEXT("Lake_A");
	Spatial.WaterRegion.GeometryRevision = 42;
	Spatial.VerticalDeltaCm = 96.25;
	Spatial.SignedDistanceToShoreCm = -12.5;
	Spatial.NearestShoreKind = ECatWaterShoreKind::OuterBoundary;
	Spatial.NearestShoreWorldPoint = FVector(10.0, 20.0, 30.0);
	Spatial.WaterSurfaceWorldPoint = FVector(40.0, 50.0, 60.0);

	const FString WaterFields = CatLogContext::BuildWaterSpatialFields(
		TEXT("CenterWater"), FVector(1.0, 2.0, 3.0), Spatial);
	TestTrue(TEXT("水域日志保留查询错误枚举"), WaterFields.Contains(TEXT("CenterWaterError=ECatWaterQueryError::HeightOutOfTolerance")));
	TestTrue(TEXT("水域日志保留闭集分类"), WaterFields.Contains(TEXT("CenterWaterContainment=ECatWaterContainment::Boundary")));
	TestTrue(TEXT("水域日志保留 Region 与几何版本"),
		WaterFields.Contains(TEXT("CenterWaterRegion=Lake_A CenterWaterGeometryRevision=42")));
	TestTrue(TEXT("水域日志保留垂直差与岸距"),
		WaterFields.Contains(TEXT("CenterWaterVerticalDeltaCm=96.250 CenterWaterSignedShoreDistanceCm=-12.500")));

	const FString MissingControllerFields = CatLogContext::BuildControllerFields(nullptr);
	TestTrue(TEXT("缺 Controller 仍输出稳定字段"), MissingControllerFields.Contains(TEXT("Controller=None")));
	TestTrue(TEXT("缺身份明确标记 Invalid"), MissingControllerFields.Contains(TEXT("StableNetId=Invalid")));
	TestTrue(TEXT("缺 Controller 明确标记非本地"), MissingControllerFields.Contains(TEXT("IsLocalController=false")));

	TestEqual(TEXT("抄网策略/几何拒绝不再伪装成依赖缺失"),
		MapDomainCommandError(ECatDomainCommandError::PolicyUndecided),
		ECatFishingCommandError::ScoopGeometryFailed);
	return !HasAnyErrors();
}

namespace
{
	class FFishingMotionLogCapture : public FOutputDevice
	{
	public:
		virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Category != TEXT("LogCatFishing")) return;
			FScopeLock Lock(&Mutex);
			Lines.Add(Message);
		}
		TArray<FString> Snapshot() const { FScopeLock Lock(&Mutex); return Lines; }
		void Reset() { FScopeLock Lock(&Mutex); Lines.Reset(); }
		int32 Count(const TCHAR* Event) const
		{
			FScopeLock Lock(&Mutex);
			return Lines.FilterByPredicate([&](const FString& Line) { return Line.Contains(Event); }).Num();
		}
	private:
		mutable FCriticalSection Mutex;
		TArray<FString> Lines;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingMotionDiagnosticTest,
	"Catfishing.Unit.Fishing.Diagnostics.PhysicalObserverLogsPreservePhaseRandomness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingMotionDiagnosticTest::RunTest(const FString& Parameters)
{
	IConsoleVariable* MotionLog = IConsoleManager::Get().FindConsoleVariable(TEXT("cat.Fishing.MotionLog"));
	if (!TestNotNull(TEXT("development motion logging switch exists"), MotionLog)) return false;
	const int32 Original = MotionLog->GetInt();
	ON_SCOPE_EXIT { MotionLog->Set(Original, ECVF_SetByCode); };
	TArray<double> PreviousDurations;
	int32 PreviousSeed = 0;
	for (const int32 Detailed : {0, 1})
	{
		MotionLog->Set(Detailed, ECVF_SetByCode);
		FTestWorldWrapper Wrapper;
		if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
		UWorld* World = Wrapper.GetTestWorld();
		auto* Player = World->SpawnActor<APlayerState>();
		auto* Rod = World->SpawnActor<ACatFishingRodActor>();
		auto* Session = World->SpawnActor<ACatFishingSession>();
		Rod->InitializeAuthoritativeIdentity(FGuid::NewGuid(), FGuid::NewGuid(), TEXT("DiagnosticRod"), TEXT("Skin"), Player, nullptr, true, false);
		if (!Wrapper.BeginPlayInTestWorld()) return false;
		FFishingMotionLogCapture Capture;
		GLog->FlushThreadedLogs();
		GLog->AddOutputDevice(&Capture);
		ON_SCOPE_EXIT { GLog->RemoveOutputDevice(&Capture); };
		const FTransform ActualPose = Rod->GetActorTransform();
		Rod->CarrierConstraintState.bFightActive = true;
		Rod->OnRep_CarrierConstraintState();
		GLog->FlushThreadedLogs();
		TestTrue(TEXT("receipt records the physical receiver and authority domain"), Capture.Snapshot().ContainsByPredicate([](const FString& Line)
			{ return Line.Contains(TEXT("Event=fishing_fight_constraint_received")) && Line.Contains(TEXT("SnapshotHolder=")); }));
		TestTrue(TEXT("a receipt never drives the physical pose"), Rod->GetActorTransform().Equals(ActualPose, 1.e-8));
		auto* Runner = NewObject<UCatFishingFightRunner>(Session);
		Runner->Session = Session; Runner->RodActor = Rod; Runner->bInitialized = Runner->bRunning = true;
		Runner->InitialFishStamina = Runner->State.FishStamina = 100.0;
		Runner->SteeringConfig.EaseOffDurationRangeSeconds = FVector2D(2.0, 4.0);
		Runner->SteeringConfig.OutwardDurationRangeSeconds = FVector2D(3.0, 5.0);
		Runner->SteeringRandom.Initialize(314159);
		TArray<double> Durations;
		for (const auto Phase : {ECatFishBehavior::EaseOff, ECatFishBehavior::OutwardRush})
		{
			TestTrue(TEXT("real behavior entry accepts configured duration ranges"), Runner->BeginFishBehaviorFromStateTree(Phase));
			Durations.Add(Runner->SteeringState.BehaviorDurationSeconds);
		}
		GLog->FlushThreadedLogs();
		TestEqual(TEXT("each phase is recorded with default diagnostics"), Capture.Count(TEXT("Event=fishing_behavior_phase_entered")), 2);
		if (Detailed)
		{
			TestTrue(TEXT("logging does not change random phase duration"), Durations == PreviousDurations);
			TestEqual(TEXT("logging consumes no additional phase random values"), Runner->SteeringRandom.GetCurrentSeed(), PreviousSeed);
		}
		else { PreviousDurations = Durations; PreviousSeed = Runner->SteeringRandom.GetCurrentSeed(); }
		Runner->bRunning = false;
	}
	return !HasAnyErrors();
}

#endif
