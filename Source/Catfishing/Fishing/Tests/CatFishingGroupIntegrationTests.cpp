#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Character/CatCharacter.h"
#include "Character/CatCharacterMovementComponent.h"
#include "Engine/World.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Simulation/CatFishingFightRunner.h"
#include "GameFramework/PlayerState.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingGroupRunnerIntegrationTest,
	"Catfishing.Unit.Fishing.Runner.FourMembersUseAcceptedMovementAndIndependentASCSettlement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingGroupRunnerIntegrationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper Wrapper;
	if (!TestTrue(TEXT("创建四人生产Runner受控世界"), Wrapper.CreateTestWorld(EWorldType::Game))) return false;
	Wrapper.ForwardErrorMessages(this);
	UWorld* World = Wrapper.GetTestWorld();
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACatFishingSession* Session = World->SpawnActor<ACatFishingSession>();
	ACatFishingRodActor* Rod = World->SpawnActor<ACatFishingRodActor>();
	if (!TestTrue(TEXT("创建真实权威会话与鱼竿"), Session && Rod && Session->HasAuthority())) return false;
	UCatFishingFightRunner* Runner = NewObject<UCatFishingFightRunner>(Session);
	Runner->Session = Session;
	Runner->RodActor = Rod;
	Runner->bInitialized = Runner->bRunning = true;
	Runner->Config.FixedStepSeconds = 0.05;
	Runner->Config.CatStaminaMaximum = 60.0;
	Runner->Config.FishMassKilograms = 3.0;
	Runner->Config.FishStrength = 40.0;
	Runner->Config.ReelSpeedCentimetersPerSecond = 80.0;
	Runner->Config.FishFullEffortSpeedCentimetersPerSecond = 75.0;
	Runner->Config.MaximumLineLengthCentimeters = 1000.0;
	Runner->Config.RodDurability = 1000.0;
	Runner->State.FishStamina = 100.0;
	Runner->State.LineLengthCentimeters = 500.0;
	Runner->State.FishWorldPosition = FVector(500.0, 0.0, 0.0);
	Runner->State.MotionIntent = ECatFishMotionIntent::StrugglingOutward;
	TArray<ACatCharacter*> Cats;
	TArray<APlayerState*> Players;
	TArray<UCatAbilitySystemComponent*> AbilitySystems;
	TArray<UCatCharacterMovementComponent*> Movements;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		ACatCharacter* Cat = World->SpawnActor<ACatCharacter>(FVector(0.0, 300.0 * Index, 0.0), FRotator::ZeroRotator, Spawn);
		APlayerState* Player = World->SpawnActor<APlayerState>();
		if (!TestTrue(TEXT("逐一生成真实角色与玩家身份"), Cat && Player)) return false;
		Player->SetPlayerId(Index + 1);
		Cat->SetPlayerState(Player);
		UCatAbilitySystemComponent* ASC = Cat->GetCatAbilitySystemComponent();
		UCatCharacterMovementComponent* Movement = Cast<UCatCharacterMovementComponent>(Cat->GetCharacterMovement());
		if (!TestTrue(TEXT("每位成员使用真实ASC与项目CMC"), ASC && Movement)) return false;
		ASC->InitAbilityActorInfo(Cat, Cat);
		ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(), 60.0f);
		ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 30.0f);
		ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 100.0f);
		Movement->bRunPhysicsWithNoController = true;
		Movement->SetMovementMode(MOVE_Flying);
		Movement->BrakingDecelerationFlying = 0.0f;
		FCatFightParticipantRuntime Participant;
		Participant.PlayerState = Player;
		Participant.Character = Cat;
		Participant.AbilitySystem = ASC;
		Participant.StaminaMaximum = 60.0;
		Participant.bPrimary = Index == 0;
		Participant.bPullHeld = Index == 0;
		Runner->Participants.Add(Player, Participant);
		Cats.Add(Cat);
		Players.Add(Player);
		AbilitySystems.Add(ASC);
		Movements.Add(Movement);
	}
	Runner->AbilitySystem = AbilitySystems[0];
	if (!TestTrue(TEXT("生产刷新从四份真实ASC形成冻结输入"), Runner->UpdateParticipantIntentAndProperties())) return false;
	TestEqual(TEXT("主位与三名辅助的折扣力量进入同一求解配置"), Runner->Config.GetCombinedCatStrength(), 250.0);
	TestEqual(TEXT("四份个人余额合计展示而没有混为主位余额"), Runner->GroupResult.TotalCurrentStamina, 120.0);
	TestEqual(TEXT("个人上限完整合计"), Runner->GroupResult.TotalMaximumStamina, 240.0);
	TestEqual(TEXT("身份映射与本步成员输入一致"), Runner->FrozenParticipantPlayers.Num(), 4);
	TestEqual(TEXT("四人冻结列表只有一个操竿角色"), Runner->GroupInput.Participants.FilterByPredicate(
		[](const FCatFightGroupParticipantInput& Participant) { return Participant.bPrimary; }).Num(), 1);
	TestFalse(TEXT("辅助不能通过旧左键入口改变线杯操作"), Runner->SetReeling(Players[3], 1, true));
	TestFalse(TEXT("辅助不能替代主位放线"), Runner->SetSlacking(Players[3], 2, true));

	// 身体上限与默认配置、入场缓存独立；下一次生产刷新必须消费真实 ASC 的当前上限。
	AbilitySystems[0]->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(), 75.0f);
	AbilitySystems[1]->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(), 90.0f);
	if (!TestTrue(TEXT("运行中修改真实ASC上限后下一固定步刷新成功"), Runner->UpdateParticipantIntentAndProperties())) return false;
	TestEqual(TEXT("主位模拟上限采用75而非默认配置或入场缓存60"), Runner->Config.CatStaminaMaximum, 75.0);
	TestEqual(TEXT("不同身体ASC上限按实际值合计"), Runner->GroupResult.TotalMaximumStamina, 285.0);
	TestEqual(TEXT("辅助上限观察缓存跟随其最新ASC属性"), Runner->FindParticipant(Players[1])->StaminaMaximum, 90.0);
	TestEqual(TEXT("上限增加不补满任何成员的真实余额"), Runner->GroupResult.TotalCurrentStamina, 120.0);
	AbilitySystems[1]->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(), 20.0f);
	if (!TestTrue(TEXT("真实ASC降低上限后下一固定步刷新成功"), Runner->UpdateParticipantIntentAndProperties())) return false;
	TestEqual(TEXT("降低上限时只由ASC把本人余额夹到新上限"),
		AbilitySystems[1]->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), 20.0f);
	TestEqual(TEXT("降低后的成员上限没有保留旧缓存"), Runner->GroupResult.TotalMaximumStamina, 215.0);
	TestEqual(TEXT("上限下降不影响其他三名成员余额"), Runner->GroupResult.TotalCurrentStamina, 110.0);
	for (UCatAbilitySystemComponent* ASC : AbilitySystems)
	{
		ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(), 60.0f);
		ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 30.0f);
	}
	if (!TestTrue(TEXT("恢复原费用测试的相同身体初值"), Runner->UpdateParticipantIntentAndProperties())) return false;

	// Use the actual CMC network-input acceptance path. Do not replace the Runner's group output.
	for (UCatCharacterMovementComponent* Movement : Movements)
	{
		Movement->MoveAutonomous(0.05f, 0.05f, 0, -FVector::ForwardVector * Movement->GetMaxAcceleration());
		TestTrue(TEXT("CMC保留服务器接受的后退输入"), Movement->GetAcceptedFishingMoveIntent().X < -0.99);
	}
	if (!TestTrue(TEXT("四人真实后退意图进入生产刷新"), Runner->UpdateParticipantIntentAndProperties())) return false;
	const double CooperativeResistance = Runner->GroupResult.SignedResistanceStrength;
	const double CooperativeSpeed = Runner->GroupResult.DesiredVelocityCentimetersPerSecond.X;
	Movements[3]->MoveAutonomous(0.10f, 0.05f, 0, FVector::ForwardVector * Movements[3]->GetMaxAcceleration());
	if (!TestTrue(TEXT("一名辅助反向输入进入同一生产刷新"), Runner->UpdateParticipantIntentAndProperties())) return false;
	TestTrue(TEXT("队友反向切实削弱沿线抵抗"), Runner->GroupResult.SignedResistanceStrength < CooperativeResistance);
	TestTrue(TEXT("反向输入切实减慢共同后退目标"), Runner->GroupResult.DesiredVelocityCentimetersPerSecond.X > CooperativeSpeed);
	for (UCatCharacterMovementComponent* Movement : Movements)
	{
		Movement->MoveAutonomous(0.15f, 0.05f, 0, FVector::ZeroVector);
		Movement->Velocity = FVector::ZeroVector;
	}
	if (!TestTrue(TEXT("停止主动移动后冻结静止成员"), Runner->UpdateParticipantIntentAndProperties())) return false;
	FCatFightRodConstraintInput Constraint;
	Constraint.bRodHeld = true;
	Constraint.RodForwardWorld = FVector::ForwardVector;
	const FCatFightStepResult Step = FCatFishingFightSimulator::Step(Runner->Config, Runner->State, Constraint, FVector::ForwardVector);
	if (!TestTrue(TEXT("真实四人输入生成一次共同收线负担"), Step.bSucceeded && Step.GetSharedCatStaminaDrain() > 0.0)) return false;
	if (!TestTrue(TEXT("共同账单经生产入口逐一写入四个ASC"), Runner->ApplyGroupStaminaChanges(Step))) return false;
	double TotalPaid = 0.0;
	double FirstPaid = -1.0;
	for (UCatAbilitySystemComponent* ASC : AbilitySystems)
	{
		const double Paid = 30.0 - ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
		TestTrue(TEXT("实际参与者各自承担共同负担"), Paid > 0.0);
		if (FirstPaid < 0.0) FirstPaid = Paid;
		else TestEqual(TEXT("同条件主辅均分，辅助力量折扣不会变成费用折扣"), Paid, FirstPaid, 1e-5);
		TotalPaid += Paid;
	}
	TestEqual(TEXT("真实总扣费与一次共同模拟账单守恒"), TotalPaid, Step.GetSharedCatStaminaDrain(), 1e-5);
	TestFalse(TEXT("同一固定步的共同账单不能重放第二次"), Runner->ApplyGroupStaminaChanges(Step));
	double TotalAfterRejectedReplay = 0.0;
	for (UCatAbilitySystemComponent* ASC : AbilitySystems)
		TotalAfterRejectedReplay += ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	TestEqual(TEXT("拒绝重复支付后四份真实ASC余额保持"), TotalAfterRejectedReplay, 120.0 - TotalPaid, 1e-5);

	// Exercise the production sampler and ASC writer with the same real CMC travel. The controlled
	// world clock models one 100ms server frame with two timer callbacks versus two 50ms frames.
	const FCatFightSimulationState SavedState = Runner->State;
	Runner->State.LineLengthCentimeters = 800.0;
	Runner->State.FishEffortRatio = 0.0;
	TArray<double> ScheduleDrains;
	TArray<double> ScheduleTravel;
	for (int32 Schedule = 0; Schedule < 2; ++Schedule)
	{
		TArray<FVector> StartPositions;
		for (int32 Index = 0; Index < Cats.Num(); ++Index)
		{
			AbilitySystems[Index]->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 30.0f);
			Cats[Index]->SetActorLocation(FVector(-1000.0, 300.0 * Index, 0.0), false, nullptr, ETeleportType::TeleportPhysics);
			Movements[Index]->MaxFlySpeed = 600.0f;
			Movements[Index]->Velocity = FVector(600.0, 0.0, 0.0);
			FCatFightParticipantRuntime& Participant = *Runner->FindParticipant(Players[Index]);
			Participant.bPullHeld = Participant.bSlackHeld = false;
			Participant.LastSampledPosition = Cats[Index]->GetActorLocation();
			Participant.LastMovementSampleWorldSeconds = World->GetTimeSeconds();
			Participant.PendingMovementSamples.Reset();
			Participant.bHasSampledPosition = true;
			StartPositions.Add(Cats[Index]->GetActorLocation());
		}
		for (int32 FixedStepIndex = 0; FixedStepIndex < 2; ++FixedStepIndex)
		{
			if (Schedule == 1 || FixedStepIndex == 0)
			{
				const float FrameSeconds = Schedule == 0 ? 0.10f : 0.05f;
				World->TimeSeconds += FrameSeconds;
				for (UCatCharacterMovementComponent* Movement : Movements)
					Movement->MoveAutonomous(static_cast<float>(World->GetTimeSeconds()), FrameSeconds, 0,
						FVector::ForwardVector * Movement->GetMaxAcceleration());
			}
			if (!TestTrue(TEXT("两种帧率均从真实CMC冻结固定步样本"), Runner->UpdateParticipantIntentAndProperties())) return false;
			if (Schedule == 0 && FixedStepIndex == 0)
			{
				const auto& Pending = Runner->FindParticipant(Players[0])->PendingMovementSamples;
				TestTrue(TEXT("100ms样本首步结算后仍保留第二步时间与真实进展"), Pending.Num() == 1
					&& FMath::IsNearlyEqual(Pending[0].DurationSeconds, 0.05, 1e-7)
					&& Pending[0].ActualDisplacementCentimeters.X > 29.9);
			}
			const auto MovementStep = FCatFishingFightSimulator::Step(Runner->Config, Runner->State, Constraint, FVector::ForwardVector);
			if (!TestTrue(TEXT("无负载同一求解器产生有效零共同账单"), MovementStep.bSucceeded
				&& MovementStep.GetSharedCatStaminaDrain() == 0.0)) return false;
			if (!TestTrue(TEXT("逐人运动账单写回真实ASC"), Runner->ApplyGroupStaminaChanges(MovementStep))) return false;
		}
		double Paid = 0.0, Travel = 0.0;
		for (int32 Index = 0; Index < Cats.Num(); ++Index)
		{
			Paid += 30.0 - AbilitySystems[Index]->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
			Travel += Cats[Index]->GetActorLocation().X - StartPositions[Index].X;
			double RemainingSeconds = 0.0;
			for (const auto& Pending : Runner->FindParticipant(Players[Index])->PendingMovementSamples)
				RemainingSeconds += Pending.DurationSeconds;
			TestTrue(TEXT("两个固定步消费完整样本，不留可重复收费的位移"), RemainingSeconds < 1e-7);
		}
		ScheduleDrains.Add(Paid);
		ScheduleTravel.Add(Travel);
	}
	TestTrue(TEXT("帧率比较包含实际移动与实际个人扣费"), ScheduleTravel[0] > 200.0 && ScheduleDrains[0] > 0.1);
	TestEqual(TEXT("一帧100ms与两帧50ms真实身体总位移一致"), ScheduleTravel[0], ScheduleTravel[1], 1e-4);
	TestEqual(TEXT("一帧追赶两步与分帧两步的四ASC扣费守恒"), ScheduleDrains[0], ScheduleDrains[1], 1e-5);

	// A teleport is position correction, not progress that can erase blocked-effort cost.
	World->TimeSeconds += 0.05;
	for (int32 Index = 0; Index < Cats.Num(); ++Index)
	{
		TestTrue(TEXT("通过Pawn正式瞬移入口调整位置"), Cats[Index]->TeleportTo(
			Cats[Index]->GetActorLocation() + FVector(1000.0, 0.0, 0.0), Cats[Index]->GetActorRotation(), false, true));
		TestTrue(TEXT("真实角色瞬移向CMC标明位置纠正"), Movements[Index]->bJustTeleported != 0);
	}
	if (!TestTrue(TEXT("瞬移后仍正常冻结本步个人移动样本"), Runner->UpdateParticipantIntentAndProperties())) return false;
	for (const auto& Samples : Runner->FrozenParticipantMovementSamples)
		for (const auto& Sample : Samples)
			TestTrue(TEXT("瞬移不能冒充主动位移抵掉受阻费用"), Sample.ActualDisplacementCentimeters.IsNearlyZero());
	const auto TeleportStep = FCatFishingFightSimulator::Step(Runner->Config, Runner->State, Constraint, FVector::ForwardVector);
	TestTrue(TEXT("瞬移步仍通过原公式结算真实主动尝试"), TeleportStep.bSucceeded && Runner->ApplyGroupStaminaChanges(TeleportStep)
		&& Runner->LastGroupStaminaDrain > 0.39);
	for (UCatCharacterMovementComponent* Movement : Movements)
	{
		Movement->MoveAutonomous(static_cast<float>(World->GetTimeSeconds()), 0.001f, 0, FVector::ZeroVector);
		Movement->Velocity = FVector::ZeroVector;
	}
	World->TimeSeconds += 0.05;
	for (ACatCharacter* Cat : Cats)
		Cat->SetActorLocation(Cat->GetActorLocation() + FVector(10.0, 0.0, 0.0));
	if (!TestTrue(TEXT("无输入被动位移仍可形成采样"), Runner->UpdateParticipantIntentAndProperties())) return false;
	const auto PassiveStep = FCatFishingFightSimulator::Step(Runner->Config, Runner->State, Constraint, FVector::ForwardVector);
	TestTrue(TEXT("无输入被动位移不制造个人移动费用"), PassiveStep.bSucceeded && Runner->ApplyGroupStaminaChanges(PassiveStep)
		&& Runner->LastGroupStaminaDrain == 0.0);
	Runner->State = SavedState;
	Runner->FindParticipant(Players[0])->bPullHeld = true;

	AbilitySystems[0]->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 0.0f);
	if (!TestTrue(TEXT("主位耗尽时重新读取真实余额"), Runner->UpdateParticipantIntentAndProperties())) return false;
	TestEqual(TEXT("零体力主位仍占实际成员人数"), Runner->GroupInput.Participants.Num(), 4);
	TestEqual(TEXT("能出力人数与成员人数分开统计"), Runner->GroupResult.ActiveParticipantCount, 3);
	TestTrue(TEXT("主位零体力仍可发布操竿指令"), Runner->SetReeling(Players[0], 1, true));
	const FCatFightStepResult HelperPowered = FCatFishingFightSimulator::Step(Runner->Config, Runner->State, Constraint, FVector::ForwardVector);
	TestTrue(TEXT("有体力的三个队友可以执行零体力主位的收线指令"), HelperPowered.bSucceeded && HelperPowered.RequestedReelDistanceCentimeters > 0.0);
	TestEqual(TEXT("队友力量不需要转移到主位体力"), static_cast<double>(AbilitySystems[0]->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute())), 0.0);

	const float LeavingStamina = AbilitySystems[3]->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	const FCatFightParticipantRuntime ReturningParticipant = *Runner->FindParticipant(Players[3]);
	Runner->Participants.Remove(Players[3]);
	if (!TestTrue(TEXT("四人减少为三人沿用同一刷新入口"), Runner->UpdateParticipantIntentAndProperties())) return false;
	const double ThreeMemberTotal = Runner->GroupResult.TotalCurrentStamina;
	TestEqual(TEXT("离开后冻结名单只剩三个身份"), Runner->FrozenParticipantPlayers.Num(), 3);
	Runner->Participants.Add(Players[3], ReturningParticipant);
	if (!TestTrue(TEXT("重新加入沿用同一刷新入口"), Runner->UpdateParticipantIntentAndProperties())) return false;
	TestEqual(TEXT("重新加入只加回本人剩余量，不能免费回满"), Runner->GroupResult.TotalCurrentStamina, ThreeMemberTotal + LeavingStamina, 1e-5);
	TestEqual(TEXT("退出再加入期间本人真实余额未改"), AbilitySystems[3]->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), LeavingStamina);
	AbilitySystems[3]->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 0.0f);
	const FVector FishBeforeTransfer = Runner->State.FishWorldPosition;
	const FVector VelocityBeforeTransfer = Runner->State.FishVelocityCentimetersPerSecond;
	const double FishStaminaBeforeTransfer = Runner->State.FishStamina;
	const double LineBeforeTransfer = Runner->State.LineLengthCentimeters;
	const double WearBeforeTransfer = Runner->State.AbsoluteRodWear;
	const float OtherStaminaBeforeTransfer = AbilitySystems[1]->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	TestTrue(TEXT("真实零体力辅助可以通过生产Runner入口接管主位"), Runner->TransferOperatorFromAuthority(
		Players[3], AbilitySystems[3], 100.0, 60.0, 0.0, 10, false, false));
	TestEqual(TEXT("接力不会回满新主位真实ASC体力"), AbilitySystems[3]->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), 0.0f);
	TestEqual(TEXT("接力不会改写其他成员真实ASC体力"), AbilitySystems[1]->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), OtherStaminaBeforeTransfer);
	TestTrue(TEXT("接力保留同一Runner鱼的位置与速度"), Runner->State.FishWorldPosition == FishBeforeTransfer
		&& Runner->State.FishVelocityCentimetersPerSecond == VelocityBeforeTransfer);
	TestEqual(TEXT("接力保留同一条鱼的体力"), Runner->State.FishStamina, FishStaminaBeforeTransfer);
	TestEqual(TEXT("接力保留已经收放的线长"), Runner->State.LineLengthCentimeters, LineBeforeTransfer);
	TestEqual(TEXT("接力保留本场绝对累计磨损"), Runner->State.AbsoluteRodWear, WearBeforeTransfer);
	TestTrue(TEXT("零体力接力后的新主位仍能提交收线指令"), Runner->SetReeling(Players[3], 11, true));
	Runner->Participants.Reset();
	Runner->State.bOperatorPresent = false;
	if (!TestTrue(TEXT("无人值守仍可刷新空组快照"), Runner->UpdateParticipantIntentAndProperties())) return false;
	TestEqual(TEXT("无人值守不保留旧成员身份"), Runner->FrozenParticipantPlayers.Num(), 0);
	TestEqual(TEXT("无人值守不保留旧总力量"), Runner->GroupResult.TotalActiveStrength, 0.0);
	TestEqual(TEXT("无人值守不把已离开个人体力留在大槽"), Runner->GroupResult.TotalCurrentStamina, 0.0);
	TestTrue(TEXT("空组求解保留有限惯性分母但不冒充身体成员"), Runner->Config.GetCombinedCatMass() > 0.0
		&& Runner->GroupResult.TotalMassKilograms == 0.0);
	Constraint.bRodHeld = false;
	const auto Unattended = FCatFishingFightSimulator::Step(Runner->Config, Runner->State, Constraint, FVector::ForwardVector);
	TestTrue(TEXT("同一鱼与线状态可继续无人值守求解"), Unattended.bSucceeded);
	TestEqual(TEXT("无人时残留主位左键不会自动收线"), Unattended.RequestedReelDistanceCentimeters, 0.0);
	TestTrue(TEXT("空组零账单也只结算一次"), Runner->ApplyGroupStaminaChanges(Unattended));
	TestFalse(TEXT("零金额和零成员仍不能重放结算事务"), Runner->ApplyGroupStaminaChanges(Unattended));
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
