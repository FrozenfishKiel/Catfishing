#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystem/Config/CatAbilitySettings.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Character/CatCharacter.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingStaminaTinyDebitTest,
	"Catfishing.Unit.AbilitySystem.FishingStamina.TinyPositiveBalanceCanBeFullyDebited",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingStaminaTinyDebitTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建真实 ASC 扣费测试世界"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
	WorldWrapper.ForwardErrorMessages(this);
	ACatCharacter* Character = WorldWrapper.GetTestWorld()->SpawnActor<ACatCharacter>();
	if (!TestNotNull(TEXT("生成角色身体"), Character)) return false;
	UCatAbilitySystemComponent* ASC = Character->GetCatAbilitySystemComponent();
	if (!TestNotNull(TEXT("使用身体正式 ASC"), ASC)) return false;
	ASC->InitAbilityActorInfo(Character, Character);
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(), 60.0f);
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 1.0e-9f);
	const float Remaining = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	if (!TestTrue(TEXT("合法极低正余额仍保留出力资格"), Remaining > 0.0f)) return false;
	TestTrue(TEXT("正式 GE 接受极低正余额对应的真实扣费"), ASC->ApplyFishingStaminaDelta(-Remaining));
	TestEqual(TEXT("扣费后余额精确到零，不能留下永远无法耗尽的力量"),
		ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), 0.0f);
	TestFalse(TEXT("零增量不能伪装成新的扣费提交"), ASC->ApplyFishingStaminaDelta(0.0f));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatFishingStaminaActorInfoRefreshTest,
	"Catfishing.Unit.AbilitySystem.FishingStamina.ActorInfoRefreshPreservesSpentBalanceAndRuntimeMaximum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingStaminaActorInfoRefreshTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UCatAbilitySettings* Settings = GetMutableDefault<UCatAbilitySettings>();
	TGuardValue<bool> RuntimeGuard(Settings->bEnableCharacterAbilityRuntime, true);
	TGuardValue<ECatAbilityReplicationPolicy> ReplicationGuard(Settings->ReplicationPolicy, ECatAbilityReplicationPolicy::Full);
	TGuardValue<bool> TuningGuard(Settings->bEnableInitialAttributeTuning, true);
	TGuardValue<FName> DefinitionGuard(Settings->DefaultCharacterDefinitionId, NAME_None);
	TGuardValue<float> PoisonGuard(Settings->InitialPoison, 0.0f);
	TGuardValue<float> StrengthGuard(Settings->InitialFishingStrength, 50.0f);
	TGuardValue<float> MaximumGuard(Settings->InitialFightStamina, 60.0f);

	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建真实身体生命周期测试世界"), WorldWrapper.CreateTestWorld(EWorldType::Game))) return false;
	WorldWrapper.ForwardErrorMessages(this);
	ACatCharacter* Character = WorldWrapper.GetTestWorld()->SpawnActor<ACatCharacter>();
	if (!TestNotNull(TEXT("生成角色身体"), Character)) return false;
	UCatAbilitySystemComponent* ASC = Character->GetCatAbilitySystemComponent();
	if (!TestNotNull(TEXT("取得 Character-owned ASC"), ASC)) return false;
	if (!TestTrue(TEXT("显式运行 gate 建立 Owner/Avatar"), ASC->InitializeCharacterOwnerAvatar(Character))) return false;
	if (!TestTrue(TEXT("一次性播种身体属性"), ASC->InitializeCharacterAttributesFromDefinition(NAME_None))) return false;
	TestEqual(TEXT("初始上限来自身体配置"), ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute()), 60.0f);
	TestEqual(TEXT("新身体初始体力按正式 ASC 上限建立"), ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), 60.0f);
	if (!TestTrue(TEXT("正式 GE 消耗 37 点体力"), ASC->ApplyFishingStaminaDelta(-37.0f))) return false;

	// 成长/装备改变运行上限不能补满；重占有只恢复 ActorInfo，也不能重新套用最初的 60 点配置。
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(), 90.0f);
	TestEqual(TEXT("上限提高不补满个人余额"), ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), 23.0f);
	ASC->ClearActorInfo();
	TestNull(TEXT("断开后不保留旧 Avatar"), ASC->GetAvatarActor());
	if (!TestTrue(TEXT("同一身体重新建立 ActorInfo"), ASC->InitializeCharacterOwnerAvatar(Character))) return false;
	if (!TestTrue(TEXT("重复生命周期播种保持幂等"), ASC->InitializeCharacterAttributesFromDefinition(NAME_None))) return false;
	TestEqual(TEXT("重新建立 ActorInfo 不回满已经消耗的体力"), ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), 23.0f);
	TestEqual(TEXT("重新建立 ActorInfo 不覆盖运行时 ASC 上限"), ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute()), 90.0f);
	TestFalse(TEXT("普通重占有不产生延迟回满请求"), ASC->HasPendingFishingStaminaReset());
	return !HasAnyErrors();
}

#endif
