#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "UObject/UnrealType.h"

#include "Animation/AnimMontage.h"
#include "Character/CatCharacter.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Fishing/Actors/CatFishEncounterActor.h"
#include "Fishing/Actors/CatFishingActorTypes.h"
#include "Fishing/Actors/CatFishingHookActor.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/Presentation/CatFishingLineCurveComponent.h"
#include "Fishing/CatFishingTypes.h"
#include "Fishing/Presentation/CatFishAnimInstance.h"
#include "Fishing/Presentation/CatFishingPresentationSettings.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingActorNativeBasesContractTest,
	"Catfishing.Unit.Fishing.Actors.NativeBasesAreBlueprintableAndExposePresentationEvents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingRodCanonicalAnchorsContractTest,
	"Catfishing.Unit.Fishing.Actors.RodCanonicalAnchorsAreSeparateFromVisualRoot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingActorIdentityContractTest,
	"Catfishing.Unit.Fishing.Actors.IdentityInitializationIsAuthorityOnlyAndIdempotent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishEncounterMovementContractTest,
	"Catfishing.Unit.Fishing.Actors.FishEncounterOwnsReplicatedMovementButNoOutcomeState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingAttemptSnapshotContractTest,
	"Catfishing.Unit.Fishing.Actors.AttemptSnapshotUsesTypedRodAndDefaultsFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingRodMutationAndLineContractTest,
	"Catfishing.Unit.Fishing.Actors.RodMutationsUseCanonicalAnchorsAndLineIsPresentationOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatFishingActorContractTest
{
	static void TestBlueprintContract(FAutomationTestBase& Test, const UClass* Class, const TCHAR* EventNames[], const int32 EventCount)
	{
		Test.TestNotNull(FString::Printf(TEXT("%s class exists"), *Class->GetName()), Class);
#if WITH_METADATA
		Test.TestTrue(FString::Printf(TEXT("%s has local IsBlueprintBase metadata"), *Class->GetName()), Class->HasMetaData(TEXT("IsBlueprintBase")));
		Test.TestTrue(FString::Printf(TEXT("%s local IsBlueprintBase is true"), *Class->GetName()), Class->GetBoolMetaData(TEXT("IsBlueprintBase")));
		Test.TestTrue(FString::Printf(TEXT("%s inherits Blueprint base support"), *Class->GetName()), Class->GetBoolMetaDataHierarchical(TEXT("IsBlueprintBase")));
		Test.TestTrue(FString::Printf(TEXT("%s has local ChildCannotTick metadata"), *Class->GetName()), Class->HasMetaData(TEXT("ChildCannotTick")));
#endif
		for (int32 Index = 0; Index < EventCount; ++Index)
		{
			const UFunction* Event = Class->FindFunctionByName(EventNames[Index]);
			Test.TestNotNull(FString::Printf(TEXT("%s exposes %s"), *Class->GetName(), EventNames[Index]), Event);
			if (Event)
			{
				Test.TestTrue(FString::Printf(TEXT("%s is a Blueprint event"), EventNames[Index]), Event->HasAnyFunctionFlags(FUNC_BlueprintEvent));
				Test.TestTrue(FString::Printf(TEXT("%s is cosmetic"), EventNames[Index]), Event->HasAnyFunctionFlags(FUNC_BlueprintCosmetic));
			}
		}
	}

	static void TestPresentationReplication(FAutomationTestBase& Test, AActor* Actor, const FName PropertyName)
	{
		FProperty* StateProperty = FindFProperty<FProperty>(Actor->GetClass(), PropertyName);
		Test.TestNotNull(FString::Printf(TEXT("%s has presentation state"), *Actor->GetName()), StateProperty);
		if (!StateProperty)
		{
			return;
		}
		Test.TestTrue(TEXT("presentation state uses ReplicatedUsing"), StateProperty->HasAnyPropertyFlags(CPF_RepNotify));

		Actor->GetClass()->SetUpRuntimeReplicationData();
		TArray<FLifetimeProperty> LifetimeProperties;
		Actor->GetLifetimeReplicatedProps(LifetimeProperties);
		Test.TestTrue(TEXT("presentation state is registered for lifetime replication"),
			LifetimeProperties.ContainsByPredicate([StateProperty](const FLifetimeProperty& Candidate)
			{
				return Candidate.RepIndex == StateProperty->RepIndex;
			}));
	}

	static USceneComponent* FindSceneComponent(AActor* Actor, const FName Name)
	{
		TInlineComponentArray<USceneComponent*> Components(Actor);
		if (USceneComponent* const* Found = Components.FindByPredicate([Name](const USceneComponent* Component)
		{
			return Component && Component->GetFName() == Name;
		}))
		{
			return *Found;
		}
		return nullptr;
	}

	static void TestRodDoesNotExposeCanonicalComponents(FAutomationTestBase& Test)
	{
		for (TFieldIterator<UFunction> It(ACatFishingRodActor::StaticClass(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			const UFunction* Function = *It;
			if (!Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure))
			{
				continue;
			}
			const FObjectPropertyBase* ReturnProperty = CastField<FObjectPropertyBase>(Function->GetReturnProperty());
			Test.TestFalse(FString::Printf(TEXT("%s does not expose a SceneComponent return"), *Function->GetName()),
				ReturnProperty && ReturnProperty->PropertyClass->IsChildOf<USceneComponent>());
		}
	}
}

bool FCatFishingActorNativeBasesContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const TCHAR* RodEvents[] = { TEXT("BP_OnRodPresentationChanged"), TEXT("BP_ApplyRodSkin"), TEXT("BP_PlayRodPresentationEvent") };
	const TCHAR* HookEvents[] = { TEXT("BP_OnHookPresentationChanged"), TEXT("BP_PlayHookPresentationEvent") };
	const TCHAR* FishEvents[] = { TEXT("BP_OnFishPresentationChanged"), TEXT("BP_PlayFishPresentationEvent") };
	CatFishingActorContractTest::TestBlueprintContract(*this, ACatFishingRodActor::StaticClass(), RodEvents, UE_ARRAY_COUNT(RodEvents));
	CatFishingActorContractTest::TestBlueprintContract(*this, ACatFishingHookActor::StaticClass(), HookEvents, UE_ARRAY_COUNT(HookEvents));
	CatFishingActorContractTest::TestBlueprintContract(*this, ACatFishEncounterActor::StaticClass(), FishEvents, UE_ARRAY_COUNT(FishEvents));
	CatFishingActorContractTest::TestRodDoesNotExposeCanonicalComponents(*this);
	const AActor* CDOs[] = {
		static_cast<const AActor*>(GetDefault<ACatFishingRodActor>()),
		static_cast<const AActor*>(GetDefault<ACatFishingHookActor>()),
		static_cast<const AActor*>(GetDefault<ACatFishEncounterActor>())
	};
	for (const AActor* CDO : CDOs)
	{
		TestFalse(FString::Printf(TEXT("%s is not abstract"), *CDO->GetClass()->GetName()), CDO->GetClass()->HasAnyClassFlags(CLASS_Abstract));
	}
	const ACatFishingRodActor* RodCDO = GetDefault<ACatFishingRodActor>();
	TestTrue(TEXT("native rod can tick while held so authority can follow the carrier"), RodCDO->PrimaryActorTick.bCanEverTick);
	TestFalse(TEXT("native rod does not start ticking while grounded"), RodCDO->PrimaryActorTick.bStartWithTickEnabled);
	#if WITH_METADATA
	TestTrue(TEXT("rod Blueprint children cannot add a second gameplay tick"),
		ACatFishingRodActor::StaticClass()->HasMetaData(TEXT("ChildCannotTick")));
	#endif
	TestFalse(TEXT("hook does not tick"), GetDefault<ACatFishingHookActor>()->PrimaryActorTick.bCanEverTick);
	TestFalse(TEXT("fish does not tick"), GetDefault<ACatFishEncounterActor>()->PrimaryActorTick.bCanEverTick);
	const UFunction* CastMontageFunction = ACatCharacter::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(ACatCharacter, PlayFishingCastMontageFromPresentation));
	const UFunction* OutcomeMontageFunction = ACatCharacter::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(ACatCharacter, PlayFishingOutcomeMontageFromPresentation));
	TestNotNull(TEXT("character exposes the replicated-state cast montage presentation entry"), CastMontageFunction);
	TestNotNull(TEXT("character exposes the server-confirmed outcome montage presentation entry"), OutcomeMontageFunction);
	if (CastMontageFunction)
	{
		TestTrue(TEXT("cast montage entry is cosmetic"), CastMontageFunction->HasAnyFunctionFlags(FUNC_BlueprintCosmetic));
	}
	if (OutcomeMontageFunction)
	{
		TestTrue(TEXT("outcome montage entry is cosmetic"), OutcomeMontageFunction->HasAnyFunctionFlags(FUNC_BlueprintCosmetic));
	}
	const UCatFishingPresentationSettings* PresentationSettings = GetDefault<UCatFishingPresentationSettings>();
	TestFalse(TEXT("cast montage is configured"), PresentationSettings->CastMontage.IsNull());
	TestNotNull(TEXT("configured cast montage resolves"), PresentationSettings->CastMontage.LoadSynchronous());
	TestFalse(TEXT("line-broken montage is configured"), PresentationSettings->LineBrokenMontage.IsNull());
	TestNotNull(TEXT("configured line-broken montage resolves"), PresentationSettings->LineBrokenMontage.LoadSynchronous());
	TestFalse(TEXT("cat-in-water montage is configured"), PresentationSettings->CatInWaterMontage.IsNull());
	TestNotNull(TEXT("configured cat-in-water montage resolves"), PresentationSettings->CatInWaterMontage.LoadSynchronous());

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("create actor contract game world"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	UWorld* World = WorldWrapper.GetTestWorld();
	if (World)
	{
		ACatFishingRodActor* Rod = World->SpawnActor<ACatFishingRodActor>();
		CatFishingActorContractTest::TestPresentationReplication(*this, Rod, TEXT("PresentationState"));
		FProperty* CarrierConstraintProperty = Rod
			? FindFProperty<FProperty>(Rod->GetClass(), TEXT("CarrierConstraintState")) : nullptr;
		TestNotNull(TEXT("rod exposes a replicated carrier constraint state"), CarrierConstraintProperty);
		if (Rod && CarrierConstraintProperty)
		{
			Rod->GetClass()->SetUpRuntimeReplicationData();
			TArray<FLifetimeProperty> LifetimeProperties;
			Rod->GetLifetimeReplicatedProps(LifetimeProperties);
			TestTrue(TEXT("carrier constraint state is registered for lifetime replication"),
				LifetimeProperties.ContainsByPredicate([CarrierConstraintProperty](const FLifetimeProperty& Candidate)
				{
					return Candidate.RepIndex == CarrierConstraintProperty->RepIndex;
				}));
		}
		CatFishingActorContractTest::TestPresentationReplication(*this, World->SpawnActor<ACatFishingHookActor>(), TEXT("PresentationState"));
		CatFishingActorContractTest::TestPresentationReplication(*this, World->SpawnActor<ACatFishEncounterActor>(), TEXT("PresentationState"));
	}
	return !HasAnyErrors();
}

bool FCatFishingRodCanonicalAnchorsContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("create rod anchor game world"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	UWorld* World = WorldWrapper.GetTestWorld();
	ACatFishingRodActor* Rod = World ? World->SpawnActor<ACatFishingRodActor>() : nullptr;
	TestNotNull(TEXT("spawn rod"), Rod);
	if (!Rod)
	{
		return false;
	}

	USceneComponent* SceneRoot = CatFishingActorContractTest::FindSceneComponent(Rod, TEXT("SceneRoot"));
	USceneComponent* VisualRoot = CatFishingActorContractTest::FindSceneComponent(Rod, TEXT("VisualRoot"));
	USceneComponent* RodTip = CatFishingActorContractTest::FindSceneComponent(Rod, TEXT("RodTipAnchor"));
	USceneComponent* Stand = CatFishingActorContractTest::FindSceneComponent(Rod, TEXT("StandAnchor"));
	USceneComponent* RightStand = CatFishingActorContractTest::FindSceneComponent(Rod, TEXT("RightStandAnchor"));
	USceneComponent* LeftStand = CatFishingActorContractTest::FindSceneComponent(Rod, TEXT("LeftStandAnchor"));
	USceneComponent* Grip = CatFishingActorContractTest::FindSceneComponent(Rod, TEXT("GripAnchor"));
	TestNotNull(TEXT("SceneRoot exists"), SceneRoot);
	TestNotNull(TEXT("VisualRoot exists"), VisualRoot);
	TestNotNull(TEXT("RodTipAnchor exists"), RodTip);
	TestNotNull(TEXT("StandAnchor exists"), Stand);
	TestNotNull(TEXT("RightStandAnchor exists"), RightStand);
	TestNotNull(TEXT("LeftStandAnchor exists"), LeftStand);
	TestNotNull(TEXT("GripAnchor exists"), Grip);
	if (!SceneRoot || !VisualRoot || !RodTip || !Stand || !RightStand || !LeftStand || !Grip)
	{
		return false;
	}
	TestEqual(TEXT("VisualRoot attaches to SceneRoot"), VisualRoot->GetAttachParent(), SceneRoot);
	for (USceneComponent* Anchor : { RodTip, Stand, RightStand, LeftStand, Grip })
	{
		TestEqual(TEXT("canonical anchor attaches to SceneRoot"), Anchor->GetAttachParent(), SceneRoot);
		TestFalse(TEXT("canonical anchor cannot be edited when inherited"), Anchor->bEditableWhenInherited);
	}
	TestTrue(TEXT("VisualRoot remains editable when inherited"), VisualRoot->bEditableWhenInherited);

	Rod->SetActorTransform(FTransform(FRotator(0.0, 35.0, 0.0), FVector(100.0, 200.0, 300.0)));
	const FTransform ExpectedTip = Rod->GetRodTipWorldTransform();
	const FTransform ExpectedStand = Rod->GetStandWorldTransform();
	const FTransform ExpectedInteraction = Rod->GetOperatorInteractionWorldTransform();
	const FTransform ExpectedLeftStand = Rod->GetOperatorStandWorldTransform(1);
	const FTransform ExpectedGrip = Rod->GetGripWorldTransform();
	VisualRoot->SetRelativeLocation(FVector(900.0, 0.0, 0.0));
	RodTip->SetRelativeLocation(FVector(-500.0, 0.0, 0.0));
	Stand->SetRelativeLocation(FVector(-400.0, 0.0, 0.0));
	RightStand->SetRelativeLocation(FVector(-450.0, 0.0, 0.0));
	LeftStand->SetRelativeLocation(FVector(-475.0, 0.0, 0.0));
	Grip->SetRelativeLocation(FVector(-300.0, 0.0, 0.0));
	TestTrue(TEXT("tip getter ignores component movement"), Rod->GetRodTipWorldTransform().Equals(ExpectedTip));
	TestTrue(TEXT("stand getter ignores component movement"), Rod->GetStandWorldTransform().Equals(ExpectedStand));
	TestTrue(TEXT("shared interaction getter ignores component movement"),
		Rod->GetOperatorInteractionWorldTransform().Equals(ExpectedInteraction));
	TestTrue(TEXT("left stand getter ignores component movement"),
		Rod->GetOperatorStandWorldTransform(1).Equals(ExpectedLeftStand));
	TestTrue(TEXT("grip getter ignores component movement"), Rod->GetGripWorldTransform().Equals(ExpectedGrip));
	Rod->SetActorLocation(FVector(700.0, 800.0, 900.0));
	TestTrue(TEXT("tip getter follows actor transform"), Rod->GetRodTipWorldTransform().Equals(Rod->GetActorTransform()));
	const FVector RightLocation = Rod->GetOperatorStandWorldTransform(0).GetLocation();
	const FVector LeftLocation = Rod->GetOperatorStandWorldTransform(1).GetLocation();
	TestTrue(TEXT("right and left stand remain centered on canonical stand"),
		((RightLocation + LeftLocation) * 0.5).Equals(Rod->GetActorLocation(), UE_KINDA_SMALL_NUMBER));
	TestTrue(TEXT("legacy stand getter aliases right primary slot"),
		Rod->GetStandWorldTransform().Equals(Rod->GetOperatorStandWorldTransform(0)));
	TestTrue(TEXT("all operators share the canonical interaction anchor"),
		Rod->GetOperatorInteractionWorldTransform().Equals(Rod->GetActorTransform()));
	TestTrue(TEXT("invalid slot falls back to canonical stand center"),
		Rod->GetOperatorStandWorldTransform(-1).Equals(Rod->GetActorTransform()));
	TestTrue(TEXT("grip getter follows actor transform"), Rod->GetGripWorldTransform().Equals(Rod->GetActorTransform()));
	return !HasAnyErrors();
}

bool FCatFishingActorIdentityContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("create identity game world"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	UWorld* World = WorldWrapper.GetTestWorld();
	ACatFishingRodActor* Rod = World ? World->SpawnActor<ACatFishingRodActor>() : nullptr;
	ACatFishingHookActor* Hook = World ? World->SpawnActor<ACatFishingHookActor>() : nullptr;
	ACatFishEncounterActor* Fish = World ? World->SpawnActor<ACatFishEncounterActor>() : nullptr;
	APlayerState* Owner = World ? World->SpawnActor<APlayerState>() : nullptr;
	APlayerState* Helper = World ? World->SpawnActor<APlayerState>() : nullptr;
	APlayerState* Third = World ? World->SpawnActor<APlayerState>() : nullptr;
	TestNotNull(TEXT("identity actors spawn"), Rod);
	TestNotNull(TEXT("identity owner spawns"), Owner);
	if (!Rod || !Hook || !Fish || !Owner || !Helper || !Third)
	{
		return false;
	}
	const FGuid RodId = FGuid::NewGuid();
	const FGuid RodItemInstanceId = FGuid::NewGuid();
	const FGuid SessionId = FGuid::NewGuid();
	const FGuid AttemptId = FGuid::NewGuid();
	TestFalse(TEXT("rod rejects invalid identity"), Rod->InitializeAuthoritativeIdentity(
		FGuid(), RodItemInstanceId, TEXT("Rod"), NAME_None, Owner, nullptr, false, false));
	TestTrue(TEXT("rod accepts first identity"), Rod->InitializeAuthoritativeIdentity(
		RodId, RodItemInstanceId, TEXT("Rod"), TEXT("SkinA"), Owner, nullptr, true, false));
	const FCatFishingRodPresentationState RodFirst = Rod->GetPresentationState();
	TestEqual(TEXT("rod starts at revision one"), RodFirst.RodActorRevision, int64{1});
	TestEqual(TEXT("rod publishes immutable item instance identity"), RodFirst.ItemInstanceId, RodItemInstanceId);
	TestTrue(TEXT("first place transition deploys the rod"), RodFirst.bDeployed);
	TestEqual(TEXT("first place transition leaves all operator slots empty"), Rod->GetOperatorCount(), 0);
	TestNull(TEXT("first place transition has no primary operator"), RodFirst.OperatorPlayerState);
	TestNull(TEXT("grounded rod has no holder"), RodFirst.HolderPlayerState);
	TestEqual(TEXT("first place transition is grounded"), RodFirst.PoseMode, ECatFishingRodPoseMode::Grounded);
	TestTrue(TEXT("rod exact identity replay succeeds"), Rod->InitializeAuthoritativeIdentity(
		RodId, RodItemInstanceId, TEXT("Rod"), TEXT("SkinB"), Owner, Owner, false, true));
	TestEqual(TEXT("rod replay preserves state"), Rod->GetPresentationState().RodSkinDefinitionId, RodFirst.RodSkinDefinitionId);
	TestFalse(TEXT("rod rejects changed immutable identity"), Rod->InitializeAuthoritativeIdentity(
		FGuid::NewGuid(), RodItemInstanceId, TEXT("Rod"), NAME_None, Owner, nullptr, false, false));
	int32 JoinedSlot = INDEX_NONE;
	TestTrue(TEXT("first operator joins right primary slot"), Rod->AddOperatorFromAuthority(Owner, 1, JoinedSlot));
	TestEqual(TEXT("first operator slot index is zero"), JoinedSlot, 0);
	TestEqual(TEXT("primary compatibility field follows slot zero"),
		Rod->GetPresentationState().OperatorPlayerState.Get(), Owner);
	TestEqual(TEXT("first operator becomes authoritative holder"),
		Rod->GetPresentationState().HolderPlayerState.Get(), Owner);
	TestEqual(TEXT("first operator atomically switches rod to held"),
		Rod->GetPresentationState().PoseMode, ECatFishingRodPoseMode::Held);
	TestTrue(TEXT("held rod accepts the authoritative coupled carrier constraint"),
		Rod->SetCarrierConstraintFromAuthority(FVector::ForwardVector,
			600.0, 30.0, 0.4, 0.75, 8.0));
	TestTrue(TEXT("coupled carrier constraint becomes active"),
		Rod->GetCarrierConstraintState().bActive);
	TestEqual(TEXT("coupled carrier constraint keeps the server speed multiplier"),
		Rod->GetCarrierConstraintState().MaximumAwaySpeedMultiplier, 0.4f);
	TestEqual(TEXT("coupled carrier constraint carries a target pull speed"),
		Rod->GetCarrierConstraintState().TargetPullSpeedCentimetersPerSecond, 30.0f);
	Rod->ClearCarrierConstraintFromAuthority();
	TestFalse(TEXT("clearing the fight constraint removes stale carrier drag"),
		Rod->GetCarrierConstraintState().bActive);
	TestEqual(TEXT("clearing the fight constraint restores unrestricted movement"),
		Rod->GetCarrierConstraintState().MaximumAwaySpeedMultiplier, 1.0f);
	TestTrue(TEXT("second operator joins left auxiliary slot"), Rod->AddOperatorFromAuthority(Helper, 2, JoinedSlot));
	TestEqual(TEXT("second operator slot index is one"), JoinedSlot, 1);
	TestEqual(TEXT("two-player occupancy is derived from compact array"), Rod->GetOperatorCount(), 2);
	TestFalse(TEXT("third operator is rejected by current two-slot configuration"),
		Rod->AddOperatorFromAuthority(Third, 3, JoinedSlot));
	APlayerState* PromotedPrimary = nullptr;
	TestTrue(TEXT("primary can leave two-player occupancy"),
		Rod->RemoveOperatorFromAuthority(Owner, 3, PromotedPrimary));
	TestEqual(TEXT("left operator is explicitly reported as promoted"), PromotedPrimary, Helper);
	TestEqual(TEXT("promoted operator becomes slot zero"), Rod->GetOperatorSlotIndex(Helper), 0);
	TestEqual(TEXT("two-to-one transition clears cooperative occupancy immediately"), Rod->GetOperatorCount(), 1);
	TestEqual(TEXT("primary mirror follows promoted operator"),
		Rod->GetPresentationState().OperatorPlayerState.Get(), Helper);
	TestEqual(TEXT("promotion atomically transfers holder"),
		Rod->GetPresentationState().HolderPlayerState.Get(), Helper);
	TestEqual(TEXT("promotion keeps rod held"), Rod->GetPresentationState().PoseMode,
		ECatFishingRodPoseMode::Held);
	TestTrue(TEXT("last operator can leave"), Rod->RemoveOperatorFromAuthority(Helper, 4, PromotedPrimary));
	TestEqual(TEXT("empty occupancy has zero count"), Rod->GetOperatorCount(), 0);
	TestNull(TEXT("empty occupancy clears primary mirror"), Rod->GetPresentationState().OperatorPlayerState);
	TestNull(TEXT("empty occupancy clears holder"), Rod->GetPresentationState().HolderPlayerState);
	TestEqual(TEXT("last operator leaving atomically grounds the rod"),
		Rod->GetPresentationState().PoseMode, ECatFishingRodPoseMode::Grounded);
	TestTrue(TEXT("hook accepts first identity"), Hook->InitializeAuthoritativeIdentity(SessionId, AttemptId));
	TestTrue(TEXT("authority hook accepts calm bobber mode"),
		Hook->SetBobberPresentationModeFromAuthority(ECatFishingBobberPresentationMode::Calm));
	TestEqual(TEXT("hook publishes calm bobber mode"), Hook->GetPresentationState().BobberMode,
		ECatFishingBobberPresentationMode::Calm);
	TestTrue(TEXT("hook bobber mode timestamp is finite"),
		FMath::IsFinite(Hook->GetPresentationState().BobberModeStartedServerTime));
	TestTrue(TEXT("hook exact identity replay succeeds"), Hook->InitializeAuthoritativeIdentity(SessionId, AttemptId));
	TestTrue(TEXT("hook accepts server line presentation scalars"),
		Hook->SetFishingLinePresentationFromAuthority(600.0, 500.0, 100.0, 0.25f, false));
	TestEqual(TEXT("hook publishes paid out line length"),
		Hook->GetPresentationState().PaidOutLineLengthCentimeters, 600.0);
	TestEqual(TEXT("hook publishes line slack"),
		Hook->GetPresentationState().SlackLineLengthCentimeters, 100.0);
	TestFalse(TEXT("hook rejects same session and attempt ids"), Hook->InitializeAuthoritativeIdentity(SessionId, SessionId));
	TestTrue(TEXT("fish accepts first identity"), Fish->InitializeAuthoritativeIdentity(
		SessionId, AttemptId, TEXT("RiverPatternFish"), 12.0, 1.25));
	TestTrue(TEXT("fish exact identity replay succeeds"), Fish->InitializeAuthoritativeIdentity(
		SessionId, AttemptId, TEXT("RiverPatternFish"), 99.0, 1.75));
	TestEqual(TEXT("fish replay preserves line length"), Fish->GetPresentationState().CurrentLineLength, 12.0);
	TestEqual(TEXT("fish replay preserves frozen visual scale"), Fish->GetPresentationState().VisualScale, 1.25);
	// CreateTestWorld does not start play automatically; exercise the same component lifecycle used by a real spawned fish.
	WorldWrapper.BeginPlayInTestWorld();
	const USkeletalMeshComponent* RuntimeFishMesh = Cast<USkeletalMeshComponent>(
		Fish->GetDefaultSubobjectByName(TEXT("FishMesh")));
	TestNotNull(TEXT("fish owns runtime mesh"), RuntimeFishMesh);
	if (RuntimeFishMesh)
	{
		TestTrue(TEXT("fish mesh consumes frozen scale"),
			RuntimeFishMesh->GetRelativeScale3D().Equals(FVector(1.25), UE_KINDA_SMALL_NUMBER));
	}
	TestTrue(TEXT("restrained fish accepts a nonzero intended swim speed"),
		Fish->ApplyFightStepFromAuthority(ECatFishMotionIntent::StrugglingOutward, 12.0,
			Fish->GetActorLocation(), 0.05f, 1.0f, 1.0f, 75.0f, true));
	TestEqual(TEXT("restrained fish publishes intent speed even without displacement"),
		Fish->GetPresentationState().IntendedSwimSpeedCentimetersPerSecond, 75.0f);
	TestFalse(TEXT("fish rejects a negative intended swim speed"),
		Fish->ApplyFightStepFromAuthority(ECatFishMotionIntent::StrugglingOutward, 12.0,
			Fish->GetActorLocation(), 0.05f, 1.0f, 1.0f, -1.0f, true));
	const double NonFiniteLineLength = TNumericLimits<double>::Max() * 2.0;
	TestFalse(TEXT("fish rejects non-finite line length"), Fish->InitializeAuthoritativeIdentity(
		FGuid::NewGuid(), FGuid::NewGuid(), TEXT("Fish"), NonFiniteLineLength, 1.0));
	ACatFishEncounterActor* InvalidScaleFish = World->SpawnActor<ACatFishEncounterActor>();
	TestFalse(TEXT("fish rejects non-positive visual scale"), InvalidScaleFish && InvalidScaleFish->InitializeAuthoritativeIdentity(
		FGuid::NewGuid(), FGuid::NewGuid(), TEXT("Fish"), 1.0, 0.0));
	ACatFishingHookActor* ClientHook = World->SpawnActor<ACatFishingHookActor>();
	ClientHook->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("client hook rejects authority initialization"), ClientHook->InitializeAuthoritativeIdentity(FGuid::NewGuid(), FGuid::NewGuid()));
	TestFalse(TEXT("client hook rejects authority bobber mode mutation"),
		ClientHook->SetBobberPresentationModeFromAuthority(ECatFishingBobberPresentationMode::Sunk));
	TestFalse(TEXT("client initialization leaves state unconfigured"), ClientHook->GetPresentationState().FishingSessionId.IsValid());
	for (UClass* Class : { ACatFishingRodActor::StaticClass(), ACatFishingHookActor::StaticClass(), ACatFishEncounterActor::StaticClass() })
	{
		TestNull(FString::Printf(TEXT("%s initializer is not a UFunction"), *Class->GetName()), Class->FindFunctionByName(TEXT("InitializeAuthoritativeIdentity")));
	}
	return !HasAnyErrors();
}

bool FCatFishEncounterMovementContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	ACatFishEncounterActor* FishCDO = GetMutableDefault<ACatFishEncounterActor>();
	TestTrue(TEXT("fish replicates"), FishCDO->GetIsReplicated());
	TestTrue(TEXT("fish replicates movement"), FishCDO->IsReplicatingMovement());
	TestFalse(TEXT("fish does not tick"), FishCDO->PrimaryActorTick.bCanEverTick);
	TestFalse(TEXT("fish is not always relevant"), FishCDO->bAlwaysRelevant);
	TestFalse(TEXT("fish does not use owner relevancy"), FishCDO->bNetUseOwnerRelevancy);
	TestFalse(TEXT("fish is not owner-only"), FishCDO->bOnlyRelevantToOwner);
	const USkeletalMeshComponent* FishMesh = Cast<USkeletalMeshComponent>(
		FishCDO->GetDefaultSubobjectByName(TEXT("FishMesh")));
	TestNotNull(TEXT("fish owns one native presentation mesh"), FishMesh);
	if (FishMesh)
	{
		TestTrue(TEXT("native fish mesh is collision-free"), FishMesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
		TestTrue(TEXT("native fish mesh is attached below VisualRoot"),
			FishMesh->GetAttachParent() == FishCDO->GetDefaultSubobjectByName(TEXT("VisualRoot")));
	}
	const UScriptStruct* StateStruct = FCatFishEncounterPresentationState::StaticStruct();
	const TSet<FName> ExpectedFields = { TEXT("FishingSessionId"), TEXT("CastAttemptId"), TEXT("FishDefinitionId"),
		TEXT("VisualScale"), TEXT("MotionIntent"), TEXT("IntendedSwimSpeedCentimetersPerSecond"),
		TEXT("bGrounded"), TEXT("GroundNormal"),
		TEXT("CurrentLineLength"), TEXT("FishLineAlignment"),
		TEXT("NormalizedLineLoad"), TEXT("bStrongConfrontation") };
	for (TFieldIterator<FProperty> It(StateStruct); It; ++It)
	{
		TestTrue(FString::Printf(TEXT("fish state field %s is whitelisted"), *It->GetName()), ExpectedFields.Contains(It->GetFName()));
	}
	const UClass* FishAnimClass = UCatFishAnimInstance::StaticClass();
	for (const FName PropertyName : { FName(TEXT("MotionIntent")), FName(TEXT("IntendedSwimSpeedCentimetersPerSecond")),
		FName(TEXT("SwimPlayRate")), FName(TEXT("FishLineAlignment")), FName(TEXT("NormalizedLineLoad")),
		FName(TEXT("bStrongConfrontation")) })
	{
		const FProperty* Property = FindFProperty<FProperty>(FishAnimClass, PropertyName);
		TestNotNull(FString::Printf(TEXT("fish anim instance exposes %s"), *PropertyName.ToString()), Property);
		if (Property)
		{
			TestTrue(FString::Printf(TEXT("fish anim property %s is Blueprint visible"), *PropertyName.ToString()),
				Property->HasAnyPropertyFlags(CPF_BlueprintVisible));
		}
	}
	return !HasAnyErrors();
}

bool FCatFishingAttemptSnapshotContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FCatFishingAttemptSnapshot Snapshot;
	TestFalse(TEXT("attempt request id defaults invalid"), Snapshot.RequestId.IsValid());
	TestFalse(TEXT("attempt session id defaults invalid"), Snapshot.FishingSessionId.IsValid());
	TestFalse(TEXT("attempt cast id defaults invalid"), Snapshot.CastAttemptId.IsValid());
	TestNull(TEXT("attempt fisher defaults null"), Snapshot.FisherPlayerState);
	TestNull(TEXT("attempt rod defaults null"), Snapshot.RodActor);
	TestTrue(TEXT("attempt rod definition defaults None"), Snapshot.RodDefinitionId.IsNone());
	TestTrue(TEXT("attempt float definition defaults None"), Snapshot.FloatDefinitionId.IsNone());
	TestTrue(TEXT("attempt bait definition defaults None"), Snapshot.BaitDefinitionId.IsNone());
	TestEqual(TEXT("attempt reservation revision defaults zero"), Snapshot.EquipmentReservationRevision, int64{0});
	TestEqual(TEXT("attempt rod revision defaults zero"), Snapshot.RodActorRevision, int64{0});
	TestEqual(TEXT("attempt landing defaults zero"), Snapshot.ServerCorrectedLandingWorldPoint, FVector::ZeroVector);
	TestTrue(TEXT("attempt water region defaults invalid"), !Snapshot.WaterRegion.IsValid());
	TestEqual(TEXT("attempt seed defaults zero"), Snapshot.ServerRandomSeed, uint64{0});
	const UScriptStruct* Struct = FCatFishingAttemptSnapshot::StaticStruct();
	const FObjectPropertyBase* RodProperty = FindFProperty<FObjectPropertyBase>(Struct, TEXT("RodActor"));
	const FObjectPropertyBase* FisherProperty = FindFProperty<FObjectPropertyBase>(Struct, TEXT("FisherPlayerState"));
	const FUInt64Property* SeedProperty = FindFProperty<FUInt64Property>(Struct, TEXT("ServerRandomSeed"));
	TestNotNull(TEXT("attempt rod property exists"), RodProperty);
	TestNotNull(TEXT("attempt fisher property exists"), FisherProperty);
	TestNotNull(TEXT("attempt seed property exists"), SeedProperty);
	if (RodProperty && FisherProperty && SeedProperty)
	{
		TestTrue(TEXT("attempt rod property is typed"), RodProperty->PropertyClass == ACatFishingRodActor::StaticClass());
		TestTrue(TEXT("attempt fisher property is typed"), FisherProperty->PropertyClass == APlayerState::StaticClass());
		TestFalse(TEXT("attempt seed is not Blueprint visible"), SeedProperty->HasAnyPropertyFlags(CPF_BlueprintVisible));
	}
	return !HasAnyErrors();
}

bool FCatFishingRodMutationAndLineContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const UCatFishingPresentationSettings* Settings = GetDefault<UCatFishingPresentationSettings>();
	ACatFishingHookActor* Hook = GetMutableDefault<ACatFishingHookActor>();
	const USceneComponent* LineStartAnchor = Cast<USceneComponent>(
		Hook->GetDefaultSubobjectByName(TEXT("FishingLineStartAnchor")));
	const UCatFishingLineCurveComponent* Curve = Cast<UCatFishingLineCurveComponent>(Hook->GetDefaultSubobjectByName(TEXT("FishingLineCurve")));
	TestNotNull(TEXT("hook owns a smoothed fishing-line start anchor"), LineStartAnchor);
	TestNotNull(TEXT("hook owns a native fishing-line curve"), Curve);
	TestNull(TEXT("hook no longer creates a legacy Cable subobject"), Hook->GetDefaultSubobjectByName(TEXT("FishingLine")));
	if (LineStartAnchor)
	{
		TestTrue(TEXT("line start anchor is world-space independent from replicated hook jumps"),
			LineStartAnchor->IsUsingAbsoluteLocation());
	}
	if (Curve)
	{
		TestFalse(TEXT("visual curve itself is not replicated"), Curve->GetIsReplicated());
		TestFalse(TEXT("visual curve has no independent simulation tick"), Curve->PrimaryComponentTick.bCanEverTick);
		TestEqual(TEXT("visual curve collision stays disabled"), Curve->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		TestTrue(TEXT("visual curve starts at the smoothing anchor"), Curve->GetAttachParent() == LineStartAnchor);
	}

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("create line presentation game world"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	if (UWorld* World = WorldWrapper.GetTestWorld())
	{
		ACatFishingRodActor* Rod = World->SpawnActor<ACatFishingRodActor>();
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = Rod;
		UClass* FormalHookClass = Settings->HookActorClass.LoadSynchronous();
		TestNotNull(TEXT("formal configured hook Blueprint loads after curve migration"), FormalHookClass);
		if (!FormalHookClass) return false;
		ACatFishingHookActor* RuntimeHook = World->SpawnActor<ACatFishingHookActor>(
			FormalHookClass, FTransform::Identity, SpawnParameters);
		TestNotNull(TEXT("spawn runtime hook for curve binding"), RuntimeHook);
		WorldWrapper.BeginPlayInTestWorld();
		if (RuntimeHook)
		{
			TestTrue(TEXT("authority identity publishes the line presentation state"),
				RuntimeHook->InitializeAuthoritativeIdentity(FGuid::NewGuid(), FGuid::NewGuid()));
			const FVector LandingPoint(500.0, 0.0, 0.0);
			TestTrue(TEXT("successful landing publishes the pre-fight line baseline"),
				RuntimeHook->FinalizeAuthoritativeLandingOnce(true, LandingPoint));
			TestEqual(TEXT("landed paid line starts at the physical rod-tip distance"),
				RuntimeHook->GetPresentationState().PaidOutLineLengthCentimeters, 500.0, 0.01);
			TestEqual(TEXT("landed line starts without manufactured slack"),
				RuntimeHook->GetPresentationState().SlackLineLengthCentimeters, 0.0, 0.01);
			TestTrue(TEXT("landed line starts taut"), RuntimeHook->GetPresentationState().bLineTaut);
			TestTrue(TEXT("authority can publish slack curve shape"),
				RuntimeHook->SetFishingLinePresentationFromAuthority(600.0, 500.0, 100.0, 0.25f, false));
			const UCatFishingLineCurveComponent* RuntimeCurve = Cast<UCatFishingLineCurveComponent>(
				RuntimeHook->GetDefaultSubobjectByName(TEXT("FishingLineCurve")));
			TestNotNull(TEXT("runtime hook keeps its curve"), RuntimeCurve);
			if (RuntimeCurve)
			{
				TestTrue(TEXT("runtime curve does not jump directly to a new paid out length"),
					RuntimeCurve->GetCurveLengthCentimeters() < 600.0);
				for (int32 TickIndex = 0; TickIndex < 120; ++TickIndex)
				{
					WorldWrapper.TickTestWorld(1.0f / 60.0f);
				}
				TestTrue(TEXT("smoothed runtime curve converges to paid out length"),
					FMath::IsNearlyEqual(RuntimeCurve->GetCurveLengthCentimeters(), 600.0, 0.5));
				const TArray<FVector>& Points = RuntimeCurve->GetCurveWorldPoints();
				TestEqual(TEXT("runtime curve uses configured subdivision"), Points.Num(), Settings->FishingLineCurveSegments + 1);
				if (Points.Num() > 1)
				{
					TestTrue(TEXT("curve ends at the rod tip"), Points.Last().Equals(Rod->GetRodTipWorldTransform().GetLocation(), 0.1));
					TestTrue(TEXT("actual slack creates downward curvature"), Points[Points.Num() / 2].Z < -1.0);
				}
				const auto BeforeVisualMotion = RuntimeHook->GetPresentationState();
				Rod->SetActorLocation(FVector(1000.0, 100.0, 200.0));
				WorldWrapper.TickTestWorld(1.0f / 30.0f);
				TestEqual(TEXT("visual endpoint motion cannot change authoritative paid line"),
					RuntimeHook->GetPresentationState().PaidOutLineLengthCentimeters, BeforeVisualMotion.PaidOutLineLengthCentimeters);
				TestEqual(TEXT("visual endpoint motion cannot change authoritative tension"),
					RuntimeHook->GetPresentationState().NormalizedTension, BeforeVisualMotion.NormalizedTension);
				RuntimeHook->SetOwner(nullptr);
				// 权威状态刷新复用与复制回调相同的端点重接入口。
				RuntimeHook->SetFishingLinePresentationFromAuthority(600.0, 500.0, 100.0, 0.25f, false);
				TestFalse(TEXT("curve hides when rod owner is lost"), RuntimeCurve->IsVisible());
				TestEqual(TEXT("curve clears stale geometry when rod owner is lost"), RuntimeCurve->GetNumSections(), 0);
				RuntimeHook->SetOwner(Rod);
				RuntimeHook->SetFishingLinePresentationFromAuthority(600.0, 500.0, 100.0, 0.25f, false);
				TestTrue(TEXT("curve returns when rod owner resolves again"), RuntimeCurve->IsVisible());
			}
			USceneComponent* HookVisualRoot = CatFishingActorContractTest::FindSceneComponent(RuntimeHook, TEXT("VisualRoot"));
			TestNotNull(TEXT("runtime hook owns visual root"), HookVisualRoot);
			if (HookVisualRoot)
			{
				const FVector BaseRelativeLocation = HookVisualRoot->GetRelativeLocation();
				RuntimeHook->SetPresentationBobOffset(12.0f);
				TestEqual(TEXT("bob offset only adds local Z"), HookVisualRoot->GetRelativeLocation(),
					BaseRelativeLocation + FVector(0.0, 0.0, 12.0));
				RuntimeHook->SetPresentationBobOffset(0.0f);
				TestEqual(TEXT("zero bob offset restores visual base"), HookVisualRoot->GetRelativeLocation(), BaseRelativeLocation);
			}
		}
	}
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
