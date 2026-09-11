#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Tests/AutomationCommon.h"

#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Fishing/CatFishingService.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "OnlineSubsystemTypes.h"
#include "Engine/World.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/Integration/CatFishingPhysicalRodComponent.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Simulation/CatFishingFightRunner.h"
#include "GameFramework/PlayerState.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingParticipantStrengthTest,
	"Catfishing.Unit.Fishing.Runner.PrimaryOperatorStrengthAndCostsIgnorePhysicalHelpers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingParticipantStrengthTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建参与者力量测试世界"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	FURL URL;
	URL.AddOption(TEXT("game=/Script/Catfishing.CatfishingGameModeBase"));
	if (!World->SetGameMode(URL) || !WorldWrapper.BeginPlayInTestWorld()) return false;
	auto* Mode = World->GetAuthGameMode<ACatfishingGameModeBase>();
	Mode->bRunCommandsOpen = true;
	Mode->RunPublicState.Phase.Phase = ECatRunPhase::DayActive;
	Mode->RunPublicState.Phase.bNewFishingBitesAllowed = true;
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACatCharacter* PrimaryCharacter = World->SpawnActor<ACatCharacter>(
		FVector(0, 0, 20), FRotator::ZeroRotator, SpawnParameters);
	ACatCharacter* HelperCharacter = World->SpawnActor<ACatCharacter>(
		FVector(300.0, 0.0, 0.0), FRotator::ZeroRotator, SpawnParameters);
	APlayerState* PrimaryPlayer = World->SpawnActor<ACatfishingPlayerState>();
	APlayerState* HelperPlayer = World->SpawnActor<ACatfishingPlayerState>();
	if (!TestTrue(TEXT("生成两名角色与玩家身份"), PrimaryCharacter && HelperCharacter && PrimaryPlayer && HelperPlayer)) return false;
	const auto PossessAndAdmit = [&](ACatCharacter* Character, APlayerState* Player, const int32 Index)
	{
		auto* Controller = World->SpawnActor<ACatfishingPlayerController>();
		if (!Controller) return false;
		Controller->PlayerState = Player;
		Character->SetPlayerState(Player);
		Controller->Possess(Character);
		Controller->SetActorTickEnabled(false);
		Player->SetPlayerId(Index);
		const FUniqueNetIdRef PlayerNetId = FUniqueNetIdString::Create(
			FString::Printf(TEXT("ParticipantStrength%d"), Index), FName(TEXT("CAT_TEST")));
		Player->SetUniqueId(FUniqueNetIdRepl(PlayerNetId));
		ACatfishingGameModeBase::FAdmissionRecord Admission;
		Admission.Phase = ACatfishingGameModeBase::EAdmissionPhase::Active;
		Admission.Controller = Controller;
		Mode->AdmissionRecords.Add(ACatfishingGameModeBase::MakeStableNetIdKey(Player->GetUniqueId()), Admission);
		return Mode->CanAcceptFishingCommand(Controller) && Character->GetPhysicalBodyComponent()->GetBody();
	};
	if (!TestTrue(TEXT("两名真实身体均有已准入的控制器与玩家身份"),
		PossessAndAdmit(PrimaryCharacter, PrimaryPlayer, 1) && PossessAndAdmit(HelperCharacter, HelperPlayer, 2))) return false;
	UCatAbilitySystemComponent* PrimaryASC = PrimaryCharacter->GetCatAbilitySystemComponent();
	UCatAbilitySystemComponent* HelperASC = HelperCharacter->GetCatAbilitySystemComponent();
	if (!TestTrue(TEXT("两个角色均有真实ASC"), PrimaryASC && HelperASC)) return false;
	PrimaryASC->InitAbilityActorInfo(PrimaryCharacter, PrimaryCharacter);
	HelperASC->InitAbilityActorInfo(HelperCharacter, HelperCharacter);
	PrimaryASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(), 60.0f);
	HelperASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(), 60.0f);
	PrimaryASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 60.0f);
	HelperASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 30.0f);
	PrimaryASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 50.0f);
	HelperASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 30.0f);

	ACatFishingSession* Session = World->SpawnActor<ACatFishingSession>();
	ACatFishingRodActor* Rod = World->SpawnActor<ACatFishingRodActor>();
	if (!TestTrue(TEXT("行为入口具备真实权威Session与鱼竿宿主"), Session && Session->HasAuthority() && Rod)) return false;
	UCatFishingFightRunner* Runner = NewObject<UCatFishingFightRunner>(Session);
	Runner->Session = Session;
	Runner->RodActor = Rod;
	Runner->AbilitySystem = PrimaryASC;
	if (!TestTrue(TEXT("初始化并登记真实鱼竿"), Rod->InitializeAuthoritativeIdentity(FGuid::NewGuid(), FGuid::NewGuid(),
		TEXT("ParticipantStrengthRod"), NAME_None, PrimaryPlayer, nullptr, true, false)
		&& World->GetSubsystem<UCatFishingService>()->RegisterDeployedRod(PrimaryPlayer, Rod))
		|| !TestTrue(TEXT("主位以真实手爪握住新竿"), Rod->BeginPhysicalHoldFromAuthority(PrimaryPlayer, true))
		|| !TestTrue(TEXT("如同R部署事务，握持成功后显式授予部署者主控"),
			Rod->SetPrimaryOperatorFromAuthority(PrimaryPlayer, Rod->GetPresentationState().RodActorRevision) && Rod->GetPhysicalRodComponent()->CommitPrimaryHold(PrimaryPlayer))) return false;
	UCatPhysicalBodyComponent* HelperBody = HelperCharacter->GetPhysicalBodyComponent();
	UBoxComponent* PrimaryBody = PrimaryCharacter->GetPhysicalBodyComponent()->GetBody();
	const FVector Contact = PrimaryBody->GetComponentTransform().TransformPosition(FVector(0, 0, PrimaryBody->GetUnscaledBoxExtent().Z));
	if (!TestTrue(TEXT("仅设置助手整个身体的初始接触位置"), HelperBody->TeleportBodyFromAuthority(
		FTransform(FRotator::ZeroRotator, Contact - UCatPhysicsGrabComponent::RestHandLocal(true)), TEXT("ParticipantContactSetup")))
		|| !TestTrue(TEXT("助手在实际接触点抓住主猫身体形成钓鱼链"), HelperBody->GetGrab()->GripFromAuthority(true, PrimaryBody, Contact))) return false;
	if (!TestEqual(TEXT("抓猫连接不把助手加入钓鱼操作位"), Rod->GetOperatorCount(), 1)
		|| !TestTrue(TEXT("Runner仅绑定已获授权的主控"),
			Runner->BindPrimaryOperatorFromAuthority(PrimaryPlayer, true, false, 0))) return false;
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
	Constraint.bPhysicalRodEndpoint = true;
	Constraint.RodForwardWorld = FVector::ForwardVector;

	const auto SetStamina = [&](const float PrimaryStamina)
	{
		PrimaryASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), PrimaryStamina);
		Runner->State.CatStamina = PrimaryASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	};
	const auto Simulate = [&]()
	{
		return FCatFishingFightSimulator::Step(Runner->Config, Runner->State, Constraint, FVector::ForwardVector);
	};
	for (const float Stamina : {60.0f, 30.0f, 1e-9f})
	{
		SetStamina(Stamina);
		if (!TestTrue(TEXT("主控满体、半体及极低正体力均可刷新"), Runner->UpdateOperatorIntentAndProperties())) return false;
		TestTrue(TEXT("极低正体力没有在ASC中被抹成零"), Runner->State.CatStamina > 0.0);
		TestEqual(TEXT("主控有效力量不随正体力比例降低"), Runner->OperatorState.ActiveFishingStrength, 50.0);
		TestEqual(TEXT("鱼线模型只收到主控完整力量"), Runner->Config.PrimaryOperatorCatStrength, 50.0);
		TestEqual(TEXT("模型只观察主控真实身体质量"), Runner->Config.PrimaryOperatorMassKilograms, double(PrimaryBody->GetMass()));
		const auto Step = Simulate();
		TestTrue(TEXT("单主控属性刷新后模拟仍有效"), Step.bSucceeded);
		TestEqual(TEXT("物理助手不会为卷线器增加折扣力量"), Step.OperatorCatStrength, 50.0);
	}
	HelperASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 5000.0f);
	if (!TestTrue(TEXT("助手属性变更后刷新主控"), Runner->UpdateOperatorIntentAndProperties())) return false;
	TestEqual(TEXT("助手很强也不增加钓鱼模型的数值力量"), Simulate().OperatorCatStrength, 50.0);
	TestEqual(TEXT("主控身份不来自物理连接图"), Runner->OperatorState.PlayerState.Get(), PrimaryPlayer);
	int32 PrimaryStaminaWrites = 0, HelperStaminaWrites = 0;
	const FDelegateHandle PrimaryWriteHandle = PrimaryASC->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetFightStaminaAttribute()).AddLambda(
		[&](const FOnAttributeChangeData&) { ++PrimaryStaminaWrites; });
	const FDelegateHandle HelperWriteHandle = HelperASC->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetFightStaminaAttribute()).AddLambda(
		[&](const FOnAttributeChangeData&) { ++HelperStaminaWrites; });
	ON_SCOPE_EXIT
	{
		PrimaryASC->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetFightStaminaAttribute()).Remove(PrimaryWriteHandle);
		HelperASC->GetGameplayAttributeValueChangeDelegate(UCatSurvivalAttributeSet::GetFightStaminaAttribute()).Remove(HelperWriteHandle);
	};
	const auto TinyBalanceStep = Simulate();
	if (!TestTrue(TEXT("极低正体力的主控仍产生真实操作账单"), TinyBalanceStep.bSucceeded && TinyBalanceStep.CatStaminaDrain > 0.0)
		|| !TestTrue(TEXT("单主控账单从真实ASC扣至零"), Runner->ApplyOperatorStaminaChanges(TinyBalanceStep))) return false;
	TestEqual(TEXT("主控极低余额确实扣至零"), PrimaryASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), 0.0f);
	TestEqual(TEXT("一份主控账单只产生一次ASC修改"), PrimaryStaminaWrites, 1);
	TestEqual(TEXT("钓鱼账单不触碰助手ASC"), HelperStaminaWrites, 0);
	TestFalse(TEXT("同一冻结账单不能重复支付"), Runner->ApplyOperatorStaminaChanges(TinyBalanceStep));
	TestEqual(TEXT("重复支付拒绝不产生第二次ASC修改"), PrimaryStaminaWrites, 1);
	if (!TestTrue(TEXT("扣款后刷新真正的零体力"), Runner->UpdateOperatorIntentAndProperties())) return false;
	TestEqual(TEXT("仅恰好零体力才关闭主控钓鱼出力"), Runner->Config.PrimaryOperatorCatStrength, 0.0);
	TestEqual(TEXT("强且有体力的助手仍不改变主控模型"), Simulate().OperatorCatStrength, 0.0);
	SetStamina(1e-9f);
	PrimaryASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 80.0f);
	if (!TestTrue(TEXT("主控恢复任意正体力并读取自己的最新力量"), Runner->UpdateOperatorIntentAndProperties())) return false;
	TestEqual(TEXT("恢复后立即获得主控最新完整力量"), Runner->Config.PrimaryOperatorCatStrength, 80.0);
	TestFalse(TEXT("真正零Delta仍然被拒绝"), PrimaryASC->ApplyFishingStaminaDelta(0.0f));

	Runner->bInitialized = true;
	Runner->bRunning = true;
	TestFalse(TEXT("辅助左键不能获得线杯操作权"), Runner->SetReeling(HelperPlayer, 1, true));
	TestFalse(TEXT("辅助右键不能获得线杯操作权"), Runner->SetSlacking(HelperPlayer, 2, true));
	SetStamina(30.0f);
	Runner->UpdateOperatorIntentAndProperties();
	Runner->State.LineLengthCentimeters = Runner->Config.MaximumLineLengthCentimeters;
	Runner->State.FishWorldPosition.X = Runner->State.LineLengthCentimeters;
	Constraint.CatRodExertionSquaredSeconds = 0.04;
	const auto WithoutRightButton = Simulate();
	TestTrue(TEXT("满线时右键按下仍记录为真实输入"), Runner->SetSlacking(PrimaryPlayer, 1, true));
	TestTrue(TEXT("满线不会清除已按住的右键"), Runner->IsSlackInputHeldForAuthority(PrimaryPlayer));
	TestEqual(TEXT("满线忽略右键后保留已按住的左键收线"), Runner->State.CatAction, ECatFightCatAction::Pull);
	TestTrue(TEXT("满线右键期间左键再次按下被记录"), Runner->SetReeling(PrimaryPlayer, 2, true));
	TestEqual(TEXT("按键次序不改变满线时左键的有效收线"), Runner->State.CatAction, ECatFightCatAction::Pull);
	const auto FullLine = Simulate();
	TestTrue(TEXT("主控满线时恢复普通收线对抗且不回体"), FullLine.bSucceeded && !FullLine.bSlackRecoveryActive
		&& FullLine.CatStaminaDrain > 0.0 && FullLine.FishStaminaDrain > 0.0);
	TestEqual(TEXT("满线同时按右键不改变原左键的总耗体"), FullLine.CatStaminaDrain, WithoutRightButton.CatStaminaDrain, 1e-9);
	TestEqual(TEXT("满线同时按右键不改变原左键的鱼耗体"), FullLine.FishStaminaDrain, WithoutRightButton.FishStaminaDrain, 1e-9);
	TestEqual(TEXT("满线同时按右键不改变原左键的磨损"), FullLine.RodWearDelta, WithoutRightButton.RodWearDelta, 1e-9);
	TestTrue(TEXT("满线同时按右键不改变原左键的鱼位移"), FullLine.ProposedFishWorldPosition.Equals(WithoutRightButton.ProposedFishWorldPosition, 1e-9));
	TestTrue(TEXT("满线收线、转杆与支撑经唯一主控生产写口支付"), Runner->ApplyOperatorStaminaChanges(FullLine));
	const double PrimaryPaid = 30.0 - PrimaryASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	const double HelperPaid = 30.0 - HelperASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	TestTrue(TEXT("主控实际支付自己的操作账单"), PrimaryPaid > 0.0);
	TestEqual(TEXT("助手不分摊钓鱼操作费用"), HelperPaid, 0.0);
	TestEqual(TEXT("主控实际扣费只有一份模拟结果"), PrimaryPaid, FullLine.CatStaminaDrain, 1e-5);
	TestTrue(TEXT("满线右键期间左键释放被接受"), Runner->SetReeling(PrimaryPlayer, 3, false));
	TestEqual(TEXT("满线松左键后进入普通锁线"), Runner->State.CatAction, ECatFightCatAction::None);
	const auto LockedAtLimit = Simulate();
	TestTrue(TEXT("只剩右键时满线仍承担正常支撑和鱼耗体"), LockedAtLimit.bSucceeded
		&& !LockedAtLimit.bSlackRecoveryActive && LockedAtLimit.GetRodActionStaminaDrain() > 0.0 && LockedAtLimit.FishStaminaDrain > 0.0);

	// 与生产固定步相同，实际收线落点写回后刷新；保留的右键无需再按一次。
	TestTrue(TEXT("之前正常左键确实完成收线"), FullLine.LineLengthCentimeters < Runner->Config.MaximumLineLengthCentimeters);
	Runner->State.LineLengthCentimeters = FullLine.LineLengthCentimeters;
	Runner->State.FishWorldPosition = FullLine.ProposedFishWorldPosition;
	Runner->State.FishVelocityCentimetersPerSecond = FullLine.ResolvedFishVelocityCentimetersPerSecond;
	if (!TestTrue(TEXT("下一步恢复前重新冻结主控ASC实际余额"), Runner->UpdateOperatorIntentAndProperties())) return false;
	TestEqual(TEXT("真正收短线杯后持续右键重新生效"), Runner->State.CatAction, ECatFightCatAction::Slack);
	const auto Recovery = Simulate();
	TestTrue(TEXT("收短后的持续右键恢复正常回体"), Recovery.bSucceeded && Recovery.bSlackRecoveryActive && Recovery.CatStaminaDrain < 0.0);
	const double PrimaryBeforeRecovery = PrimaryASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	const double HelperBeforeRecovery = HelperASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	TestTrue(TEXT("收短后的恢复通过唯一主控写口到账"), Runner->ApplyOperatorStaminaChanges(Recovery));
	TestTrue(TEXT("主位实际体力恢复"), PrimaryASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()) > PrimaryBeforeRecovery);
	TestEqual(TEXT("主控放线不会恢复助手的钓鱼体力"), static_cast<double>(HelperASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute())), HelperBeforeRecovery);
	TestEqual(TEXT("未满线右键期间鱼不耗体"), Recovery.FishStaminaDrain, 0.0);
	TestTrue(TEXT("重新按下左键仍记录为按住"), Runner->SetReeling(PrimaryPlayer, 4, true));
	TestEqual(TEXT("未满线时恢复原右键优先级"), Runner->State.CatAction, ECatFightCatAction::Slack);
	TestFalse(TEXT("过期右键释放不能结束回体"), Runner->SetSlacking(PrimaryPlayer, 3, false));
	TestTrue(TEXT("最新右键释放被接受"), Runner->SetSlacking(PrimaryPlayer, 5, false));
	TestEqual(TEXT("松右键后立即恢复仍按住的左键收线"), Runner->State.CatAction, ECatFightCatAction::Pull);
	const auto Resumed = Simulate();
	TestTrue(TEXT("右键释放后双方恢复对抗耗体"), Resumed.bSucceeded && Resumed.CatStaminaDrain > 0.0 && Resumed.FishStaminaDrain > 0.0);
	Constraint.CatRodExertionSquaredSeconds = 0.0;
	Runner->State.LineLengthCentimeters = 500.0;
	Runner->State.FishWorldPosition = FVector(500.0, 0.0, 0.0);

	// 固定步在能力扣款之后重新读取主猫ASC，再决定是否进入持续外冲。
	SetStamina(0.0f);
	Runner->UpdateOperatorIntentAndProperties();
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
	Runner->OperatorState.bPullHeld = false;
	Runner->OperatorState.bSlackHeld = true;
	TestTrue(TEXT("主控ASC耗尽后由Runner接管持续外冲"), Runner->UpdateFishBehaviorForCurrentOperator(true));
	TestEqual(TEXT("力竭时覆盖休息请求为持续挣扎"), Runner->State.MotionIntent, ECatFishMotionIntent::StrugglingOutward);
	TestEqual(TEXT("力竭时暂时锁线而非放线回体"), Runner->State.CatAction, ECatFightCatAction::None);
	TestTrue(TEXT("力竭后新的右键按下仍能记录"), Runner->SetSlacking(PrimaryPlayer, 6, true));
	TestTrue(TEXT("新右键按下不能解除零体力强制拖拽"), Runner->UpdateFishBehaviorForCurrentOperator(true));
	if (!TestTrue(TEXT("强拖在同一执行记忆中发布真实满出力"), AdvanceEffort(true))) return false;
	TestEqual(TEXT("强拖实际出力同步到一"), Runner->State.FishEffortRatio, 1.0);
	TestEqual(TEXT("强拖保留缓游命令而不重选状态"), Runner->SteeringState.Behavior, ECatFishBehavior::EaseOff);
	TestEqual(TEXT("强拖保留获救后要恢复的目标出力"), Runner->SteeringState.TargetEffortRatio, EaseOffTarget);
	TestEqual(TEXT("力竭强制拖拽不会被右键恢复体力"), Simulate().CatStaminaDrain, 0.0);
	if (!TestTrue(TEXT("助手仍有体力且很强时刷新主控状态"), Runner->UpdateOperatorIntentAndProperties())) return false;
	TestTrue(TEXT("助手物理帮助不能改写主控力竭状态"), Runner->UpdateFishBehaviorForCurrentOperator(true));
	TestEqual(TEXT("助手不替主控恢复钓鱼体力"), Runner->State.CatStamina, 0.0);
	SetStamina(30.0f);
	Runner->UpdateOperatorIntentAndProperties();
	TestFalse(TEXT("只有主控自身恢复后才解除力竭规则"), Runner->UpdateFishBehaviorForCurrentOperator(true));
	TestEqual(TEXT("获救保留原行为树缓游命令"), Runner->SteeringState.Behavior, ECatFishBehavior::EaseOff);
	TestEqual(TEXT("获救当刻实际出力仍是满力，不瞬间套入目标"), Runner->State.FishEffortRatio, 1.0);
	TestEqual(TEXT("获救后仍保留真实右键状态"), Runner->State.CatAction, ECatFightCatAction::Slack);
	const auto RescuedStep = Simulate();
	TestTrue(TEXT("主控自身恢复使模拟器交回普通右键规则"), RescuedStep.bSucceeded && !RescuedStep.bExhaustedCatEscape
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
	Runner->OperatorState.bPullHeld = true;
	Runner->OperatorState.bSlackHeld = false;

	SetStamina(0.0f);
	Runner->State.bFishExhausted = true;
	Runner->State.FishStamina = 0.0;
	Runner->State.MotionIntent = ECatFishMotionIntent::AutoHauling;
	if (!TestTrue(TEXT("鱼力竭时主控零体力仍能刷新配置"), Runner->UpdateOperatorIntentAndProperties())) return false;
	const auto ExhaustedReel = Simulate();
	TestTrue(TEXT("主控零体力不阻断鱼力竭后的收尾收线"), ExhaustedReel.bSucceeded && ExhaustedReel.RequestedReelDistanceCentimeters > 0.0);
	TestEqual(TEXT("力竭后的收线不扣猫体力"), ExhaustedReel.CatStaminaDrain, 0.0);
	TestEqual(TEXT("体力归零不会把助手质量并入主控模型"), Runner->Config.PrimaryOperatorMassKilograms, double(PrimaryBody->GetMass()));
	TestEqual(TEXT("所有钓鱼刷新、支付与恢复都未写入助手ASC"), HelperStaminaWrites, 0);
	TestEqual(TEXT("物理助手保留原始钓鱼体力余额"), HelperASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), 30.0f);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
