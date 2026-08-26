#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "AbilitySystem/CatAbilitySystemComponent.h"
#include "AbilitySystem/CatSurvivalAttributeSet.h"
#include "GameFramework/Actor.h"

#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatSurvivalAttributeSetValueContractTest,
	"Catfishing.Unit.AbilitySystem.SurvivalAttributeSet.ASCReadsAndWritesThreeIndependentAttributes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatAbilitySystemComponentPoisonDeltaContractTest,
	"Catfishing.Unit.AbilitySystem.AbilitySystemComponent.PoisonDeltaUsesGameplayEffectAndClampsRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatSurvivalAttributeSetTest
{
	/** 真实 ASC 与 Survival AttributeSet 的最小装配；测试只通过 ASC 属性接口读写，不直接修改 AttributeSet 字段。 */
	struct FAbilityFixture
	{
		/** 承载 ASC 组件的 authority Actor；组件注册依赖真实 Owner 生命周期。 */
		TObjectPtr<AActor> Owner = nullptr;

		/** 测试使用的项目 AbilitySystemComponent；它是属性读写、GE 提交和 delegate 的唯一入口。 */
		TObjectPtr<UCatAbilitySystemComponent> AbilitySystem = nullptr;

		/** 被 ASC 持有的项目 Survival AttributeSet；测试不把它当成第二个写口。 */
		TObjectPtr<UCatSurvivalAttributeSet> AttributeSet = nullptr;
	};

	// 装配流程：生成真实 Actor、注册 ASC 组件，再把 Survival AttributeSet 作为 ASC 子对象加入，使读写路径与 Character 运行时一致。
	static FAbilityFixture CreateFixture(UWorld* World)
	{
		FAbilityFixture Fixture;
		Fixture.Owner = World ? World->SpawnActor<AActor>() : nullptr;
		Fixture.AbilitySystem = Fixture.Owner ? NewObject<UCatAbilitySystemComponent>(Fixture.Owner) : nullptr;
		Fixture.AttributeSet = Fixture.Owner ? NewObject<UCatSurvivalAttributeSet>(Fixture.Owner) : nullptr;
		if (Fixture.Owner && Fixture.AbilitySystem && Fixture.AttributeSet)
		{
			Fixture.Owner->AddInstanceComponent(Fixture.AbilitySystem);
			Fixture.AbilitySystem->RegisterComponent();
			Fixture.AbilitySystem->AddAttributeSetSubobject(Fixture.AttributeSet.Get());
			Fixture.AbilitySystem->InitAbilityActorInfo(Fixture.Owner, Fixture.Owner);
		}
		return Fixture;
	}
}

// 测试流程：在真实 Game World 中装配 ASC/AttributeSet，通过 ASC 设置三项属性并逐项读取；最后只改 Poison，确认搏斗两项不会被联动改写。
bool FCatSurvivalAttributeSetValueContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Survival AttributeSet 测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建测试 World"), World);
	if (!World)
	{
		return false;
	}

	const CatSurvivalAttributeSetTest::FAbilityFixture Fixture = CatSurvivalAttributeSetTest::CreateFixture(World);
	TestNotNull(TEXT("ASC 宿主 Actor 已创建"), Fixture.Owner.Get());
	TestNotNull(TEXT("真实 ASC 已创建"), Fixture.AbilitySystem.Get());
	TestNotNull(TEXT("真实 Survival AttributeSet 已创建"), Fixture.AttributeSet.Get());
	if (!Fixture.AbilitySystem || !Fixture.AttributeSet)
	{
		return false;
	}

	Fixture.AbilitySystem->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetPoisonAttribute(), 3.0f);
	Fixture.AbilitySystem->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), 4.0f);
	Fixture.AbilitySystem->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 5.0f);

	TestEqual(TEXT("ASC 可读取 Poison"), Fixture.AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetPoisonAttribute()), 3.0f);
	TestEqual(TEXT("ASC 可读取 FishingStrength"), Fixture.AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetFishingStrengthAttribute()), 4.0f);
	TestEqual(TEXT("ASC 可读取 FightStamina"), Fixture.AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), 5.0f);

	Fixture.AbilitySystem->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetPoisonAttribute(), 7.0f);
	TestEqual(TEXT("Poison 可独立变化"), Fixture.AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetPoisonAttribute()), 7.0f);
	TestEqual(TEXT("Poison 变化不改 FishingStrength"), Fixture.AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetFishingStrengthAttribute()), 4.0f);
	TestEqual(TEXT("Poison 变化不改 FightStamina"), Fixture.AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), 5.0f);
	return !HasAnyErrors();
}

// 测试流程：以项目 ASC 的 Poison delta 接口提交正向中毒和负向恢复；验证调用方不直接写基值时也能通过 GE 改 Poison，并且恢复不会降到 0 以下。
bool FCatAbilitySystemComponentPoisonDeltaContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Poison delta 测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建测试 World"), World);
	if (!World)
	{
		return false;
	}

	const CatSurvivalAttributeSetTest::FAbilityFixture Fixture = CatSurvivalAttributeSetTest::CreateFixture(World);
	TestNotNull(TEXT("Poison delta 测试 ASC 已创建"), Fixture.AbilitySystem.Get());
	TestNotNull(TEXT("Poison delta 测试 AttributeSet 已创建"), Fixture.AttributeSet.Get());
	if (!Fixture.AbilitySystem || !Fixture.AttributeSet)
	{
		return false;
	}

	Fixture.AbilitySystem->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetPoisonAttribute(), 2.0f);
	TestTrue(TEXT("正向 Poison delta 通过 GameplayEffect 提交"), Fixture.AbilitySystem->ApplyPoisonDelta(4.0f));
	TestEqual(TEXT("Poison 增加到预期值"), Fixture.AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetPoisonAttribute()), 6.0f);
	TestTrue(TEXT("负向 Poison delta 通过 GameplayEffect 提交"), Fixture.AbilitySystem->ApplyPoisonDelta(-3.0f));
	TestEqual(TEXT("Poison 恢复到预期值"), Fixture.AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetPoisonAttribute()), 3.0f);
	TestTrue(TEXT("过量恢复被夹到零且仍视为提交成功"), Fixture.AbilitySystem->ApplyPoisonDelta(-20.0f));
	TestEqual(TEXT("Poison 不低于零"), Fixture.AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetPoisonAttribute()), 0.0f);
	TestFalse(TEXT("非法 Poison delta 被拒绝"), Fixture.AbilitySystem->ApplyPoisonDelta(std::numeric_limits<float>::quiet_NaN()));
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
