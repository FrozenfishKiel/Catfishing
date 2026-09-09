#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Character/CatCharacter.h"
#include "Character/CatCharacterMovementComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Condition/CatConditionComponent.h"
#include "Condition/CatConditionSettings.h"
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
#include "Fishing/Actors/CatFishEncounterActor.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/Config/CatFishingFightBalanceDefinition.h"
#include "Fishing/Presentation/CatFishingPresentationSettings.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Inventory/CatInventoryComponent.h"
#include "OnlineSubsystemTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingBorrowedRodCastTest,
	"Catfishing.Unit.Fishing.Service.BorrowedRodCastUsesCallerBaitAndPreservesOwnerInstance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingBorrowedRodCastTest::RunTest(const FString& Parameters)
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
	// 独立 World 覆盖取消、两资源宿主真实离场、预留通知重入，以及搏斗中个人身体失效与支付通知毁人。
	// 仅注入入场身份/测试岸线；装备、正式 Actor/StateTree 和会话事务均走生产入口。
	for (int32 ExitScenario = 0; ExitScenario < 12; ++ExitScenario)
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
		Geometry.RegionId = CatalogWaterRegion;
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
			if (!ASC->InitializeFishingStaminaForSession()) Result.Equipment = nullptr;
			return Result;
		};
		const FPlayer Owner = MakePlayer(TEXT("BorrowedRodOwner"), FVector(0.0, 0.0, 100.0));
		const FPlayer Caster = MakePlayer(TEXT("BorrowedRodCaster"), FVector(0.0, 120.0, 100.0));
		const FPlayer Relay = MakePlayer(TEXT("BorrowedRodRelay"), FVector(-100.0, 250.0, 100.0));
		const FPlayer Anchor = MakePlayer(TEXT("BorrowedRodAnchor"), FVector(-120.0, 200.0, 100.0));
		for (const FPlayer& Player : {Owner, Caster, Relay, Anchor})
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
		Place.ExpectedInventoryRevision = Owner.Character->GetInventoryComponent()->GetInventoryRevision();
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

		if (ExitScenario >= 3 && ExitScenario <= 4)
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

		UCatEquipmentComponent* ReservationEquipment = Caster.Equipment;
		UCatEquipmentComponent* RodLedger = Owner.Equipment;
		double ExpectedDurability = OwnerBeforeDeploy.RodDurability;
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
			// 正式 Service 入口建立四人队列；三名辅助只加入同一竿，不各建会话或预留鱼饵。
			for (const FPlayer& Helper : {Relay, Anchor, Owner})
			{
				Helper.Character->SetActorLocation(Rod->GetOperatorInteractionWorldTransform().GetLocation());
				Operate.Context = Context();
				if (!TestTrue(TEXT("helper joins the existing rod through production service"),
					Fishing->OperateRod(Helper.Controller, Operate).bCommitted)) return false;
			}
			TestEqual(TEXT("four members share one rod"), Rod->GetOperatorCount(), 4);
			TestEqual(TEXT("four members still have one session"), Fishing->GetTrackedSessionCountForDiagnostics(), 1);
			double ExpectedWaitingMaximum = 0.0;
			double ExpectedWaitingStamina = 0.0;
			for (const FPlayer& Player : {Caster, Relay, Anchor, Owner})
			{
				const float Maximum = Player.Character->GetCatAbilitySystemComponent()->GetNumericAttribute(
					UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute());
				if (!TestTrue(TEXT("every member resolves their current ASC stamina maximum"),
					FMath::IsFinite(Maximum) && Maximum > 0.0f)) return false;
				ExpectedWaitingMaximum += Maximum;
				ExpectedWaitingStamina += Player.Character->GetCatAbilitySystemComponent()->GetNumericAttribute(
					UCatSurvivalAttributeSet::GetFightStaminaAttribute());
			}
			TestEqual(TEXT("waiting-stage total uses all four current balances"), Session->GetSnapshot().CombinedFightStamina, ExpectedWaitingStamina);
			TestEqual(TEXT("waiting-stage maximum sums actual member baselines"),
				Session->GetSnapshot().CombinedFightStaminaMaximum, ExpectedWaitingMaximum);
			const auto* Balance = GetDefault<UCatFishingSettings>()->LoadFightBalanceDefinition();
			if (!TestNotNull(TEXT("waiting summary uses the same formal fight balance"), Balance)) return false;
			TestEqual(TEXT("waiting strength applies the same helper contribution coefficient"),
				Session->GetSnapshot().CombinedFishingStrength, 10.0 * (1.0 + 3.0 * Balance->HelperStrengthMultiplier));
			if (ExitScenario >= 7)
			{
				// 本单元测真实固定步的身体/资源通知；冻结 CMC 只避免无输入夹具在等待期被相互碰撞推离岸台。
				for (const FPlayer& Player : {Caster, Relay, Anchor, Owner})
				{
					Player.Character->GetCharacterMovement()->SetComponentTickEnabled(false);
					Player.Character->GetCatAbilitySystemComponent()->SetNumericAttributeBase(
						UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 50.0f);
				}
				Session->RefreshOperatorMembershipFromAuthority();
				const double StrengthBeforeFight = Session->GetSnapshot().CombinedFishingStrength;
				for (int32 Frame = 0; Frame < 4500 && !Session->IsTerminal()
					&& Session->GetSnapshot().Phase != ECatFishingPhase::TrueBiteWindow; ++Frame)
					Wrapper.TickTestWorld(0.01f);
				if (!TestEqual(TEXT("real waiting timers reach a true bite before body regression"),
					Session->GetSnapshot().Phase, ECatFishingPhase::TrueBiteWindow)) return false;
				if (!TestTrue(TEXT("production hook selection starts the fixed-step fight"),
					Session->RequestHookFromAuthority(FGuid::NewGuid()).bCommitted)) return false;
				if (!TestTrue(TEXT("hook confirmation selected a fish and entered HookedFight"),
					Session->GetSnapshot().Phase == ECatFishingPhase::HookedFight
					&& IsValid(Session->GetSnapshot().FishEncounterActor))) return false;
				TestEqual(TEXT("fight entry preserves the waiting summary's strength meaning"),
					Session->GetSnapshot().CombinedFishingStrength, StrengthBeforeFight);
				for (int32 Frame = 0; Frame < 6 && !Session->IsTerminal(); ++Frame) Wrapper.TickTestWorld(0.01f);
				TestEqual(TEXT("first real group calculation preserves the same role-weighted strength"),
					Session->GetSnapshot().CombinedFishingStrength, StrengthBeforeFight);
				ACatFishEncounterActor* FishActor = Session->GetSnapshot().FishEncounterActor.Get();
				if (!TestNotNull(TEXT("body regression retains a real fish encounter"), FishActor)) return false;
				if (ExitScenario == 11)
				{
					ACatFishingSession* PreviousSession = Session;
					const uint32 PreviousAimEpoch = Rod->GetCarrierConstraintState().AimInputEpoch;
					AddExpectedErrorPlain(TEXT("Outcome=ECatFishingOutcome::Cancelled"), EAutomationExpectedErrorFlags::Contains, 1);
					if (!TestTrue(TEXT("first actual fight finalizes before the rod is reused"),
						PreviousSession->CancelFromAuthority(FGuid::NewGuid()).bCommitted)) return false;
					TestFalse(TEXT("finalization stops the first fight runner immediately"), PreviousSession->IsFightRunnerRunning());
					// 保留真实终态 Actor，模拟它的复制保留窗与下一场重叠；仍通过 Destroy 触发真正 EndPlay。
					PreviousSession->SetLifeSpan(0.0f);
					const auto NextCast = Fishing->BeginCast(Caster.Controller, CastCommand());
					if (!TestTrue(TEXT("same held rod begins a second cast while the terminal session still exists"), NextCast.Command.bCommitted)) return false;
					Session = Fishing->FindSession(NextCast.Command.FishingSessionId);
					if (!TestNotNull(TEXT("second production session exists"), Session)) return false;
					TestNotEqual(TEXT("second cast has its own session actor"), Session, PreviousSession);
					for (int32 Frame = 0; Frame < 4500 && !Session->IsTerminal()
						&& Session->GetSnapshot().Phase != ECatFishingPhase::TrueBiteWindow; ++Frame)
						Wrapper.TickTestWorld(0.01f);
					if (!TestEqual(TEXT("second cast reaches a real true bite"), Session->GetSnapshot().Phase, ECatFishingPhase::TrueBiteWindow)) return false;
					if (!TestTrue(TEXT("second hook selects a real fish"), Session->RequestHookFromAuthority(FGuid::NewGuid()).bCommitted)
						|| !TestEqual(TEXT("second session enters actual HookedFight"), Session->GetSnapshot().Phase, ECatFishingPhase::HookedFight)) return false;
					const auto IsNewGroupSolveReady = [&]()
					{
						for (const FPlayer& Player : {Caster, Relay, Anchor, Owner})
						{
							const auto* Movement = Cast<UCatCharacterMovementComponent>(Player.Character->GetCharacterMovement());
							const FCatExternalTractionInput Input = Movement ? Movement->GetExternalTraction() : FCatExternalTractionInput{};
							if (!Input.bGroupDriven || Input.bWaitingForGroupSolve || Input.bUnloadedMovement
								|| Input.AimInputEpoch != Rod->GetCarrierConstraintState().AimInputEpoch) return false;
						}
						return true;
					};
					// Timer 启动先发布搏斗域，完整组速度要等实际固定步；不能以渲染帧数假定已经完成。
					for (int32 Frame = 0; Frame < 50 && !Session->IsTerminal() && !IsNewGroupSolveReady(); ++Frame)
						Wrapper.TickTestWorld(0.01f);
					if (!TestTrue(TEXT("new fight has a complete current group solve before old EndPlay"), IsNewGroupSolveReady())) return false;
					if (!TestTrue(TEXT("second runner is running before old EndPlay"), Session->IsFightRunnerRunning())
						|| !TestTrue(TEXT("second runner publishes its own fight constraint"), Rod->GetCarrierConstraintState().bFightActive)) return false;
					const uint32 CurrentAimEpoch = Rod->GetCarrierConstraintState().AimInputEpoch;
					TestTrue(TEXT("new fight has a newer aim input epoch"), CurrentAimEpoch > PreviousAimEpoch);
					const FVector CurrentGroupAnchor = Rod->GetGroupAnchorWorld();
					ACatFishEncounterActor* CurrentFish = Session->GetSnapshot().FishEncounterActor.Get();
					if (!TestTrue(TEXT("terminal old session is still alive until explicit destruction"), IsValid(PreviousSession))
						|| !TestTrue(TEXT("destroying the old session executes its real EndPlay"), PreviousSession->Destroy())) return false;
					TestTrue(TEXT("old EndPlay cannot clear the new fight constraint"), Rod->GetCarrierConstraintState().bFightActive);
					TestEqual(TEXT("old EndPlay preserves the new fight aim epoch"), Rod->GetCarrierConstraintState().AimInputEpoch, CurrentAimEpoch);
					TestTrue(TEXT("old EndPlay preserves the group anchor"), Rod->GetGroupAnchorWorld().Equals(CurrentGroupAnchor, 0.001));
					for (const FPlayer& Player : {Caster, Relay, Anchor, Owner})
					{
						const auto* Movement = Cast<UCatCharacterMovementComponent>(Player.Character->GetCharacterMovement());
						const FCatExternalTractionInput Input = Movement ? Movement->GetExternalTraction() : FCatExternalTractionInput{};
						TestTrue(TEXT("old EndPlay preserves every current member's solved CMC binding"), Input.bGroupDriven && !Input.bWaitingForGroupSolve);
						TestEqual(TEXT("surviving CMC binding keeps its source rod"), Input.SourceId, Rod->GetPresentationState().RodActorId);
					}
					for (int32 Frame = 0; Frame < 10 && !Session->IsTerminal(); ++Frame) Wrapper.TickTestWorld(0.01f);
					TestTrue(TEXT("second fight continues after old session destruction"), Session->IsFightRunnerRunning() && !Session->IsTerminal());
					TestEqual(TEXT("second fight keeps its fish after old EndPlay"), Session->GetSnapshot().FishEncounterActor.Get(), CurrentFish);
					TestEqual(TEXT("old EndPlay cannot restart the new fight's aim epoch"), Rod->GetCarrierConstraintState().AimInputEpoch, CurrentAimEpoch);
					FCatInventoryEndpointSnapshot NewLock;
					TestEqual(TEXT("old EndPlay cannot release the second cast's original rod lock"), Owner.Equipment->ReadInventoryTransferEndpoint(
						TEXT("ActiveUse"), OwnerRodId, NewLock), ECatDomainCommandError::InvalidPhase);
					const auto* PrimaryMovement = Cast<UCatCharacterMovementComponent>(Caster.Character->GetCharacterMovement());
					const FVector DesiredVelocityBeforeExhaustion = PrimaryMovement->GetExternalTraction().GroupDesiredVelocity;
					if (!TestTrue(TEXT("real phase entry transitions the running fight to exhausted reeling"),
						Session->EnterPhaseFromStateTree(ECatFishingPhase::ExhaustedReel).bApplied)) return false;
					TestTrue(TEXT("exhaustion keeps the fight constraint active before the next fixed step"), Rod->GetCarrierConstraintState().bFightActive);
					TestEqual(TEXT("exhaustion preserves the existing aim domain immediately"), Rod->GetCarrierConstraintState().AimInputEpoch, CurrentAimEpoch);
					TestEqual(TEXT("exhaustion immediately removes previous fish pull acceleration"),
						Rod->GetCarrierConstraintState().PullAccelerationCentimetersPerSecondSquared, 0.0f);
					TestEqual(TEXT("exhaustion immediately removes previous fish torque"),
						Rod->GetCarrierConstraintState().MaximumFishTorqueStrengthMeters, 0.0f);
					TestTrue(TEXT("exhaustion retains the already solved group velocity until the next step"),
						PrimaryMovement->GetExternalTraction().GroupDesiredVelocity.Equals(DesiredVelocityBeforeExhaustion, 0.001));
					for (const FPlayer& Player : {Caster, Relay, Anchor, Owner})
					{
						const auto* Movement = Cast<UCatCharacterMovementComponent>(Player.Character->GetCharacterMovement());
						TestTrue(TEXT("exhaustion does not interrupt any current CMC group solve"),
							Movement->GetExternalTraction().bGroupDriven && !Movement->GetExternalTraction().bWaitingForGroupSolve
							&& !Movement->GetExternalTraction().bUnloadedMovement);
					}
					for (int32 Frame = 0; Frame < 6 && !Session->IsTerminal(); ++Frame) Wrapper.TickTestWorld(0.01f);
					TestTrue(TEXT("exhausted reeling continues through the next fixed step"), Session->IsFightRunnerRunning()
						&& Session->GetSnapshot().Phase == ECatFishingPhase::ExhaustedReel && Rod->GetCarrierConstraintState().bFightActive);
					TestEqual(TEXT("exhausted fixed step does not replace the aim domain"), Rod->GetCarrierConstraintState().AimInputEpoch, CurrentAimEpoch);
					TestFalse(TEXT("exhausted fixed step does not select unloaded group movement"), PrimaryMovement->GetExternalTraction().bUnloadedMovement);
					if (!HasAnyErrors()) AddInfo(TEXT("Event=fishing_old_session_endplay_preserved_new_fight_verified Members=4 Evidence=runtime_behavior"));
					AddExpectedErrorPlain(TEXT("Outcome=ECatFishingOutcome::Cancelled"), EAutomationExpectedErrorFlags::Contains, 1);
					Session->CancelFromAuthority(FGuid::NewGuid());
					continue;
				}
				const uint32 PreviousControlEpoch = Rod->GetControlEpoch();
				const UCatConditionSettings* Conditions = GetDefault<UCatConditionSettings>();
				if (ExitScenario == 7 || ExitScenario == 8)
				{
					const FPlayer& WetPlayer = ExitScenario == 7 ? Caster : Relay;
					WetPlayer.Character->SetActorLocation(FVector(1000.0, 900.0,
						WetPlayer.Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
						- Conditions->DangerousWaterDepthCentimeters - 5.0));
					AddExpectedErrorPlain(TEXT("Event=fishing_cat_entered_dangerous_water"), EAutomationExpectedErrorFlags::Contains, 1);
					for (int32 Frame = 0; Frame < 150 && Rod->GetOperatorCount() == 4 && !Session->IsTerminal(); ++Frame)
						Wrapper.TickTestWorld(0.01f);
					TestEqual(TEXT("real dangerous water removes exactly the affected member"), Rod->GetOperatorCount(), 3);
					TestEqual(TEXT("affected body preserves the real Dangerous condition"),
						WetPlayer.Character->GetConditionComponent()->GetSnapshot().WaterExposure, ECatWaterExposureState::Dangerous);
					TestEqual(TEXT("wet body no longer holds any rod slot"), Rod->GetOperatorSlotIndex(WetPlayer.State), INDEX_NONE);
					TestEqual(TEXT("only primary water departure promotes the earliest helper"),
						Session->GetSnapshot().FisherPlayerState.Get(), static_cast<APlayerState*>(ExitScenario == 7 ? Relay.State : Caster.State));
					TestEqual(TEXT("only primary water departure advances control epoch"),
						Rod->GetControlEpoch() != PreviousControlEpoch, ExitScenario == 7);
				}
				else if (ExitScenario == 9)
				{
					// 构造已超过阈值的 Poison，仍由正式恢复命令经过 GE 与 Condition 唯一倒地裁决口。
					for (const FPlayer& Downing : {Relay, Caster})
					{
						Downing.Character->GetCatAbilitySystemComponent()->SetNumericAttributeBase(
							UCatSurvivalAttributeSet::GetPoisonAttribute(),
							Conditions->PoisonDownedThreshold + Conditions->FieldRestPoisonRelief + 1.0);
						AddExpectedErrorPlain(TEXT("Event=character_downed"), EAutomationExpectedErrorFlags::Contains, 1);
						TestTrue(TEXT("production body command evaluates downed state"),
							Downing.Character->GetConditionComponent()->RequestFieldSelfRecovery(Downing.Controller, FGuid::NewGuid()).bCommitted);
						TestTrue(TEXT("body retains downed state"), Downing.Character->GetConditionComponent()->GetSnapshot().bDowned);
						TestEqual(TEXT("downed member is removed without requesting a user leave"), Rod->GetOperatorSlotIndex(Downing.State), INDEX_NONE);
					}
					TestEqual(TEXT("helper and primary downing leaves two active members"), Rod->GetOperatorCount(), 2);
					TestEqual(TEXT("primary succession skips the already downed helper"),
						Session->GetSnapshot().FisherPlayerState.Get(), static_cast<APlayerState*>(Anchor.State));
				}
				else
				{
					bool bDestroyedDuringPayment = false;
					bool bBoundaryObserved = false;
					int32 MembershipDuringNotification = 0;
					UCatAbilitySystemComponent* PayingASC = Caster.Character->GetCatAbilitySystemComponent();
					const FDelegateHandle DestroyObserver = PayingASC->GetGameplayAttributeValueChangeDelegate(
						UCatSurvivalAttributeSet::GetFightStaminaAttribute()).AddLambda([&](const FOnAttributeChangeData& Change)
						{
							if (bDestroyedDuringPayment || Change.NewValue >= Change.OldValue) return;
							bBoundaryObserved = Session->IsFixedStepMutationBoundaryActive();
							bDestroyedDuringPayment = Owner.Character->Destroy();
							MembershipDuringNotification = Rod->GetOperatorCount();
						});
					int32 DebitNotifications[3] = {0, 0, 0};
					TArray<TPair<UCatAbilitySystemComponent*, FDelegateHandle>> DebitObservers;
					int32 ParticipantIndex = 0;
					for (const FPlayer& Remaining : {Caster, Relay, Anchor})
					{
						const int32 Index = ParticipantIndex++;
						UCatAbilitySystemComponent* ASC = Remaining.Character->GetCatAbilitySystemComponent();
						DebitObservers.Emplace(ASC, ASC->GetGameplayAttributeValueChangeDelegate(
							UCatSurvivalAttributeSet::GetFightStaminaAttribute()).AddLambda([&, Index](const FOnAttributeChangeData& Change)
							{ if (Change.NewValue < Change.OldValue) ++DebitNotifications[Index]; }));
					}
					AddExpectedErrorPlain(TEXT("Event=fishing_group_stamina_skipped"), EAutomationExpectedErrorFlags::Contains, 1);
					TestTrue(TEXT("primary starts actual paid reeling"), Session->SetReelingFromAuthority(Caster.State, 1, true));
					for (int32 Frame = 0; Frame < 100 && !bDestroyedDuringPayment && !Session->IsTerminal(); ++Frame)
						Wrapper.TickTestWorld(0.01f);
					PayingASC->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetFightStaminaAttribute()).Remove(DestroyObserver);
					for (const auto& Observer : DebitObservers)
						Observer.Key->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetFightStaminaAttribute()).Remove(Observer.Value);
					TestTrue(TEXT("real GAS debit notification destroyed the original rod resource host"), bDestroyedDuringPayment);
					TestTrue(TEXT("ASC notification observes the fixed-step mutation boundary"), bBoundaryObserved);
					TestEqual(TEXT("forced removal waits until the frozen payment finishes"), MembershipDuringNotification, 4);
					TestEqual(TEXT("boundary flush removes disposed member exactly once"), Rod->GetOperatorCount(), 3);
					for (int32 Count : DebitNotifications)
						TestTrue(TEXT("each surviving ASC receives at most one debit in the interrupted step"), Count <= 1);
					for (TActorIterator<ACatFishingResourceCustodian> It(World); It; ++It)
						if (IsValid(*It)) RodLedger = It->GetEquipment();
					TestNotEqual(TEXT("original rod ledger moved before its body is disposed"), RodLedger, Owner.Equipment);
				}
				TestFalse(TEXT("individual body failure preserves the current fish session"), Session->IsTerminal());
				TestEqual(TEXT("individual body failure preserves the exact fish actor"), Session->GetSnapshot().FishEncounterActor.Get(), FishActor);
				TestEqual(TEXT("individual body failure preserves the exact hook actor"), Session->GetSnapshot().HookActor.Get(), Hook);
				for (int32 Frame = 0; Frame < 10 && !Session->IsTerminal(); ++Frame) Wrapper.TickTestWorld(0.01f);
				TestFalse(TEXT("remaining members continue through subsequent fixed steps"), Session->IsTerminal());
				TestEqual(TEXT("step summary converges to the surviving membership"), Session->GetSnapshot().FightParticipantCount, Rod->GetOperatorCount());
				double ExpectedTotal = 0.0;
				for (const FPlayer& Remaining : {Caster, Relay, Anchor, Owner})
					if (IsValid(Remaining.Character) && Rod->GetOperatorSlotIndex(Remaining.State) != INDEX_NONE)
						ExpectedTotal += Remaining.Character->GetCatAbilitySystemComponent()->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
				TestTrue(TEXT("published total equals exactly the remaining personal balances"),
					FMath::IsNearlyEqual(Session->GetSnapshot().CombinedFightStamina, ExpectedTotal, 0.001));
				FCatInventoryEndpointSnapshot LiveLock;
				TestEqual(TEXT("body departure keeps the original rod resource lock"), RodLedger->ReadInventoryTransferEndpoint(
					TEXT("ActiveUse"), OwnerRodId, LiveLock), ECatDomainCommandError::InvalidPhase);
				TestEqual(TEXT("body departure never spends another original bait"), Quantity(Caster.Equipment, TEXT("BugBait")), 3);
				AddInfo(FString::Printf(TEXT("Event=fishing_group_body_departure_verified Scenario=%d Members=%d SessionId=%s ControlEpoch=%u Evidence=runtime_behavior"),
					ExitScenario, Rod->GetOperatorCount(), *Session->GetSnapshot().FishingSessionId.ToString(), Rod->GetControlEpoch()));
				AddExpectedErrorPlain(TEXT("Outcome=ECatFishingOutcome::Cancelled"), EAutomationExpectedErrorFlags::Contains, 1);
				Session->CancelFromAuthority(FGuid::NewGuid());
				continue;
			}
			const double RelayInitialStamina = Relay.Character->GetCatAbilitySystemComponent()->GetNumericAttribute(
				UCatSurvivalAttributeSet::GetFightStaminaAttribute());
			Relay.Character->GetCatAbilitySystemComponent()->SetNumericAttributeBase(
				UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 0.0f);
			Session->RefreshOperatorMembershipFromAuthority();
			TestEqual(TEXT("empty personal balance still counts as one of four members"), Session->GetSnapshot().FightParticipantCount, 4);
			TestEqual(TEXT("zero stamina changes the total balance but not member maximum"),
				Session->GetSnapshot().CombinedFightStamina, ExpectedWaitingStamina - RelayInitialStamina);
			TestEqual(TEXT("zero stamina retains that member's configured maximum"),
				Session->GetSnapshot().CombinedFightStaminaMaximum, ExpectedWaitingMaximum);
			const uint32 OldControlEpoch = Rod->GetControlEpoch();
			Leave.Context = Context();
			if (!TestTrue(TEXT("caster leaves without ending their reservation"), Fishing->LeaveRod(Caster.Controller, Leave).bCommitted)) return false;
			TestEqual(TEXT("earliest helper with zero stamina becomes primary"), Session->GetSnapshot().FisherPlayerState.Get(), static_cast<APlayerState*>(Relay.State));
			TestTrue(TEXT("primary departure advances control epoch"), Rod->GetControlEpoch() != OldControlEpoch);
			TestEqual(TEXT("takeover preserves zero individual stamina"), Relay.Character->GetCatAbilitySystemComponent()->GetNumericAttribute(
				UCatSurvivalAttributeSet::GetFightStaminaAttribute()), 0.0f);
			TestEqual(TEXT("takeover does not reserve more original caller bait"), Quantity(Caster.Equipment, TEXT("BugBait")), 3);
			TestFalse(TEXT("relay does not acquire an equipment reservation"), Relay.Equipment->HasActiveFishingUse());
			Fishing->ReleaseFishingOperatorForCharacter(ExitScenario == 1 ? Owner.Character : Caster.Character);
			TestFalse(TEXT("equipment host losing participation does not terminate the relayed session"), Session->IsTerminal());
			TestTrue(TEXT("original bait reservation remains live"), Caster.Equipment->HasActiveFishingUse());
			Relay.Controller->UnPossess();
			TestFalse(TEXT("real UnPossess preserves the same session"), Session->IsTerminal());
			TestEqual(TEXT("real UnPossess promotes the next joined helper"), Session->GetSnapshot().FisherPlayerState.Get(), static_cast<APlayerState*>(Anchor.State));
			TestEqual(TEXT("handoff retains the existing hook actor"), Session->GetSnapshot().HookActor.Get(), Hook);
			TestEqual(TEXT("handoff retains session identity"), Session->GetSnapshot().FishingSessionId, CastResult.Command.FishingSessionId);
			if (ExitScenario >= 5)
			{
				const FPlayer& Destroying = ExitScenario == 5 ? Owner : Caster;
				const FString OriginalId = Destroying.State->GetUniqueId()->ToString();
				bool bObservedMovedState = false;
				bool bSecondMigrationCommitted = false;
				bool bDepartureSnapshotExported = false;
				bool bDepartureDeploymentRetired = false;
				bool bRetirementPreservedRod = false;
				FCatEquipmentLoadoutSnapshot DepartureSnapshot;
				FText DepartureExportFailure;
				const FDelegateHandle MigrationObserver = Destroying.Equipment->OnSnapshotChanged.AddLambda([&]()
				{
					if (bObservedMovedState) return;
					bObservedMovedState = true;
					bSecondMigrationCommitted |= Fishing->PreserveFishingResourcesForEquipmentShutdown(Destroying.Equipment);
					// 真正 EndPlay 的托管发布点仍有原 PlayerState：保存只能捕获留下的普通背包，不能收走队友的在用竿。
					bDepartureSnapshotExported = Destroying.Equipment->ExportSnapshotFromAuthority(
						DepartureSnapshot, DepartureExportFailure);
					if (bDepartureSnapshotExported)
						bDepartureDeploymentRetired = Destroying.Equipment->RetireDeploymentAfterPersistentCapture(*Destroying.State);
					bRetirementPreservedRod = IsValid(Rod) && Fishing->FindDeployedRodById(Placed.RodActorId) == Rod;
				});
				TestTrue(TEXT("destroying the original equipment host runs real EndPlay"), Destroying.Character->Destroy());
				Destroying.Equipment->OnSnapshotChanged.Remove(MigrationObserver);
				TestTrue(TEXT("original player identity can also leave the world"), Destroying.State->Destroy());
				TestTrue(TEXT("custody publishes after moving the old records"), bObservedMovedState);
				TestFalse(TEXT("publication reentry cannot transfer the same records twice"), bSecondMigrationCommitted);
				TestTrue(*FString::Printf(TEXT("departing host exports after custody: %s"),
					*DepartureExportFailure.ToString()), bDepartureSnapshotExported);
				TestFalse(TEXT("departing save cannot duplicate the live borrowed rod"), DepartureSnapshot.InventorySlots.ContainsByPredicate(
					[OwnerRodId](const FCatRunInventorySlot& Slot) { return Slot.ItemInstanceId == OwnerRodId; }));
				TestNotEqual(TEXT("departing selection cannot refer to the custody rod"), DepartureSnapshot.RodItemInstanceId, OwnerRodId);
				if (ExitScenario == 6)
					TestTrue(TEXT("departing caster still saves their own ordinary rod"), DepartureSnapshot.InventorySlots.ContainsByPredicate(
						[&CasterBefore](const FCatRunInventorySlot& Slot) { return Slot.ItemInstanceId == CasterBefore.RodItemInstanceId; }));
				TestTrue(TEXT("departing deployment retirement succeeds after custody"), bDepartureDeploymentRetired);
				TestTrue(TEXT("departing persistence never destroys the custody rod"), bRetirementPreservedRod);
				ACatFishingResourceCustodian* Custodian = nullptr;
				int32 CustodianCount = 0;
				for (TActorIterator<ACatFishingResourceCustodian> It(World); It; ++It)
				{
					if (IsValid(*It)) { Custodian = *It; ++CustodianCount; }
				}
				if (!TestEqual(TEXT("exactly one server custodian survives actual owner destruction"), CustodianCount, 1)) return false;
				TestEqual(TEXT("custody retains original private ownership identity"), Custodian->GetOriginalOwnerStableId(), OriginalId);
				TestTrue(TEXT("custodian is server-only"), !Custodian->GetIsReplicated());
				TestTrue(TEXT("ordinary backpack is not copied to custody"), Custodian->GetEquipment()->GetSnapshot().InventorySlots.IsEmpty());
				if (ExitScenario == 5) RodLedger = Custodian->GetEquipment();
				else ReservationEquipment = Custodian->GetEquipment();
				TestFalse(TEXT("real resource host destruction preserves the session"), Session->IsTerminal());
				TestEqual(TEXT("existing rod stays registered after original host destruction"), Fishing->FindDeployedRodById(Placed.RodActorId), Rod);
				TestTrue(TEXT("rebased coordinator still owns the original bait reservation"), ReservationEquipment->IsFishingUseActive(CastResult.Command.FishingSessionId));
				const auto Commit = ReservationEquipment->CommitFishingBaitDeferred(CastResult.Command.FishingSessionId);
				TestTrue(TEXT("future bait settlement succeeds through moved exact locks"), Commit.bApplied);
				TestEqual(TEXT("repeat bait commit cannot spend another bait"), ReservationEquipment->CommitFishingBaitDeferred(
					CastResult.Command.FishingSessionId).Error, ECatDomainCommandError::AlreadyResolved);
				const auto Wear = ReservationEquipment->ApplyFishingRodWear(CastResult.Command.FishingSessionId, 1, 2.0);
				TestTrue(TEXT("future wear still updates the original rod instance"), Wear.bApplied);
				ExpectedDurability -= 2.0;
				TestEqual(TEXT("custody preserves monotone wear sequence"), ReservationEquipment->ApplyFishingRodWear(
					CastResult.Command.FishingSessionId, 1, 2.0).Error, ECatDomainCommandError::AlreadyResolved);
				TestFalse(TEXT("original disposed coordinator no longer owns an active reservation"),
					ExitScenario == 6 && Caster.Equipment->HasActiveFishingUse());
			}
			AddExpectedErrorPlain(TEXT("Outcome=ECatFishingOutcome::Cancelled"), EAutomationExpectedErrorFlags::Contains, 1);
			TestTrue(TEXT("explicit cancellation closes preserved reservation"), Session->CancelFromAuthority(FGuid::NewGuid()).bCommitted);
		}
		TestEqual(TEXT("closing before a bite returns bait to original caller"), Quantity(Caster.Equipment, TEXT("BugBait")), ExitScenario >= 5 ? 3 : 4);
		TestFalse(TEXT("terminal closes original caller reservation"), ReservationEquipment->HasActiveFishingUse());
		FCatInventoryEndpointSnapshot ReleasedRod;
		TestEqual(TEXT("terminal releases the original rod's inventory transfer lock"), RodLedger->ReadInventoryTransferEndpoint(
			TEXT("ActiveUse"), OwnerRodId, ReleasedRod), ECatDomainCommandError::None);
		if (!TestEqual(TEXT("original owner still holds exactly one active physical rod"), ReleasedRod.Slots.Num(), 1)) return false;
		TestEqual(TEXT("physical rod identity never changes"), ReleasedRod.Slots[0].ItemInstanceId, OwnerRodId);
		TestEqual(TEXT("unused physical rod durability survives rollback and cancellation"), ReleasedRod.Slots[0].RodDurability, ExpectedDurability);
		TestFalse(TEXT("borrowed rod was never copied to the caller inventory"), Caster.Equipment->GetSnapshot().InventorySlots.ContainsByPredicate(
			[OwnerRodId](const FCatRunInventorySlot& Slot) { return Slot.ItemInstanceId == OwnerRodId; }));
		AddInfo(FString::Printf(TEXT("Event=borrowed_rod_service_verified Scenario=%d SessionId=%s RodItemInstanceId=%s CallerBait=%d OwnerDurability=%.3f"),
			ExitScenario, *CastResult.Command.FishingSessionId.ToString(), *OwnerRodId.ToString(),
			Quantity(Caster.Equipment, TEXT("BugBait")), ReleasedRod.Slots[0].RodDurability));
	}
	return !HasAnyErrors();
}

#endif
