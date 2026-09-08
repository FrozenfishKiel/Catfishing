#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Character/CatCharacter.h"
#include "Engine/World.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Simulation/CatFishingFightRunner.h"
#include "GameFramework/PlayerState.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingParticipantStrengthTest,
	"Catfishing.Unit.Fishing.Runner.ParticipantStrengthRemainsFullUntilStaminaIsZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingParticipantStrengthTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建参与者力量测试世界"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACatCharacter* PrimaryCharacter = World->SpawnActor<ACatCharacter>(
		FVector::ZeroVector, FRotator::ZeroRotator, SpawnParameters);
	ACatCharacter* HelperCharacter = World->SpawnActor<ACatCharacter>(
		FVector(300.0, 0.0, 0.0), FRotator::ZeroRotator, SpawnParameters);
	APlayerState* PrimaryPlayer = World->SpawnActor<APlayerState>();
	APlayerState* HelperPlayer = World->SpawnActor<APlayerState>();
	if (!TestTrue(TEXT("生成两名角色与玩家身份"), PrimaryCharacter && HelperCharacter && PrimaryPlayer && HelperPlayer)) return false;
	PrimaryCharacter->SetPlayerState(PrimaryPlayer);
	HelperCharacter->SetPlayerState(HelperPlayer);
	PrimaryPlayer->SetPlayerId(1);
	HelperPlayer->SetPlayerId(2);
	UCatAbilitySystemComponent* PrimaryASC = PrimaryCharacter->GetCatAbilitySystemComponent();
	UCatAbilitySystemComponent* HelperASC = HelperCharacter->GetCatAbilitySystemComponent();
	if (!TestTrue(TEXT("两个角色均有真实ASC"), PrimaryASC && HelperASC)) return false;
	PrimaryASC->InitAbilityActorInfo(PrimaryCharacter, PrimaryCharacter);
	HelperASC->InitAbilityActorInfo(HelperCharacter, HelperCharacter);
	PrimaryASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 50.0f);
	HelperASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 30.0f);

	ACatFishingSession* Session = World->SpawnActor<ACatFishingSession>();
	ACatFishingRodActor* Rod = World->SpawnActor<ACatFishingRodActor>();
	if (!TestTrue(TEXT("行为入口具备真实权威Session与鱼竿宿主"), Session && Session->HasAuthority() && Rod)) return false;
	UCatFishingFightRunner* Runner = NewObject<UCatFishingFightRunner>(Session);
	Runner->Session = Session;
	Runner->RodActor = Rod;
	Runner->AbilitySystem = PrimaryASC;
	FCatFightParticipantRuntime PrimaryParticipant;
	PrimaryParticipant.PlayerState = PrimaryPlayer;
	PrimaryParticipant.Character = PrimaryCharacter;
	PrimaryParticipant.AbilitySystem = PrimaryASC;
	PrimaryParticipant.StaminaMaximum = 60.0;
	PrimaryParticipant.bPrimary = true;
	PrimaryParticipant.bPullHeld = true;
	FCatFightParticipantRuntime HelperParticipant;
	HelperParticipant.PlayerState = HelperPlayer;
	HelperParticipant.Character = HelperCharacter;
	HelperParticipant.AbilitySystem = HelperASC;
	HelperParticipant.StaminaMaximum = 60.0;
	HelperParticipant.bPullHeld = false;
	Runner->Participants.Add(TWeakObjectPtr<APlayerState>(PrimaryPlayer), PrimaryParticipant);
	Runner->Participants.Add(TWeakObjectPtr<APlayerState>(HelperPlayer), HelperParticipant);
	Runner->Config.FixedStepSeconds = 0.05;
	Runner->Config.FishMassKilograms = 3.0;
	Runner->Config.FishStrength = 40.0;
	Runner->Config.CatStaminaMaximum = 60.0;
	Runner->Config.ReelSpeedCentimetersPerSecond = 80.0;
	Runner->Config.FishFullEffortSpeedCentimetersPerSecond = 75.0;
	Runner->Config.MaximumLineLengthCentimeters = 1000.0;
	Runner->Config.RodDurability = 1000.0;
	Runner->State.FishStamina = 100.0;
	Runner->State.LineLengthCentimeters = 500.0;
	Runner->State.FishWorldPosition = FVector(500.0, 0.0, 0.0);
	Runner->State.MotionIntent = ECatFishMotionIntent::StrugglingOutward;
	FCatFightRodConstraintInput Constraint;
	Constraint.bRodHeld = true;
	Constraint.RodForwardWorld = FVector::ForwardVector;

	const auto SetStamina = [&](const float PrimaryStamina, const float HelperStamina)
	{
		PrimaryASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), PrimaryStamina);
		HelperASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), HelperStamina);
		// 与固定步调度一致，模拟输入中的当前体力从真实ASC取得；力量始终由生产刷新方法写入。
		Runner->State.CatStamina = PrimaryASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	};
	const auto Simulate = [&]()
	{
		return FCatFishingFightSimulator::Step(Runner->Config, Runner->State, Constraint, FVector::ForwardVector);
	};
	for (const float Stamina : {60.0f, 30.0f, 1e-9f})
	{
		SetStamina(Stamina, Stamina);
		if (!TestTrue(TEXT("满体、半体及极低正体力均可刷新真实参与者"), Runner->UpdateParticipantIntentAndProperties())) return false;
		TestTrue(TEXT("极低正体力没有在ASC中被抹成零"), Runner->State.CatStamina > 0.0);
		TestEqual(TEXT("主位有效力量不随正体力比例降低"), Runner->FindParticipant(PrimaryPlayer)->ActiveFishingStrength, 50.0);
		TestEqual(TEXT("辅助折扣后的有效力量不随正体力比例降低"), Runner->FindParticipant(HelperPlayer)->ActiveFishingStrength, 15.0);
		TestEqual(TEXT("配置收到主位完整力量"), Runner->Config.PrimaryOperatorCatStrength, 50.0);
		TestEqual(TEXT("辅助无需左键，站定默认贡献五成力量"), Runner->Config.SecondCatStrength, 15.0);
		TestEqual(TEXT("主位等效质量不随体力改变"), Runner->Config.PrimaryOperatorMassKilograms, 5.0);
		TestEqual(TEXT("辅助等效质量不随体力改变"), Runner->Config.HelperMassKilograms, 5.0);
		const auto Step = Simulate();
		TestTrue(TEXT("真实刷新后的力量能进入模拟"), Step.bSucceeded);
		TestEqual(TEXT("模拟器在各正体力档使用同一完整折扣合力"), Step.CombinedCatStrength, 65.0);
		TestEqual(TEXT("模拟器对抗加速度不随正体力降低"), Step.CatDriveAccelerationCentimetersPerSecondSquared, 650.0);
	}

	SetStamina(0.0f, 30.0f);
	if (!TestTrue(TEXT("主位恰好零体力时刷新成功"), Runner->UpdateParticipantIntentAndProperties())) return false;
	TestEqual(TEXT("主位恰好零体力才停止贡献力量"), Runner->Config.PrimaryOperatorCatStrength, 0.0);
	TestEqual(TEXT("主位力竭不会关闭有体力的辅助"), Runner->Config.SecondCatStrength, 15.0);
	const auto HelperOnly = Simulate();
	TestTrue(TEXT("主位力竭时辅助合力仍能收线"), HelperOnly.bSucceeded && HelperOnly.RequestedReelDistanceCentimeters > 0.0);
	Runner->FindParticipant(HelperPlayer)->bPullHeld = true;
	if (!TestTrue(TEXT("旧辅助按钮记录变化后重新刷新"), Runner->UpdateParticipantIntentAndProperties())) return false;
	TestEqual(TEXT("旧辅助左键记录不再是出力开关"), Runner->Config.SecondCatStrength, 15.0);
	Runner->FindParticipant(HelperPlayer)->bPullHeld = false;

	SetStamina(1e-9f, 30.0f);
	if (!TestTrue(TEXT("主位从零恢复极少体力后立即刷新"), Runner->UpdateParticipantIntentAndProperties())) return false;
	TestEqual(TEXT("恢复任意正体力立即恢复主位完整力量"), Runner->Config.PrimaryOperatorCatStrength, 50.0);
	PrimaryASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 80.0f);
	HelperASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 40.0f);
	if (!TestTrue(TEXT("力量属性实际改变后重新刷新"), Runner->UpdateParticipantIntentAndProperties())) return false;
	TestEqual(TEXT("主位使用ASC最新力量而非入场缓存"), Runner->Config.PrimaryOperatorCatStrength, 80.0);
	TestEqual(TEXT("辅助使用ASC最新力量再施加独立五成折扣"), Runner->Config.SecondCatStrength, 20.0);
	TestEqual(TEXT("模拟器收到属性修改后的合力"), Simulate().CombinedCatStrength, 100.0);

	SetStamina(30.0f, 0.0f);
	if (!TestTrue(TEXT("辅助恰好零体力后重新刷新"), Runner->UpdateParticipantIntentAndProperties())) return false;
	TestEqual(TEXT("辅助恰好零体力时不提供力量"), Runner->Config.SecondCatStrength, 0.0);
	SetStamina(30.0f, 1e-9f);
	if (!TestTrue(TEXT("辅助恢复极少正体力后重新刷新"), Runner->UpdateParticipantIntentAndProperties())) return false;
	TestEqual(TEXT("辅助恢复后立即提供自身最新折扣力量"), Runner->Config.SecondCatStrength, 20.0);

	SetStamina(1e-9f, 1e-9f);
	if (!TestTrue(TEXT("双方极低正体力仍先保持完整力量"), Runner->UpdateParticipantIntentAndProperties())) return false;
	const float TinyPrimaryStamina = PrimaryASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	TestTrue(TEXT("真实GAS接受扣尽主位极低正体力的非零Delta"), PrimaryASC->ApplyFishingStaminaDelta(-TinyPrimaryStamina));
	TestEqual(TEXT("主位极低剩余体力确实扣至零"),
		PrimaryASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), 0.0f);
	if (!TestTrue(TEXT("支付主位后重新冻结真实余额，不能重复用旧账单"), Runner->UpdateParticipantIntentAndProperties())) return false;
	const auto TinyBalanceStep = Simulate();
	TestTrue(TEXT("实际鱼线负担能生成极低体力共同账单"), TinyBalanceStep.bSucceeded && TinyBalanceStep.GetSharedCatStaminaDrain() > 0.0);
	TestTrue(TEXT("共同生产扣费路径处理辅助极低正体力"), Runner->ApplyGroupStaminaChanges(TinyBalanceStep));
	TestEqual(TEXT("助手极低剩余体力确实扣至零"),
		HelperASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), 0.0f);
	if (!TestTrue(TEXT("实际支付耗尽后重新刷新双方力量"), Runner->UpdateParticipantIntentAndProperties())) return false;
	TestEqual(TEXT("主位极低体力实际扣尽后停止发力"), Runner->Config.PrimaryOperatorCatStrength, 0.0);
	TestEqual(TEXT("助手极低体力实际扣尽后停止发力"), Runner->Config.SecondCatStrength, 0.0);
	TestFalse(TEXT("真正零Delta仍然被拒绝"), PrimaryASC->ApplyFishingStaminaDelta(0.0f));

	Runner->bInitialized = true;
	Runner->bRunning = true;
	TestFalse(TEXT("辅助左键不能获得线杯操作权"), Runner->SetReeling(HelperPlayer, 1, true));
	TestFalse(TEXT("辅助右键不能获得线杯操作权"), Runner->SetSlacking(HelperPlayer, 2, true));
	SetStamina(30.0f, 30.0f);
	Runner->UpdateParticipantIntentAndProperties();
	Runner->State.LineLengthCentimeters = Runner->Config.MaximumLineLengthCentimeters;
	Runner->State.FishWorldPosition.X = Runner->State.LineLengthCentimeters;
	Constraint.CarrierDesiredVelocityCentimetersPerSecond = FVector::ZeroVector;
	Constraint.CatRodExertionSquaredSeconds = 0.04;
	const auto WithoutRightButton = Simulate();
	TestTrue(TEXT("满线时右键按下仍记录为真实输入"), Runner->SetSlacking(PrimaryPlayer, 1, true));
	TestTrue(TEXT("满线不会清除已按住的右键"), Runner->IsSlackInputHeldForAuthority(PrimaryPlayer));
	TestEqual(TEXT("满线忽略右键后保留已按住的左键收线"), Runner->State.CatAction, ECatFightCatAction::Pull);
	TestTrue(TEXT("满线右键期间左键再次按下被记录"), Runner->SetReeling(PrimaryPlayer, 2, true));
	TestEqual(TEXT("按键次序不改变满线时左键的有效收线"), Runner->State.CatAction, ECatFightCatAction::Pull);
	const auto FullLine = Simulate();
	TestTrue(TEXT("真实主辅满线时恢复普通收线对抗且不回体"), FullLine.bSucceeded && !FullLine.bSlackRecoveryActive
		&& FullLine.CatStaminaDrain > 0.0 && FullLine.FishStaminaDrain > 0.0);
	TestEqual(TEXT("满线同时按右键不改变原左键的总耗体"), FullLine.CatStaminaDrain, WithoutRightButton.CatStaminaDrain, 1e-9);
	TestEqual(TEXT("满线同时按右键不改变原左键的鱼耗体"), FullLine.FishStaminaDrain, WithoutRightButton.FishStaminaDrain, 1e-9);
	TestEqual(TEXT("满线同时按右键不改变原左键的磨损"), FullLine.RodWearDelta, WithoutRightButton.RodWearDelta, 1e-9);
	TestTrue(TEXT("满线同时按右键不改变原左键的鱼位移"), FullLine.ProposedFishWorldPosition.Equals(WithoutRightButton.ProposedFishWorldPosition, 1e-9));
	TestTrue(TEXT("满线收线、转杆与支撑经唯一生产写口共同支付"), Runner->ApplyGroupStaminaChanges(FullLine));
	const double PrimaryPaid = 30.0 - PrimaryASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	const double HelperPaid = 30.0 - HelperASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	TestTrue(TEXT("满线主位与实际出力助手均实际扣体"), PrimaryPaid > 0.0 && HelperPaid > 0.0);
	TestEqual(TEXT("主辅实际总扣费只有一份模拟结果"), PrimaryPaid + HelperPaid, FullLine.CatStaminaDrain, 1e-5);
	TestEqual(TEXT("主位与辅助均分操作负担，不按八十比二十力量转嫁主位"), PrimaryPaid, HelperPaid, 1e-5);
	TestTrue(TEXT("满线右键期间左键释放被接受"), Runner->SetReeling(PrimaryPlayer, 3, false));
	TestEqual(TEXT("满线松左键后进入普通锁线"), Runner->State.CatAction, ECatFightCatAction::None);
	const auto LockedAtLimit = Simulate();
	TestTrue(TEXT("只剩右键时满线仍承担正常支撑和鱼耗体"), LockedAtLimit.bSucceeded
		&& !LockedAtLimit.bSlackRecoveryActive && LockedAtLimit.GetSharedCatStaminaDrain() > 0.0 && LockedAtLimit.FishStaminaDrain > 0.0);

	// 与生产固定步相同，实际收线落点写回后刷新；保留的右键无需再按一次。
	TestTrue(TEXT("之前正常左键确实完成收线"), FullLine.LineLengthCentimeters < Runner->Config.MaximumLineLengthCentimeters);
	Runner->State.LineLengthCentimeters = FullLine.LineLengthCentimeters;
	Runner->State.FishWorldPosition = FullLine.ProposedFishWorldPosition;
	if (!TestTrue(TEXT("下一步恢复前重新冻结双ASC实际余额"), Runner->UpdateParticipantIntentAndProperties())) return false;
	TestEqual(TEXT("真正收短线杯后持续右键重新生效"), Runner->State.CatAction, ECatFightCatAction::Slack);
	const auto Recovery = Simulate();
	TestTrue(TEXT("收短后的持续右键恢复正常回体"), Recovery.bSucceeded && Recovery.bSlackRecoveryActive && Recovery.CatStaminaDrain < 0.0);
	const double PrimaryBeforeRecovery = PrimaryASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	const double HelperBeforeRecovery = HelperASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	TestTrue(TEXT("收短后的恢复通过唯一生产写口分别到账"), Runner->ApplyGroupStaminaChanges(Recovery));
	TestTrue(TEXT("主位实际体力恢复"), PrimaryASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()) > PrimaryBeforeRecovery);
	TestTrue(TEXT("有效放线期间辅助恢复自己的体力"), static_cast<double>(HelperASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute())) > HelperBeforeRecovery);
	TestEqual(TEXT("未满线右键期间鱼不耗体"), Recovery.FishStaminaDrain, 0.0);
	TestTrue(TEXT("重新按下左键仍记录为按住"), Runner->SetReeling(PrimaryPlayer, 4, true));
	TestEqual(TEXT("未满线时恢复原右键优先级"), Runner->State.CatAction, ECatFightCatAction::Slack);
	TestFalse(TEXT("过期右键释放不能结束回体"), Runner->SetSlacking(PrimaryPlayer, 3, false));
	TestTrue(TEXT("最新右键释放被接受"), Runner->SetSlacking(PrimaryPlayer, 5, false));
	TestEqual(TEXT("松右键后立即恢复仍按住的左键收线"), Runner->State.CatAction, ECatFightCatAction::Pull);
	const auto Resumed = Simulate();
	TestTrue(TEXT("右键释放后双方恢复对抗耗体"), Resumed.bSucceeded && Resumed.CatStaminaDrain > 0.0 && Resumed.FishStaminaDrain > 0.0);
	Constraint.CarrierDesiredVelocityCentimetersPerSecond = FVector::ZeroVector;
	Constraint.CatRodExertionSquaredSeconds = 0.0;
	Runner->State.LineLengthCentimeters = 500.0;
	Runner->State.FishWorldPosition = FVector(500.0, 0.0, 0.0);

	// 固定步在能力扣款之后重新读取主猫ASC，再决定是否进入持续外冲。
	SetStamina(0.0f, 0.0f);
	Runner->UpdateParticipantIntentAndProperties();
	Runner->InitialFishStamina = 100.0;
	Runner->SteeringRandom.Initialize(1427);
	if (!TestTrue(TEXT("行为树仍可请求缓游调整"), Runner->BeginFishBehaviorFromStateTree(ECatFishBehavior::EaseOff))) return false;
	FVector DesiredDirection;
	const auto AdvanceEffort = [&](const bool bForceOutward)
	{
		// 此夹具覆盖参与者策略与连续执行器；正式树计时/显示分类由独立Runner与StateTree回归覆盖。
		const bool bAdvanced = FCatFishSteeringModel::Step(Runner->SteeringConfig, FVector::ForwardVector,
			Runner->Config.FixedStepSeconds, Runner->SteeringRandom, Runner->SteeringState, DesiredDirection, bForceOutward);
		if (bAdvanced) Runner->State.FishEffortRatio = Runner->SteeringState.CurrentEffortRatio;
		return bAdvanced;
	};
	const double EaseOffTarget = Runner->SteeringState.TargetEffortRatio;
	for (int32 Index = 0; Index < 60; ++Index)
		if (!TestTrue(TEXT("原缓游命令通过真实连续执行器降力"), AdvanceEffort(false))) return false;
	TestEqual(TEXT("夹具先形成已降低的实际出力"), Runner->State.FishEffortRatio, EaseOffTarget, 1e-9);
	Runner->FindParticipant(PrimaryPlayer)->bPullHeld = false;
	Runner->FindParticipant(PrimaryPlayer)->bSlackHeld = true;
	TestTrue(TEXT("真实双方ASC耗尽后由Runner接管持续外冲"), Runner->UpdateFishBehaviorForCurrentOperator(true));
	TestEqual(TEXT("力竭时覆盖休息请求为持续挣扎"), Runner->State.MotionIntent, ECatFishMotionIntent::StrugglingOutward);
	TestEqual(TEXT("力竭时暂时锁线而非放线回体"), Runner->State.CatAction, ECatFightCatAction::None);
	TestTrue(TEXT("力竭后新的右键按下仍能记录"), Runner->SetSlacking(PrimaryPlayer, 6, true));
	TestTrue(TEXT("新右键按下不能解除零体力强制拖拽"), Runner->UpdateFishBehaviorForCurrentOperator(true));
	if (!TestTrue(TEXT("强拖在同一执行记忆中发布真实满出力"), AdvanceEffort(true))) return false;
	TestEqual(TEXT("强拖实际出力同步到一"), Runner->State.FishEffortRatio, 1.0);
	TestEqual(TEXT("强拖保留缓游命令而不重选状态"), Runner->SteeringState.Behavior, ECatFishBehavior::EaseOff);
	TestEqual(TEXT("强拖保留获救后要恢复的目标出力"), Runner->SteeringState.TargetEffortRatio, EaseOffTarget);
	TestEqual(TEXT("力竭强制拖拽不会被右键恢复体力"), Simulate().CatStaminaDrain, 0.0);
	SetStamina(0.0f, 30.0f);
	Runner->UpdateParticipantIntentAndProperties();
	TestFalse(TEXT("真实助手恢复并发力后解除强制拖拽"), Runner->UpdateFishBehaviorForCurrentOperator(true));
	TestTrue(TEXT("解除覆盖来自助手实际合力而非改写主位体力"), Runner->Config.SecondCatStrength > 0.0 && Runner->State.CatStamina == 0.0);
	TestEqual(TEXT("获救保留原行为树缓游命令"), Runner->SteeringState.Behavior, ECatFishBehavior::EaseOff);
	TestEqual(TEXT("获救当刻实际出力仍是满力，不瞬间套入目标"), Runner->State.FishEffortRatio, 1.0);
	TestEqual(TEXT("获救后仍保留真实右键状态"), Runner->State.CatAction, ECatFightCatAction::Slack);
	const auto RescuedStep = Simulate();
	TestTrue(TEXT("真实助手使模拟器交回普通右键规则"), RescuedStep.bSucceeded && !RescuedStep.bExhaustedCatEscape
		&& RescuedStep.bSlackRecoveryActive);
	for (int32 Index = 0; Index < 60; ++Index)
	{
		const double PreviousEffort = Runner->State.FishEffortRatio;
		if (!TestTrue(TEXT("获救后执行同一缓游命令"), AdvanceEffort(false))) return false;
		TestTrue(TEXT("获救后的每步降力受既定速度上限约束"), PreviousEffort >= Runner->State.FishEffortRatio
			&& PreviousEffort - Runner->State.FishEffortRatio
				<= Runner->SteeringConfig.EffortFallPerSecond * Runner->Config.FixedStepSeconds + 1e-9);
		TestEqual(TEXT("平滑恢复期间不会另选行为"), Runner->SteeringState.Behavior, ECatFishBehavior::EaseOff);
	}
	TestEqual(TEXT("获救后最终回到原缓游出力而非永久满力"), Runner->State.FishEffortRatio, EaseOffTarget, 1e-9);
	const auto EasedStep = Simulate();
	TestTrue(TEXT("低出力已进入实际物理推进"), EasedStep.bSucceeded && !EasedStep.bExhaustedCatEscape
		&& EasedStep.Trace.FishThrustNewtons < RescuedStep.Trace.FishThrustNewtons
		&& EasedStep.IntendedSwimSpeedCentimetersPerSecond < RescuedStep.IntendedSwimSpeedCentimetersPerSecond);
	Runner->FindParticipant(PrimaryPlayer)->bPullHeld = true;
	Runner->FindParticipant(PrimaryPlayer)->bSlackHeld = false;

	SetStamina(0.0f, 0.0f);
	Runner->State.bFishExhausted = true;
	Runner->State.FishStamina = 0.0;
	Runner->State.MotionIntent = ECatFishMotionIntent::AutoHauling;
	if (!TestTrue(TEXT("鱼力竭时双猫零体力仍能刷新配置"), Runner->UpdateParticipantIntentAndProperties())) return false;
	const auto ExhaustedReel = Simulate();
	TestTrue(TEXT("零合力不阻断鱼力竭后的收尾收线"), ExhaustedReel.bSucceeded && ExhaustedReel.RequestedReelDistanceCentimeters > 0.0);
	TestEqual(TEXT("力竭后的收线不扣猫体力"), ExhaustedReel.CatStaminaDrain, 0.0);
	TestEqual(TEXT("体力归零不会改变双方基础等效质量"), Runner->Config.GetCombinedCatMass(), 10.0);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
