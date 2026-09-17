#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Character/CatCharacter.h"
#include "Components/BoxComponent.h"
#include "Engine/LocalPlayer.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Environment/Tests/CatWaterTestFixtures.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/Actors/CatFishingHookActor.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/Integration/CatFishingPhysicalRodComponent.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "Inventory/CatBackPackComponent.h"
#include "OnlineSubsystemTypes.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingRodEscapeTest,
	"Catfishing.Unit.Fishing.RodEscape.TimeoutFallDragAndTargetedPickup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingRodEscapeTest::RunTest(const FString& Parameters)
{
	for (const bool bHeld : {false, true})
	for (const float Mass : {0.035f, 0.35f})
	{
		FTestWorldWrapper Wrapper;
		if (!Wrapper.CreateTestWorld(EWorldType::Game)) return false;
		Wrapper.ForwardErrorMessages(this);
		UWorld* World = Wrapper.GetTestWorld();
		FURL URL;
		URL.AddOption(TEXT("game=/Script/Catfishing.CatfishingGameModeBase"));
		if (!World->SetGameMode(URL) || !Wrapper.BeginPlayInTestWorld()) return false;
		auto* Mode = World->GetAuthGameMode<ACatfishingGameModeBase>();
		Mode->bRunCommandsOpen = true;
		Mode->RunPublicState.Phase.Phase = ECatRunPhase::DayActive;
		Mode->RunPublicState.Phase.bNewFishingBitesAllowed = true;
		auto* PC = World->SpawnActor<ACatfishingPlayerController>();
		auto* Player = World->SpawnActor<ACatfishingPlayerState>();
		auto* Cat = World->SpawnActor<ACatCharacter>(FVector(0, 0, 100), FRotator::ZeroRotator);
		if (!PC || !Player || !Cat) return false;
		PC->PlayerState = Player; Cat->SetPlayerState(Player); PC->Possess(Cat);
		const FUniqueNetIdRef NetId = FUniqueNetIdString::Create(TEXT("RodEscape"), FName(TEXT("CAT_TEST")));
		Player->SetUniqueId(FUniqueNetIdRepl(NetId));
		ACatfishingGameModeBase::FAdmissionRecord Admission;
		Admission.Phase = ACatfishingGameModeBase::EAdmissionPhase::Active;
		Admission.Controller = PC;
		Mode->AdmissionRecords.Add(ACatfishingGameModeBase::MakeStableNetIdKey(Player->GetUniqueId()), Admission);
		TStrongObjectPtr<ULocalPlayer> LocalPlayer(NewObject<ULocalPlayer>(GEngine));
		PC->SetPlayer(LocalPlayer.Get());
		PC->SetControlRotation(FRotator(15, 0, 0));
		auto* Ground = World->SpawnActor<AActor>();
		auto* Box = NewObject<UBoxComponent>(Ground);
		Ground->SetRootComponent(Box); Ground->AddInstanceComponent(Box);
		Box->InitBoxExtent(FVector(2000, 2000, 10));
		Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Box->SetCollisionResponseToAllChannels(ECR_Block); Box->RegisterComponent();
		Ground->SetActorLocation(FVector(0, 0, -10));
		for (int Frame = 0; Frame < 90; ++Frame) Wrapper.TickTestWorld(1.f / 120.f);
		auto* Equipment = Cat->GetEquipmentComponent();
		if (!Equipment->GrantEquipmentFromAuthority(FGuid::NewGuid(), Equipment->GetSnapshot().Revision, 37).bCommitted) return false;
		auto* Inventory = CastChecked<UCatBackPackComponent>(Cat->GetInventoryComponent());
		const FGuid ItemId = Equipment->GetSnapshot().RodItemInstanceId;
		const int32 Slot = Inventory->FindInventorySlotIndexFromInstanceId(ItemId);
		PC->RequestSelectQuickbarSlotFromInput(Slot);
		auto* Fishing = World->GetSubsystem<UCatFishingService>();
		auto* Rod = Fishing->FindRodOperatedBy(Player);
		if (!TestNotNull(TEXT("formal equipment deploys physical rod"), Rod)) return false;
		if (!bHeld) PC->ParkHeldRodFromInput();
		auto* Physical = Rod->GetPhysicalRodComponent();
		auto* Body = Rod->GetPhysicalRodBody();
		Body->SetMassOverrideInKg(NAME_None, Mass, true);
		const FTransform Before = Body->GetComponentTransform();
		for (const ECatFishingPhase EarlyPhase : {ECatFishingPhase::Waiting, ECatFishingPhase::Probe})
		{
			auto* Early = World->SpawnActor<ACatFishingSession>();
			Early->Snapshot.FishingSessionId = FGuid::NewGuid();
			Early->Snapshot.RodActor = Rod; Early->Snapshot.Phase = EarlyPhase;
			AddExpectedErrorPlain(TEXT("Outcome=ECatFishingOutcome::EmptyHook"), EAutomationExpectedErrorFlags::Contains, 1);
			TestTrue(TEXT("early hook still resolves normally"), Early->RequestHookFromAuthority(FGuid::NewGuid()).bCommitted);
			TestFalse(TEXT("early hook never drops rod"), Physical->RequiresTargetedPickup());
		}
		auto* Hook = World->SpawnActor<ACatFishingHookActor>();
		auto* Session = World->SpawnActor<ACatFishingSession>();
		Session->Snapshot.FishingSessionId = FGuid::NewGuid();
		Session->Snapshot.CastAttemptId = FGuid::NewGuid();
		Session->Snapshot.RodActor = Rod; Session->Snapshot.HookActor = Hook;
		Session->Snapshot.Phase = ECatFishingPhase::TrueBiteWindow;
		Session->AttemptSnapshot.ServerCorrectedLandingWorldPoint = FVector(1500, 0, 0);
		Hook->SetOwner(Rod); Hook->SetActorLocation(FVector(1500, 0, 0));
		Hook->InitializeAuthoritativeIdentity(Session->Snapshot.FishingSessionId, Session->Snapshot.CastAttemptId);
		AddExpectedErrorPlain(TEXT("Outcome=ECatFishingOutcome::HookWindowExpired"), EAutomationExpectedErrorFlags::Contains, 1);
		Session->HandleTrueBiteWindowExpired();
		TestEqual(TEXT("timeout resolves exactly once"), Session->GetSnapshot().Outcome, ECatFishingOutcome::HookWindowExpired);
		TestTrue(TEXT("timeout requires explicit pickup"), Physical->RequiresTargetedPickup());
		TestNull(TEXT("timeout releases primary"), Fishing->FindRodOperatedBy(Player));
		TestEqual(TEXT("same item survives"), Rod->GetPresentationState().ItemInstanceId, ItemId);
		TestTrue(TEXT("release starts from original pose"), Body->GetComponentTransform().Equals(Before, 0.01));
		TestTrue(TEXT("only held rod launches upward"), bHeld ? Body->GetPhysicsLinearVelocity().Z > 0 : Body->GetPhysicsLinearVelocity().IsNearlyZero());
		const auto Revision = Rod->GetPresentationState().RodActorRevision;
		Session->HandleTrueBiteWindowExpired();
		TestEqual(TEXT("duplicate timeout cannot rethrow"), Rod->GetPresentationState().RodActorRevision, Revision);
		TestTrue(TEXT("line survives session terminal cleanup"), IsValid(Hook) && Physical->OwnsEscapeHook(Hook));
		FCatOperateRodCommand Operate;
		Operate.Context.RequestId = FGuid::NewGuid(); Operate.Context.RodActorId = Rod->GetPresentationState().RodActorId;
		Operate.Context.ExpectedRodActorRevision = Revision;
		AddExpectedErrorPlain(TEXT("Event=fishing_rod_escape_pickup_rejected"), EAutomationExpectedErrorFlags::Contains, 1);
		TestFalse(TEXT("legacy nearest/quickbar operate cannot bypass E"), Fishing->OperateRod(PC, Operate).bCommitted);
		bool bSawDrag = false;
		FVector DragStart = FVector::ZeroVector;
		double MaxDragSpeed = 0;
		for (int Frame = 0; Frame < 1000 && Rod->GetPresentationState().EscapePhase != ECatFishingRodEscapePhase::Stopped; ++Frame)
		{
			Wrapper.TickTestWorld(1.f / 120.f);
			if (Rod->GetPresentationState().EscapePhase == ECatFishingRodEscapePhase::Dragging)
			{
				if (!bSawDrag) DragStart = Body->GetComponentLocation();
				bSawDrag = true;
				MaxDragSpeed = FMath::Max(MaxDragSpeed, Body->GetPhysicsLinearVelocity().Size2D());
			}
		}
		TestTrue(TEXT("fall reaches ground drag"), bSawDrag);
		TestTrue(TEXT("ground drag visibly travels at least half a metre"), FVector::Dist2D(DragStart, Body->GetComponentLocation()) > 50);
		TestTrue(TEXT("mass-independent drag cap"), MaxDragSpeed <= GetDefault<UCatFishingSettings>()->RodEscapeDragSpeed + 2);
		TestEqual(TEXT("bounded escape stops"), Rod->GetPresentationState().EscapePhase, ECatFishingRodEscapePhase::Stopped);
		TestTrue(TEXT("total displacement stays bounded"), FVector::Dist2D(Before.GetLocation(), Body->GetComponentLocation()) <= GetDefault<UCatFishingSettings>()->RodEscapeMaximumTravel + 2);
		TestTrue(TEXT("stopped rod lies on ground"), FMath::Abs(Body->GetForwardVector().Z) < 0.3);
		TestFalse(TEXT("stop destroys visual hook"), IsValid(Hook));
		const FVector Stopped = Body->GetComponentLocation();
		for (int Frame = 0; Frame < 12; ++Frame) Wrapper.TickTestWorld(1.f / 120.f);
		TestTrue(TEXT("no auto-parking after stop"), Body->GetComponentLocation().Equals(Stopped, 0.1));
		// Bring the player into the production interaction radius; the rod itself stays at its real resting pose.
		Cat->SetActorLocation(Rod->GetGripWorldTransform().GetLocation() + FVector(0, 60, 60));
		const FGuid Pickup = FGuid::NewGuid();
		TestTrue(TEXT("target E picks up the original item"), Rod->Interact_Implementation(PC, Pickup));
		TestEqual(TEXT("E restores primary"), Fishing->FindRodOperatedBy(Player), Rod);
		TestFalse(TEXT("E clears penalty"), Physical->RequiresTargetedPickup());
		TestEqual(TEXT("E reserves same item"), Inventory->GetQuickbarHeldSlot().ItemInstanceId, ItemId);
		TestTrue(TEXT("replayed E does not drop the rod"), Rod->Interact_Implementation(PC, Pickup));
		TestEqual(TEXT("replay preserves holder"), Fishing->FindRodOperatedBy(Player), Rod);
		// A fresh timeout can be intercepted while moving, without reviving the terminated cast.
		if (!bHeld) PC->ParkHeldRodFromInput();
		TestTrue(TEXT("second timeout starts"), Physical->BeginBiteTimeoutEscape(FGuid::NewGuid(), FVector(1500, 0, 0), nullptr));
		for (int Frame = 0; Frame < 180 && Rod->GetPresentationState().EscapePhase == ECatFishingRodEscapePhase::Falling; ++Frame)
			Wrapper.TickTestWorld(1.f / 120.f);
		TestEqual(TEXT("rod can be intercepted during drag"), Rod->GetPresentationState().EscapePhase, ECatFishingRodEscapePhase::Dragging);
		Cat->SetActorLocation(Rod->GetGripWorldTransform().GetLocation() + FVector(0, 60, 60));
		TestTrue(TEXT("E takes moving rod"), Rod->Interact_Implementation(PC, FGuid::NewGuid()));
		TestFalse(TEXT("pickup cancels drag immediately"), Physical->RequiresTargetedPickup());
		if (bHeld && Mass > 0.1f)
		{
			const double BankX = Rod->GetGripWorldTransform().GetLocation().X + 50;
			FCatWaterGeometryBuildInput WaterInput;
			WaterInput.RegionId = TEXT("RodEscapeWater");
			WaterInput.WaterPointVerticalToleranceCm = 100;
			WaterInput.BankHeightToleranceCm = 50;
			WaterInput.BoundaryToleranceCm = 1;
			WaterInput.MaxLandingCorrectionCm = 100;
			WaterInput.MinimumWaterInsetCm = 1;
			FCatWaterPolygonBuildInput Polygon;
			Polygon.BoundaryId = TEXT("EscapeBank");
			Polygon.Vertices = {{BankX, -2000}, {3000, -2000}, {3000, 2000}, {BankX, 2000}};
			WaterInput.Boundaries.Add(Polygon);
			const auto Baked = FCatWaterGeometry::Build(WaterInput);
			if (!TestTrue(TEXT("bank geometry valid"), Baked.bSucceeded)) return false;
			auto* Region = World->SpawnActorDeferred<ACatWaterRegion>(ACatWaterRegion::StaticClass(), FTransform::Identity);
			FCatWaterRegionTestAccess::InjectBakedGeometry(*Region, Baked.Cache);
			Region->FinishSpawning(FTransform::Identity);
			TestTrue(TEXT("bank escape starts"), Physical->BeginBiteTimeoutEscape(FGuid::NewGuid(), FVector(1500, 0, 0), nullptr));
			for (int Frame = 0; Frame < 1000 && Rod->GetPresentationState().EscapePhase != ECatFishingRodEscapePhase::Stopped; ++Frame)
				Wrapper.TickTestWorld(1.f / 120.f);
			TestEqual(TEXT("bank stops escape"), Rod->GetPresentationState().EscapePhase, ECatFishingRodEscapePhase::Stopped);
			TestTrue(TEXT("grip stays reachable on dry side"), Rod->GetGripWorldTransform().GetLocation().X < BankX);
			TestTrue(TEXT("bank recovery lies down instead of freezing airborne"), FMath::Abs(Body->GetForwardVector().Z) < 0.3 && Body->GetComponentLocation().Z < 10);
		}
		AddInfo(FString::Printf(TEXT("Held=%d MassKg=%.3f DragObserved=%d MaxDragSpeedCmS=%.3f"), bHeld, Mass, bSawDrag, MaxDragSpeed));
	}
	return !HasAnyErrors();
}
#endif
