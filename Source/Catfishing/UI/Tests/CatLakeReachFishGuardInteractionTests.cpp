#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"

#include "AbilitySystem/Config/CatAbilitySettings.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/BodyAction/CatBodyActionAbility.h"
#include "Character/CatCharacter.h"
#include "Condition/CatConditionSettings.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "Growth/CatGrowthSettings.h"
#include "Items/CatItemsService.h"
#include "Items/CatItemsSettings.h"
#include "OnlineSubsystemTypes.h"
#include "TimerManager.h"
#include "UI/CatLakeReachModel.h"
#include "UI/CatLakeReachPageController.h"
#include "UI/CatLakeReachWidget.h"

namespace CatLakeReachFishGuardInteractionTest
{
	/** 测试玩家稳定身份；GameMode、Items 和 PlayerState 都使用同一个文本，避免权限拒绝掩盖 UI 链路问题。 */
	const FString StableNetIdValue(TEXT("UIReachPlayerA"));

	/** 测试鱼定义 ID；目录、捕获命令和吃鱼预检共享它，证明后端按正式鱼表解析效果。 */
	const FName FishDefinitionId(TEXT("UIReachEdibleFish"));

	/** 自动化测试统计 ASC 上的 BodyAction 网关数量；用于确认 UI 点击前正式服务器动作入口已授予。 */
	static int32 CountBodyActionAbilitySpecs(const UCatAbilitySystemComponent* AbilitySystem)
	{
		int32 Count = 0;
		if (!AbilitySystem)
		{
			return Count;
		}
		for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
		{
			if (Spec.Ability && Spec.Ability->IsA(UCatGA_BodyActionCommand::StaticClass()))
			{
				++Count;
			}
		}
		return Count;
	}

	/** 自动化测试只读扫描活跃 BodyAction；用于区分 UI 动作是卡在等待窗口，还是没有进入 Ability 生命周期。 */
	static bool HasActiveBodyActionAbility(const UCatAbilitySystemComponent* AbilitySystem)
	{
		if (!AbilitySystem)
		{
			return false;
		}
		for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
		{
			if (Spec.IsActive() && Spec.Ability && Spec.Ability->IsA(UCatGA_BodyActionCommand::StaticClass()))
			{
				return true;
			}
		}
		return false;
	}

	/** 构造一条可捕获、可吃、可转缸的安全鱼定义；测试只验证吃鱼，但保留转缸资格以覆盖 UI 动作同源 DTO。 */
	static UCatFishDefinition* MakeReadyFishDefinition()
	{
		UCatFishDefinition* Definition = NewObject<UCatFishDefinition>(GetTransientPackage());
		if (!Definition)
		{
			return nullptr;
		}
		Definition->bEnableRuntimeDefinition = true;
		Definition->FishDefinitionId = FishDefinitionId;
		Definition->BodyClass = ECatFishBodyClass::Standard;
		Definition->SacrificeContribution = 2;
		Definition->RarityTierId = TEXT("Common");
		Definition->RegionIds = {TEXT("LakeA")};
		Definition->TimeOfDay = {ECatEnvironmentTimeOfDay::Day};
		Definition->Weather = {ECatEnvironmentWeather::Clear};
		Definition->SpawnWeight = 1.0;
		Definition->MinimumWeightKilograms = 1.0;
		Definition->MaximumWeightKilograms = 3.0;
		Definition->ScoopTargetRadiusCentimeters = 120.0;
		Definition->MinimumFightParticipants = 1;
		Definition->FishStrength = 1.0;
		Definition->FishFightStamina = 1.0;
		Definition->BitePersonalityId = TEXT("UIReachBite");
		Definition->FightPersonalityId = TEXT("UIReachFight");
		Definition->FoodSafety = ECatFishFoodSafety::Safe;
		Definition->EatingExperience = 1.0;
		Definition->PoisonIncrease = 0.0;
		Definition->bTankDisplayEligible = true;
		return Definition;
	}

	/** 测试期间的运行配置覆盖；它只改内存默认对象，使真实 Character、Items、Condition 和 Growth 能通过正式 gate。 */
	struct FScopedRuntimeSettings
	{
		/** Ability runtime 原值；析构恢复，避免后续测试意外获得 Character ASC。 */
		bool bSavedAbilityRuntime = false;

		/** 初始属性 gate 原值；析构恢复，避免后续测试自动播种身体数值。 */
		bool bSavedInitialAttributeTuning = false;

