#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "AbilitySystem/Config/CatAbilitySet.h"
#include "AbilitySystem/Config/CatAbilityInputConfig.h"
#include "AbilitySystem/Config/CatAbilitySettings.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/BodyAction/CatBodyActionAbility.h"
#include "AbilitySystem/BodyAction/CatBodyActionPresentationSettings.h"
#include "AbilitySystem/Fishing/CatFishingInputAbilities.h"
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Character/CatCharacter.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Fishing/CatFishingSession.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "InputAction.h"
#include "Misc/ScopeExit.h"

namespace
{
	/** 自动化测试覆盖的正式 BodyAction 命令集合；配置、标签和生命周期测试共用它，避免漏测某个 RPC 动作。 */
	const ECatBodyActionAbilityCommand GBodyActionCommands[] = {
		ECatBodyActionAbilityCommand::RequestSacrifice,
		ECatBodyActionAbilityCommand::CampRest,
		ECatBodyActionAbilityCommand::CampfirePlayback,
		ECatBodyActionAbilityCommand::TransferObjectBetweenContainers,
		ECatBodyActionAbilityCommand::RescueCharacterToCamp,
		ECatBodyActionAbilityCommand::RepairRodAtCamp,
		ECatBodyActionAbilityCommand::UseHerbOnCharacter,
		ECatBodyActionAbilityCommand::ConsumeFish,
		ECatBodyActionAbilityCommand::BeginTheft,
		ECatBodyActionAbilityCommand::CatchTheft,
		ECatBodyActionAbilityCommand::RequestManualHelp,
		ECatBodyActionAbilityCommand::RequestMischief,
		ECatBodyActionAbilityCommand::PlaceProtectionSign,
		ECatBodyActionAbilityCommand::CompleteShakeDry
	};

	/** 自动化测试判断一个 AbilitySpec 是否是 BodyAction 网关；只比较 Ability 资产标签，避免依赖具体句柄来源。 */
	bool IsBodyActionAbilitySpec(const FGameplayAbilitySpec& Spec)
	{
		return Spec.Ability && Spec.Ability->GetAssetTags().HasTagExact(CatFishingAbilityTags::Ability_Body_Command);
	}

	/** 自动化测试统计 ASC 上的 BodyAction 网关数量；用于避免测试额外授予一份默认已经存在的网关。 */
	int32 CountBodyActionAbilitySpecs(const UCatAbilitySystemComponent* AbilitySystem)
	{
		int32 Count = 0;
		if (!AbilitySystem)
		{
			return Count;
		}
		for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
		{
			if (IsBodyActionAbilitySpec(Spec))
			{
				++Count;
			}
		}
		return Count;
	}

