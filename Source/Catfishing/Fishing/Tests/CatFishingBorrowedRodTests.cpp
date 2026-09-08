#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Character/CatCharacter.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Environment/Tests/CatWaterTestFixtures.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Fishing/Actors/CatFishingHookActor.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Presentation/CatFishingPresentationSettings.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "OnlineSubsystemTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingBorrowedRodCastTest,
	"Catfishing.Unit.Fishing.Service.BorrowedRodCastUsesCallerBaitAndPreservesOwnerInstance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingBorrowedRodCastTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	// 独立 World 验证取消、接力后两宿主离场，以及预留广播中两宿主失去占有。
	// 仅注入入场身份/测试岸线；装备、正式 Actor/StateTree 和会话事务均走生产入口。
	for (int32 ExitScenario = 0; ExitScenario < 5; ++ExitScenario)
	{
		UCatEquipmentSettings* EquipmentSettings = GetMutableDefault<UCatEquipmentSettings>();
		TGuardValue<bool> NoStarterNet(EquipmentSettings->bAutoGrantStarterScoopNet, false);
		FTestWorldWrapper Wrapper;
		if (!TestTrue(TEXT("creates borrowed-rod authority world"), Wrapper.CreateTestWorld(EWorldType::Game))) return false;
		Wrapper.ForwardErrorMessages(this);
		UWorld* World = Wrapper.GetTestWorld();
		FURL URL;
		URL.AddOption(TEXT("game=/Script/Catfishing.CatfishingGameModeBase"));
		if (!TestTrue(TEXT("creates real project game mode"), World->SetGameMode(URL))) return false;

		FCatWaterGeometryBuildInput Geometry;
		Geometry.RegionId = TEXT("BorrowedRodWater");
		Geometry.WaterPointVerticalToleranceCm = 100.0;
		Geometry.BankHeightToleranceCm = 50.0;
		Geometry.BoundaryToleranceCm = 1.0;
		Geometry.MaxLandingCorrectionCm = 100.0;
		Geometry.MinimumWaterInsetCm = 1.0;
		FCatWaterPolygonBuildInput& Boundary = Geometry.Boundaries.AddDefaulted_GetRef();
		Boundary.BoundaryId = TEXT("BorrowedRodShore");
		Boundary.Vertices = {{400, -1200}, {1800, -1200}, {1800, 1200}, {400, 1200}};
		const auto Baked = FCatWaterGeometry::Build(Geometry);
		if (!TestTrue(TEXT("bakes real water geometry"), Baked.bSucceeded)) return false;
		ACatWaterRegion* Region = World->SpawnActor<ACatWaterRegion>();
		if (!TestNotNull(TEXT("spawns water region"), Region)) return false;
		FCatWaterRegionTestAccess::InjectBakedGeometry(*Region, Baked.Cache);
		if (!TestTrue(TEXT("starts world and registers baked water"), Wrapper.BeginPlayInTestWorld())) return false;
		UCatFishingService* Fishing = World->GetSubsystem<UCatFishingService>();
		ACatfishingGameModeBase* GameMode = World->GetAuthGameMode<ACatfishingGameModeBase>();
		if (!Fishing || !GameMode) return false;
		GameMode->bRunCommandsOpen = true;
		GameMode->RunPublicState.Phase.Phase = ECatRunPhase::DayActive;
		GameMode->RunPublicState.Phase.bFishingAllowed = true;

		AActor* Ground = World->SpawnActor<AActor>();
		UBoxComponent* GroundBox = NewObject<UBoxComponent>(Ground);
		Ground->SetRootComponent(GroundBox);
		Ground->AddInstanceComponent(GroundBox);
		GroundBox->InitBoxExtent(FVector(300.0, 1000.0, 10.0));
		GroundBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		GroundBox->SetCollisionResponseToAllChannels(ECR_Block);
		GroundBox->RegisterComponent();
		Ground->SetActorLocation(FVector(0.0, 0.0, -10.0));

		struct FPlayer
		{
			ACatfishingPlayerController* Controller = nullptr;
			ACatfishingPlayerState* State = nullptr;
			ACatCharacter* Character = nullptr;
			UCatEquipmentComponent* Equipment = nullptr;
		};
		const auto MakePlayer = [&](const TCHAR* Name, const FVector& Position)
		{
			FPlayer Result;
			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Result.Controller = World->SpawnActor<ACatfishingPlayerController>();
			Result.State = World->SpawnActor<ACatfishingPlayerState>();
			Result.Character = World->SpawnActor<ACatCharacter>(Position, FRotator::ZeroRotator, Spawn);
			if (!Result.Controller || !Result.State || !Result.Character) return Result;
			Result.Controller->PlayerState = Result.State;
			Result.Character->SetPlayerState(Result.State);
			Result.Controller->Possess(Result.Character);
			Result.Controller->SetControlRotation(FRotator::ZeroRotator);
			const FUniqueNetIdRef UniqueId = FUniqueNetIdString::Create(Name, FName(TEXT("CAT_TEST")));
			Result.State->SetUniqueId(FUniqueNetIdRepl(UniqueId));
			ACatfishingGameModeBase::FAdmissionRecord Admission;
			Admission.Phase = ACatfishingGameModeBase::EAdmissionPhase::Active;
			Admission.Controller = Result.Controller;
			GameMode->AdmissionRecords.Add(ACatfishingGameModeBase::MakeStableNetIdKey(Result.State->GetUniqueId()), Admission);
			Result.Equipment = Result.Character->GetEquipmentComponent();
			UCatAbilitySystemComponent* ASC = Result.Character->GetCatAbilitySystemComponent();
			if (!ASC)
			{
				Result.Equipment = nullptr;
				return Result;
			}
			ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 10.0f);
			ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 100.0f);
			return Result;
		};
		const FPlayer Owner = MakePlayer(TEXT("BorrowedRodOwner"), FVector(0.0, 0.0, 100.0));
		const FPlayer Caster = MakePlayer(TEXT("BorrowedRodCaster"), FVector(0.0, 120.0, 100.0));
		const FPlayer Relay = MakePlayer(TEXT("BorrowedRodRelay"), FVector(-100.0, 250.0, 100.0));
		for (const FPlayer& Player : {Owner, Caster, Relay})
		{
			if (!TestTrue(TEXT("real player passes production fishing gate"), Player.Equipment
				&& GameMode->CanAcceptFishingCommand(Player.Controller))) return false;
		}
		if (!TestTrue(TEXT("owner obtains a formal T2 rod"), Owner.Equipment->GrantEquipmentFromAuthority(
			FGuid::NewGuid(), Owner.Equipment->GetSnapshot().Revision, TEXT("ShopRodT2")).bCommitted)) return false;
		for (const FName DefinitionId : {FName(TEXT("StarterRodT1")), FName(TEXT("FeatherFloat"))})
		{
			if (!TestTrue(TEXT("caster obtains their own different rod and float"), Caster.Equipment->GrantEquipmentFromAuthority(
				FGuid::NewGuid(), Caster.Equipment->GetSnapshot().Revision, DefinitionId).bCommitted)) return false;
		}
		if (!TestTrue(TEXT("caster obtains four actual bait portions"), Caster.Equipment->GrantInventoryQuantityFromAuthority(
			FGuid::NewGuid(), Caster.Equipment->GetSnapshot().Revision, TEXT("BugBait"), 4).bCommitted)) return false;
		const auto Quantity = [](const UCatEquipmentComponent* Equipment, FName DefinitionId)
		{
			int32 Total = 0;
			for (const FCatRunInventorySlot& Slot : Equipment->GetSnapshot().InventorySlots)
				if (Slot.DefinitionId == DefinitionId) Total += Slot.Quantity;
			return Total;
		};
		const FCatEquipmentLoadoutSnapshot OwnerBeforeDeploy = Owner.Equipment->GetSnapshot();
		const FGuid OwnerRodId = OwnerBeforeDeploy.RodItemInstanceId;
		const FCatEquipmentLoadoutSnapshot CasterBefore = Caster.Equipment->GetSnapshot();
		FCatPlaceRodCommand Place;
		Place.RequestId = FGuid::NewGuid();
		Place.ExpectedEquipmentRevision = OwnerBeforeDeploy.Revision;
		const FCatFishingCommandResult Placed = Fishing->PlaceRod(Owner.Controller, Place);
		if (!TestTrue(TEXT("production PlaceRod deploys owner's physical rod"), Placed.bCommitted)) return false;
		ACatFishingRodActor* Rod = Fishing->FindDeployedRodById(Placed.RodActorId);
		if (!TestNotNull(TEXT("placed rod is registered"), Rod)) return false;
		const UCatEquipmentDefinition* RodDefinition = EquipmentSettings->FindRuntimeDefinition(TEXT("ShopRodT2"));
		TestEqual(TEXT("rod is the formal configured Blueprint"), Rod->GetClass(), RodDefinition->UseActorClass.Get());
		TestEqual(TEXT("physical ledger owner is frozen in deployment Instigator"), Rod->GetInstigator(), static_cast<APawn*>(Owner.Character));
		const auto Context = [&]()
		{
			FCatRodCommandContext Result;
			Result.RequestId = FGuid::NewGuid();
			Result.RodActorId = Rod->GetPresentationState().RodActorId;
			Result.ExpectedRodActorRevision = Rod->GetPresentationState().RodActorRevision;
			return Result;
		};
		FCatLeaveRodCommand Leave;
		Leave.Context = Context();
		if (!TestTrue(TEXT("owner leaves the deployed empty rod"), Fishing->LeaveRod(Owner.Controller, Leave).bCommitted)) return false;
		Owner.Character->SetActorLocation(FVector(-100.0, -150.0, 100.0));
		FCatOperateRodCommand Operate;
		Operate.Context = Context();
		if (!TestTrue(TEXT("another player takes the primary rod position"), Fishing->OperateRod(Caster.Controller, Operate).bCommitted)) return false;
		TestEqual(TEXT("borrowing keeps deployment ownership"), Rod->GetPresentationState().OwnerPlayerState.Get(), static_cast<APlayerState*>(Owner.State));
		TestEqual(TEXT("borrowing does not consume caster deployment slots"), Fishing->GetDeployedRodCount(Caster.State), 0);
		const auto CastCommand = [&]()
		{
			FCatBeginCastCommand Result;
			Result.RequestId = FGuid::NewGuid();
			Result.RodActorId = Rod->GetPresentationState().RodActorId;
			Result.ExpectedRodActorRevision = Rod->GetPresentationState().RodActorRevision;
			Result.ExpectedEquipmentRevision = Caster.Equipment->GetSnapshot().Revision;
			Result.ClientCandidateWorldPoint = FVector(650.0, 120.0, 0.0);
			Result.ExpectedWaterRegionHandle = Baked.Cache.Handle;
			return Result;
		};

		if (ExitScenario >= 3)
		{
			const FPlayer& Exiting = ExitScenario == 3 ? Caster : Owner;
			bool bExitTriggered = false;
			const FDelegateHandle ExitObserver = Exiting.Equipment->OnSnapshotChanged.AddLambda([&]()
			{
				if (bExitTriggered) return;
				bExitTriggered = true;
				TestTrue(TEXT("exit callback observes both committed resource reservations"),
					Caster.Equipment->HasActiveFishingUse() && Owner.Equipment->HasActiveFishingUse());
				TestEqual(TEXT("exit occurs before the new session is registered"), Fishing->GetTrackedSessionCountForDiagnostics(), 0);
				Exiting.Controller->UnPossess();
			});
			const FCatBeginCastCommand InterruptedCommand = CastCommand();
			AddExpectedErrorPlain(TEXT("Stage=EquipmentReservationPublication"), EAutomationExpectedErrorFlags::Contains, 1);
			const FCatBeginCastResult Interrupted = Fishing->BeginCast(Caster.Controller, InterruptedCommand);
			Exiting.Equipment->OnSnapshotChanged.Remove(ExitObserver);
			TestTrue(TEXT("inventory publication triggered the real UnPossess path"), bExitTriggered);
			TestNull(TEXT("exiting controller no longer possesses a pawn"), Exiting.Controller->GetPawn());
			TestFalse(TEXT("publication-time exit aborts the pending cast"), Interrupted.Command.bCommitted);
			TestEqual(TEXT("publication-time exit reports a dependency interruption"), Interrupted.Command.Error,
				ECatFishingCommandError::DependencyUnavailable);
			TestEqual(TEXT("publication-time exit refunds caller bait"), Quantity(Caster.Equipment, TEXT("BugBait")), 4);
			TestFalse(TEXT("publication-time exit releases caller reservation"), Caster.Equipment->HasActiveFishingUse());
			TestFalse(TEXT("publication-time exit releases owner rod lock"), Owner.Equipment->HasActiveFishingUse());
			FCatInventoryEndpointSnapshot ReturnedLock;
			TestEqual(TEXT("publication-time exit leaves original physical rod available for recall"),
				Owner.Equipment->ReadInventoryTransferEndpoint(TEXT("ActiveUse"), OwnerRodId, ReturnedLock), ECatDomainCommandError::None);
			TestEqual(TEXT("publication-time exit leaves no registered session"), Fishing->GetTrackedSessionCountForDiagnostics(), 0);
			int32 HookCount = 0;
			for (TActorIterator<ACatFishingHookActor> It(World); It; ++It) ++HookCount;
			TestEqual(TEXT("publication-time exit is detected before spawning a hook"), HookCount, 0);
			const int64 InterruptedRevision = Caster.Equipment->GetSnapshot().Revision;
			const auto Replay = Fishing->BeginCast(Caster.Controller, InterruptedCommand);
			TestEqual(TEXT("interrupted request replay preserves the first rollback revision"), Replay.Command.EquipmentRevision,
				Interrupted.Command.EquipmentRevision);
			TestEqual(TEXT("interrupted request replay never duplicates the refund"), Caster.Equipment->GetSnapshot().Revision, InterruptedRevision);
			AddInfo(FString::Printf(TEXT("Event=borrowed_rod_service_publication_exit_verified Scenario=%d RequestId=%s CallerBait=%d Sessions=%d Hooks=%d"),
				ExitScenario, *InterruptedCommand.RequestId.ToString(), Quantity(Caster.Equipment, TEXT("BugBait")),
				Fishing->GetTrackedSessionCountForDiagnostics(), HookCount));
			continue;
		}

		// 故障发生在装备预留之后，证明 rollback 同时归还抛钩者的饵并解除原竿实例的锁。
		UCatFishingPresentationSettings* Presentation = GetMutableDefault<UCatFishingPresentationSettings>();
		const FCatBeginCastCommand FailedCommand = CastCommand();
		{
			TGuardValue<TSoftClassPtr<ACatFishingHookActor>> MissingHook(Presentation->HookActorClass, TSoftClassPtr<ACatFishingHookActor>());
			AddExpectedErrorPlain(TEXT("Stage=HookClass"), EAutomationExpectedErrorFlags::Contains, 1);
			const FCatBeginCastResult Failed = Fishing->BeginCast(Caster.Controller, FailedCommand);
			TestFalse(TEXT("post-reservation missing hook does not commit"), Failed.Command.bCommitted);
			TestEqual(TEXT("post-reservation failure retains dependency diagnosis"), Failed.Command.Error, ECatFishingCommandError::DependencyUnavailable);
			TestEqual(TEXT("failed cast restores caller bait"), Quantity(Caster.Equipment, TEXT("BugBait")), 4);
			TestFalse(TEXT("failed cast closes caller reservation"), Caster.Equipment->HasActiveFishingUse());
			FCatInventoryEndpointSnapshot AvailableRod;
			TestEqual(TEXT("failed cast releases original rod transfer lock"), Owner.Equipment->ReadInventoryTransferEndpoint(
				TEXT("ActiveUse"), OwnerRodId, AvailableRod), ECatDomainCommandError::None);
			const int64 AfterFailureRevision = Caster.Equipment->GetSnapshot().Revision;
			const auto Replay = Fishing->BeginCast(Caster.Controller, FailedCommand);
			TestEqual(TEXT("failed request replays first rollback revision"), Replay.Command.EquipmentRevision, Failed.Command.EquipmentRevision);
			TestEqual(TEXT("failed replay does not reserve and refund twice"), Caster.Equipment->GetSnapshot().Revision, AfterFailureRevision);
			TestEqual(TEXT("failed cast leaves no active session"), Fishing->GetTrackedSessionCountForDiagnostics(), 0);
		}

		const FCatBeginCastCommand Begin = CastCommand();
		const FCatBeginCastResult CastResult = Fishing->BeginCast(Caster.Controller, Begin);
		if (!TestTrue(TEXT("borrowing an empty rod can create a real cast session"), CastResult.Command.bCommitted)) return false;
		ACatFishingSession* Session = Fishing->FindSession(CastResult.Command.FishingSessionId);
		if (!TestNotNull(TEXT("production session is registered"), Session)) return false;
		ACatFishingHookActor* Hook = Session->GetSnapshot().HookActor;
		if (!TestNotNull(TEXT("production session owns a spawned hook"), Hook)) return false;
		TestEqual(TEXT("hook is the formal configured Blueprint"), Hook->GetClass(), Presentation->HookActorClass.Get());
		TestEqual(TEXT("hook montage Instigator belongs to the caster"), Hook->GetInstigator(), static_cast<APawn*>(Caster.Character));
		TestEqual(TEXT("hook identity belongs to the new cast"), Hook->GetPresentationState().FishingSessionId, CastResult.Command.FishingSessionId);
		TestEqual(TEXT("caster bait is reserved exactly once"), Quantity(Caster.Equipment, TEXT("BugBait")), 3);
		TestEqual(TEXT("rod owner did not need or lose bait"), Quantity(Owner.Equipment, TEXT("BugBait")), 0);
		TestEqual(TEXT("owner's T2 durability initializes the session"), Session->GetSnapshot().RodDurabilityRemaining, OwnerBeforeDeploy.RodDurability);
		TestEqual(TEXT("caster's other selected rod remains unchanged"), Caster.Equipment->GetSnapshot().RodDurability, CasterBefore.RodDurability);
		const int64 BoundRevision = Caster.Equipment->GetSnapshot().Revision;
		const auto Replayed = Fishing->BeginCast(Caster.Controller, Begin);
		TestEqual(TEXT("same request replays same session"), Replayed.Command.FishingSessionId, CastResult.Command.FishingSessionId);
		TestEqual(TEXT("same request cannot reserve a second bait"), Caster.Equipment->GetSnapshot().Revision, BoundRevision);
		TestEqual(TEXT("same rod has only one registered session"), Fishing->GetTrackedSessionCountForDiagnostics(), 1);
		FCatInventoryEndpointSnapshot LockedRod;
		TestEqual(TEXT("active borrowed session locks owner's ActiveUse transfer"), Owner.Equipment->ReadInventoryTransferEndpoint(
			TEXT("ActiveUse"), OwnerRodId, LockedRod), ECatDomainCommandError::InvalidPhase);
		FCatPackRodCommand BorrowedPack;
		BorrowedPack.Context = Context();
		BorrowedPack.Context.ExpectedEquipmentRevision = Caster.Equipment->GetSnapshot().Revision;
		AddExpectedErrorPlain(TEXT("Event=fishing_rod_pack_rejected"), EAutomationExpectedErrorFlags::Contains, 1);
		TestEqual(TEXT("borrowing does not grant permission to take ownership by packing"), Fishing->PackRod(
			Caster.Controller, BorrowedPack).Error, ECatFishingCommandError::NotFisher);

		// 真实定时器推进正式 Hook BP；没有直接伪造落水回调或 Session 阶段。
		const double FlightDuration = Hook->GetPresentationState().CastTrajectory.DurationSeconds;
		for (int32 Frame = 0; Frame < FMath::CeilToInt((FlightDuration + 0.1) / 0.01); ++Frame) Wrapper.TickTestWorld(0.01f);
		TestEqual(TEXT("formal hook actually lands after flight"), Hook->GetPresentationState().Phase, ECatFishingHookPresentationPhase::Landed);
		TestFalse(TEXT("landed borrowed-rod session remains active"), Session->IsTerminal());
		TestTrue(TEXT("formal hook reaches the server corrected water point"), Hook->GetActorLocation().Equals(CastResult.ServerCorrectedLandingWorldPoint, 1.0));

		if (ExitScenario == 0)
		{
			const FGuid CancelRequest = FGuid::NewGuid();
			AddExpectedErrorPlain(TEXT("Outcome=ECatFishingOutcome::Cancelled"), EAutomationExpectedErrorFlags::Contains, 1);
			const auto Cancelled = Session->CancelFromAuthority(CancelRequest);
			TestTrue(TEXT("borrowed cast can be cancelled"), Cancelled.bCommitted);
			const int64 CancelRevision = Caster.Equipment->GetSnapshot().Revision;
			TestTrue(TEXT("cancel retry replays committed result"), Session->CancelFromAuthority(CancelRequest).bCommitted);
			TestEqual(TEXT("cancel retry cannot duplicate returned bait"), Caster.Equipment->GetSnapshot().Revision, CancelRevision);
		}
		else
		{
			Leave.Context = Context();
			if (!TestTrue(TEXT("caster leaves without ending their reservation"), Fishing->LeaveRod(Caster.Controller, Leave).bCommitted)) return false;
			Relay.Character->SetActorLocation(FVector(0.0, 150.0, 100.0));
			Operate.Context = Context();
			if (!TestTrue(TEXT("third player takes over the existing borrowed session"), Fishing->OperateRod(Relay.Controller, Operate).bCommitted)) return false;
			TestEqual(TEXT("session follows the new primary player"), Session->GetSnapshot().FisherPlayerState.Get(), static_cast<APlayerState*>(Relay.State));
			TestEqual(TEXT("takeover does not reserve more original caller bait"), Quantity(Caster.Equipment, TEXT("BugBait")), 3);
			TestFalse(TEXT("relay does not acquire an equipment reservation"), Relay.Equipment->HasActiveFishingUse());
			AddExpectedErrorPlain(TEXT("Reason=\"Character unavailable\""), EAutomationExpectedErrorFlags::Contains, 1);
			Fishing->TerminateSessionsForCharacter(ExitScenario == 1 ? Owner.Character : Caster.Character);
			TestTrue(TEXT("a frozen equipment host leaving terminates the relayed session"), Session->IsTerminal());
		}
		TestEqual(TEXT("closing before a bite returns bait to original caller"), Quantity(Caster.Equipment, TEXT("BugBait")), 4);
		TestFalse(TEXT("terminal closes original caller reservation"), Caster.Equipment->HasActiveFishingUse());
		FCatInventoryEndpointSnapshot ReleasedRod;
		TestEqual(TEXT("terminal releases the original rod's inventory transfer lock"), Owner.Equipment->ReadInventoryTransferEndpoint(
			TEXT("ActiveUse"), OwnerRodId, ReleasedRod), ECatDomainCommandError::None);
		if (!TestEqual(TEXT("original owner still holds exactly one active physical rod"), ReleasedRod.Slots.Num(), 1)) return false;
		TestEqual(TEXT("physical rod identity never changes"), ReleasedRod.Slots[0].ItemInstanceId, OwnerRodId);
		TestEqual(TEXT("unused physical rod durability survives rollback and cancellation"), ReleasedRod.Slots[0].RodDurability, OwnerBeforeDeploy.RodDurability);
		TestFalse(TEXT("borrowed rod was never copied to the caller inventory"), Caster.Equipment->GetSnapshot().InventorySlots.ContainsByPredicate(
			[OwnerRodId](const FCatRunInventorySlot& Slot) { return Slot.ItemInstanceId == OwnerRodId; }));
		AddInfo(FString::Printf(TEXT("Event=borrowed_rod_service_verified Scenario=%d SessionId=%s RodItemInstanceId=%s CallerBait=%d OwnerDurability=%.3f"),
			ExitScenario, *CastResult.Command.FishingSessionId.ToString(), *OwnerRodId.ToString(),
			Quantity(Caster.Equipment, TEXT("BugBait")), ReleasedRod.Slots[0].RodDurability));
	}
	return !HasAnyErrors();
}

#endif
