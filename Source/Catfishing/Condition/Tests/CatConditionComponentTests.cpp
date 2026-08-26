#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystem/Config/CatAbilitySettings.h"
#include "Character/CatCharacter.h"
#include "Condition/CatConditionComponent.h"
#include "Condition/CatConditionSettings.h"
#include "Data/CatFishDefinition.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "Growth/CatGrowthSettings.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatConditionComponentWetAuthorityTest,
	"Catfishing.Unit.Condition.Component.AuthorityWetChangesRevisionAndRepeatDoesNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatConditionComponentCarryToCampTest,
	"Catfishing.Unit.Condition.Component.CarryToCampRequiresValidRescueFactAndReplays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatConditionComponentHerbRecoveryRangeTest,
	"Catfishing.Unit.Condition.Component.HerbRecoveryRequiresAuthorityRangeAndReplays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatConditionComponentTest
{
	/** 测试期间的身体/成长运行配置覆盖；它只改内存 CDO，让真实 Character 能用 ASC、Condition 和 Growth 形成倒地事实。 */
	struct FScopedConditionRuntimeSettings
	{
		/** Ability runtime 原值；析构恢复，避免后续测试读到本用例的 Character-owned ASC gate。 */
		bool bSavedAbilityRuntime = false;

		/** Attribute 初值 gate 原值；析构恢复，防止其他测试被强制播种身体数值。 */
		bool bSavedInitialAttributeTuning = false;

		/** ASC 复制策略原值；测试只需要 Full，不改变全局 Mixed 验证口径。 */
		ECatAbilityReplicationPolicy SavedReplicationPolicy = ECatAbilityReplicationPolicy::Undecided;

		/** 初始 Poison 原值；目标 Character 需要从健康状态开始再通过吃毒鱼倒地。 */
		float SavedInitialPoison = -1.0f;

		/** 初始 FishingStrength 原值；测试只给正值以通过 Character 属性播种。 */
		float SavedInitialFishingStrength = -1.0f;

		/** 初始 FightStamina 原值；测试只给正值以通过 Character 属性播种。 */
		float SavedInitialFightStamina = -1.0f;

		/** Condition runtime 原值；析构恢复，避免倒地阈值泄漏到其他用例。 */
		bool bSavedConditionRuntime = false;

		/** Poison 倒地阈值原值；本测试用很低阈值稳定制造 Downed。 */
		double SavedPoisonDownedThreshold = 0.0;

		/** 野外恢复量原值；本用例不测恢复，但保留它防止覆盖不完整。 */
		double SavedFieldRestPoisonRelief = 0.0;

		/** 营地恢复量原值；本用例不测恢复，但保留它防止覆盖不完整。 */
		double SavedCampRestPoisonRelief = 0.0;

		/** 草药恢复量原值；本用例不测草药，但保留它防止覆盖不完整。 */
		double SavedHerbPoisonRelief = 0.0;

		/** 草药距离原值；本用例不测施药距离，但恢复完整 CDO 能隔离测试。 */
		double SavedHerbUseRangeCentimeters = 0.0;

		/** Growth runtime 原值；吃鱼预检要求 Growth 可运行。 */
		bool bSavedGrowthRuntime = false;

		/** 成长槽长度原值；本测试只需要正槽长，不消费 Buff。 */
		int32 SavedExperiencePerChoiceSlot = 0;

		/** 可写 Ability 默认对象；构造写入测试 gate，析构按原值恢复。 */
		UCatAbilitySettings* AbilitySettings = nullptr;

		/** 可写 Condition 默认对象；构造写入倒地阈值，析构按原值恢复。 */
		UCatConditionSettings* ConditionSettings = nullptr;

		/** 可写 Growth 默认对象；构造写入正槽长，析构按原值恢复。 */
		UCatGrowthSettings* GrowthSettings = nullptr;

		/** 构造流程：保存三组默认设置，再只在内存中打开 Character ASC、Condition 阈值和 Growth 槽长。 */
		FScopedConditionRuntimeSettings()
		{
			AbilitySettings = GetMutableDefault<UCatAbilitySettings>();
			if (AbilitySettings)
			{
				bSavedAbilityRuntime = AbilitySettings->bEnableCharacterAbilityRuntime;
				bSavedInitialAttributeTuning = AbilitySettings->bEnableInitialAttributeTuning;
				SavedReplicationPolicy = AbilitySettings->ReplicationPolicy;
				SavedInitialPoison = AbilitySettings->InitialPoison;
				SavedInitialFishingStrength = AbilitySettings->InitialFishingStrength;
				SavedInitialFightStamina = AbilitySettings->InitialFightStamina;
				AbilitySettings->bEnableCharacterAbilityRuntime = true;
				AbilitySettings->bEnableInitialAttributeTuning = true;
				AbilitySettings->ReplicationPolicy = ECatAbilityReplicationPolicy::Full;
				AbilitySettings->InitialPoison = 0.0f;
				AbilitySettings->InitialFishingStrength = 5.0f;
				AbilitySettings->InitialFightStamina = 5.0f;
			}
			ConditionSettings = GetMutableDefault<UCatConditionSettings>();
			if (ConditionSettings)
			{
				bSavedConditionRuntime = ConditionSettings->bEnableConditionRuntime;
				SavedPoisonDownedThreshold = ConditionSettings->PoisonDownedThreshold;
				SavedFieldRestPoisonRelief = ConditionSettings->FieldRestPoisonRelief;
				SavedCampRestPoisonRelief = ConditionSettings->CampRestPoisonRelief;
				SavedHerbPoisonRelief = ConditionSettings->HerbPoisonRelief;
				SavedHerbUseRangeCentimeters = ConditionSettings->HerbUseRangeCentimeters;
				ConditionSettings->bEnableConditionRuntime = true;
				ConditionSettings->PoisonDownedThreshold = 5.0;
				ConditionSettings->FieldRestPoisonRelief = 1.0;
				ConditionSettings->CampRestPoisonRelief = 5.0;
				ConditionSettings->HerbPoisonRelief = 10.0;
				ConditionSettings->HerbUseRangeCentimeters = 250.0;
			}
			GrowthSettings = GetMutableDefault<UCatGrowthSettings>();
			if (GrowthSettings)
			{
				bSavedGrowthRuntime = GrowthSettings->bEnableGrowthRuntime;
				SavedExperiencePerChoiceSlot = GrowthSettings->ExperiencePerChoiceSlot;
				GrowthSettings->bEnableGrowthRuntime = true;
				GrowthSettings->ExperiencePerChoiceSlot = 10;
			}
		}

		/** 析构流程：逐项还原进入测试前的内存 CDO，不调用 SaveConfig，也不留下跨用例运行 gate。 */
		~FScopedConditionRuntimeSettings()
		{
			if (AbilitySettings)
			{
				AbilitySettings->bEnableCharacterAbilityRuntime = bSavedAbilityRuntime;
				AbilitySettings->bEnableInitialAttributeTuning = bSavedInitialAttributeTuning;
				AbilitySettings->ReplicationPolicy = SavedReplicationPolicy;
				AbilitySettings->InitialPoison = SavedInitialPoison;
				AbilitySettings->InitialFishingStrength = SavedInitialFishingStrength;
				AbilitySettings->InitialFightStamina = SavedInitialFightStamina;
			}
			if (ConditionSettings)
			{
				ConditionSettings->bEnableConditionRuntime = bSavedConditionRuntime;
				ConditionSettings->PoisonDownedThreshold = SavedPoisonDownedThreshold;
				ConditionSettings->FieldRestPoisonRelief = SavedFieldRestPoisonRelief;
				ConditionSettings->CampRestPoisonRelief = SavedCampRestPoisonRelief;
				ConditionSettings->HerbPoisonRelief = SavedHerbPoisonRelief;
				ConditionSettings->HerbUseRangeCentimeters = SavedHerbUseRangeCentimeters;
			}
			if (GrowthSettings)
			{
				GrowthSettings->bEnableGrowthRuntime = bSavedGrowthRuntime;
				GrowthSettings->ExperiencePerChoiceSlot = SavedExperiencePerChoiceSlot;
			}
		}
	};

	// 组件装配流程：用普通 authority Actor 承载 Condition 组件；这些测试只覆盖不依赖 ASC/Character 的公开合同。
	static UCatConditionComponent* AddConditionComponent(AActor* Host)
	{
		UCatConditionComponent* Component = Host ? NewObject<UCatConditionComponent>(Host) : nullptr;
		if (Host && Component)
		{
			Host->AddInstanceComponent(Component);
			Component->RegisterComponent();
		}
		return Component;
	}

	/** 创建一条完整可运行的 Toxic 鱼定义；测试只消费食用字段，其余字段用于通过正式定义 gate。 */
	static UCatFishDefinition* MakeToxicFishDefinition()
	{
		UCatFishDefinition* Definition = NewObject<UCatFishDefinition>(GetTransientPackage());
		if (!Definition)
		{
			return nullptr;
		}
		Definition->bEnableRuntimeDefinition = true;
		Definition->FishDefinitionId = FName(TEXT("ConditionToxicFish"));
		Definition->BodyClass = ECatFishBodyClass::Standard;
		Definition->SacrificeContribution = 1;
		Definition->RarityTierId = FName(TEXT("Common"));
		Definition->RegionIds = {FName(TEXT("River"))};
		Definition->TimeOfDay = {ECatEnvironmentTimeOfDay::Day};
		Definition->Weather = {ECatEnvironmentWeather::Clear};
		Definition->SpawnWeight = 1.0;
		Definition->MinimumWeightKilograms = 1.0;
		Definition->MaximumWeightKilograms = 2.0;
		Definition->ScoopTargetRadiusCentimeters = 30.0;
		Definition->MinimumFightParticipants = 1;
		Definition->FishStrength = 1.0;
		Definition->FishFightStamina = 1.0;
		Definition->BitePersonalityId = FName(TEXT("Bite"));
		Definition->FightPersonalityId = FName(TEXT("Fight"));
		Definition->FoodSafety = ECatFishFoodSafety::Toxic;
		Definition->EatingExperience = 1.0;
		Definition->PoisonIncrease = 10.0;
		return Definition;
	}
}