	/** 自动化测试扫描 ASC 当前是否有活跃 BodyAction Ability；只读 AbilitySpec，不触碰输入或取消状态。 */
	bool HasActiveBodyActionAbility(const UCatAbilitySystemComponent* AbilitySystem)
	{
		if (!AbilitySystem)
		{
			return false;
		}
		for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities())
		{
			if (Spec.IsActive() && IsBodyActionAbilitySpec(Spec))
			{
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatAbilitySetGrantRemoveTest,
	"Catfishing.Unit.AbilitySystem.AbilitySet.GrantsAndRemovesAsAGroup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatAbilityInputStateTest,
	"Catfishing.Unit.AbilitySystem.Input.PressReleaseHeldAndFrameClearAreIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatAbilityPossessResetTest,
	"Catfishing.Integration.AbilitySystem.Input.RepeatedPossessAndUnPossessDoNotLeakState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingPrimaryEdgeIdentityTest,
	"Catfishing.Integration.Fishing.AbilityInput.PrimaryEdgesUseIndependentRequestsAndSharedCorrelation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingStaminaGameplayEffectBoundaryTest,
	"Catfishing.Unit.AbilitySystem.FishingStamina.SetByCallerResetAndPendingRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatAbilityInputConfigReadinessTest,
	"Catfishing.Unit.AbilitySystem.InputConfig.RequiresSixUniqueFishingMappings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingPredictedInstanceLifecyclePolicyTest,
	"Catfishing.Unit.AbilitySystem.FishingAbility.RemoteAuthorityMirrorWaitsForClient",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatAbilityInputPauseAndTapTest,
	"Catfishing.Unit.AbilitySystem.Input.PausePreservesEdgesAndSameFrameTapActivates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingPreHookTerminationDoesNotResetStaminaTest,
	"Catfishing.Integration.AbilitySystem.FishingStamina.PreHookTerminationDoesNotResetUntouchedPool",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatPendingStaminaBlocksAbilityActivationTest,
	"Catfishing.Unit.AbilitySystem.FishingStamina.PendingRecoveryBlocksInputActivation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatBodyActionPayloadEventTagTest,
	"Catfishing.Unit.AbilitySystem.BodyAction.PayloadMapsEveryCommandToGameplayEvent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatBodyActionPresentationSettingsContractTest,
	"Catfishing.Unit.AbilitySystem.BodyAction.PresentationSettingsDriveCancellableLeadIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatBodyActionCancelWindowContractTest,
	"Catfishing.Unit.AbilitySystem.BodyAction.CancelWindowUsesAbilityLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatAbilitySetGrantRemoveTest::RunTest(const FString& Parameters)
{
	// AbilitySet 授予测试构造正式 Fishing 六输入加 BodyAction 事件网关：Primary/Slack/Chum 必须是按住型，
	// 其余 Fishing 入口是离散触发型；BodyAction 无输入 Tag。这个测试只证明授予契约，不替代默认资产和双客户端生命周期验收。
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("creates ability set world"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	AActor* Owner = World ? World->SpawnActor<AActor>() : nullptr;
	TestNotNull(TEXT("ability set owner is available"), Owner);
	if (!Owner)
	{
		return false;
	}
	UCatAbilitySystemComponent* AbilitySystem = NewObject<UCatAbilitySystemComponent>(Owner);
	Owner->AddInstanceComponent(AbilitySystem);
	AbilitySystem->RegisterComponent();
	AbilitySystem->InitAbilityActorInfo(Owner, Owner);

	UCatAbilitySet* AbilitySet = NewObject<UCatAbilitySet>(GetTransientPackage());
	FCatAbilitySetAbility Entry;
	Entry.Ability = UCatGA_FishingPrimaryAction::StaticClass();
	Entry.InputTag = CatFishingAbilityTags::Input_Fishing_Primary;
	Entry.Level = 2;
	Entry.ActivationPolicy = ECatAbilityActivationPolicy::WhileInputActive;
	AbilitySet->GrantedAbilities.Add(Entry);
	const auto AddInputAbility = [AbilitySet](const TSubclassOf<UGameplayAbility> Ability, const FGameplayTag InputTag,
		const ECatAbilityActivationPolicy Policy = ECatAbilityActivationPolicy::OnInputTriggered)
	{
		FCatAbilitySetAbility AdditionalEntry;
		AdditionalEntry.Ability = Ability;
		AdditionalEntry.InputTag = InputTag;
		AdditionalEntry.Level = 1;
		AdditionalEntry.ActivationPolicy = Policy;
		AbilitySet->GrantedAbilities.Add(AdditionalEntry);
	};
	AddInputAbility(UCatGA_FishingRodInteract::StaticClass(), CatFishingAbilityTags::Input_Fishing_RodInteract);
	AddInputAbility(UCatGA_FishingSlack::StaticClass(), CatFishingAbilityTags::Input_Fishing_Slack,
		ECatAbilityActivationPolicy::WhileInputActive);
	AddInputAbility(UCatGA_FishingCancel::StaticClass(), CatFishingAbilityTags::Input_Fishing_Cancel);
	AddInputAbility(UCatGA_FishingScoop::StaticClass(), CatFishingAbilityTags::Input_Fishing_Scoop);
	AddInputAbility(UCatGA_FishingChum::StaticClass(), CatFishingAbilityTags::Input_Fishing_Chum,
		ECatAbilityActivationPolicy::WhileInputActive);
	AddInputAbility(UCatGA_BodyActionCommand::StaticClass(), FGameplayTag());
	const FCatAbilitySetAbility* SlackEntry = AbilitySet->GrantedAbilities.FindByPredicate([](const FCatAbilitySetAbility& Candidate)
	{
		return Candidate.InputTag == CatFishingAbilityTags::Input_Fishing_Slack;
	});
	TestNotNull(TEXT("Slack ability entry is present in formal Fishing set"), SlackEntry);
	if (SlackEntry)
	{
		TestEqual(TEXT("Slack ability remains a held input ability"), SlackEntry->ActivationPolicy,
			ECatAbilityActivationPolicy::WhileInputActive);
	}

	FCatGrantedAbilitySetHandles Handles;
	TestTrue(TEXT("authority ASC accepts a complete ability set"), AbilitySet->GiveToAbilitySystem(AbilitySystem, Handles));
	TestEqual(TEXT("seven ability handles are recorded"), Handles.GetAbilitySpecHandles().Num(), 7);
	TestEqual(TEXT("six Fishing inputs plus BodyAction spec are present"), AbilitySystem->GetActivatableAbilities().Num(), 7);
	TestFalse(TEXT("inactive BodyAction gateway is not reported as canceled"),
		AbilitySystem->CancelBodyActionAbilitiesFromAuthority());
	if (Handles.GetAbilitySpecHandles().IsEmpty())
	{
		return false;
	}
	const FGameplayAbilitySpec* GrantedSpec = AbilitySystem->FindAbilitySpecFromHandle(Handles.GetAbilitySpecHandles()[0]);
	TestNotNull(TEXT("granted spec is addressable by recorded handle"), GrantedSpec);
	if (GrantedSpec)
	{
		TestEqual(TEXT("configured level is preserved"), GrantedSpec->Level, 2);
		TestTrue(TEXT("dynamic input tag is attached"), GrantedSpec->GetDynamicSpecSourceTags().HasTagExact(Entry.InputTag));
	}
	AbilitySystem->AbilityInputTagPressed(Entry.InputTag);
	TestEqual(TEXT("OnGiveAbility builds the input tag route from dynamic spec tags"),
		AbilitySystem->GetHeldInputCount(), 1);
	AbilitySystem->ResetAbilityInput();

	Handles.TakeFromAbilitySystem(AbilitySystem);
	TestEqual(TEXT("group removal clears the ability"), AbilitySystem->GetActivatableAbilities().Num(), 0);
	TestEqual(TEXT("group removal invalidates recorded handles"), Handles.GetAbilitySpecHandles().Num(), 0);
	AbilitySet->GrantedAbilities.RemoveAt(AbilitySet->GrantedAbilities.Num() - 1);
	FCatGrantedAbilitySetHandles RejectedHandles;
	TestFalse(TEXT("AbilitySet without BodyAction gateway fails readiness"), AbilitySet->IsRuntimeReady());
	TestFalse(TEXT("partial AbilitySet grants nothing"), AbilitySet->GiveToAbilitySystem(AbilitySystem, RejectedHandles));
	TestEqual(TEXT("rejected set leaves ASC unchanged"), AbilitySystem->GetActivatableAbilities().Num(), 0);

	FCatAbilitySetAbility InvalidBodyActionEntry;
	InvalidBodyActionEntry.Ability = UCatGA_BodyActionCommand::StaticClass();
	InvalidBodyActionEntry.InputTag = CatFishingAbilityTags::Input_Fishing_Cancel;
	InvalidBodyActionEntry.Level = 1;
	InvalidBodyActionEntry.ActivationPolicy = ECatAbilityActivationPolicy::OnInputTriggered;
	AbilitySet->GrantedAbilities.Add(InvalidBodyActionEntry);
	TestFalse(TEXT("BodyAction gateway must not bind a direct input tag"), AbilitySet->IsRuntimeReady());
	return !HasAnyErrors();
}

bool FCatAbilityInputStateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("creates input state world"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	AActor* Owner = World ? World->SpawnActor<AActor>() : nullptr;
	TestNotNull(TEXT("input state owner is available"), Owner);
	if (!Owner)
	{
		return false;
	}
	UCatAbilitySystemComponent* AbilitySystem = NewObject<UCatAbilitySystemComponent>(Owner);
	Owner->AddInstanceComponent(AbilitySystem);
	AbilitySystem->RegisterComponent();
	AbilitySystem->InitAbilityActorInfo(Owner, Owner);
	const FGameplayAbilitySpecHandle Handle = AbilitySystem->GiveAbility(
		FGameplayAbilitySpec(UCatGA_FishingPrimaryAction::StaticClass(), 1));
	AbilitySystem->RegisterAbilityInput(Handle, CatFishingAbilityTags::Input_Fishing_Primary,
		ECatAbilityActivationPolicy::WhileInputActive);

	AbilitySystem->AbilityInputTagPressed(CatFishingAbilityTags::Input_Fishing_Primary);
	TestEqual(TEXT("press edge is recorded once"), AbilitySystem->GetPressedInputCount(), 1);
	TestEqual(TEXT("press also establishes held state"), AbilitySystem->GetHeldInputCount(), 1);
	TestEqual(TEXT("press does not fabricate release"), AbilitySystem->GetReleasedInputCount(), 0);

	AbilitySystem->ProcessAbilityInput(0.016f, false);
	TestEqual(TEXT("processed press clears at frame boundary"), AbilitySystem->GetPressedInputCount(), 0);
	TestEqual(TEXT("held survives frame boundary"), AbilitySystem->GetHeldInputCount(), 1);

	AbilitySystem->AbilityInputTagReleased(CatFishingAbilityTags::Input_Fishing_Primary);
	TestEqual(TEXT("release edge is independently recorded"), AbilitySystem->GetReleasedInputCount(), 1);
	TestEqual(TEXT("release removes held state immediately"), AbilitySystem->GetHeldInputCount(), 0);
	AbilitySystem->ProcessAbilityInput(0.016f, false);
	TestEqual(TEXT("processed release clears at frame boundary"), AbilitySystem->GetReleasedInputCount(), 0);
	return !HasAnyErrors();
}

bool FCatAbilityPossessResetTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("creates game world"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	ACatCharacter* Character = World ? World->SpawnActor<ACatCharacter>() : nullptr;
	ACatfishingPlayerController* Controller = World ? World->SpawnActor<ACatfishingPlayerController>() : nullptr;
	TestNotNull(TEXT("character is available"), Character);
	TestNotNull(TEXT("controller is available"), Controller);
	if (!Character || !Controller)
	{
		return false;
	}

	UCatAbilitySystemComponent* AbilitySystem = Character->GetCatAbilitySystemComponent();
	TestNotNull(TEXT("character owns the project ASC type"), AbilitySystem);
	if (!AbilitySystem)
	{
		return false;
	}
	AbilitySystem->InitAbilityActorInfo(Character, Character);
	const FGameplayAbilitySpecHandle Handle = AbilitySystem->GiveAbility(
		FGameplayAbilitySpec(UCatGA_FishingPrimaryAction::StaticClass(), 1));
	AbilitySystem->RegisterAbilityInput(Handle, CatFishingAbilityTags::Input_Fishing_Primary,
		ECatAbilityActivationPolicy::WhileInputActive);

	Controller->Possess(Character);
	Controller->Possess(Character);
	AbilitySystem->AbilityInputTagPressed(CatFishingAbilityTags::Input_Fishing_Primary);
	TestEqual(TEXT("repeated possess still records one held handle"), AbilitySystem->GetHeldInputCount(), 1);
	Controller->UnPossess();
	TestEqual(TEXT("unpossess clears pressed input"), AbilitySystem->GetPressedInputCount(), 0);
	TestEqual(TEXT("unpossess clears released input"), AbilitySystem->GetReleasedInputCount(), 0);
	TestEqual(TEXT("unpossess clears held input"), AbilitySystem->GetHeldInputCount(), 0);

	Controller->Possess(Character);
	Controller->UnPossess();
	TestEqual(TEXT("second lifecycle remains clean"), AbilitySystem->GetHeldInputCount(), 0);
	return !HasAnyErrors();
}

bool FCatFishingPrimaryEdgeIdentityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("creates command route world"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	ACatfishingPlayerController* Controller = World ? World->SpawnActor<ACatfishingPlayerController>() : nullptr;
	UCatFishingCommandComponent* Commands = Controller ? Controller->GetFishingCommandComponent() : nullptr;
	TestNotNull(TEXT("controller owns the fishing command route"), Commands);
	if (!Commands)
	{
		return false;
	}

	const FCatFishingInputEdge Press = Commands->SubmitPrimaryPressed();
	const FCatFishingInputEdge Release = Commands->SubmitPrimaryReleased();
	TestTrue(TEXT("press request is valid"), Press.RequestId.IsValid());
	TestTrue(TEXT("release request is valid"), Release.RequestId.IsValid());
	TestNotEqual(TEXT("press and release use independent request ids"), Press.RequestId, Release.RequestId);
	TestTrue(TEXT("one hold cycle has a valid correlation"), Press.ActivationCorrelationId.IsValid());
	TestEqual(TEXT("release shares the press correlation"), Release.ActivationCorrelationId, Press.ActivationCorrelationId);
	TestTrue(TEXT("release sequence advances"), Release.InputSequence > Press.InputSequence);

	const FCatFishingInputEdge NextPress = Commands->SubmitPrimaryPressed();
	TestNotEqual(TEXT("next hold cycle gets a new correlation"), NextPress.ActivationCorrelationId, Press.ActivationCorrelationId);
	Commands->ResetTransientCommandState();
	const FCatFishingInputEdge OrphanRelease = Commands->SubmitPrimaryReleased();
	TestFalse(TEXT("unpossess-style reset prevents an orphan release correlation"), OrphanRelease.ActivationCorrelationId.IsValid());
	return !HasAnyErrors();
}

bool FCatFishingStaminaGameplayEffectBoundaryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("creates stamina GE world"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	AActor* Owner = World ? World->SpawnActor<AActor>() : nullptr;
	TestNotNull(TEXT("stamina GE owner is available"), Owner);
	if (!Owner)
	{
		return false;
	}
	UCatAbilitySystemComponent* AbilitySystem = NewObject<UCatAbilitySystemComponent>(Owner);
	UCatSurvivalAttributeSet* Attributes = NewObject<UCatSurvivalAttributeSet>(Owner);
	Owner->AddInstanceComponent(AbilitySystem);
	AbilitySystem->RegisterComponent();
	AbilitySystem->AddAttributeSetSubobject(Attributes);
	AbilitySystem->InitAbilityActorInfo(Owner, Owner);

	UCatAbilitySettings* Settings = GetMutableDefault<UCatAbilitySettings>();
	const bool OldRuntime = Settings->bEnableCharacterAbilityRuntime;
	const ECatAbilityReplicationPolicy OldPolicy = Settings->ReplicationPolicy;
	const bool OldTuning = Settings->bEnableInitialAttributeTuning;
	const float OldPoison = Settings->InitialPoison;
	const float OldStrength = Settings->InitialFishingStrength;
	const float OldStamina = Settings->InitialFightStamina;
	Settings->bEnableCharacterAbilityRuntime = true;
	Settings->ReplicationPolicy = ECatAbilityReplicationPolicy::Full;
	Settings->bEnableInitialAttributeTuning = true;
	Settings->InitialPoison = 0.0f;
	Settings->InitialFishingStrength = 1.0f;
	Settings->InitialFightStamina = 10.0f;

	AbilitySystem->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 4.0f);
	TestTrue(TEXT("session initialization applies the stamina GE"), AbilitySystem->InitializeFishingStaminaForSession());
	TestEqual(TEXT("initialization reaches configured baseline instead of adding the baseline"),
		AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), 10.0f);
	TestTrue(TEXT("fight drain applies through SetByCaller GE"), AbilitySystem->ApplyFishingStaminaDelta(-3.0f));
	TestEqual(TEXT("fight drain changes stamina"),
		AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), 7.0f);
	TestTrue(TEXT("terminal reset applies synchronously with ActorInfo"), AbilitySystem->RequestFishingStaminaReset());
	TestEqual(TEXT("terminal reset restores baseline"),
		AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), 10.0f);

