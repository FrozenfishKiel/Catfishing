#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "AbilitySystem/Config/CatAbilitySettings.h"
#include "AbilitySystem/Config/CatAbilitySet.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Character/CatCharacter.h"

// 默认能力集回归：用项目配置与真实角色 ASC 验证删掉旧动作后仍能完整授予和回收能力。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatDefaultAbilitySetTest,
	"Catfishing.Contract.AbilitySystem.DefaultConfiguredSetGrantsAndRevokes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 先加载正式能力配置并验证就绪，再建立 authority 角色；通过生产授予入口检查能力数量、重复授予拒绝和整组回收。
// 不替换项目配置或手工授予测试 Ability，确保资产空槽、数量门禁和默认授予接线失配都会使回归失败。
bool FCatDefaultAbilitySetTest::RunTest(const FString& Parameters)
{
	const UCatAbilitySettings* Settings = GetDefault<UCatAbilitySettings>();
	const UCatAbilitySet* AbilitySet = Settings->DefaultAbilitySet.LoadSynchronous();
	if (!TestNotNull(TEXT("正式默认能力集存在"), AbilitySet)
		|| !TestTrue(TEXT("正式能力配置就绪"), Settings->IsFishingRuntimeReady())) return false;

	FTestWorldWrapper Scene;
	if (!TestTrue(TEXT("建立服务器测试世界"), Scene.CreateTestWorld(EWorldType::Game))) return false;
	ACatCharacter* Character = Scene.GetTestWorld()->SpawnActor<ACatCharacter>();
	if (!TestNotNull(TEXT("角色存在"), Character)) return false;
	UCatAbilitySystemComponent* ASC = Character->GetCatAbilitySystemComponent();
	if (!TestNotNull(TEXT("角色能力组件存在"), ASC)
		|| !TestTrue(TEXT("按正式配置建立角色能力身份"), ASC->InitializeCharacterOwnerAvatar(Character))) return false;
	ASC->RevokeConfiguredDefaultAbilitySet();
	TestTrue(TEXT("默认能力集完整授予"), ASC->GrantConfiguredDefaultAbilitySetFromAuthority());
	TestEqual(TEXT("所有配置能力都进入真实 ASC"), ASC->GetActivatableAbilities().Num(), AbilitySet->GrantedAbilities.Num());
	TestFalse(TEXT("重复授予不能叠加能力"), ASC->GrantConfiguredDefaultAbilitySetFromAuthority());
	ASC->RevokeConfiguredDefaultAbilitySet();
	TestEqual(TEXT("回收后没有遗留能力"), ASC->GetActivatableAbilities().Num(), 0);
	return true;
}

#endif