// 测试流程：在 authority Actor 上切换 Wet，读取公开 Snapshot 的 Revision/Wet；重复写相同值必须保持 Revision 不变。
bool FCatConditionComponentWetAuthorityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建 Condition Wet 测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UWorld* World = WorldWrapper.GetTestWorld();
		AActor* Host = World ? World->SpawnActor<AActor>() : nullptr;
		UCatConditionComponent* Component = CatConditionComponentTest::AddConditionComponent(Host);
		TestNotNull(TEXT("Wet 测试宿主 Actor 可创建"), Host);
		TestNotNull(TEXT("Wet 测试 Condition 组件可创建"), Component);
		if (Component)
		{
			TestEqual(TEXT("初始 Condition Revision 为 0"), Component->GetSnapshot().Revision, int64{0});
			TestFalse(TEXT("初始 Wet 为 false"), Component->GetSnapshot().bWet);
			Component->SetWetFromAuthority(true);
			TestTrue(TEXT("authority 设置 Wet 后为 true"), Component->GetSnapshot().bWet);
			TestEqual(TEXT("authority 设置 Wet 推进 Revision"), Component->GetSnapshot().Revision, int64{1});
			Component->SetWetFromAuthority(true);
			TestEqual(TEXT("重复设置相同 Wet 不推进 Revision"), Component->GetSnapshot().Revision, int64{1});
			Component->SetWetFromAuthority(false);
			TestFalse(TEXT("authority 清 Wet 后为 false"), Component->GetSnapshot().bWet);
			TestEqual(TEXT("Wet 真实变化再次推进 Revision"), Component->GetSnapshot().Revision, int64{2});
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

// 测试流程：先用无效救援事实和健康目标证明 CarryToCamp 拒绝；再通过真实 Character 吃毒鱼形成 Downed，随后提交搬运并原样重放。
bool FCatConditionComponentCarryToCampTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatConditionComponentTest::FScopedConditionRuntimeSettings SettingsOverride;
	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建 Condition 搬运测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UWorld* World = WorldWrapper.GetTestWorld();
		ACatCharacter* TargetCharacter = World ? World->SpawnActor<ACatCharacter>() : nullptr;
		APlayerController* TargetController = World ? World->SpawnActor<APlayerController>() : nullptr;
		AController* Helper = World ? World->SpawnActor<APlayerController>() : nullptr;
		UCatConditionComponent* Component = TargetCharacter ? TargetCharacter->GetConditionComponent() : nullptr;
		TestNotNull(TEXT("搬运测试目标 Character 可创建"), TargetCharacter);
		TestNotNull(TEXT("搬运测试目标 Controller 可创建"), TargetController);
		TestNotNull(TEXT("搬运测试 Controller 可创建"), Helper);
		TestNotNull(TEXT("搬运测试 Condition 组件可创建"), Component);
		if (TargetController && TargetCharacter)
		{
			TargetController->Possess(TargetCharacter);
		}
		UCatFishDefinition* ToxicFish = CatConditionComponentTest::MakeToxicFishDefinition();
		TestNotNull(TEXT("可创建完整 Toxic 鱼定义"), ToxicFish);
		if (Component && ToxicFish)
		{
			const FGuid InvalidRequestId = FGuid::NewGuid();
			const FCatDomainCommandResult Invalid = Component->CompleteCarryToCamp(Helper, InvalidRequestId, false);
			TestFalse(TEXT("未到营地救援点时搬运不提交"), Invalid.bCommitted);
			TestEqual(TEXT("未到营地救援点返回 InvalidPayload"), Invalid.Error, ECatDomainCommandError::InvalidPayload);
			TestEqual(TEXT("失败搬运不推进 Revision"), Component->GetSnapshot().Revision, int64{0});

			const FCatDomainCommandResult HealthyTarget = Component->CompleteCarryToCamp(Helper, FGuid::NewGuid(), true);
			TestFalse(TEXT("未倒地目标不能提交搬运完成"), HealthyTarget.bCommitted);
			TestEqual(TEXT("未倒地目标返回 InvalidPhase"), HealthyTarget.Error, ECatDomainCommandError::InvalidPhase);
			TestEqual(TEXT("健康目标拒绝后 Revision 仍不变"), Component->GetSnapshot().Revision, int64{0});

			AddExpectedErrorPlain(TEXT("Event=character_downed"), EAutomationExpectedErrorFlags::Contains, 1);
			const FCatDomainCommandResult Poisoned = Component->ConsumeCommittedFish(FGuid::NewGuid(), ToxicFish);
			TestTrue(TEXT("Toxic 鱼消费能提交身体状态"), Poisoned.bCommitted);
			TestTrue(TEXT("Toxic 鱼消费后目标进入 Downed"), Component->GetSnapshot().bDowned);
			const int64 DownedRevision = Component->GetSnapshot().Revision;

			const FGuid RequestId = FGuid::NewGuid();
			const FCatDomainCommandResult First = Component->CompleteCarryToCamp(Helper, RequestId, true);
			TestTrue(TEXT("有效搬运提交成功"), First.bCommitted);
			TestEqual(TEXT("有效搬运无错误"), First.Error, ECatDomainCommandError::None);
			TestEqual(TEXT("有效搬运只在倒地基础上推进一次 Revision"), First.Revision, DownedRevision + 1);
			TestEqual(TEXT("有效搬运写入恢复方式"), Component->GetSnapshot().RecoveryMode, ECatRecoveryMode::CarriedToCamp);

			const FCatDomainCommandResult Replay = Component->CompleteCarryToCamp(Helper, RequestId, true);
			TestFalse(TEXT("搬运重放不再次提交"), Replay.bCommitted);
			TestEqual(TEXT("搬运重放返回 AlreadyResolved"), Replay.Error, ECatDomainCommandError::AlreadyResolved);
			TestEqual(TEXT("搬运重放不推进 Revision"), Replay.Revision, First.Revision);
			TestEqual(TEXT("搬运重放后 Snapshot Revision 不变"), Component->GetSnapshot().Revision, First.Revision);
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

// 测试流程：先让目标通过正式吃鱼路径倒地；远距离 helper 的草药 preflight/commit 都拒绝，移到配置范围内后才恢复并按 RequestId 重放。
bool FCatConditionComponentHerbRecoveryRangeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatConditionComponentTest::FScopedConditionRuntimeSettings SettingsOverride;
	FTestWorldWrapper WorldWrapper;
	if (TestTrue(TEXT("创建 Condition 草药测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		UWorld* World = WorldWrapper.GetTestWorld();
		ACatCharacter* TargetCharacter = World ? World->SpawnActor<ACatCharacter>() : nullptr;
		APlayerController* TargetController = World ? World->SpawnActor<APlayerController>() : nullptr;
		ACatCharacter* HelperCharacter = World ? World->SpawnActor<ACatCharacter>() : nullptr;
		APlayerController* HelperController = World ? World->SpawnActor<APlayerController>() : nullptr;
		UCatConditionComponent* Component = TargetCharacter ? TargetCharacter->GetConditionComponent() : nullptr;
		UCatFishDefinition* ToxicFish = CatConditionComponentTest::MakeToxicFishDefinition();
		TestNotNull(TEXT("草药测试目标 Character 可创建"), TargetCharacter);
		TestNotNull(TEXT("草药测试目标 Controller 可创建"), TargetController);
		TestNotNull(TEXT("草药测试施药 Character 可创建"), HelperCharacter);
		TestNotNull(TEXT("草药测试施药 Controller 可创建"), HelperController);
		TestNotNull(TEXT("草药测试 Condition 组件可创建"), Component);
		TestNotNull(TEXT("草药测试 Toxic 鱼定义可创建"), ToxicFish);
		if (TargetController && TargetCharacter)
		{
			TargetController->Possess(TargetCharacter);
		}
		if (HelperController && HelperCharacter)
		{
			HelperController->Possess(HelperCharacter);
		}
		if (TargetCharacter)
		{
			TargetCharacter->SetActorLocation(FVector::ZeroVector);
		}
		if (HelperCharacter)
		{
			HelperCharacter->SetActorLocation(FVector(1000.0, 0.0, 0.0));
		}
		if (Component && ToxicFish)
		{
			AddExpectedErrorPlain(TEXT("Event=character_downed"), EAutomationExpectedErrorFlags::Contains, 1);
			const FCatDomainCommandResult Poisoned = Component->ConsumeCommittedFish(FGuid::NewGuid(), ToxicFish);
			TestTrue(TEXT("草药测试前 Toxic 鱼能让目标倒地"), Poisoned.bCommitted);
			TestTrue(TEXT("草药测试目标已倒地"), Component->GetSnapshot().bDowned);
			TestEqual(TEXT("远距离施药 preflight 拒绝"), Component->ValidateHerbRecovery(HelperController),
				ECatDomainCommandError::PolicyUndecided);

			const FGuid RequestId = FGuid::NewGuid();
			const FCatDomainCommandResult FarCommit = Component->ApplyCommittedHerbRecovery(HelperController, RequestId);
			TestFalse(TEXT("远距离施药不会提交身体恢复"), FarCommit.bCommitted);
			TestEqual(TEXT("远距离施药返回 PolicyUndecided"), FarCommit.Error, ECatDomainCommandError::PolicyUndecided);
			TestTrue(TEXT("远距离施药后目标仍倒地"), Component->GetSnapshot().bDowned);

			if (HelperCharacter)
			{
				HelperCharacter->SetActorLocation(FVector(100.0, 0.0, 0.0));
			}
			const FCatDomainCommandResult FarReplay = Component->ApplyCommittedHerbRecovery(HelperController, RequestId);
			TestFalse(TEXT("远距离失败的施药请求重放不提交"), FarReplay.bCommitted);
			TestEqual(TEXT("远距离失败的施药请求重放返回 AlreadyResolved"), FarReplay.Error,
				ECatDomainCommandError::AlreadyResolved);
			TestTrue(TEXT("失败请求重放后目标仍倒地"), Component->GetSnapshot().bDowned);

			TestEqual(TEXT("范围内施药 preflight 通过"), Component->ValidateHerbRecovery(HelperController),
				ECatDomainCommandError::None);
			const FGuid CloseRequestId = FGuid::NewGuid();
			const FCatDomainCommandResult CloseCommit = Component->ApplyCommittedHerbRecovery(HelperController, CloseRequestId);
			TestTrue(TEXT("范围内施药提交恢复"), CloseCommit.bCommitted);
			TestEqual(TEXT("范围内施药无错误"), CloseCommit.Error, ECatDomainCommandError::None);
			TestFalse(TEXT("范围内施药后目标解除倒地"), Component->GetSnapshot().bDowned);

			const FCatDomainCommandResult Replay = Component->ApplyCommittedHerbRecovery(HelperController, CloseRequestId);
			TestFalse(TEXT("草药恢复重放不再次提交"), Replay.bCommitted);
			TestEqual(TEXT("草药恢复重放返回 AlreadyResolved"), Replay.Error, ECatDomainCommandError::AlreadyResolved);
			TestEqual(TEXT("草药恢复重放保留首次 Revision"), Replay.Revision, CloseCommit.Revision);
		}
		WorldWrapper.ForwardErrorMessages(this);
	}
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