	AbilitySystem->ClearActorInfo();
	AbilitySystem->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 2.0f);
	TestTrue(TEXT("missing ActorInfo records a reliable pending reset"), AbilitySystem->RequestFishingStaminaReset());
	TestTrue(TEXT("pending reset is observable"), AbilitySystem->HasPendingFishingStaminaReset());
	AbilitySystem->InitAbilityActorInfo(Owner, Owner);
	TestFalse(TEXT("ActorInfo restoration consumes pending reset"), AbilitySystem->HasPendingFishingStaminaReset());
	TestEqual(TEXT("pending compensation restores baseline before next session"),
		AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), 10.0f);

	Settings->bEnableCharacterAbilityRuntime = OldRuntime;
	Settings->ReplicationPolicy = OldPolicy;
	Settings->bEnableInitialAttributeTuning = OldTuning;
	Settings->InitialPoison = OldPoison;
	Settings->InitialFishingStrength = OldStrength;
	Settings->InitialFightStamina = OldStamina;
	return !HasAnyErrors();
}

bool FCatAbilityInputConfigReadinessTest::RunTest(const FString& Parameters)
{
	// Readiness 测试按正式玩家入口逐项补齐六个 Tag：每次追加前五项都必须保持关闭，
	// 六项齐全后才开放；随后复用 Action 验证唯一映射约束仍会重新关闭入口。
	(void)Parameters;
	UCatAbilityInputConfig* Config = NewObject<UCatAbilityInputConfig>(GetTransientPackage());
	const FGameplayTag RequiredTags[] = {
		CatFishingAbilityTags::Input_Fishing_RodInteract,
		CatFishingAbilityTags::Input_Fishing_Primary,
		CatFishingAbilityTags::Input_Fishing_Slack,
		CatFishingAbilityTags::Input_Fishing_Cancel,
		CatFishingAbilityTags::Input_Fishing_Scoop,
		CatFishingAbilityTags::Input_Fishing_Chum
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(RequiredTags); ++Index)
	{
		FCatAbilityInputAction Entry;
		Entry.InputAction = NewObject<UInputAction>(Config);
		Entry.InputTag = RequiredTags[Index];
		Config->AbilityInputActions.Add(Entry);
		if (Index + 1 < UE_ARRAY_COUNT(RequiredTags))
		{
			TestFalse(TEXT("partial Fishing mapping remains fail-closed"), Config->IsRuntimeReady());
		}
	}
	TestTrue(TEXT("six unique required mappings are ready"), Config->IsRuntimeReady());
	Config->AbilityInputActions.Last().InputAction = Config->AbilityInputActions[0].InputAction;
	TestFalse(TEXT("one InputAction cannot drive two formal Fishing tags"), Config->IsRuntimeReady());
	return !HasAnyErrors();
}

