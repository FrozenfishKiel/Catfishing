#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Physics/CatPhysicalEffortComponent.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/Integration/CatFishingPhysicalRodComponent.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/CatFishingService.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "OnlineSubsystemTypes.h"
#include "Fishing/Simulation/CatFishingFightRunner.h"
#include "GameFramework/PlayerState.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingOperatorRunnerIntegrationTest,
	"Catfishing.Unit.Fishing.Runner.OperatorSamplesAndSingleASCSettlementRemainConservative",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingOperatorRunnerIntegrationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper Wrapper;
	if (!TestTrue(TEXT("single-operator sampler contract world"), Wrapper.CreateTestWorld(EWorldType::Game))) return false;
	Wrapper.ForwardErrorMessages(this);
	UWorld* World = Wrapper.GetTestWorld();
	FURL URL;
	URL.AddOption(TEXT("game=/Script/Catfishing.CatfishingGameModeBase"));
	if (!World->SetGameMode(URL) || !Wrapper.BeginPlayInTestWorld()) return false;
	auto* Mode = World->GetAuthGameMode<ACatfishingGameModeBase>();
	Mode->bRunCommandsOpen = true;
	Mode->RunPublicState.Phase.Phase = ECatRunPhase::DayActive;
	Mode->RunPublicState.Phase.bNewFishingBitesAllowed = true;
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACatCharacter* Cat = World->SpawnActor<ACatCharacter>(FVector(0, 0, 20), FRotator::ZeroRotator, Spawn);
	APlayerState* Player = World->SpawnActor<ACatfishingPlayerState>();
	APlayerState* Deployer = World->SpawnActor<ACatfishingPlayerState>();
	auto* Controller = World->SpawnActor<ACatfishingPlayerController>();
	ACatFishingSession* Session = World->SpawnActor<ACatFishingSession>();
	ACatFishingRodActor* Rod = World->SpawnActor<ACatFishingRodActor>();
	if (!Cat || !Player || !Deployer || !Controller || !Session || !Rod) return false;
	Controller->PlayerState = Player;
	Controller->Possess(Cat);
	Controller->SetActorTickEnabled(false);
	Player->SetPlayerId(1);
	const FUniqueNetIdRef NetId = FUniqueNetIdString::Create(TEXT("OperatorSamplingContract"), FName(TEXT("CAT_TEST")));
	Player->SetUniqueId(FUniqueNetIdRepl(NetId));
	ACatfishingGameModeBase::FAdmissionRecord Admission;
	Admission.Phase = ACatfishingGameModeBase::EAdmissionPhase::Active;
	Admission.Controller = Controller;
	Mode->AdmissionRecords.Add(ACatfishingGameModeBase::MakeStableNetIdKey(Player->GetUniqueId()), Admission);
	UCatAbilitySystemComponent* ASC = Cat->GetCatAbilitySystemComponent();
	UCatPhysicalBodyComponent* Movement = Cat->GetPhysicalBodyComponent();
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(), 60.0f);
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 30.0f);
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 100.0f);
	if (!TestTrue(TEXT("the operator is admitted and borrows a real rod deployed by another player"), Mode->CanAcceptFishingCommand(Controller)
		&& Rod->InitializeAuthoritativeIdentity(FGuid::NewGuid(), FGuid::NewGuid(), 1997958, NAME_None, Deployer, nullptr, true, false)
		&& World->GetSubsystem<UCatFishingService>()->RegisterDeployedRod(Deployer, Rod))) return false;
	const auto HoldAndAuthorize = [&]()
	{
		return Rod->BeginPhysicalHoldFromAuthority(Player, true)
			&& Rod->SetPrimaryOperatorFromAuthority(Player, Rod->GetPresentationState().RodActorRevision) && Rod->GetPhysicalRodComponent()->CommitPrimaryHold(Player);
	};
	if (!TestTrue(TEXT("a real hand constraint precedes explicit primary control"), HoldAndAuthorize())) return false;
	UCatFishingFightRunner* Runner = NewObject<UCatFishingFightRunner>(Session);
	Runner->Session = Session;
	Runner->RodActor = Rod;
	Runner->AbilitySystem = ASC;
	Runner->bInitialized = Runner->bRunning = true;
	if (!TestTrue(TEXT("runner can resume an explicitly authorized borrower using their own ASC"),
		Runner->ResumePrimaryFromAuthority(Player, ASC, 100.0, 60.0, 30.0, 0, false, false))) return false;
	if (!Runner->BindPrimaryOperatorFromAuthority(Player, true, false, 0)) return false;
	Runner->Config.FixedStepSeconds = 0.05;
	Runner->Config.CatStaminaMaximum = 60.0;
	Runner->Config.FishMassKilograms = 3.0;
	Runner->Config.FishStrength = 40.0;
	Runner->Config.ReelSpeedCentimetersPerSecond = 80.0;
	Runner->Config.FishFullEffortSpeedCentimetersPerSecond = 75.0;
	Runner->Config.MaximumLineLengthCentimeters = 1000.0;
	Runner->Config.RodDurability = 1000.0;
	// 2026-09-13：本用例有「主控保留正常放线回体」的断言，需要成长项给出的正速率作前置。
	// 设计把**基础**放线回体定为 0（钓鱼规则 §4.4:213），9bfb4d5 把模拟器默认从 1.5 改成 0 之后
	// 这类断言永远不成立；同时它让「不该回体」的几条从空断言变成真检查。
	Runner->Config.SlackStaminaRegenPerSecond = 2.75;
	Runner->State.FishStamina = 100.0;
	Runner->State.LineLengthCentimeters = 500.0;
	Runner->State.FishWorldPosition = FVector(500.0, 0.0, 0.0);
	Runner->State.MotionIntent = ECatFishMotionIntent::StrugglingOutward;

	if (!TestTrue(TEXT("the production refresh reads the actual primary ASC"), Runner->UpdateOperatorIntentAndProperties())) return false;
	TestEqual(TEXT("only primary strength enters the reel model"), Runner->Config.PrimaryOperatorCatStrength, 100.0);
	TestEqual(TEXT("only primary stamina enters the session model"), Runner->State.CatStamina, 30.0);
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(), 75.0f);
	Runner->UpdateOperatorIntentAndProperties();
	TestEqual(TEXT("a changed primary maximum is sampled rather than cached at entry"), Runner->Config.CatStaminaMaximum, 75.0);
	TestEqual(TEXT("raising maximum does not refill the actual primary balance"), Runner->State.CatStamina, 30.0);
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(), 20.0f);
	Runner->UpdateOperatorIntentAndProperties();
	TestEqual(TEXT("only ASC clamps a reduced maximum"), Runner->State.CatStamina, 20.0);
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(), 60.0f);
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 30.0f);
	Runner->UpdateOperatorIntentAndProperties();
	FCatFightRodConstraintInput Constraint;
	Constraint.bRodHeld = true;
	Constraint.bPhysicalRodEndpoint = true;
	Constraint.RodForwardWorld = FVector::ForwardVector;
	const auto Step = FCatFishingFightSimulator::Step(Runner->Config, Runner->State, Constraint, FVector::ForwardVector);
	if (!TestTrue(TEXT("the live line solver generates one operation bill"), Step.bSucceeded && Step.CatStaminaDrain > 0.0)
		|| !TestTrue(TEXT("the production writer charges the primary ASC"), Runner->ApplyOperatorStaminaChanges(Step))) return false;
	const float Balance = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	TestEqual(TEXT("one operation bill is conserved in the actual ASC"), 30.0 - Balance, Step.CatStaminaDrain, 1e-5);
	TestFalse(TEXT("the frozen bill cannot be replayed"), Runner->ApplyOperatorStaminaChanges(Step));
	TestEqual(TEXT("replay rejection preserves the balance"), ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), Balance);

	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 1.e-9f);
	ASC->ApplyYellowFightStaminaDelta(20.0f);
	if (!Runner->UpdateOperatorIntentAndProperties()) return false;
	TestEqual(TEXT("yellow reserve participates in full operator strength"), Runner->Config.PrimaryOperatorCatStrength, 100.0);
	FCatFightStepResult ReserveBill;
	ReserveBill.CatReelStaminaDrain = 5.0;
	if (!TestTrue(TEXT("production runner can bill past the last green fraction"), Runner->ApplyOperatorStaminaChanges(ReserveBill))) return false;
	TestEqual(TEXT("green tiny balance is exhausted exactly"), ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), 0.0f);
	TestEqual(TEXT("runner charges remainder to yellow"), ASC->GetYellowFightStamina(), 15.0f);
	TestEqual(TEXT("session model reflects total actual remaining balance"), Runner->State.CatStamina, 15.0);
	TestEqual(TEXT("same-step summary capacity tracks remaining yellow rather than the pre-bill reserve"), Session->GetSnapshot().CombinedFightStaminaMaximum, 75.0);
	TestFalse(TEXT("yellow payment also rejects a replay"), Runner->ApplyOperatorStaminaChanges(ReserveBill));
	ASC->ClearYellowFightStaminaFromAuthority();

	// Equal totals cannot authorize a bill sampled from a different green/yellow composition.
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 30.0f);
	ASC->ApplyYellowFightStaminaDelta(20.0f);
	if (!Runner->UpdateOperatorIntentAndProperties()) return false;
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 20.0f);
	ASC->ApplyYellowFightStaminaDelta(10.0f);
	// 两次本地快照失配，加上后面的跨池旧账重放，合计三次有意拒绝。
	AddExpectedErrorPlain(TEXT("Event=fishing_stamina_bill_rejected"), EAutomationExpectedErrorFlags::Contains, 3);
	TestFalse(TEXT("same total with changed green/yellow rejects the old bill"), Runner->ApplyOperatorStaminaChanges(ReserveBill));
	TestEqual(TEXT("rejected bill preserves green"), ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), 20.0f);
	TestEqual(TEXT("rejected bill preserves yellow"), ASC->GetYellowFightStamina(), 30.0f);
	TestFalse(TEXT("a rejected old bill cannot be replayed"), Runner->ApplyOperatorStaminaChanges(ReserveBill));
	if (!Runner->UpdateOperatorIntentAndProperties()) return false;
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(), 75.0f);
	TestFalse(TEXT("changed green maximum rejects a bill even when balance is unchanged"), Runner->ApplyOperatorStaminaChanges(ReserveBill));
	TestEqual(TEXT("maximum mismatch leaves the full balance untouched"), ASC->GetTotalFightStamina(), 50.0);
	if (!Runner->UpdateOperatorIntentAndProperties()) return false;
	TestTrue(TEXT("a newly sampled bill can be paid after an attribute change"), Runner->ApplyOperatorStaminaChanges(ReserveBill));
	TestEqual(TEXT("fresh bill spends green first"), ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), 15.0f);
	TestEqual(TEXT("fresh bill retains yellow while green suffices"), ASC->GetYellowFightStamina(), 30.0f);
	ASC->ClearYellowFightStaminaFromAuthority();
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(), 60.0f);

	// Contract evidence: these are controlled body observations and sampling times, not a
	// second physics integrator. Separate Chaos runtime tests verify actual motor travel.
	Runner->State.LineLengthCentimeters = 800.0;
	Runner->State.FishEffortRatio = 0.0;
	TArray<double> ScheduleDrains, ScheduleTravel;
	for (int32 Schedule = 0; Schedule < 2; ++Schedule)
	{
		ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 30.0f);
		Movement->TeleportBodyFromAuthority(FTransform(FVector(-1000, 0, 20)), TEXT("SettlementFixtureStart"));
		if (!TestTrue(TEXT("reset fixture reestablishes real hand contact and primary control"), HoldAndAuthorize())) return false;
		Movement->SetMovementSpeed(600.0);
		Movement->SetMoveIntent(FVector::ForwardVector);
		FCatFightOperatorRuntime& Operator = Runner->OperatorState;
		Operator.bPullHeld = Operator.bSlackHeld = false;
		Operator.LastSampledPosition = Movement->GetBody()->GetComponentLocation();
		Operator.LastBodyResetEpoch = Movement->GetResetEpoch();
		Operator.LastMovementSampleWorldSeconds = World->GetTimeSeconds();
		Operator.PendingMovementSamples.Reset();
		Operator.bHasSampledPosition = true;
		const double StartX = Movement->GetBody()->GetComponentLocation().X;
		for (int32 FixedStepIndex = 0; FixedStepIndex < 2; ++FixedStepIndex)
		{
			if (Schedule == 1 || FixedStepIndex == 0)
			{
				const float FrameSeconds = Schedule == 0 ? 0.10f : 0.05f;
				World->TimeSeconds += FrameSeconds;
				Movement->GetBody()->SetWorldLocation(Movement->GetBody()->GetComponentLocation()
					+ FVector(300.0 * FrameSeconds, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
			}
			if (!TestTrue(TEXT("each fixed step freezes the real body observation"), Runner->UpdateOperatorIntentAndProperties())) return false;
			if (Schedule == 0 && FixedStepIndex == 0)
			{
				const auto& Pending = Runner->OperatorState.PendingMovementSamples;
				TestTrue(TEXT("a 100 ms sample retains the second 50 ms and its progress"), Pending.Num() == 1
					&& FMath::IsNearlyEqual(Pending[0].DurationSeconds, 0.05, 1e-7)
					&& Pending[0].ActualDisplacementCentimeters.X > 14.9);
			}
			const auto MovementStep = FCatFishingFightSimulator::Step(Runner->Config, Runner->State, Constraint, FVector::ForwardVector);
			if (!TestTrue(TEXT("unloaded line has no extra operation bill"), MovementStep.bSucceeded && MovementStep.CatStaminaDrain == 0.0)
				|| !TestTrue(TEXT("personal movement bill reaches the sole ASC writer"), Runner->ApplyOperatorStaminaChanges(MovementStep))) return false;
		}
		ScheduleDrains.Add(30.0 - ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()));
		ScheduleTravel.Add(Movement->GetBody()->GetComponentLocation().X - StartX);
		double RemainingSeconds = 0.0;
		for (const auto& Pending : Runner->OperatorState.PendingMovementSamples) RemainingSeconds += Pending.DurationSeconds;
		TestTrue(TEXT("two fixed steps consume the complete sample exactly once"), RemainingSeconds < 1e-7);
	}
	TestTrue(TEXT("schedule comparison includes real observations and a nonzero ASC bill"), ScheduleTravel[0] > 29.9 && ScheduleDrains[0] > 0.1);
	TestEqual(TEXT("one 100 ms frame and two 50 ms frames conserve observed progress"), ScheduleTravel[0], ScheduleTravel[1], 1e-4);
	TestEqual(TEXT("catch-up timers cannot double-charge one body's movement"), ScheduleDrains[0], ScheduleDrains[1], 1e-5);

	World->TimeSeconds += 0.05;
	const uint32 BeforeReset = Movement->GetResetEpoch();
	Movement->TeleportBodyFromAuthority(FTransform(Movement->GetBody()->GetComponentLocation() + FVector(1000, 0, 0)), TEXT("SettlementCorrection"));
	// Teleport really releases the rod. Rebuild contact and explicitly authorize the same
	// deployment owner before resuming the sampler; no helper or automatic promotion exists.
	if (!TestTrue(TEXT("the sampler continues only after real owner regrip"), HoldAndAuthorize())) return false;
	Movement->SetMoveIntent(FVector::ForwardVector);
	TestTrue(TEXT("teleport changes the actual body reset epoch"), Movement->GetResetEpoch() != BeforeReset);
	if (!Runner->UpdateOperatorIntentAndProperties()) return false;
	for (const auto& Sample : Runner->FrozenOperatorMovementSamples)
		TestTrue(TEXT("teleport cannot be billed as voluntary progress"), Sample.ActualDisplacementCentimeters.IsNearlyZero());
	const auto TeleportStep = FCatFishingFightSimulator::Step(Runner->Config, Runner->State, Constraint, FVector::ForwardVector);
	TestTrue(TEXT("reset boundary has no observed duration to bill"), TeleportStep.bSucceeded && Runner->ApplyOperatorStaminaChanges(TeleportStep)
		&& Runner->LastOperatorStaminaDrain == 0.0);
	World->TimeSeconds += 0.05;
	if (!Runner->UpdateOperatorIntentAndProperties()) return false;
	const auto Blocked = FCatFishingFightSimulator::Step(Runner->Config, Runner->State, Constraint, FVector::ForwardVector);
	// 墓碑（2026-09-14）：旧 >0.09 断言仍按未完成位移计费；现行 W 已是固定 1.5 点/秒。
	// 本轮不改该玩法公式，只验证 50ms 的实际 ASC 回执为 0.075 点（钓鱼规则 §4.4）。
	TestTrue(TEXT("the next interval reaches the authoritative writer"),Blocked.bSucceeded && Runner->ApplyOperatorStaminaChanges(Blocked));
	TestEqual(TEXT("50 ms forward intention costs 0.075 stamina even when blocked"),Runner->LastOperatorStaminaDrain,0.075,1e-5);
	Movement->SetMoveIntent(FVector::ZeroVector);
	World->TimeSeconds += 0.05;
	Movement->GetBody()->SetWorldLocation(Movement->GetBody()->GetComponentLocation() + FVector(10, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
	if (!Runner->UpdateOperatorIntentAndProperties()) return false;
	const auto Passive = FCatFishingFightSimulator::Step(Runner->Config, Runner->State, Constraint, FVector::ForwardVector);
	TestTrue(TEXT("passive body movement cannot create a voluntary bill"), Passive.bSucceeded && Runner->ApplyOperatorStaminaChanges(Passive)
		&& Runner->LastOperatorStaminaDrain == 0.0);
	// 修正卷线能力不得顺带改变腿部价格：真实身体输入、固定步采样和唯一ASC写口继续保留四方向契约。
	const FVector InputForward = FRotator(0.0, Movement->GetViewIntent().Yaw, 0.0).Vector();
	const FVector InputSide(-InputForward.Y, InputForward.X, 0.0);
	const FVector MoveIntents[] = {InputForward, -InputForward, InputSide, -InputSide};
	const double ExpectedMovementDrains[] = {0.075, 0.15, 0.0, 0.0};
	for (int32 DirectionIndex = 0; DirectionIndex < UE_ARRAY_COUNT(MoveIntents); ++DirectionIndex)
	{
		Movement->SetMoveIntent(MoveIntents[DirectionIndex]);
		World->TimeSeconds += 0.05;
		if (!TestTrue(TEXT("direction-specific movement still uses the primary production sampler"), Runner->UpdateOperatorIntentAndProperties())) return false;
		FCatFightStepResult MovementOnly;
		if (!TestTrue(TEXT("direction-specific movement still uses one ASC settlement"), Runner->ApplyOperatorStaminaChanges(MovementOnly))) return false;
		TestEqual(TEXT("forward/backward rates and the unpriced lateral direction stay unchanged"), Runner->LastOperatorStaminaDrain, ExpectedMovementDrains[DirectionIndex], 1e-5);
		TestFalse(TEXT("the directional movement bill cannot be replayed"), Runner->ApplyOperatorStaminaChanges(MovementOnly));
	}
	Movement->SetMoveIntent(FVector::ZeroVector);
	for (int32 Phase = 0; Phase < 3; ++Phase)
	{
		Runner->State.bFishExhausted = Phase == 1;
		Runner->State.LineLengthCentimeters = Phase == 2 ? Runner->Config.MaximumLineLengthCentimeters : 800.0;
		Runner->OperatorState.bSlackHeld = true;
		World->TimeSeconds += 0.05;
		Movement->SetMoveIntent(FVector::ForwardVector);
		Movement->GetBody()->SetWorldLocation(Movement->GetBody()->GetComponentLocation() + FVector(10, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
		if (!Runner->UpdateOperatorIntentAndProperties()) return false;
		TestTrue(TEXT("free-phase checks include a nonempty voluntary body sample"), !Runner->FrozenOperatorMovementSamples.IsEmpty()
			&& Runner->FrozenOperatorMovementSamples[0].ActualDisplacementCentimeters.X > 9.9);
		const auto PhaseStep = FCatFishingFightSimulator::Step(Runner->Config, Runner->State, Constraint, FVector::ForwardVector);
		if (!TestTrue(TEXT("phase policy and movement reach one ASC writer"), PhaseStep.bSucceeded && Runner->ApplyOperatorStaminaChanges(PhaseStep))) return false;
		if (Phase < 2) TestTrue(TEXT("slack recovery and exhausted fish suppress voluntary movement charges"), Runner->LastOperatorStaminaDrain <= 0.0);
		else TestTrue(TEXT("full spool restores the real primary movement bill"), !PhaseStep.bSlackRecoveryActive && Runner->LastOperatorStaminaDrain > 0.0);
	}
	AddInfo(FString::Printf(TEXT("Event=fishing_operator_sampling_contract_verified OneFrameTravelCm=%.3f TwoFrameTravelCm=%.3f OneFrameDrain=%.6f TwoFrameDrain=%.6f Evidence=contract"),
		ScheduleTravel[0], ScheduleTravel[1], ScheduleDrains[0], ScheduleDrains[1]));
	Runner->State.bFishExhausted = false;
	Runner->State.LineLengthCentimeters = 800;
	Movement->SetMoveIntent(FVector::ZeroVector);
	for (int32 LoadCase=0; LoadCase<4; ++LoadCase)
	{
		Movement->ClearExternalForce(Session);
		Movement->ClearExternalForce(Rod);
		if (LoadCase<2) Movement->SetExternalForceFromAuthority(Session, FVector(1000,0,0));
		if (LoadCase==1) Movement->SetExternalForceFromAuthority(Rod, FVector(-1000,0,0));
		World->TimeSeconds += .05;
		if (!Runner->UpdateOperatorIntentAndProperties()) return false;
		auto SlackStep = FCatFishingFightSimulator::Step(Runner->Config, Runner->State, Constraint, FVector::ForwardVector);
		if (!TestTrue(TEXT("load recovery cases retain the existing free-spool action"), SlackStep.bSucceeded && SlackStep.bSlackRecoveryActive)) return false;
		if (LoadCase==2) SlackStep.RodLineForceNewtons = FVector(1,0,0);
		if (!Runner->ApplyOperatorStaminaChanges(SlackStep)) return false;
		if (LoadCase<3) TestEqual(TEXT("one primary ASC writer blocks recovery for applied or cancelling loads"), Runner->LastOperatorStaminaDrain, 0.0);
		else TestTrue(TEXT("the primary retains normal unloaded slack recovery"), Runner->LastOperatorStaminaDrain<0);
	}

	// 2026-09-14 裁决②④⑥：生产读取/冻结/写口/真实持竿的完整黄色体力消费者回归。
	Runner->OperatorState.bSlackHeld = false;
	Runner->OperatorState.bPullHeld = true;
	Movement->SetMoveIntent(FVector::ZeroVector);
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(),1.0f);
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetYellowFightStaminaAttribute(),80.0f);
	TestTrue(TEXT("entry binding accepts a total larger than the green maximum"), Runner->BindPrimaryOperatorFromAuthority(Player,true,false,0));
	TestTrue(TEXT("yellow-inclusive snapshot freezes successfully"), Runner->UpdateOperatorIntentAndProperties());
	TestEqual(TEXT("snapshot contains green plus yellow"),Runner->State.CatStamina,81.0);
	TestEqual(TEXT("snapshot capacity includes current yellow, not just green maximum"),Runner->Config.CatStaminaMaximum,140.0);
	FCatFightStepResult CrossPoolBill;
	CrossPoolBill.CatReelStaminaDrain = 2.0;
	TestTrue(TEXT("one production bill crosses green into yellow"),Runner->ApplyOperatorStaminaChanges(CrossPoolBill));
	TestEqual(TEXT("green is consumed first"),ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()),0.0f);
	TestEqual(TEXT("overflow is paid by yellow"),ASC->GetYellowFightStamina(),79.0f);
	TestEqual(TEXT("actual receipt includes both pools"),Runner->LastOperatorStaminaDrain,2.0);
	TestEqual(TEXT("session publishes total balance"),Session->GetSnapshot().CombinedFightStamina,79.0);
	TestEqual(TEXT("session publishes remaining total capacity"),Session->GetSnapshot().CombinedFightStaminaMaximum,139.0);
	TestFalse(TEXT("cross-pool charge is not replayable"),Runner->ApplyOperatorStaminaChanges(CrossPoolBill));
	TestTrue(TEXT("yellow-only snapshot retains full strength"),Runner->UpdateOperatorIntentAndProperties()
		&& Runner->Config.PrimaryOperatorCatStrength==100.0);
	auto* PhysicalRod = Rod->GetPhysicalRodComponent();
	PhysicalRod->UpdatePrimaryMotorBudget();
	TestEqual(TEXT("yellow-only primary motor gets the full force budget"),Movement->CaptureDriveSample().MaxForce,10000.0);
	Rod->CarrierConstraintState.bFightActive = true;
	Rod->CarrierConstraintState.CatTorqueCapacityStrengthMeters = 100.0;
	FCatFishingRodRotationInput Rotation;
	TestTrue(TEXT("yellow-only controlled rotation keeps its torque"),PhysicalRod->BuildControlledRotationInput(Rotation) && Rotation.CatTorqueCapacity==100.0);
	FString StableId;
	ACatCharacter* CapableCharacter = nullptr;
	double CapabilityStrength=0,CapabilityStamina=0;
	TestTrue(TEXT("selection and group capability accept yellow-only stamina"),UCatFishingService::TryGetFightCapability(Controller,StableId,CapableCharacter,CapabilityStrength,CapabilityStamina)
		&& CapabilityStrength==100.0 && CapabilityStamina==79.0);
	// Equal total but different segments must invalidate a frozen bill.
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(),1.0f);
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetYellowFightStaminaAttribute(),78.0f);
	TestFalse(TEXT("same total cannot hide changed frozen segments"),Runner->ApplyOperatorStaminaChanges(CrossPoolBill));
	TestEqual(TEXT("rejected freeze does not spend"),ASC->GetTotalFightStamina(),79.0);
	Runner->OperatorState.bSlackHeld = true;
	Runner->OperatorState.bPullHeld = false;
	Runner->UpdateOperatorIntentAndProperties();
	FCatFightStepResult RecoveryStep;
	RecoveryStep.bSlackRecoveryActive = true;
	TestTrue(TEXT("slack recovery works even with total above green maximum"),Runner->ApplyOperatorStaminaChanges(RecoveryStep));
	TestTrue(TEXT("recovery writes green"),ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute())>1.0f);
	TestEqual(TEXT("recovery cannot recreate yellow"),ASC->GetYellowFightStamina(),78.0f);
	Runner->OperatorState.bSlackHeld = false;
	Runner->UpdateOperatorIntentAndProperties();
	CrossPoolBill.CatReelStaminaDrain = 1000;
	TestTrue(TEXT("drain caps at total balance"),Runner->ApplyOperatorStaminaChanges(CrossPoolBill));
	TestEqual(TEXT("both segments exhausted"),ASC->GetTotalFightStamina(),0.0);
	Runner->UpdateOperatorIntentAndProperties();
	TestTrue(TEXT("only dual zero invokes the exhausted-cat escape policy"),FCatFishingFightSimulator::ShouldEscapeExhaustedCat(Runner->Config,Runner->State,true));
	PhysicalRod->UpdatePrimaryMotorBudget();
	TestEqual(TEXT("dual-zero primary motor is zero"),Movement->CaptureDriveSample().MaxForce,0.0);
	TestTrue(TEXT("dual-zero rod torque is zero"),PhysicalRod->BuildControlledRotationInput(Rotation) && Rotation.CatTorqueCapacity==0.0);
	PhysicalRod->ReleasePrimaryHold(Player,TEXT("ZeroTakeoverFixture"));
	TestTrue(TEXT("zero-stamina cat can physically retake an unattended rod"),HoldAndAuthorize());
	ECatFishingCommandError HandoffError;
	auto* Service = World->GetSubsystem<UCatFishingService>();
	Session->Snapshot.HandoffRequestedByPlayerState = nullptr;
	TestFalse(TEXT("zero stamina does not bypass the consent handshake"),Service->CanAcceptHandoffTakeover(Session,Rod,Controller,HandoffError));
	Session->Snapshot.HandoffRequestedByPlayerState = Player;
	TestTrue(TEXT("a valid consent request has no stamina threshold"),Service->CanAcceptHandoffTakeover(Session,Rod,Controller,HandoffError)
		&& HandoffError==ECatFishingCommandError::None);
	Session->Snapshot.HandoffRequestedByPlayerState = Deployer;
	TestFalse(TEXT("a stale request from a former operator remains invalid"),Service->CanAcceptHandoffTakeover(Session,Rod,Controller,HandoffError));
	Session->Snapshot.HandoffRequestedByPlayerState = nullptr;
	Session->Snapshot.FishingSessionId = FGuid::NewGuid();
	Session->Snapshot.RodActor = Rod;
	Session->Snapshot.FisherPlayerState = Player;
	Session->Snapshot.Phase = ECatFishingPhase::Waiting;
	Session->FightRunner = Runner;
	Service->Sessions.Add(Session->Snapshot.FishingSessionId,Session);
	Runner->bRunning = false;
	PhysicalRod->UpdatePrimaryMotorBudget();
	FCatBodyDriveSample WaitingDrive = Movement->CaptureDriveSample();
	WaitingDrive.bUnderLoad = false;
	TestTrue(TEXT("waiting fixture still owns the fishing motor"),WaitingDrive.bFishing);
	auto* Effort = Cat->FindComponentByClass<UCatPhysicalEffortComponent>();
	Rod->CarrierConstraintState.bFightActive = false;
	const double BeforeRest = ASC->GetTotalFightStamina();
	Effort->SettleMovementFromAuthority(WaitingDrive,FVector::ZeroVector,FVector::ZeroVector,0.05,true);
	TestTrue(TEXT("holding an unloaded waiting rod immediately recovers at five points per second"),
		FMath::IsNearlyEqual(ASC->GetTotalFightStamina() - BeforeRest, 0.25, 0.0001));
	TestEqual(TEXT("natural recovery never recreates yellow stamina"),ASC->GetYellowFightStamina(),0.0f);
	Session->Snapshot.Phase = ECatFishingPhase::HookedFight;
	Runner->bRunning = true;
	const double BeforeFightRest = ASC->GetTotalFightStamina();
	Effort->SettleMovementFromAuthority(WaitingDrive,FVector::ZeroVector,FVector::ZeroVector,0.05,true);
	TestEqual(TEXT("a running fight retains the runner as its sole stamina writer"),ASC->GetTotalFightStamina(),BeforeFightRest);
	Runner->bRunning = false;
	Service->Sessions.Remove(Session->Snapshot.FishingSessionId);
	ASC->ApplyYellowFightStaminaDelta(20.0f);
	const float GreenBeforeDay = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	Mode->RunPublicState.Phase.RunId = FGuid::NewGuid();
	Mode->bRunStartupInProgress = true;
	const auto Day = Mode->EnterRunPhaseFromStateTree(ECatRunPhase::DayActive, ECatRunTransitionReason::AllEligibleReady);
	Mode->bRunStartupInProgress = false;
	TestTrue(TEXT("formal day-entry authority path succeeds"), Day.bApplied);
	TestEqual(TEXT("day entry clears the actual body's yellow reserve"), ASC->GetYellowFightStamina(), 0.0f);
	TestEqual(TEXT("day entry preserves green balance"), ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), GreenBeforeDay);
	return !HasAnyErrors();
}
#endif
