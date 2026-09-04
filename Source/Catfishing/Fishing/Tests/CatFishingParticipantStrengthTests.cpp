#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Character/CatCharacter.h"
#include "Engine/World.h"
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
	UCatAbilitySystemComponent* PrimaryASC = PrimaryCharacter->GetCatAbilitySystemComponent();
	UCatAbilitySystemComponent* HelperASC = HelperCharacter->GetCatAbilitySystemComponent();
	if (!TestTrue(TEXT("两个角色均有真实ASC"), PrimaryASC && HelperASC)) return false;
	PrimaryASC->InitAbilityActorInfo(PrimaryCharacter, PrimaryCharacter);
	HelperASC->InitAbilityActorInfo(HelperCharacter, HelperCharacter);
	PrimaryASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 50.0f);
	HelperASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 30.0f);

	UCatFishingFightRunner* Runner = NewObject<UCatFishingFightRunner>(GetTransientPackage());
	FCatFightParticipantRuntime PrimaryParticipant;
	PrimaryParticipant.PlayerState = PrimaryPlayer;
	PrimaryParticipant.Character = PrimaryCharacter;
	PrimaryParticipant.AbilitySystem = PrimaryASC;
	PrimaryParticipant.bPrimary = true;
	PrimaryParticipant.bPullHeld = true;
	FCatFightParticipantRuntime HelperParticipant;
	HelperParticipant.PlayerState = HelperPlayer;
	HelperParticipant.Character = HelperCharacter;
	HelperParticipant.AbilitySystem = HelperASC;
	HelperParticipant.bPullHeld = true;
	Runner->Participants.Add(TWeakObjectPtr<APlayerState>(PrimaryPlayer), PrimaryParticipant);
	Runner->Participants.Add(TWeakObjectPtr<APlayerState>(HelperPlayer), HelperParticipant);
	Runner->Config.FixedStepSeconds = 0.05;
	Runner->Config.FishMassKilograms = 3.0;
	Runner->Config.FishStrength = 40.0;
	Runner->Config.RodStrength = 1000.0;
	Runner->Config.CatStaminaMaximum = 60.0;
	Runner->Config.ReelSpeedCentimetersPerSecond = 80.0;
	Runner->Config.FishCalmSpeedCentimetersPerSecond = 25.0;
	Runner->Config.FishStruggleSpeedCentimetersPerSecond = 75.0;
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
		TestEqual(TEXT("辅助有效力量不随正体力比例降低"), Runner->FindParticipant(HelperPlayer)->ActiveFishingStrength, 30.0);
		TestEqual(TEXT("配置收到主位完整力量"), Runner->Config.PrimaryOperatorCatStrength, 50.0);
		TestEqual(TEXT("按住拉线的辅助完整参与合力"), Runner->Config.SecondCatStrength, 30.0);
		TestEqual(TEXT("主位等效质量不随体力改变"), Runner->Config.PrimaryOperatorMassKilograms, 5.0);
		TestEqual(TEXT("辅助等效质量不随体力改变"), Runner->Config.HelperMassKilograms, 3.0);
		const auto Step = Simulate();
		TestTrue(TEXT("真实刷新后的力量能进入模拟"), Step.bSucceeded);
		TestEqual(TEXT("模拟器在各正体力档使用同一完整合力"), Step.CombinedCatStrength, 80.0);
		TestEqual(TEXT("模拟器对抗加速度不随正体力降低"), Step.CatDriveAccelerationCentimetersPerSecondSquared, 400.0);
	}

	SetStamina(0.0f, 30.0f);
	if (!TestTrue(TEXT("主位恰好零体力时刷新成功"), Runner->UpdateParticipantIntentAndProperties())) return false;
	TestEqual(TEXT("主位恰好零体力才停止贡献力量"), Runner->Config.PrimaryOperatorCatStrength, 0.0);
	TestEqual(TEXT("主位力竭不会关闭有体力的辅助"), Runner->Config.SecondCatStrength, 30.0);
	const auto HelperOnly = Simulate();
	TestTrue(TEXT("主位力竭时辅助合力仍能收线"), HelperOnly.bSucceeded && HelperOnly.RequestedReelDistanceCentimeters > 0.0);
	Runner->FindParticipant(HelperPlayer)->bPullHeld = false;
	if (!TestTrue(TEXT("辅助松键后重新刷新"), Runner->UpdateParticipantIntentAndProperties())) return false;
	TestEqual(TEXT("辅助松键不再参与合力"), Runner->Config.SecondCatStrength, 0.0);
	TestEqual(TEXT("辅助松键不改变其自身完整力量"), Runner->FindParticipant(HelperPlayer)->ActiveFishingStrength, 30.0);
	TestEqual(TEXT("零合力时活鱼收线停止"), Simulate().RequestedReelDistanceCentimeters, 0.0);

	SetStamina(1e-9f, 30.0f);
	if (!TestTrue(TEXT("主位从零恢复极少体力后立即刷新"), Runner->UpdateParticipantIntentAndProperties())) return false;
	TestEqual(TEXT("恢复任意正体力立即恢复主位完整力量"), Runner->Config.PrimaryOperatorCatStrength, 50.0);
	PrimaryASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 80.0f);
	HelperASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 40.0f);
	Runner->FindParticipant(HelperPlayer)->bPullHeld = true;
	if (!TestTrue(TEXT("力量属性实际改变后重新刷新"), Runner->UpdateParticipantIntentAndProperties())) return false;
	TestEqual(TEXT("主位使用ASC最新力量而非入场缓存"), Runner->Config.PrimaryOperatorCatStrength, 80.0);
	TestEqual(TEXT("辅助使用ASC最新力量而非入场缓存"), Runner->Config.SecondCatStrength, 40.0);
	TestEqual(TEXT("模拟器收到属性修改后的合力"), Simulate().CombinedCatStrength, 120.0);

	SetStamina(30.0f, 0.0f);
	if (!TestTrue(TEXT("辅助恰好零体力后重新刷新"), Runner->UpdateParticipantIntentAndProperties())) return false;
	TestEqual(TEXT("辅助按住按钮但零体力时不提供力量"), Runner->Config.SecondCatStrength, 0.0);
	SetStamina(30.0f, 1e-9f);
	if (!TestTrue(TEXT("辅助恢复极少正体力后重新刷新"), Runner->UpdateParticipantIntentAndProperties())) return false;
	TestEqual(TEXT("辅助恢复后立即提供自身最新完整力量"), Runner->Config.SecondCatStrength, 40.0);

	SetStamina(1e-9f, 1e-9f);
	if (!TestTrue(TEXT("双方极低正体力仍先保持完整力量"), Runner->UpdateParticipantIntentAndProperties())) return false;
	const float TinyPrimaryStamina = PrimaryASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	TestTrue(TEXT("真实GAS接受扣尽主位极低正体力的非零Delta"), PrimaryASC->ApplyFishingStaminaDelta(-TinyPrimaryStamina));
	TestEqual(TEXT("主位极低剩余体力确实扣至零"),
		PrimaryASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), 0.0f);
	TestTrue(TEXT("助手生产扣费路径处理极低正体力"), Runner->ApplyHelperStaminaChanges(0.1));
	TestEqual(TEXT("助手极低剩余体力确实扣至零"),
		HelperASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), 0.0f);
	if (!TestTrue(TEXT("实际支付耗尽后重新刷新双方力量"), Runner->UpdateParticipantIntentAndProperties())) return false;
	TestEqual(TEXT("主位极低体力实际扣尽后停止发力"), Runner->Config.PrimaryOperatorCatStrength, 0.0);
	TestEqual(TEXT("助手极低体力实际扣尽后停止发力"), Runner->Config.SecondCatStrength, 0.0);
	TestFalse(TEXT("真正零Delta仍然被拒绝"), PrimaryASC->ApplyFishingStaminaDelta(0.0f));

	SetStamina(0.0f, 0.0f);
	Runner->State.bFishExhausted = true;
	Runner->State.FishStamina = 0.0;
	Runner->State.MotionIntent = ECatFishMotionIntent::AutoHauling;
	if (!TestTrue(TEXT("鱼力竭时双猫零体力仍能刷新配置"), Runner->UpdateParticipantIntentAndProperties())) return false;
	const auto ExhaustedReel = Simulate();
	TestTrue(TEXT("零合力不阻断鱼力竭后的收尾收线"), ExhaustedReel.bSucceeded && ExhaustedReel.RequestedReelDistanceCentimeters > 0.0);
	TestEqual(TEXT("力竭后的收线不扣猫体力"), ExhaustedReel.CatStaminaDrain, 0.0);
	TestEqual(TEXT("体力归零不会改变双方基础等效质量"), Runner->Config.GetCombinedCatMass(), 12.0);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