bool FCatFishingPredictedInstanceLifecyclePolicyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestTrue(TEXT("remote authority mirror waits for the owning client prediction"),
		UCatFishingGameplayAbility::ShouldWaitForRemoteClient(true, false));
	TestFalse(TEXT("listen-server local authority submits through its local instance"),
		UCatFishingGameplayAbility::ShouldWaitForRemoteClient(true, true));
	TestFalse(TEXT("owning client submits instead of waiting"),
		UCatFishingGameplayAbility::ShouldWaitForRemoteClient(false, true));
	return !HasAnyErrors();
}

bool FCatAbilityInputPauseAndTapTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("creates tap input world"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	AActor* Owner = World ? World->SpawnActor<AActor>() : nullptr;
	if (!Owner)
	{
		return false;
	}
	UCatAbilitySystemComponent* AbilitySystem = NewObject<UCatAbilitySystemComponent>(Owner);
	Owner->AddInstanceComponent(AbilitySystem);
	AbilitySystem->RegisterComponent();
	AbilitySystem->InitAbilityActorInfo(Owner, Owner);
	const FGameplayAbilitySpecHandle Handle = AbilitySystem->GiveAbility(
		FGameplayAbilitySpec(UCatGA_FishingPrimaryAction::StaticClass(), 1));
	AbilitySystem->RegisterAbilityInput(Handle, CatFishingAbilityTags::Input_Fishing_Primary,
		ECatAbilityActivationPolicy::WhileInputActive);
	int32 ActivationCount = 0;
	AbilitySystem->AbilityActivatedCallbacks.AddLambda([&ActivationCount](UGameplayAbility*)
	{
		++ActivationCount;
	});

	AbilitySystem->AbilityInputTagPressed(CatFishingAbilityTags::Input_Fishing_Primary);
	AbilitySystem->AbilityInputTagReleased(CatFishingAbilityTags::Input_Fishing_Primary);
	AbilitySystem->ProcessAbilityInput(0.016f, true);
	TestEqual(TEXT("pause preserves press edge"), AbilitySystem->GetPressedInputCount(), 1);
	TestEqual(TEXT("pause preserves release edge"), AbilitySystem->GetReleasedInputCount(), 1);
	AbilitySystem->ProcessAbilityInput(0.016f, false);
	TestEqual(TEXT("same-frame press/release still attempts one activation"), ActivationCount, 1);
	TestEqual(TEXT("unpaused frame clears press"), AbilitySystem->GetPressedInputCount(), 0);
	TestEqual(TEXT("unpaused frame clears release"), AbilitySystem->GetReleasedInputCount(), 0);
	return !HasAnyErrors();
}

