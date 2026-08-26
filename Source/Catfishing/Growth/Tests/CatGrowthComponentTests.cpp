#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Data/CatFishDefinition.h"
#include "GameFramework/Actor.h"
#include "Growth/CatGrowthComponent.h"
#include "Growth/CatGrowthSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatGrowthComponentExperienceSlotTest,
	"Catfishing.Unit.Growth.Component.CommittedFishAddsExperienceAndReplays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatGrowthComponentTest
{
	/** 测试期间临时改写 Growth Settings 默认对象；析构恢复，避免一个自动化个案污染后续测试。 */
	struct FSettingsOverride
	{
		/** 被临时改写的默认配置对象；Growth 运行时只读取 GetDefault，因此测试必须安全恢复它。 */
		UCatGrowthSettings* Settings = GetMutableDefault<UCatGrowthSettings>();

		/** 原始 Growth runtime gate。 */
		bool bOldRuntime = false;

		/** 原始经验槽长度。 */
		int32 OldExperiencePerChoiceSlot = 0;

		// 保存流程：构造时复制默认对象，再写入测试需要的槽长；不调用 SaveConfig。
		explicit FSettingsOverride(const int32 SlotLength)
		{
			if (Settings)
			{
				bOldRuntime = Settings->bEnableGrowthRuntime;
				OldExperiencePerChoiceSlot = Settings->ExperiencePerChoiceSlot;
				Settings->bEnableGrowthRuntime = true;
				Settings->ExperiencePerChoiceSlot = SlotLength;
			}
		}

		// 恢复流程：把默认对象还原到测试前状态，防止后续测试继承本用例的槽长。
		~FSettingsOverride()
		{
			if (Settings)
			{
				Settings->bEnableGrowthRuntime = bOldRuntime;
				Settings->ExperiencePerChoiceSlot = OldExperiencePerChoiceSlot;
			}
		}
	};

	// 构造流程：只填 RuntimeDefinition 所需字段；测试通过 EatingExperience 参数表达不同体重档带来的经验差异。
	static UCatFishDefinition* MakeFishDefinition(const double EatingExperience)
	{
		UCatFishDefinition* Definition = NewObject<UCatFishDefinition>(GetTransientPackage());
		Definition->bEnableRuntimeDefinition = true;
		Definition->FishDefinitionId = TEXT("GrowthFish");
		Definition->BodyClass = ECatFishBodyClass::Standard;
		Definition->SacrificeContribution = 1;
		Definition->RarityTierId = TEXT("GrowthTier");
		Definition->RegionIds = {TEXT("LakeA")};
		Definition->TimeOfDay = {ECatEnvironmentTimeOfDay::Day};
		Definition->Weather = {ECatEnvironmentWeather::Clear};
		Definition->SpawnWeight = 1.0;
		Definition->MinimumWeightKilograms = 1.0;
		Definition->MaximumWeightKilograms = 2.0;
		Definition->MinimumFightParticipants = 1;
		Definition->FishStrength = 1.0;
		Definition->FishFightStamina = 1.0;
		Definition->BitePersonalityId = TEXT("GrowthBite");
		Definition->FightPersonalityId = TEXT("GrowthFight");
		Definition->FoodSafety = ECatFishFoodSafety::Safe;
		Definition->EatingExperience = EatingExperience;
		Definition->PoisonIncrease = 0.0;
		return Definition;
	}

	// 组件装配流程：用普通 authority Actor 承载 Growth 组件；这些测试只覆盖吃鱼经验槽公开合同。
	static UCatGrowthComponent* AddGrowthComponent(AActor* Host)
	{
		UCatGrowthComponent* Component = Host ? NewObject<UCatGrowthComponent>(Host) : nullptr;
		if (Host && Component)
		{
			Host->AddInstanceComponent(Component);
			Component->RegisterComponent();
		}
		return Component;
	}
}

// 测试流程：把两次已提交吃鱼经验压入槽长 10 的 Growth 组件，确认溢出继承、待选次数和 RequestId 重放都稳定。
bool FCatGrowthComponentExperienceSlotTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatGrowthComponentTest::FSettingsOverride SettingsOverride(10);
	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建 Growth 测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UWorld* World = WorldWrapper.GetTestWorld();
		AActor* Host = World ? World->SpawnActor<AActor>() : nullptr;
		UCatGrowthComponent* Growth = CatGrowthComponentTest::AddGrowthComponent(Host);
		UCatFishDefinition* MediumFish = CatGrowthComponentTest::MakeFishDefinition(8.0);
		UCatFishDefinition* SmallFish = CatGrowthComponentTest::MakeFishDefinition(3.0);
		TestNotNull(TEXT("Growth 测试宿主 Actor 可创建"), Host);
		TestNotNull(TEXT("Growth 测试组件可创建"), Growth);
		TestNotNull(TEXT("Growth 测试鱼定义可创建"), MediumFish);
		if (Growth && MediumFish && SmallFish)
		{
			TestEqual(TEXT("初始 Growth Revision 为 0"), Growth->GetSnapshot().Revision, int64{0});
			const FGuid FirstRequestId = FGuid::NewGuid();
			const FCatDomainCommandResult First = Growth->ApplyCommittedFish(FirstRequestId, MediumFish);
			TestTrue(TEXT("第一次吃鱼成长提交"), First.bCommitted);
			TestEqual(TEXT("第一次提交无错误"), First.Error, ECatDomainCommandError::None);
			TestEqual(TEXT("第一次吃鱼累计经验"), Growth->GetSnapshot().TotalExperience, 8);
			TestEqual(TEXT("第一次吃鱼留在槽内"), Growth->GetSnapshot().ExperienceInCurrentSlot, 8);
			TestEqual(TEXT("第一次吃鱼没有满槽待选"), Growth->GetSnapshot().PendingChoiceCount, 0);

			const FGuid SecondRequestId = FGuid::NewGuid();
			const FCatDomainCommandResult Second = Growth->ApplyCommittedFish(SecondRequestId, SmallFish);
			TestTrue(TEXT("第二次吃鱼成长提交"), Second.bCommitted);
			TestEqual(TEXT("第二次吃鱼累计经验"), Growth->GetSnapshot().TotalExperience, 11);
			TestEqual(TEXT("经验满槽后溢出继承"), Growth->GetSnapshot().ExperienceInCurrentSlot, 1);
			TestEqual(TEXT("满槽生成一个待选次数"), Growth->GetSnapshot().PendingChoiceCount, 1);

			const FCatDomainCommandResult Replay = Growth->ApplyCommittedFish(SecondRequestId, SmallFish);
			TestFalse(TEXT("重复 RequestId 不再次提交"), Replay.bCommitted);
			TestEqual(TEXT("重复 RequestId 返回 AlreadyResolved"), Replay.Error, ECatDomainCommandError::AlreadyResolved);
			TestEqual(TEXT("重放不增加累计经验"), Growth->GetSnapshot().TotalExperience, 11);
			TestEqual(TEXT("重放不增加待选次数"), Growth->GetSnapshot().PendingChoiceCount, 1);
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
