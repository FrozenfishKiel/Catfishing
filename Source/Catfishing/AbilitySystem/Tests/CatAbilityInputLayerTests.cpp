#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "AbilitySystem/CatAbilitySet.h"
#include "AbilitySystem/CatAbilityInputConfig.h"
#include "AbilitySystem/CatAbilitySettings.h"
#include "AbilitySystem/CatAbilitySystemComponent.h"
#include "AbilitySystem/CatFishingAbilities.h"
#include "AbilitySystem/CatFishingAbilityTags.h"
#include "AbilitySystem/CatSurvivalAttributeSet.h"
#include "Character/CatCharacter.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Fishing/CatFishingSession.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "InputAction.h"

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
	"Catfishing.Unit.AbilitySystem.InputConfig.RequiresFiveUniqueFishingMappings",
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

bool FCatAbilitySetGrantRemoveTest::RunTest(const FString& Parameters)
{
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
	AddInputAbility(UCatGA_FishingCancel::StaticClass(), CatFishingAbilityTags::Input_Fishing_Cancel);
	AddInputAbility(UCatGA_FishingScoop::StaticClass(), CatFishingAbilityTags::Input_Fishing_Scoop);
	AddInputAbility(UCatGA_FishingChum::StaticClass(), CatFishingAbilityTags::Input_Fishing_Chum,
		ECatAbilityActivationPolicy::WhileInputActive);

	FCatGrantedAbilitySetHandles Handles;
	TestTrue(TEXT("authority ASC accepts a complete ability set"), AbilitySet->GiveToAbilitySystem(AbilitySystem, Handles));
	TestEqual(TEXT("five ability handles are recorded"), Handles.GetAbilitySpecHandles().Num(), 5);
	TestEqual(TEXT("five activatable specs are present"), AbilitySystem->GetActivatableAbilities().Num(), 5);
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
	TestFalse(TEXT("partial required AbilitySet fails readiness"), AbilitySet->IsRuntimeReady());
	TestFalse(TEXT("partial AbilitySet grants nothing"), AbilitySet->GiveToAbilitySystem(AbilitySystem, RejectedHandles));
	TestEqual(TEXT("rejected set leaves ASC unchanged"), AbilitySystem->GetActivatableAbilities().Num(), 0);
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
	const float OldHunger = Settings->InitialHunger;
	const float OldFatigue = Settings->InitialFatigue;
	const float OldPoison = Settings->InitialPoison;
	const float OldStrength = Settings->InitialFishingStrength;
	const float OldStamina = Settings->InitialFightStamina;
	Settings->bEnableCharacterAbilityRuntime = true;
	Settings->ReplicationPolicy = ECatAbilityReplicationPolicy::Full;
	Settings->bEnableInitialAttributeTuning = true;
	Settings->InitialHunger = 0.0f;
	Settings->InitialFatigue = 0.0f;
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
	Settings->InitialHunger = OldHunger;
	Settings->InitialFatigue = OldFatigue;
	Settings->InitialPoison = OldPoison;
	Settings->InitialFishingStrength = OldStrength;
	Settings->InitialFightStamina = OldStamina;
	return !HasAnyErrors();
}

bool FCatAbilityInputConfigReadinessTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UCatAbilityInputConfig* Config = NewObject<UCatAbilityInputConfig>(GetTransientPackage());
	const FGameplayTag RequiredTags[] = {
		CatFishingAbilityTags::Input_Fishing_RodInteract,
		CatFishingAbilityTags::Input_Fishing_Primary,
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
	TestTrue(TEXT("five unique required mappings are ready"), Config->IsRuntimeReady());
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
	const float OldHunger = Settings->InitialHunger;
	const float OldFatigue = Settings->InitialFatigue;
	const float OldPoison = Settings->InitialPoison;
	const float OldStrength = Settings->InitialFishingStrength;
	const float OldStamina = Settings->InitialFightStamina;
	Settings->bEnableCharacterAbilityRuntime = true;
	Settings->ReplicationPolicy = ECatAbilityReplicationPolicy::Full;
	Settings->bEnableInitialAttributeTuning = true;
	Settings->InitialHunger = 0.0f;
	Settings->InitialFatigue = 0.0f;
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
	Settings->InitialHunger = OldHunger;
	Settings->InitialFatigue = OldFatigue;
	Settings->InitialPoison = OldPoison;
	Settings->InitialFishingStrength = OldStrength;
	Settings->InitialFightStamina = OldStamina;
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