bool FCatFishingPreHookTerminationDoesNotResetStaminaTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("creates pre-hook termination world"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	ACatCharacter* Character = World ? World->SpawnActor<ACatCharacter>() : nullptr;
	ACatFishingSession* Session = World ? World->SpawnActor<ACatFishingSession>() : nullptr;
	if (!Character || !Session)
	{
		return false;
	}
	UCatAbilitySystemComponent* AbilitySystem = Character->GetCatAbilitySystemComponent();
	UCatSurvivalAttributeSet* Attributes = NewObject<UCatSurvivalAttributeSet>(Character);
	AbilitySystem->AddAttributeSetSubobject(Attributes);
	AbilitySystem->InitAbilityActorInfo(Character, Character);
	AbilitySystem->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFightStaminaAttribute(), 4.0f);
	Session->TerminateSession(ECatFishingOutcome::Cancelled, TEXT("Pre-hook test"));
	TestEqual(TEXT("session that never initialized stamina leaves the pool untouched"),
		AbilitySystem->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()), 4.0f);
	return !HasAnyErrors();
}

bool FCatPendingStaminaBlocksAbilityActivationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("creates pending recovery world"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	AActor* Owner = World ? World->SpawnActor<AActor>() : nullptr;
	if (!Owner)
	{
		return false;
	}
	UCatAbilitySystemComponent* AbilitySystem = NewObject<UCatAbilitySystemComponent>(Owner);
	UCatSurvivalAttributeSet* Attributes = NewObject<UCatSurvivalAttributeSet>(Owner);
	Owner->AddInstanceComponent(AbilitySystem);
	AbilitySystem->RegisterComponent();
	AbilitySystem->AddAttributeSetSubobject(Attributes);
	AbilitySystem->InitAbilityActorInfo(Owner, Owner);
	const FGameplayAbilitySpecHandle Handle = AbilitySystem->GiveAbility(
		FGameplayAbilitySpec(UCatGA_FishingPrimaryAction::StaticClass(), 1));
	AbilitySystem->RegisterAbilityInput(Handle, CatFishingAbilityTags::Input_Fishing_Primary,
		ECatAbilityActivationPolicy::WhileInputActive);
	int32 ActivationCount = 0;
	AbilitySystem->AbilityActivatedCallbacks.AddLambda([&ActivationCount](UGameplayAbility*) { ++ActivationCount; });

	UCatAbilitySettings* Settings = GetMutableDefault<UCatAbilitySettings>();
	const bool OldRuntime = Settings->bEnableCharacterAbilityRuntime;
	const ECatAbilityReplicationPolicy OldPolicy = Settings->ReplicationPolicy;
	const bool OldTuning = Settings->bEnableInitialAttributeTuning;
	const float OldPoison = Settings->InitialPoison;
	const float OldStrength = Settings->InitialFishingStrength;
	const float OldStamina = Settings->InitialFightStamina;
	Settings->bEnableCharacterAbilityRuntime = true;
	Settings->ReplicationPolicy = ECatAbilityReplicationPolicy::Full;
	Settings->bEnableInitialAttributeTuning = true;
	Settings->InitialPoison = 0.0f;
	Settings->InitialFishingStrength = 1.0f;
	Settings->InitialFightStamina = -1.0f;
	TestFalse(TEXT("invalid reset configuration keeps recovery pending"), AbilitySystem->RequestFishingStaminaReset());
	AbilitySystem->AbilityInputTagPressed(CatFishingAbilityTags::Input_Fishing_Primary);
	AbilitySystem->ProcessAbilityInput(0.016f, false);
	TestEqual(TEXT("pending recovery blocks default ability activation"), ActivationCount, 0);

	Settings->InitialFightStamina = 10.0f;
	AbilitySystem->AbilityInputTagPressed(CatFishingAbilityTags::Input_Fishing_Primary);
	AbilitySystem->ProcessAbilityInput(0.016f, false);
	TestFalse(TEXT("valid recovery clears pending before activation"), AbilitySystem->HasPendingFishingStaminaReset());
	TestEqual(TEXT("ability activation resumes after compensation"), ActivationCount, 1);

	Settings->bEnableCharacterAbilityRuntime = OldRuntime;
	Settings->ReplicationPolicy = OldPolicy;
	Settings->bEnableInitialAttributeTuning = OldTuning;
	Settings->InitialPoison = OldPoison;
	Settings->InitialFishingStrength = OldStrength;
	Settings->InitialFightStamina = OldStamina;
	return !HasAnyErrors();
}

