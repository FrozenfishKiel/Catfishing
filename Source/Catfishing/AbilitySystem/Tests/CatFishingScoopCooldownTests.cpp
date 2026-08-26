#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystem/CatFishingAbilities.h"
#include "AbilitySystem/CatFishingAbilityTags.h"
#include "GameplayEffect.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingScoopCooldownContractTest,
	"Catfishing.Unit.AbilitySystem.ScoopCooldown.AbilityOwnsTaggedDurationEffect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatFishingScoopCooldownContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const UCatGA_FishingScoop* Ability = GetDefault<UCatGA_FishingScoop>();
	TestNotNull(TEXT("抄网 Ability CDO 存在"), Ability);
	if (!Ability)
	{
		return false;
	}

	const UGameplayEffect* CooldownEffect = Ability->GetCooldownGameplayEffect();
	TestNotNull(TEXT("抄网 Ability 配置了冷却 GameplayEffect"), CooldownEffect);
	if (CooldownEffect)
	{
		TestEqual(TEXT("冷却 GameplayEffect 是持续型"), CooldownEffect->DurationPolicy,
			EGameplayEffectDurationType::HasDuration);
		TestTrue(TEXT("冷却 GameplayEffect 授予专用标签"),
			CooldownEffect->GetGrantedTags().HasTagExact(CatFishingAbilityTags::Cooldown_Fishing_Scoop));
	}
	const FGameplayTagContainer* CooldownTags = Ability->GetCooldownTags();
	TestTrue(TEXT("GAS 激活检查能读取抄网冷却标签"), CooldownTags
		&& CooldownTags->HasTagExact(CatFishingAbilityTags::Cooldown_Fishing_Scoop));
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
