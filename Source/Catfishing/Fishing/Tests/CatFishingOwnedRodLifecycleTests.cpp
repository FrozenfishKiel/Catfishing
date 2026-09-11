#include "Inventory/CatInventorySettings.h"
#include "Fishing/Tests/CatFishingEquipmentTestFixtures.h"
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Tests/AutomationCommon.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Fishing/Integration/CatFishingPhysicalRodComponent.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Components/SphereComponent.h"
#include "Components/BoxComponent.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "EngineUtils.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Environment/Tests/CatWaterTestFixtures.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatFishingResourceCustodian.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Fishing/Actors/CatFishingHookActor.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Presentation/CatFishingPresentationSettings.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "Inventory/CatInventoryComponent.h"
#include "OnlineSubsystemTypes.h"
#include "UI/CatFishingViewBridge.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingOwnedRodLifecycleTest,
	"Catfishing.Unit.Fishing.Service.OwnedRodTransactionsSurviveCancellationAndOwnerDeparture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingOwnedRodLifecycleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FName CatalogWaterRegion;
	const auto* Catalog = GetDefault<UCatFishCatalogSettings>();
	for (const auto& Reference : Catalog->Definitions)
	{
		const UCatFishDefinition* Definition = Reference.LoadSynchronous();
		if (Definition && Catalog->FindRuntimeDefinition(Definition->FishDefinitionId) == Definition
			&& !Definition->RegionIds.IsEmpty())
		{
			CatalogWaterRegion = Definition->RegionIds[0];
			break;
		}
	}
	if (!TestFalse(TEXT("formal fish catalog supplies a real water region"), CatalogWaterRegion.IsNone())) return false;
	// 独立 World 覆盖本人取消、失去控制、预留发布重入离场及装备宿主销毁托管。
	// 仅注入入场身份/测试岸线；装备、正式 Actor/StateTree 和会话事务均走生产入口。
	for (int32 ExitScenario = 0; ExitScenario < 4; ++ExitScenario)
	{
		UCatEquipmentSettings* EquipmentSettings = GetMutableDefault<UCatEquipmentSettings>();
		FTestWorldWrapper Wrapper;
		if (!TestTrue(TEXT("creates owned-rod authority world"), Wrapper.CreateTestWorld(EWorldType::Game))) return false;
		Wrapper.ForwardErrorMessages(this);
		UWorld* World = Wrapper.GetTestWorld();
		FURL URL;
		URL.AddOption(TEXT("game=/Script/Catfishing.CatfishingGameModeBase"));
		if (!TestTrue(TEXT("creates real project game mode"), World->SetGameMode(URL))) return false;

		FCatWaterGeometryBuildInput Geometry;
		Geometry.RegionId = CatalogWaterRegion;
		Geometry.WaterPointVerticalToleranceCm = 100.0;
		Geometry.BankHeightToleranceCm = 50.0;
		Geometry.BoundaryToleranceCm = 1.0;
		Geometry.MaxLandingCorrectionCm = 100.0;
		Geometry.MinimumWaterInsetCm = 1.0;
		FCatWaterPolygonBuildInput& Boundary = Geometry.Boundaries.AddDefaulted_GetRef();
		Boundary.BoundaryId = TEXT("OwnedRodShore");
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
		ON_SCOPE_EXIT { Fishing->CloseCommandsAndTerminateAll(); };
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
			Result.Controller->SetActorTickEnabled(false);
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
			ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 50.0f);
			if (!ASC->InitializeFishingStaminaForSession()) Result.Equipment = nullptr;
			return Result;
		};
		const FPlayer Owner = MakePlayer(TEXT("OwnedRodOwner"), FVector(0.0, 0.0, 20.0));
		const FPlayer Helper = MakePlayer(TEXT("OwnedRodPhysicalHelper"), FVector(-100.0, 150.0, 20.0));
		const auto TickConnectedWorld = [&](float DeltaSeconds)
		{
			for (const FPlayer& Player : {Owner, Helper})
				if (IsValid(Player.Character) && !Player.Character->IsActorBeingDestroyed())
					Player.Character->GetPhysicalBodyComponent()->SetMoveIntent(Player.Character->GetPhysicalBodyComponent()->GetMoveIntent());
			Wrapper.TickTestWorld(DeltaSeconds);
		};
		for (const FPlayer& Player : {Owner, Helper})
		{
			if (!TestTrue(TEXT("real player passes production fishing gate"), Player.Equipment
				&& GameMode->CanAcceptFishingCommand(Player.Controller))) return false;
		}
		if (!TestTrue(TEXT("owner obtains a formal T2 rod"), Owner.Equipment->GrantEquipmentFromAuthority(
			FGuid::NewGuid(), Owner.Equipment->GetSnapshot().Revision, TEXT("ShopRodT2")).bCommitted)) return false;
		if (!TestTrue(TEXT("owner obtains a formal float"), Owner.Equipment->GrantEquipmentFromAuthority(
			FGuid::NewGuid(), Owner.Equipment->GetSnapshot().Revision, TEXT("FeatherFloat")).bCommitted)) return false;
		if (!TestTrue(TEXT("owner obtains four actual bait portions"), Owner.Equipment->GrantInventoryQuantityFromAuthority(
			FGuid::NewGuid(), Owner.Equipment->GetSnapshot().Revision, TEXT("BugBait"), 4).bCommitted)) return false;
		const auto Quantity = [](const UCatEquipmentComponent* Equipment, FName DefinitionId)
		{
			int32 Total = 0;
			for (const FCatInventoryEntry& Slot : CatFishingTest::Entries(Equipment))
				if (CatFishingTest::DefinitionId(Slot) == DefinitionId) Total += Slot.StackCount;
			return Total;
		};
		const FCatEquipmentLoadoutSnapshot OwnerBeforeDeploy = Owner.Equipment->GetSnapshot();
		const FGuid OwnerRodId = OwnerBeforeDeploy.RodItemInstanceId;
		FCatPlaceRodCommand Place;
		Place.RequestId = FGuid::NewGuid();
		Place.ExpectedEquipmentRevision = OwnerBeforeDeploy.Revision;
		const FCatFishingCommandResult Placed = Fishing->PlaceRod(Owner.Controller, Place);
		if (!TestTrue(TEXT("production PlaceRod deploys owner's physical rod"), Placed.bCommitted)) return false;
		ACatFishingRodActor* Rod = Fishing->FindDeployedRodById(Placed.RodActorId);
		if (!TestNotNull(TEXT("placed rod is registered"), Rod)) return false;
		const UCatEquipmentDefinition* RodDefinition = GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentDefinition>(TEXT("ShopRodT2"));
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
		const auto CastCommand = [&]()
		{
			FCatBeginCastCommand Result;
			Result.RequestId = FGuid::NewGuid();
			Result.RodActorId = Rod->GetPresentationState().RodActorId;
			Result.ExpectedRodActorRevision = Rod->GetPresentationState().RodActorRevision;
			Result.ExpectedEquipmentRevision = Owner.Equipment->GetSnapshot().Revision;
			Result.ClientCandidateWorldPoint = FVector(650.0, 0.0, 0.0);
			Result.ExpectedWaterRegionHandle = Baked.Cache.Handle;
			return Result;
		};

		if (ExitScenario == 2)
		{
			const FPlayer& Exiting = Owner;
			bool bExitTriggered = false;
			const FDelegateHandle ExitObserver = Exiting.Equipment->OnSnapshotChanged.AddLambda([&]()
			{
				if (bExitTriggered) return;
				bExitTriggered = true;
				TestTrue(TEXT("exit callback observes the committed bait and rod reservation"),
					Owner.Equipment->HasActiveFishingUse());
				TestEqual(TEXT("exit occurs before the new session is registered"), Fishing->GetTrackedSessionCountForDiagnostics(), 0);
				Exiting.Controller->UnPossess();
			});
			const FCatBeginCastCommand InterruptedCommand = CastCommand();
			AddExpectedErrorPlain(TEXT("Stage=EquipmentReservationPublication"), EAutomationExpectedErrorFlags::Contains, 1);
			const FCatBeginCastResult Interrupted = Fishing->BeginCast(Owner.Controller, InterruptedCommand);
			Exiting.Equipment->OnSnapshotChanged.Remove(ExitObserver);
			TestTrue(TEXT("inventory publication triggered the real UnPossess path"), bExitTriggered);
			TestNull(TEXT("exiting controller no longer possesses a pawn"), Exiting.Controller->GetPawn());
			TestFalse(TEXT("publication-time exit aborts the pending cast"), Interrupted.Command.bCommitted);
			TestEqual(TEXT("publication-time exit reports a dependency interruption"), Interrupted.Command.Error,
				ECatFishingCommandError::DependencyUnavailable);
			TestEqual(TEXT("publication-time exit refunds owner bait"), Quantity(Owner.Equipment, TEXT("BugBait")), 4);
			TestFalse(TEXT("publication-time exit releases owner reservation"), Owner.Equipment->HasActiveFishingUse());
			TestFalse(TEXT("publication-time exit releases owner rod lock"), Owner.Equipment->HasActiveFishingUse());
			FCatInventoryEntry ReturnedLock;
			TestEqual(TEXT("publication-time exit leaves original physical rod available for recall"),
				CatFishingTest::ReadHeldRod(Owner.Equipment, OwnerRodId, ReturnedLock), ECatDomainCommandError::None);
			TestEqual(TEXT("publication-time exit leaves no registered session"), Fishing->GetTrackedSessionCountForDiagnostics(), 0);
			int32 HookCount = 0;
			for (TActorIterator<ACatFishingHookActor> It(World); It; ++It) ++HookCount;
			TestEqual(TEXT("publication-time exit is detected before spawning a hook"), HookCount, 0);
			const int64 InterruptedRevision = Owner.Equipment->GetSnapshot().Revision;
			const auto Replay = Fishing->BeginCast(Owner.Controller, InterruptedCommand);
			TestEqual(TEXT("interrupted request replay preserves the first rollback revision"), Replay.Command.EquipmentRevision,
				Interrupted.Command.EquipmentRevision);
			TestEqual(TEXT("interrupted request replay never duplicates the refund"), Owner.Equipment->GetSnapshot().Revision, InterruptedRevision);
			AddInfo(FString::Printf(TEXT("Event=owned_rod_service_publication_exit_verified Scenario=%d RequestId=%s CallerBait=%d Sessions=%d Hooks=%d"),
				ExitScenario, *InterruptedCommand.RequestId.ToString(), Quantity(Owner.Equipment, TEXT("BugBait")),
				Fishing->GetTrackedSessionCountForDiagnostics(), HookCount));
			continue;
		}

		// 故障发生在装备预留之后，证明 rollback 同时归还抛钩者的饵并解除原竿实例的锁。
		UCatFishingPresentationSettings* Presentation = GetMutableDefault<UCatFishingPresentationSettings>();
		const FCatBeginCastCommand FailedCommand = CastCommand();
		{
			TGuardValue<TSoftClassPtr<ACatFishingHookActor>> MissingHook(Presentation->HookActorClass, TSoftClassPtr<ACatFishingHookActor>());
			AddExpectedErrorPlain(TEXT("Stage=HookClass"), EAutomationExpectedErrorFlags::Contains, 1);
			const FCatBeginCastResult Failed = Fishing->BeginCast(Owner.Controller, FailedCommand);
			TestFalse(TEXT("post-reservation missing hook does not commit"), Failed.Command.bCommitted);
			TestEqual(TEXT("post-reservation failure retains dependency diagnosis"), Failed.Command.Error, ECatFishingCommandError::DependencyUnavailable);
			TestEqual(TEXT("failed cast restores owner bait"), Quantity(Owner.Equipment, TEXT("BugBait")), 4);
			TestFalse(TEXT("failed cast closes owner reservation"), Owner.Equipment->HasActiveFishingUse());
			FCatInventoryEntry AvailableRod;
			TestEqual(TEXT("failed cast releases original rod transfer lock"), CatFishingTest::ReadHeldRod(Owner.Equipment, OwnerRodId, AvailableRod), ECatDomainCommandError::None);
			const int64 AfterFailureRevision = Owner.Equipment->GetSnapshot().Revision;
			const auto Replay = Fishing->BeginCast(Owner.Controller, FailedCommand);
			TestEqual(TEXT("failed request replays first rollback revision"), Replay.Command.EquipmentRevision, Failed.Command.EquipmentRevision);
			TestEqual(TEXT("failed replay does not reserve and refund twice"), Owner.Equipment->GetSnapshot().Revision, AfterFailureRevision);
			TestEqual(TEXT("failed cast leaves no active session"), Fishing->GetTrackedSessionCountForDiagnostics(), 0);
		}

		const FCatBeginCastCommand Begin = CastCommand();
		const FCatBeginCastResult CastResult = Fishing->BeginCast(Owner.Controller, Begin);
		if (!TestTrue(TEXT("owner creates a real cast session"), CastResult.Command.bCommitted)) return false;
		ACatFishingSession* Session = Fishing->FindSession(CastResult.Command.FishingSessionId);
		if (!TestNotNull(TEXT("production session is registered"), Session)) return false;
		ACatFishingHookActor* Hook = Session->GetSnapshot().HookActor;
		if (!TestNotNull(TEXT("production session owns a spawned hook"), Hook)) return false;
		TestEqual(TEXT("hook is the formal configured Blueprint"), Hook->GetClass(), Presentation->HookActorClass.Get());
		TestEqual(TEXT("hook montage Instigator belongs to the owner"), Hook->GetInstigator(), static_cast<APawn*>(Owner.Character));
		TestEqual(TEXT("hook identity belongs to the new cast"), Hook->GetPresentationState().FishingSessionId, CastResult.Command.FishingSessionId);
		TestEqual(TEXT("owner bait is reserved exactly once"), Quantity(Owner.Equipment, TEXT("BugBait")), 3);
		TestEqual(TEXT("owner's T2 durability initializes the session"), Session->GetSnapshot().RodDurabilityRemaining, OwnerBeforeDeploy.RodDurability);
		const int64 BoundRevision = Owner.Equipment->GetSnapshot().Revision;
		const auto Replayed = Fishing->BeginCast(Owner.Controller, Begin);
		TestEqual(TEXT("same request replays same session"), Replayed.Command.FishingSessionId, CastResult.Command.FishingSessionId);
		TestEqual(TEXT("same request cannot reserve a second bait"), Owner.Equipment->GetSnapshot().Revision, BoundRevision);
		TestEqual(TEXT("same rod has only one registered session"), Fishing->GetTrackedSessionCountForDiagnostics(), 1);
		FCatInventoryEntry LockedRod;
		TestEqual(TEXT("active owned session locks owner's ActiveUse transfer"), CatFishingTest::ReadHeldRod(Owner.Equipment, OwnerRodId, LockedRod), ECatDomainCommandError::InvalidPhase);

		// 真实定时器推进正式 Hook BP；没有直接伪造落水回调或 Session 阶段。
		const double FlightDuration = Hook->GetPresentationState().CastTrajectory.DurationSeconds;
		for (int32 Frame = 0; Frame < FMath::CeilToInt((FlightDuration + 0.1) / 0.01); ++Frame) TickConnectedWorld(0.01f);
		TestEqual(TEXT("formal hook actually lands after flight"), Hook->GetPresentationState().Phase, ECatFishingHookPresentationPhase::Landed);
		TestFalse(TEXT("landed owned-rod session remains active"), Session->IsTerminal());
		TestTrue(TEXT("formal hook reaches the server corrected water point"), Hook->GetActorLocation().Equals(CastResult.ServerCorrectedLandingWorldPoint, 1.0));

		// 旁人只有真实约束，不调用 OperateRod，不进入会话或取得装备预留。
		const auto AttachPhysicalHelper = [&]()
		{
			auto* Body = Helper.Character->GetPhysicalBodyComponent();
			const FVector Contact = Rod->GetGripWorldTransform().GetLocation()
				+ Rod->GetAuthoritativeRodForwardVector() * 35.0;
			if (!Body->TeleportBodyFromAuthority(FTransform(Helper.Character->GetActorRotation(),
				Body->GetBody()->GetComponentLocation() + Contact - Body->GetHand(true)->GetComponentLocation()),
				TEXT("OwnedRodHelperContact"))) return false;
			return Body->GetGrab()->GripFromAuthority(true, Rod->GetPhysicalRodBody(), Contact);
		};
		const float HelperStamina = Helper.Character->GetCatAbilitySystemComponent()->GetNumericAttribute(
			UCatSurvivalAttributeSet::GetFightStaminaAttribute());
		if (ExitScenario == 1 || ExitScenario == 3)
		{
			if (!TestTrue(TEXT("ordinary helper establishes a real rod constraint"), AttachPhysicalHelper())) return false;
			TestEqual(TEXT("a physical helper never adds a fishing operator"), Rod->GetOperatorCount(), 1);
			TestEqual(TEXT("owner remains the only session controller"), Session->GetSnapshot().FisherPlayerState.Get(), static_cast<APlayerState*>(Owner.State));
			TestNull(TEXT("helper has no session ownership index"), UCatFishingViewBridge::FindFishingSessionForPlayerState(World, Helper.State));
			TestFalse(TEXT("helper has no fishing equipment reservation"), Helper.Equipment->HasActiveFishingUse());
		}

		UCatEquipmentComponent* ReservationEquipment = Owner.Equipment;
		UCatEquipmentComponent* RodLedger = Owner.Equipment;
		double ExpectedDurability = OwnerBeforeDeploy.RodDurability;
		if (ExitScenario == 1)
		{
			const uint32 ControlEpoch = Rod->GetControlEpoch();
			Owner.Controller->UnPossess();
			TestNull(TEXT("real owner UnPossess relinquishes its pawn"), Owner.Controller->GetPawn());
			TestEqual(TEXT("owner departure clears control without promoting a helper"), Rod->GetOperatorCount(), 0);
			TestNull(TEXT("unattended session has no active fisher"), Session->GetSnapshot().FisherPlayerState.Get());
			TestTrue(TEXT("owner departure advances the input epoch"), Rod->GetControlEpoch() != ControlEpoch);
			TestFalse(TEXT("owner departure parks the rod and clears helper rod grips"), Helper.Character->GetPhysicalBodyComponent()->GetGrab()->IsGripping(true));
			TestEqual(TEXT("unattended session keeps its exact hook"), Session->GetSnapshot().HookActor.Get(), Hook);
			TestEqual(TEXT("unattended session keeps its identity"), Session->GetSnapshot().FishingSessionId, CastResult.Command.FishingSessionId);
			ACatFishingResourceCustodian* Custodian = nullptr;
			int32 CustodianCount = 0;
			for (TActorIterator<ACatFishingResourceCustodian> It(World); It; ++It)
				if (IsValid(*It)) { Custodian = *It; ++CustodianCount; }
			if (!TestEqual(TEXT("UnPossess moves the original reservation into exactly one custodian"), CustodianCount, 1)) return false;
			TestEqual(TEXT("UnPossess custody retains the deployment owner's identity"), Custodian->GetOriginalOwnerStableId(), Owner.State->GetUniqueId()->ToString());
			ReservationEquipment = RodLedger = Custodian->GetEquipment();
			TestFalse(TEXT("the original equipment no longer duplicates a transferred reservation"), Owner.Equipment->HasActiveFishingUse());
			TestTrue(TEXT("the exact session reservation stays live in custody"), ReservationEquipment->IsFishingUseActive(CastResult.Command.FishingSessionId));
			TestFalse(TEXT("loss of control does not finalize the cast"), Session->IsTerminal());
		}
		else if (ExitScenario == 3)
		{
			const FString OriginalId = Owner.State->GetUniqueId()->ToString();
			bool bObservedMovedState = false;
			bool bSecondMigrationCommitted = false;
			bool bDepartureSnapshotExported = false;
			bool bDepartureDeploymentRetired = false;
			bool bRetirementPreservedRod = false;
			FCatEquipmentLoadoutSnapshot DepartureSnapshot;
			TArray<FCatInventoryEntry> DepartureInventory;
			FText DepartureExportFailure;
			const FDelegateHandle MigrationObserver = Owner.Equipment->OnSnapshotChanged.AddLambda([&]()
			{
				if (bObservedMovedState) return;
				bObservedMovedState = true;
				bSecondMigrationCommitted |= Fishing->PreserveFishingResourcesForEquipmentShutdown(Owner.Equipment);
				// 托管发布时，离场保存只捕获留下的普通背包，不能复制在用竿和已经预留的鱼饵。
				bDepartureSnapshotExported = Owner.Equipment->ExportSnapshotFromAuthority(DepartureSnapshot, DepartureExportFailure);
				DepartureInventory = CatFishingTest::Entries(Owner.Equipment);
				if (bDepartureSnapshotExported)
					bDepartureDeploymentRetired = Owner.Equipment->RetireDeploymentAfterPersistentCapture(*Owner.State);
				bRetirementPreservedRod = IsValid(Rod) && Fishing->FindDeployedRodById(Placed.RodActorId) == Rod;
			});
			TestTrue(TEXT("destroying the equipment host runs real EndPlay"), Owner.Character->Destroy());
			Owner.Equipment->OnSnapshotChanged.Remove(MigrationObserver);
			TestTrue(TEXT("original player identity may leave after resources are preserved"), Owner.State->Destroy());
			TestTrue(TEXT("custody publishes after moving the original records"), bObservedMovedState);
			TestFalse(TEXT("publication reentry cannot transfer the same records twice"), bSecondMigrationCommitted);
			TestTrue(*FString::Printf(TEXT("departing host exports after custody: %s"), *DepartureExportFailure.ToString()), bDepartureSnapshotExported);
			TestFalse(TEXT("departing save cannot duplicate the active rod"), DepartureInventory.ContainsByPredicate(
				[OwnerRodId](const FCatInventoryEntry& Slot) { return CatFishingTest::InstanceId(Slot) == OwnerRodId; }));
			TestNotEqual(TEXT("departing selection cannot refer to the custody rod"), DepartureSnapshot.RodItemInstanceId, OwnerRodId);
			int32 SavedBait = 0;
			for (const auto& Slot : DepartureInventory) if (CatFishingTest::DefinitionId(Slot) == TEXT("BugBait")) SavedBait += Slot.StackCount;
			TestEqual(TEXT("departure saves only the three unreserved bait portions"), SavedBait, 3);
			TestTrue(TEXT("departing deployment retirement succeeds after custody"), bDepartureDeploymentRetired);
			TestTrue(TEXT("persistence retirement preserves the active physical rod"), bRetirementPreservedRod);
			ACatFishingResourceCustodian* Custodian = nullptr;
			int32 CustodianCount = 0;
			for (TActorIterator<ACatFishingResourceCustodian> It(World); It; ++It)
				if (IsValid(*It)) { Custodian = *It; ++CustodianCount; }
			if (!TestEqual(TEXT("exactly one server custodian survives owner destruction"), CustodianCount, 1)) return false;
			TestEqual(TEXT("custody retains original private ownership identity"), Custodian->GetOriginalOwnerStableId(), OriginalId);
			TestFalse(TEXT("custodian remains server-only"), Custodian->GetIsReplicated());
			TestFalse(TEXT("ordinary backpack items are not copied to custody"), CatFishingTest::Entries(Custodian->GetEquipment()).ContainsByPredicate([](const FCatInventoryEntry& Entry) { return Entry.Instance != nullptr && Entry.StackCount > 0; }));
			ReservationEquipment = RodLedger = Custodian->GetEquipment();
			TestFalse(TEXT("resource host destruction preserves the session"), Session->IsTerminal());
			TestEqual(TEXT("resource host destruction grants nobody control"), Rod->GetOperatorCount(), 0);
			TestNull(TEXT("helper is not promoted after the owner is destroyed"), Session->GetSnapshot().FisherPlayerState.Get());
			TestFalse(TEXT("owner destruction parks the retained rod and clears helper rod grips"), Helper.Character->GetPhysicalBodyComponent()->GetGrab()->IsGripping(true));
			TestEqual(TEXT("existing rod remains registered"), Fishing->FindDeployedRodById(Placed.RodActorId), Rod);
			TestTrue(TEXT("rebased coordinator owns the original bait reservation"), ReservationEquipment->IsFishingUseActive(CastResult.Command.FishingSessionId));
			TestTrue(TEXT("bait settles through the exact moved lock"), ReservationEquipment->CommitFishingBaitDeferred(CastResult.Command.FishingSessionId).bApplied);
			TestEqual(TEXT("repeat bait commit cannot spend another bait"), ReservationEquipment->CommitFishingBaitDeferred(
				CastResult.Command.FishingSessionId).Error, ECatDomainCommandError::AlreadyResolved);
			TestTrue(TEXT("later wear updates the original rod instance"), ReservationEquipment->ApplyFishingRodWear(
				CastResult.Command.FishingSessionId, 1, 2.0).bApplied);
			ExpectedDurability -= 2.0;
			TestEqual(TEXT("custody preserves monotone wear sequence"), ReservationEquipment->ApplyFishingRodWear(
				CastResult.Command.FishingSessionId, 1, 2.0).Error, ECatDomainCommandError::AlreadyResolved);
			TestFalse(TEXT("disposed coordinator no longer owns a live reservation"), Owner.Equipment->HasActiveFishingUse());
		}

		const FGuid CancelRequest = FGuid::NewGuid();
		AddExpectedErrorPlain(TEXT("Outcome=ECatFishingOutcome::Cancelled"), EAutomationExpectedErrorFlags::Contains, 1);
		if (!TestTrue(TEXT("explicit cancellation closes the owned reservation"), Session->CancelFromAuthority(CancelRequest).bCommitted)) return false;
		const int64 CancelRevision = ReservationEquipment->GetSnapshot().Revision;
		TestTrue(TEXT("cancel retry replays the committed result"), Session->CancelFromAuthority(CancelRequest).bCommitted);
		TestEqual(TEXT("cancel retry cannot duplicate bait or inventory writes"), ReservationEquipment->GetSnapshot().Revision, CancelRevision);
		const int32 OriginalBait = Quantity(Owner.Equipment, TEXT("BugBait"));
		const int32 CustodyBait = ReservationEquipment == Owner.Equipment ? 0 : Quantity(ReservationEquipment, TEXT("BugBait"));
		TestEqual(TEXT("all original and custody ledgers conserve bait; committed bait is spent once"),
			OriginalBait + CustodyBait, ExitScenario == 3 ? 3 : 4);
		if (ExitScenario == 1) TestEqual(TEXT("cancel returns only the reserved portion to the current custodian"), CustodyBait, 1);
		TestFalse(TEXT("terminal closes the exact original reservation"), ReservationEquipment->HasActiveFishingUse());
		FCatInventoryEntry ReleasedRod;
		TestEqual(TEXT("terminal releases the original rod transfer lock"), CatFishingTest::ReadHeldRod(RodLedger, OwnerRodId, ReleasedRod), ECatDomainCommandError::None);
		if (!TestEqual(TEXT("one original physical rod survives"), int32(ReleasedRod.Instance != nullptr), 1)) return false;
		TestEqual(TEXT("physical rod identity never changes"), CatFishingTest::InstanceId(ReleasedRod), OwnerRodId);
		TestEqual(TEXT("physical rod preserves exactly the committed durability"), CatFishingTest::Durability(ReleasedRod), ExpectedDurability);
		TestEqual(TEXT("physical assistance never writes helper fishing stamina"), Helper.Character->GetCatAbilitySystemComponent()->GetNumericAttribute(
			UCatSurvivalAttributeSet::GetFightStaminaAttribute()), HelperStamina);
		TestFalse(TEXT("physical assistance never acquires a fishing equipment reservation"), Helper.Equipment->HasActiveFishingUse());
		TestNull(TEXT("physical assistance never acquires a session index"), UCatFishingViewBridge::FindFishingSessionForPlayerState(World, Helper.State));
		AddInfo(FString::Printf(TEXT("Event=owned_rod_service_lifecycle_verified Scenario=%d SessionId=%s RodItemInstanceId=%s Operators=%d Durability=%.3f Evidence=runtime_behavior"),
			ExitScenario, *CastResult.Command.FishingSessionId.ToString(), *OwnerRodId.ToString(), Rod->GetOperatorCount(), CatFishingTest::Durability(ReleasedRod)));
	}
	return !HasAnyErrors();
}

#endif