bool FCatBodyActionPayloadEventTagTest::RunTest(const FString& Parameters)
{
	// BodyAction 映射测试流程：逐个创建正式非 Fishing 身体动作载荷，确认它们都有可触发 Ability 的 GameplayEvent；
	// Unknown 必须保持关闭，避免新增枚举时忘记补标签却被 Controller 路由吞掉。它只证明映射契约，不证明表现手感或整场交付就绪。
	(void)Parameters;
	for (const ECatBodyActionAbilityCommand Command : GBodyActionCommands)
	{
		UCatBodyActionPayload* Payload = NewObject<UCatBodyActionPayload>(GetTransientPackage());
		TestTrue(TEXT("正式 BodyAction 命令能初始化事件标签"), Payload->InitializeForCommand(Command));
		TestTrue(TEXT("正式 BodyAction 命令载荷可投递"), Payload->IsRuntimeValid());
		TestTrue(TEXT("正式 BodyAction 命令有有效 GameplayEvent"),
			UCatBodyActionPayload::GetEventTagForCommand(Command).IsValid());
	}
	UCatBodyActionPayload* UnknownPayload = NewObject<UCatBodyActionPayload>(GetTransientPackage());
	TestFalse(TEXT("Unknown BodyAction 不会生成事件标签"),
		UnknownPayload->InitializeForCommand(ECatBodyActionAbilityCommand::Unknown));
	TestFalse(TEXT("Unknown BodyAction 不可投递"), UnknownPayload->IsRuntimeValid());
	return !HasAnyErrors();
}

