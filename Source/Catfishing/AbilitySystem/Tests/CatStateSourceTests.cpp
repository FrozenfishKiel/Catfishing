#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Tags/CatStateTags.h"
#include "Character/CatCharacter.h"
#include "Condition/CatConditionComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatStateSourceTest,
	"Catfishing.Contract.AbilitySystem.StateSourcesOwnIndependentEffects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 使用真实角色验证来源幂等、独立撤销和旧读模型同源；不以手工 LooseTag 代替生产 GE 链。
bool FCatStateSourceTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper Scene;
	if (!Scene.CreateTestWorld(EWorldType::Game)) return false;
	auto* Character = Scene.GetTestWorld()->SpawnActor<ACatCharacter>();
	if (!TestNotNull(TEXT("角色创建成功"), Character)) return false;
	auto* ASC = Character->GetCatAbilitySystemComponent();
	if (!ASC->InitializeCharacterOwnerAvatar(Character)) return false;
	const FGameplayTagContainer Wet(CatStateTags::Wet);
	TestTrue(TEXT("天气来源授予湿毛"), ASC->SetStateTagsFromAuthority(TEXT("Weather"), Wet));
	TestTrue(TEXT("相同来源重复提交成功"), ASC->SetStateTagsFromAuthority(TEXT("Weather"), Wet));
	TestEqual(TEXT("同源没有重复叠加"), ASC->GetTagCount(CatStateTags::Wet), 1);
	TestTrue(TEXT("第二来源独立授予"), ASC->SetStateTagsFromAuthority(TEXT("Water"), Wet));
	ASC->SetStateTagsFromAuthority(TEXT("Weather"), FGameplayTagContainer());
	TestEqual(TEXT("撤销天气不删除水域湿毛"), ASC->GetTagCount(CatStateTags::Wet), 1);
	TestTrue(TEXT("兼容投影只读同一状态"), Character->GetConditionComponent()->GetSnapshot().bWet);
	ASC->SetStateTagsFromAuthority(TEXT("Water"), FGameplayTagContainer(CatStateTags::Downed));
	TestFalse(TEXT("来源更新移除旧状态"), ASC->HasMatchingGameplayTag(CatStateTags::Wet));
	TestTrue(TEXT("来源更新授予新状态"), Character->GetConditionComponent()->GetSnapshot().bDowned);
	Character->GetConditionComponent()->SetDownedFromAuthority(true);
	ASC->SetStateTagsFromAuthority(TEXT("Water"), FGameplayTagContainer());
	TestTrue(TEXT("兼容写口仍有自己的来源"), ASC->HasMatchingGameplayTag(CatStateTags::Downed));
	ASC->ClearStateSourcesFromAuthority();
	TestFalse(TEXT("最终清理所有本组件状态"), ASC->HasMatchingGameplayTag(CatStateTags::State));
	return true;
}

#endif
