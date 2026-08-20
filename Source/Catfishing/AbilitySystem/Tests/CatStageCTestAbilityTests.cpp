#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/CatAbilitySettings.h"
#include "AbilitySystem/CatStageCTestAbility.h"
#include "AbilitySystem/CatSurvivalAttributeSet.h"
#include "GameFramework/Actor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatStageCTestAbilityAuthorityGateTest,
	"Catfishing.Unit.AbilitySystem.StageCTestAbility.AuthorityActivationAppliesConfiguredPoisonDelta",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatStageCTestAbilityTest
{
	/** 测试期间临时改写默认 Ability Settings 的守卫；析构恢复，避免一个自动化个案污染后续测试。 */
	struct FAbilitySettingsOverride
	{
		/** 被临时改写的默认配置对象；Ability 运行时只读取 GetDefault，因此测试必须安全恢复它。 */
		UCatAbilitySettings* Settings = GetMutableDefault<UCatAbilitySettings>();

		/** 原始 runtime gate。 */
		bool bOldRuntime = false;

		/** 原始复制策略。 */
		ECatAbilityReplicationPolicy OldReplication = ECatAbilityReplicationPolicy::Undecided;

		/** 原始诊断 Ability gate。 */
		bool bOldDiagnostic = false;

		/** 原始诊断 Poison 改变量。 */
		float OldDelta = 0.0f;

		// 保存流程：在构造时复制默认对象的可变字段，随后写入本测试需要的显式可运行配置。
		explicit FAbilitySettingsOverride(const float DiagnosticDelta)
		{
			if (Settings)
			{
				bOldRuntime = Settings->bEnableCharacterAbilityRuntime;
				OldReplication = Settings->ReplicationPolicy;
				bOldDiagnostic = Settings->bEnableDiagnosticAbility;
				OldDelta = Settings->DiagnosticPoisonDelta;
				Settings->bEnableCharacterAbilityRuntime = true;
				Settings->ReplicationPolicy = ECatAbilityReplicationPolicy::Full;
				Settings->bEnableDiagnosticAbility = true;
				Settings->DiagnosticPoisonDelta = DiagnosticDelta;
			}
		}

		// 恢复流程：把默认对象还原到测试前状态；不调用 SaveConfig，避免写入项目配置。
		~FAbilitySettingsOverride()
		{
			if (Settings)
			{
				Settings->bEnableCharacterAbilityRuntime = bOldRuntime;
				Settings->ReplicationPolicy = OldReplication;
				Settings->bEnableDiagnosticAbility = bOldDiagnostic;
				Settings->DiagnosticPoisonDelta = OldDelta;
			}
		}
	};

	/** 诊断 Ability 的真实执行环境；ASC、AttributeSet 与 AbilitySpec 都经正式 GameplayAbilities 接口装配。 */
	struct FAbilityFixture
	{
		/** authority Actor Owner/Avatar。 */
		TObjectPtr<AActor> Owner = nullptr;

		/** 激活 Ability 的 ASC。 */
		TObjectPtr<UAbilitySystemComponent> AbilitySystem = nullptr;

		/** ASC 持有的 Survival AttributeSet。 */
		TObjectPtr<UCatSurvivalAttributeSet> AttributeSet = nullptr;
	};

	// 装配流程：用真实 Actor/ASC/AttributeSet 建立 ActorInfo，再通过 GiveAbility 添加项目诊断 Ability，测试只从 TryActivateAbilityByClass 进入。
	static FAbilityFixture CreateFixture(UWorld* World)
	{
		FAbilityFixture Fixture;
		Fixture.Owner = World ? World->SpawnActor<AActor>() : nullptr;
		Fixture.AbilitySystem = Fixture.Owner ? NewObject<UAbilitySystemComponent>(Fixture.Owner) : nullptr;
		Fixture.AttributeSet = Fixture.Owner ? NewObject<UCatSurvivalAttributeSet>(Fixture.Owner) : nullptr;
		if (Fixture.Owner && Fixture.AbilitySystem && Fixture.AttributeSet)
		{
			Fixture.Owner->AddInstanceComponent(Fixture.AbilitySystem);
			Fixture.AbilitySystem->RegisterComponent();
			Fixture.AbilitySystem->AddAttributeSetSubobject(Fixture.AttributeSet.Get());
			Fixture.AbilitySystem->InitAbilityActorInfo(Fixture.Owner, Fixture.Owner);
			Fixture.AbilitySystem->GiveAbility(FGameplayAbilitySpec(UCatStageCTestAbility::StaticClass(), 1));
		}
		return Fixture;
	}
}

// 测试流程：临时开启诊断 Ability 配置，在真实 ASC 上激活项目 Ability，核对它按配置把 Poison 改成预期值。
// 这个用例真正保护的不变量是「Ability 只写它声明的那一项属性」，而这条不变量必须靠一个对照属性才能成立：
// 只断言 Poison 的话，一个把整张 AttributeSet 清零、或者顺手改动别的属性的实现照样能通过。
// 对照属性选 FightStamina：它是 FishingSession 参战谓词读取的属性之一（体力非正不能参战/协作），被误写会直接改变能否参战，
// 在剩下的两项搏斗属性里离玩法判定最近，所以由它来钉住「没被碰过」这件事最有价值。
bool FCatStageCTestAbilityAuthorityGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	// AbilitySystemGlobals 初始化时会对没有配置 GameplayCueNotifyPaths 的项目发一次警告，而本项目 Config 下确实没有配置这一项。
	// 该警告每进程只发一次，落在哪个用例窗口里完全取决于本次执行顺序：单独跑这个用例时它会落进来，跟在别的 ASC 用例后面跑时就不会。
	// 出现次数传负数是引擎里唯一能同时容下这两种情况的写法：按 AutomationTest.h 的约定，>0 要求次数完全相等，==0 要求
	// 至少出现一次（一次都没有反而判失败），
	// <0 才是「匹配到就静默吞掉，没匹配到也不追究」。这样无论警告落不落在本用例窗口内，结论都只取决于属性断言本身。
	AddExpectedErrorPlain(TEXT("No GameplayCueNotifyPaths were specified"), EAutomationExpectedErrorFlags::Contains, -1);
	CatStageCTestAbilityTest::FAbilitySettingsOverride SettingsOverride(-2.5f);
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Stage C Ability 测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	TestNotNull(TEXT("可创建测试 World"), World);
	if (!World)
	{
		return false;
	}

	const CatStageCTestAbilityTest::FAbilityFixture Fixture = CatStageCTestAbilityTest::CreateFixture(World);
	TestNotNull(TEXT("Ability 测试 ASC 已创建"), Fixture.AbilitySystem.Get());
	TestNotNull(TEXT("Ability 测试 AttributeSet 已创建"), Fixture.AttributeSet.Get());
	if (!Fixture.AbilitySystem || !Fixture.AttributeSet)
	{
		return false;
	}

	Fixture.AbilitySystem->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetPoisonAttribute(), 10.0f);
	Fixture.AbilitySystem->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 20.0f);
	TestTrue(TEXT("正式 TryActivateAbilityByClass 接受诊断 Ability"),
		Fixture.AbilitySystem->TryActivateAbilityByClass(UCatStageCTestAbility::StaticClass()));
	TestEqual(TEXT("Ability 按配置修改 Poison"), Fixture.AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetPoisonAttribute()), 7.5f);
	TestEqual(TEXT("Ability 不修改 FightStamina"), Fixture.AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), 20.0f);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