bool FCatBodyActionPresentationSettingsContractTest::RunTest(const FString& Parameters)
{
	// 表现配置测试流程：只验证 BodyAction Ability 有统一动作前摇和角色表现通道；
	// 它现在要求每个 BodyAction 都能从正式配置加载 Montage，但不把 GameplayCue、双端手感或最终美术质量写成已关闭。
	(void)Parameters;
	const UCatBodyActionPresentationSettings* Settings = GetDefault<UCatBodyActionPresentationSettings>();
	TestNotNull(TEXT("BodyAction presentation settings are available"), Settings);
	const UCatGA_BodyActionCommand* Ability = GetDefault<UCatGA_BodyActionCommand>();
	TestNotNull(TEXT("BodyAction ability CDO is available for presentation contract"), Ability);
	if (!Settings || !Ability)
	{
		return false;
	}
	TestTrue(TEXT("default BodyAction lead-in is cancellable"), Settings->DefaultLeadInSeconds > 0.0f);
	for (const ECatBodyActionAbilityCommand Command : GBodyActionCommands)
	{
		const FCatBodyActionPresentationConfig* Config = Settings->FindPresentationConfig(Command);
		TestNotNull(TEXT("every BodyAction command has a presentation row"), Config);
		TestTrue(TEXT("BodyAction command lead-in stays non-negative"),
			Settings->GetLeadInSecondsForCommand(Command) >= 0.0f);
		TestEqual(TEXT("Ability reads the same lead-in as presentation settings"),
			Ability->GetBodyActionLeadInSecondsForAutomation(Command), Settings->GetLeadInSecondsForCommand(Command));
		TestTrue(TEXT("BodyAction command exposes a presentation event tag"),
			Settings->GetPresentationEventTagForCommand(Command).IsValid());
		TestEqual(TEXT("Ability freezes the same presentation event tag as settings"),
			Ability->GetBodyActionPresentationEventTagForAutomation(Command),
			Settings->GetPresentationEventTagForCommand(Command));
		TestFalse(TEXT("BodyAction command has a configured formal Montage path"), Config->Montage.IsNull());
		TestNotNull(TEXT("BodyAction command formal Montage resolves"),
			Settings->LoadMontageForCommand(Command));
	}
	UCatBodyActionPresentationSettings* MutableSettings = GetMutableDefault<UCatBodyActionPresentationSettings>();
	TestNotNull(TEXT("BodyAction presentation settings CDO can be temporarily overridden"), MutableSettings);
	const TArray<FCatBodyActionPresentationConfig> SavedPresentationConfigs = MutableSettings
		? MutableSettings->ActionPresentationConfigs : TArray<FCatBodyActionPresentationConfig>();
	// 下面临时追加重复 Command 配置只用于证明“后追加行覆盖默认行”的读取规则；Scope 退出时还原 CDO 内存状态，不调用 SaveConfig，避免污染后续自动化和项目配置。
	ON_SCOPE_EXIT
	{
		if (MutableSettings)
		{
			MutableSettings->ActionPresentationConfigs = SavedPresentationConfigs;
		}
	};
	if (MutableSettings)
	{
		FCatBodyActionPresentationConfig OverrideConfig;
		OverrideConfig.Command = ECatBodyActionAbilityCommand::RequestManualHelp;
		OverrideConfig.LeadInSeconds = 0.42f;
		OverrideConfig.PresentationEventTag = CatFishingAbilityTags::AbilityEvent_Body_RequestMischief;
		MutableSettings->ActionPresentationConfigs.Add(OverrideConfig);
		TestEqual(TEXT("later duplicate BodyAction presentation rows override the default lead-in"),
			Settings->GetLeadInSecondsForCommand(ECatBodyActionAbilityCommand::RequestManualHelp),
			OverrideConfig.LeadInSeconds);
		TestEqual(TEXT("later duplicate BodyAction presentation rows override the default tag"),
			Settings->GetPresentationEventTagForCommand(ECatBodyActionAbilityCommand::RequestManualHelp),
			OverrideConfig.PresentationEventTag);
	}
	const UFunction* PlayRpc = ACatCharacter::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(ACatCharacter, Multicast_PlayBodyActionPresentation));
	const UFunction* StopRpc = ACatCharacter::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(ACatCharacter, Multicast_StopBodyActionPresentation));
	const UFunction* PlayEvent = ACatCharacter::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(ACatCharacter, BP_PlayBodyActionPresentation));
	const UFunction* StopEvent = ACatCharacter::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(ACatCharacter, BP_StopBodyActionPresentation));
	const UFunction* PlayMontage = ACatCharacter::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(ACatCharacter, PlayBodyActionMontageFromPresentation));
	const UFunction* StopMontage = ACatCharacter::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(ACatCharacter, StopBodyActionMontageFromPresentation));
	TestNotNull(TEXT("BodyAction play presentation multicast exists"), PlayRpc);
	TestNotNull(TEXT("BodyAction stop presentation multicast exists"), StopRpc);
	TestNotNull(TEXT("BodyAction play presentation Blueprint event exists"), PlayEvent);
	TestNotNull(TEXT("BodyAction stop presentation Blueprint event exists"), StopEvent);
	TestNotNull(TEXT("BodyAction play montage callable exists"), PlayMontage);
	TestNotNull(TEXT("BodyAction stop montage callable exists"), StopMontage);
	if (PlayRpc)
	{
		TestTrue(TEXT("BodyAction play presentation is NetMulticast"), PlayRpc->HasAnyFunctionFlags(FUNC_NetMulticast));
	}
	if (StopRpc)
	{
		TestTrue(TEXT("BodyAction stop presentation is NetMulticast"), StopRpc->HasAnyFunctionFlags(FUNC_NetMulticast));
		TestTrue(TEXT("BodyAction stop presentation is reliable so cancellation can clear loop presentation"),
			StopRpc->HasAnyFunctionFlags(FUNC_NetReliable));
	}
	if (PlayEvent)
	{
		TestTrue(TEXT("BodyAction play presentation event is cosmetic"),
			PlayEvent->HasAnyFunctionFlags(FUNC_BlueprintCosmetic));
	}
	if (StopEvent)
	{
		TestTrue(TEXT("BodyAction stop presentation event is cosmetic"),
			StopEvent->HasAnyFunctionFlags(FUNC_BlueprintCosmetic));
	}
	if (PlayMontage)
	{
		TestTrue(TEXT("BodyAction play montage entry is cosmetic"),
			PlayMontage->HasAnyFunctionFlags(FUNC_BlueprintCosmetic));
	}
	if (StopMontage)
	{
		TestTrue(TEXT("BodyAction stop montage entry is cosmetic"),
			StopMontage->HasAnyFunctionFlags(FUNC_BlueprintCosmetic));
	}
	return !HasAnyErrors();
}

