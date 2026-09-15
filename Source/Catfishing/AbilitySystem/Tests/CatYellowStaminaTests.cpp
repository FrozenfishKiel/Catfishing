#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Physics/CatPhysicalEffortComponent.h"
#include "Character/Physics/Tests/CatPhysicalTestWorld.h"
#include "Condition/CatConditionComponent.h"
#include "Data/CatFishDefinition.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatYellowStaminaBalanceTest,
	"Catfishing.PhysicalEffort.Runtime.YellowBalanceRecoveryAndFoodReplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FCatYellowStaminaBalanceTest::RunTest(const FString&)
{
	CatPhysicalTest::FScene Scene;
	if (!Scene.Initialize(this)) return false;
	auto* Cat = Scene.SpawnCat(FVector(0,0,20));
	if (!Cat) return false;
	auto* ASC = Cat->GetCatAbilitySystemComponent();
	auto* Effort = Cat->FindComponentByClass<UCatPhysicalEffortComponent>();
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(), 100);
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 10);
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 60);
	TestEqual(TEXT("new body starts without reserve"), ASC->GetYellowFightStamina(), 0.0f);
	TestTrue(TEXT("grant has no green-capacity ceiling"), ASC->ApplyYellowFightStaminaDelta(150));
	TestEqual(TEXT("reserve stacks above green maximum"), ASC->GetTotalFightStamina(), 160.0);
	TestTrue(TEXT("cross-segment bill applies"), ASC->ApplyFishingStaminaDelta(-15));
	TestEqual(TEXT("green is consumed first"), ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), 0.0f);
	TestEqual(TEXT("only remainder is charged to yellow"), ASC->GetYellowFightStamina(), 145.0f);
	TestTrue(TEXT("yellow-only balance still supplies full strength"), Effort->GetMaximumForceKgCmS2() == 6000.0);
	TestTrue(TEXT("recovery applies only to green"), ASC->ApplyFishingStaminaDelta(5));
	TestEqual(TEXT("recovery cannot replenish yellow"), ASC->GetYellowFightStamina(), 145.0f);
	ASC->ClearYellowFightStaminaFromAuthority();
	TestEqual(TEXT("day cleanup leaves green unchanged"), ASC->GetTotalFightStamina(), 5.0);
	for (float Tiny : {1.e-9f, 1.e-6f})
	{
		ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), Tiny);
		ASC->ApplyYellowFightStaminaDelta(Tiny);
		TestTrue(TEXT("tiny nonzero bill succeeds"), ASC->ApplyFishingStaminaDelta(-Tiny * 4));
		TestEqual(TEXT("no tiny balance can create an infinite motor"), ASC->GetTotalFightStamina(), 0.0);
	}
	ASC->ApplyYellowFightStaminaDelta(20);
	FCatBodyDriveSample Idle;
	Idle.bLocomotion = false; // No pose prerequisite.
	Effort->SettleMovementFromAuthority(Idle, FVector::ZeroVector, FVector::ZeroVector, 1.0, false);
	TestEqual(TEXT("first outside-fight second gives five green points"), ASC->GetTotalFightStamina(), 25.0);
	Idle.bUnderLoad = true;
	Effort->SettleMovementFromAuthority(Idle, FVector::ZeroVector, FVector::ZeroVector, 2.0, false);
	TestEqual(TEXT("being pulled pauses recovery even without movement"), ASC->GetTotalFightStamina(), 25.0);
	Idle.bUnderLoad = false;
	Idle.bFishing = true; // A held rod alone does not prove an active fight.
	Effort->SettleMovementFromAuthority(Idle, FVector(10,0,0), FVector(10,0,0), 1.0, true);
	TestEqual(TEXT("waiting with a rod can recover while moving"), ASC->GetTotalFightStamina(), 30.0);
	const auto* Fish = LoadObject<UCatFishDefinition>(nullptr, TEXT("/Game/Catfishing/Data/Fish/Fish_LittleColor.Fish_LittleColor"));
	if (!TestNotNull(TEXT("formal fish definition loads"), Fish)) return false;
	TestEqual(TEXT("formal fish grant comes from table"), Fish->YellowStaminaGrant, 20.0);
	for (const TCHAR* Path : {TEXT("/Game/Catfishing/Data/Fish/Fish_Windbell.Fish_Windbell"), TEXT("/Game/Catfishing/Data/Fish/Fish_Blackfish.Fish_Blackfish")})
	{
		const auto* Definition = LoadObject<UCatFishDefinition>(nullptr, Path);
		if (!TestNotNull(TEXT("every migrated fish loads"), Definition)) return false;
		TestTrue(TEXT("migrated fish preserves native readiness"), Definition->IsRuntimeDefinitionReady());
		TestEqual(TEXT("each formal grant matches fish table"), Definition->YellowStaminaGrant, 20.0);
	}
	const auto Request = FGuid::NewGuid();
	const auto Result = Cat->GetConditionComponent()->ConsumeCommittedFish(Request, Fish, 0.5);
	TestTrue(TEXT("formal food chain commits"), Result.bCommitted);
	TestEqual(TEXT("food grants reserve once"), ASC->GetYellowFightStamina(), 40.0f);
	const auto Replay = Cat->GetConditionComponent()->ConsumeCommittedFish(Request, Fish, 0.5);
	TestTrue(TEXT("food request replay is recognized"), Replay.bTerminalReplay);
	TestEqual(TEXT("replay cannot double-grant reserve"), ASC->GetYellowFightStamina(), 40.0f);
	ASC->ClearActorInfo();
	TestFalse(TEXT("grant with no authority avatar is rejected"), ASC->ApplyYellowFightStaminaDelta(1));
	TestFalse(TEXT("day cleanup also requires authority avatar"), ASC->ClearYellowFightStaminaFromAuthority());
	ASC->InitAbilityActorInfo(Cat, Cat);
	TestEqual(TEXT("repossessing does not fill green or clear yellow"), ASC->GetTotalFightStamina(), 50.0);
	return !HasAnyErrors();
}
#endif