		/** Ability 复制策略原值；本测试只需要 Full 策略来稳定初始化 ASC。 */
		ECatAbilityReplicationPolicy SavedReplicationPolicy = ECatAbilityReplicationPolicy::Undecided;

		/** 初始 Poison 原值；本测试从健康状态吃安全鱼，不依赖毒性。 */
		float SavedInitialPoison = -1.0f;

		/** 初始 FishingStrength 原值；正值只用于通过 Character 属性播种。 */
		float SavedInitialFishingStrength = -1.0f;

		/** 初始 FightStamina 原值；正值只用于通过 Character 属性播种。 */
		float SavedInitialFightStamina = -1.0f;

		/** Condition runtime 原值；吃鱼预检要求倒地阈值已显式裁定。 */
		bool bSavedConditionRuntime = false;

		/** Poison 倒地阈值原值；测试设置为正数来启用身体裁决。 */
		double SavedPoisonDownedThreshold = 0.0;

		/** Growth runtime 原值；吃鱼预检要求成长系统可运行。 */
		bool bSavedGrowthRuntime = false;

		/** 成长槽长度原值；测试设置为正数来允许吃鱼推进经验。 */
		int32 SavedExperiencePerChoiceSlot = 0;

		/** 个人鱼护容量原值；测试需要至少一个格子容纳种入的鱼。 */
		int32 SavedPersonalGuardCapacity = 0;

		/** 共享鱼缸容量原值；测试鱼定义保持转缸资格，容量也保持有效配置。 */
		int32 SavedSharedFishTankCapacity = 0;

		/** 鱼表定义清单原值；测试只临时加入一条瞬态可运行定义。 */
		TArray<TSoftObjectPtr<UCatFishDefinition>> SavedFishDefinitions;

		/** 可写 Ability 设置默认对象；构造写入测试 gate，析构恢复。 */
		UCatAbilitySettings* AbilitySettings = nullptr;

		/** 可写 BodyAction Ability 默认对象；构造期安装进程覆盖，析构清除覆盖并恢复正式动作级表现设置读取。 */
		UCatGA_BodyActionCommand* BodyActionAbility = nullptr;

		/** 可写 Condition 设置默认对象；构造写入测试 gate，析构恢复。 */
		UCatConditionSettings* ConditionSettings = nullptr;

		/** 可写 Growth 设置默认对象；构造写入测试 gate，析构恢复。 */
		UCatGrowthSettings* GrowthSettings = nullptr;

		/** 可写 Items 设置默认对象；构造写入测试容量，析构恢复。 */
		UCatItemsSettings* ItemsSettings = nullptr;

		/** 可写鱼表设置默认对象；构造挂载瞬态定义，析构恢复清单。 */
		UCatFishCatalogSettings* FishCatalogSettings = nullptr;

		/** 测试期间持有的瞬态鱼定义；防止默认对象软引用在自动化过程中丢失目标。 */
		TStrongObjectPtr<UCatFishDefinition> FishDefinition;

		/** 构造流程：保存所有相关 CDO 字段，再只打开本测试吃鱼链路需要的 gate、容量和鱼定义。 */
		FScopedRuntimeSettings()
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
			BodyActionAbility = GetMutableDefault<UCatGA_BodyActionCommand>();
			if (BodyActionAbility)
			{
				BodyActionAbility->SetBodyActionCommitWindowSecondsForAutomation(0.0f);
			}
			ConditionSettings = GetMutableDefault<UCatConditionSettings>();
			if (ConditionSettings)
			{
				bSavedConditionRuntime = ConditionSettings->bEnableConditionRuntime;
				SavedPoisonDownedThreshold = ConditionSettings->PoisonDownedThreshold;
				ConditionSettings->bEnableConditionRuntime = true;
				ConditionSettings->PoisonDownedThreshold = 10.0;
			}
			GrowthSettings = GetMutableDefault<UCatGrowthSettings>();
			if (GrowthSettings)
			{
				bSavedGrowthRuntime = GrowthSettings->bEnableGrowthRuntime;
				SavedExperiencePerChoiceSlot = GrowthSettings->ExperiencePerChoiceSlot;
				GrowthSettings->bEnableGrowthRuntime = true;
				GrowthSettings->ExperiencePerChoiceSlot = 10;
			}
			ItemsSettings = GetMutableDefault<UCatItemsSettings>();
			if (ItemsSettings)
			{
				SavedPersonalGuardCapacity = ItemsSettings->PersonalGuardCapacity;
				SavedSharedFishTankCapacity = ItemsSettings->SharedFishTankCapacity;
				ItemsSettings->PersonalGuardCapacity = 3;
				ItemsSettings->SharedFishTankCapacity = 5;
			}
			FishCatalogSettings = GetMutableDefault<UCatFishCatalogSettings>();
			if (FishCatalogSettings)
			{
				SavedFishDefinitions = FishCatalogSettings->Definitions;
				FishDefinition.Reset(MakeReadyFishDefinition());
				FishCatalogSettings->Definitions = {FishDefinition.Get()};
			}
		}