bool FCatBodyActionCancelWindowContractTest::RunTest(const FString& Parameters)
{
	// BodyAction 生命周期测试只看 Ability 层契约：默认网关必须有可取消提交窗口，并且用 BodyAction 资产标签供 ASC 精准取消。
	// 它不假装覆盖正式 Montage 资产、双客户端手感或领域命令执行结果；这些仍要靠运行时/行为验收补齐。
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("creates BodyAction ability world"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	ACatCharacter* Character = World ? World->SpawnActor<ACatCharacter>() : nullptr;
	ACatfishingPlayerController* Controller = World ? World->SpawnActor<ACatfishingPlayerController>() : nullptr;
	TestNotNull(TEXT("BodyAction character is available"), Character);
	TestNotNull(TEXT("BodyAction controller is available"), Controller);
	if (!Character || !Controller)
	{
		return false;
	}
	Controller->Possess(Character);
	UCatAbilitySystemComponent* AbilitySystem = Character->GetCatAbilitySystemComponent();
	TestNotNull(TEXT("BodyAction ASC is available"), AbilitySystem);
	if (!AbilitySystem)
	{
		return false;
	}
	AbilitySystem->InitAbilityActorInfo(Character, Character);
	if (CountBodyActionAbilitySpecs(AbilitySystem) == 0)
	{
		AbilitySystem->GiveAbility(FGameplayAbilitySpec(UCatGA_BodyActionCommand::StaticClass(), 1));
	}
	TestEqual(TEXT("one BodyAction gateway spec is present"), CountBodyActionAbilitySpecs(AbilitySystem), 1);

	const UCatGA_BodyActionCommand* Ability = GetDefault<UCatGA_BodyActionCommand>();
	TestNotNull(TEXT("BodyAction ability CDO is available"), Ability);
	if (!Ability)
	{
		return false;
	}
	TestTrue(TEXT("BodyAction waits before committing domain command"),
		Ability->GetBodyActionCommitWindowSecondsForAutomation() > 0.0f);
	UCatGA_BodyActionCommand* MutableAbility = GetMutableDefault<UCatGA_BodyActionCommand>();
	TestNotNull(TEXT("BodyAction ability CDO can clear automation override"), MutableAbility);
	if (MutableAbility)
	{
		MutableAbility->SetBodyActionCommitWindowSecondsForAutomation(0.0f);
		TestTrue(TEXT("BodyAction automation override becomes active"), MutableAbility->HasBodyActionCommitWindowOverrideForAutomation());
		MutableAbility->ClearBodyActionCommitWindowOverrideForAutomation();
		TestFalse(TEXT("BodyAction automation override clears back to settings-driven mode"),
			MutableAbility->HasBodyActionCommitWindowOverrideForAutomation());
	}
	TestTrue(TEXT("BodyAction RequestManualHelp uses a cancellable presentation lead-in"),
		Ability->GetBodyActionLeadInSecondsForAutomation(ECatBodyActionAbilityCommand::RequestManualHelp) > 0.0f);
	TestTrue(TEXT("BodyAction RequestManualHelp has a presentation event tag"),
		Ability->GetBodyActionPresentationEventTagForAutomation(ECatBodyActionAbilityCommand::RequestManualHelp).IsValid());
	TestTrue(TEXT("BodyAction can be targeted by ASC cancel tag"),
		Ability->GetAssetTags().HasTagExact(CatFishingAbilityTags::Ability_Body_Command));
	UCatBodyActionPayload* Payload = NewObject<UCatBodyActionPayload>(AbilitySystem);
	TestTrue(TEXT("BodyAction cancel test payload initializes"),
		Payload->InitializeForCommand(ECatBodyActionAbilityCommand::RequestManualHelp));
	Payload->RequestId = FGuid::NewGuid();
	Payload->HelpKind = ECatHelpSignalKind::ManualFishing;
	FGameplayEventData EventData;
	EventData.EventTag = Payload->EventTag;
	EventData.Instigator = Character;
	EventData.Target = Character;
	EventData.OptionalObject = Payload;
	TestEqual(TEXT("BodyAction GameplayEvent activates the gateway"), AbilitySystem->HandleGameplayEvent(Payload->EventTag, &EventData), 1);
	TestTrue(TEXT("BodyAction stays active inside the commit window"), HasActiveBodyActionAbility(AbilitySystem));
	TestTrue(TEXT("ASC cancels the active BodyAction by tag"), AbilitySystem->CancelBodyActionAbilitiesFromAuthority());
	TestFalse(TEXT("BodyAction is inactive after cancel"), HasActiveBodyActionAbility(AbilitySystem));
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