		/** 析构流程：逐项恢复测试前 CDO 值，不调用 SaveConfig，也不改变项目默认资产配置。 */
		~FScopedRuntimeSettings()
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
			if (BodyActionAbility)
			{
				BodyActionAbility->ClearBodyActionCommitWindowOverrideForAutomation();
			}
			if (ConditionSettings)
			{
				ConditionSettings->bEnableConditionRuntime = bSavedConditionRuntime;
				ConditionSettings->PoisonDownedThreshold = SavedPoisonDownedThreshold;
			}
			if (GrowthSettings)
			{
				GrowthSettings->bEnableGrowthRuntime = bSavedGrowthRuntime;
				GrowthSettings->ExperiencePerChoiceSlot = SavedExperiencePerChoiceSlot;
			}
			if (ItemsSettings)
			{
				ItemsSettings->PersonalGuardCapacity = SavedPersonalGuardCapacity;
				ItemsSettings->SharedFishTankCapacity = SavedSharedFishTankCapacity;
			}
			if (FishCatalogSettings)
			{
				FishCatalogSettings->Definitions = SavedFishDefinitions;
			}
		}
	};

	/** 捕获命令构造流程：从当前真实鱼护快照取 ExpectedRevision，随后让 Items 正式创建一条实物鱼。 */
	static FCatCaptureCommitCommand MakeCaptureCommand(const ACatCharacter* Character,
		const FCatContainerSnapshot& GuardSnapshot, const FGuid FishInstanceId)
	{
		FCatCaptureCommitCommand Command;
		Command.Context.RequestId = FGuid::NewGuid();
		Command.Context.ExpectedRevision = GuardSnapshot.Revision;
		Command.Context.StableNetId = StableNetIdValue;
		Command.FishingSessionId = FGuid::NewGuid();
		Command.FishInstanceId = FishInstanceId;
		Command.FishDefinitionId = FishDefinitionId;
		Command.TargetContainerId = Character ? Character->GetPersonalFishGuardId() : FGuid();
		Command.WeightKilograms = 2.0;
		Command.SacrificeContribution = 2;
		return Command;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatLakeReachFishGuardConsumeClickBackendTest,
	"Catfishing.Unit.UI.Reach.FishGuardConsumeClickReachesBackendAndUpdatesGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 测试流程：创建真实 Game World、项目 Controller/Character/Items 鱼护和 UIReach MVC；通过 Widget 点击吃鱼，等待 BodyAction 提交窗口后核对 Items 快照、Controller 结果和 Model 投影。
bool FCatLakeReachFishGuardConsumeClickBackendTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	CatLakeReachFishGuardInteractionTest::FScopedRuntimeSettings SettingsOverride;
	FTestWorldWrapper WorldWrapper;
	if (!TestTrue(TEXT("创建 UIReach 鱼护点击测试 World"), WorldWrapper.CreateTestWorld(EWorldType::Game)))
	{
		return false;
	}
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	if (!TestNotNull(TEXT("读取 UIReach 鱼护点击测试 World"), World))
	{
		return false;
	}
	World->GetWorldSettings()->DefaultGameMode = ACatfishingGameModeBase::StaticClass();
	if (!TestTrue(TEXT("启动 UIReach 鱼护点击测试 World"), WorldWrapper.BeginPlayInTestWorld()))
	{
		WorldWrapper.ForwardErrorMessages(this);
		return false;
	}

	UCatItemsService* ItemsService = World->GetSubsystem<UCatItemsService>();
	ACatfishingGameModeBase* GameMode = World->GetAuthGameMode<ACatfishingGameModeBase>();
	TStrongObjectPtr<ULocalPlayer> LocalPlayer(GEngine ? NewObject<ULocalPlayer>(GEngine) : nullptr);
	ACatfishingPlayerController* Controller = World->SpawnActor<ACatfishingPlayerController>();
	ACatfishingPlayerState* PlayerState = World->SpawnActor<ACatfishingPlayerState>();
	ACatCharacter* Character = World->SpawnActor<ACatCharacter>();
	if (!TestNotNull(TEXT("取得 UIReach 测试 ItemsService"), ItemsService)
		|| !TestNotNull(TEXT("取得 UIReach 测试 GameMode"), GameMode)
		|| !TestNotNull(TEXT("创建 UIReach 测试 LocalPlayer"), LocalPlayer.Get())
		|| !TestNotNull(TEXT("生成 UIReach 测试 Controller"), Controller)
		|| !TestNotNull(TEXT("生成 UIReach 测试 PlayerState"), PlayerState)
		|| !TestNotNull(TEXT("生成 UIReach 测试 Character"), Character))
	{
		return false;
	}

	Controller->SetPlayer(LocalPlayer.Get());
	const FUniqueNetIdRef UniqueId = FUniqueNetIdString::Create(
		CatLakeReachFishGuardInteractionTest::StableNetIdValue, FName(TEXT("CAT_TEST")));
	PlayerState->SetUniqueId(FUniqueNetIdRepl(UniqueId));
	Controller->PlayerState = PlayerState;
	Character->SetPlayerState(PlayerState);
	Controller->Possess(Character);
	UCatAbilitySystemComponent* AbilitySystem = Character->GetCatAbilitySystemComponent();
	if (!TestNotNull(TEXT("UIReach 点击测试 Character ASC 可用"), AbilitySystem))
	{
		return false;
	}
	AbilitySystem->InitAbilityActorInfo(Character, Character);
	if (CatLakeReachFishGuardInteractionTest::CountBodyActionAbilitySpecs(AbilitySystem) == 0)
	{
		AbilitySystem->GiveAbility(FGameplayAbilitySpec(UCatGA_BodyActionCommand::StaticClass(), 1));
	}
	TestTrue(TEXT("UIReach 点击测试 Controller 具备 authority"), Controller->HasAuthority());
	TestEqual(TEXT("UIReach 点击测试 ASC 只有一个 BodyAction 网关"),
		CatLakeReachFishGuardInteractionTest::CountBodyActionAbilitySpecs(AbilitySystem), 1);

	ACatfishingGameModeBase::FAdmissionRecord Record;
	Record.Phase = ACatfishingGameModeBase::EAdmissionPhase::Active;
	Record.Controller = Controller;
	GameMode->AdmissionRecords.Add(CatLakeReachFishGuardInteractionTest::StableNetIdValue, Record);
	GameMode->RunPublicState.Phase.RunId = FGuid::NewGuid();
	GameMode->RunPublicState.Phase.Phase = ECatRunPhase::DayActive;
	GameMode->RunPublicState.Phase.bFishingAllowed = true;
	GameMode->RunPublicState.Phase.bQuotaOpen = true;
	GameMode->RunPublicState.Revision = 1;
	GameMode->bRunCommandsOpen = true;

	FCatContainerSnapshot InitialGuard;
	if (!TestTrue(TEXT("占有后注册真实个人鱼护"),
		ItemsService->TryGetContainerSnapshot(Character->GetPersonalFishGuardId(), InitialGuard)))
	{
		return false;
	}
	const FGuid FishInstanceId = FGuid::NewGuid();
	const FCatCaptureCommitResult CaptureResult = ItemsService->CommitCapture(
		CatLakeReachFishGuardInteractionTest::MakeCaptureCommand(Character, InitialGuard, FishInstanceId));
	TestTrue(TEXT("通过 Items 正式捕获提交种入一条鱼"), CaptureResult.Command.bCommitted);
	FCatContainerSnapshot SeededGuard;
	ItemsService->TryGetContainerSnapshot(Character->GetPersonalFishGuardId(), SeededGuard);
	TestEqual(TEXT("点击前鱼护里有一条真实鱼"), SeededGuard.Fish.Num(), 1);

	TStrongObjectPtr<UCatLakeReachModel> Model(NewObject<UCatLakeReachModel>(GetTransientPackage()));
	TStrongObjectPtr<UCatLakeReachWidget> Widget(NewObject<UCatLakeReachWidget>(GetTransientPackage()));
	TStrongObjectPtr<UCatLakeReachPageController> PageController(NewObject<UCatLakeReachPageController>(GetTransientPackage()));
	if (!TestNotNull(TEXT("创建 UIReach Model"), Model.Get())
		|| !TestNotNull(TEXT("创建 UIReach Widget"), Widget.Get())
		|| !TestNotNull(TEXT("创建 UIReach PageController"), PageController.Get()))
	{
		return false;
	}
	TestTrue(TEXT("Model 绑定正式 LocalPlayer/Controller/Character"),
		Model->Bind(LocalPlayer.Get(), Controller, Character));
	TestTrue(TEXT("PageController 绑定 Model 与 View"),
		PageController->Bind(LocalPlayer.Get(), Controller, Model.Get(), Widget.Get()));
	Model->SetMenuOpen(true);
	const FCatUIReachViewState BeforeClickState = Model->GetViewState();
	TestTrue(TEXT("点击前 Model 选择鱼护中的真实鱼"), BeforeClickState.bHasSelectedFishGuardFish);
	TestEqual(TEXT("点击前 Model 选择的是种入鱼"), BeforeClickState.SelectedFishGuardFish.FishInstanceId, FishInstanceId);

	Widget->RequestConsumeSelectedFish();
	const FCatUIReachViewState SubmittedState = Model->GetViewState();
	const FCatFishConsumeResult ImmediateResult = Controller->GetLastFishConsumeResult();
	const bool bPendingAfterClick = SubmittedState.bFishGuardActionPending
		&& SubmittedState.PendingFishGuardAction == ECatUIReachFishGuardAction::ConsumeSelectedFish
		&& SubmittedState.PendingFishGuardRequestId.IsValid();
	const bool bCompletedDuringClick = SubmittedState.bHasFishGuardCommandResult
		&& SubmittedState.LastFishGuardAction == ECatUIReachFishGuardAction::ConsumeSelectedFish
		&& SubmittedState.LastFishGuardCommandResult.RequestId.IsValid();
	TestTrue(TEXT("点击后 Model 进入 pending 或收到吃鱼终态"), bPendingAfterClick || bCompletedDuringClick);
	const FGuid PendingRequestId = bPendingAfterClick
		? SubmittedState.PendingFishGuardRequestId : SubmittedState.LastFishGuardCommandResult.RequestId;
	TestTrue(TEXT("吃鱼请求 ID 有效"), PendingRequestId.IsValid());
	TestTrue(TEXT("点击后 BodyAction 已进入等待或即时返回结果"),
		CatLakeReachFishGuardInteractionTest::HasActiveBodyActionAbility(AbilitySystem)
		|| ImmediateResult.Command.RequestId == PendingRequestId);

	for (int32 TickIndex = 0; TickIndex < 8; ++TickIndex)
	{
		World->GetTimerManager().Tick(0.05f);
		World->Tick(LEVELTICK_All, 0.05f);
	}

	FCatContainerSnapshot FinalGuard;
	ItemsService->TryGetContainerSnapshot(Character->GetPersonalFishGuardId(), FinalGuard);
	const FCatFishConsumeResult ControllerResult = Controller->GetLastFishConsumeResult();
	const FCatUIReachViewState AfterClickState = Model->GetViewState();
	TestEqual(TEXT("Controller 吃鱼结果保持同一个 RequestId"), ControllerResult.Command.RequestId, PendingRequestId);
	TestTrue(TEXT("Controller 吃鱼结果证明 Items 已提交"), ControllerResult.Command.bCommitted);
	TestEqual(TEXT("Controller 吃鱼结果错误为 None"), ControllerResult.Command.Error, ECatDomainCommandError::None);
	TestEqual(TEXT("Controller 吃鱼结果携带被消费鱼"), ControllerResult.Fish.FishInstanceId, FishInstanceId);
	TestEqual(TEXT("后端鱼护已移除被吃掉的鱼"), FinalGuard.Fish.Num(), 0);
	TestFalse(TEXT("Model 收到吃鱼结果后清除 pending"), AfterClickState.bFishGuardActionPending);
	TestTrue(TEXT("Model 暴露鱼护动作结果"), AfterClickState.bHasFishGuardCommandResult);
	TestEqual(TEXT("Model 鱼护结果保持同一个 RequestId"),
		AfterClickState.LastFishGuardCommandResult.RequestId, PendingRequestId);
	TestTrue(TEXT("Model 鱼护结果证明后端已提交"), AfterClickState.LastFishGuardCommandResult.bCommitted);
	TestEqual(TEXT("Model 重读后鱼护为空"), AfterClickState.PersonalFishGuard.Fish.Num(), 0);

	// PageController 和 Model 在测试里绑定了玩家、角色和状态委托；结束前显式解绑，避免自动化 World 销毁后还有回调指向本轮夹具对象。
	PageController->Unbind();
	Model->Unbind();
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
