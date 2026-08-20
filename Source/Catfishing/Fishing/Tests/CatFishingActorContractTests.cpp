#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "UObject/UnrealType.h"

#include "CableComponent.h"
#include "Animation/AnimMontage.h"
#include "Character/CatCharacter.h"
#include "Components/SceneComponent.h"
#include "Fishing/Actors/CatFishEncounterActor.h"
#include "Fishing/Actors/CatFishingActorTypes.h"
#include "Fishing/Actors/CatFishingHookActor.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/Components/CatFishingLineComponent.h"
#include "Fishing/CatFishingTypes.h"
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
		TestFalse(FString::Printf(TEXT("%s does not tick"), *CDO->GetClass()->GetName()), CDO->PrimaryActorTick.bCanEverTick);
	}
	const UFunction* CastMontageFunction = ACatCharacter::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(ACatCharacter, PlayFishingCastMontageFromPresentation));
	TestNotNull(TEXT("character exposes the replicated-state cast montage presentation entry"), CastMontageFunction);
	if (CastMontageFunction)
	{
		TestTrue(TEXT("cast montage entry is cosmetic"), CastMontageFunction->HasAnyFunctionFlags(FUNC_BlueprintCosmetic));
	}
	const UCatFishingPresentationSettings* PresentationSettings = GetDefault<UCatFishingPresentationSettings>();
	TestFalse(TEXT("cast montage is configured"), PresentationSettings->CastMontage.IsNull());
	TestNotNull(TEXT("configured cast montage resolves"), PresentationSettings->CastMontage.LoadSynchronous());

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("create actor contract game world"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	UWorld* World = WorldWrapper.GetTestWorld();
	if (World)
	{
		CatFishingActorContractTest::TestPresentationReplication(*this, World->SpawnActor<ACatFishingRodActor>(), TEXT("PresentationState"));
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
	USceneComponent* Grip = CatFishingActorContractTest::FindSceneComponent(Rod, TEXT("GripAnchor"));
	TestNotNull(TEXT("SceneRoot exists"), SceneRoot);
	TestNotNull(TEXT("VisualRoot exists"), VisualRoot);
	TestNotNull(TEXT("RodTipAnchor exists"), RodTip);
	TestNotNull(TEXT("StandAnchor exists"), Stand);
	TestNotNull(TEXT("GripAnchor exists"), Grip);
	if (!SceneRoot || !VisualRoot || !RodTip || !Stand || !Grip)
	{
		return false;
	}
	TestEqual(TEXT("VisualRoot attaches to SceneRoot"), VisualRoot->GetAttachParent(), SceneRoot);
	for (USceneComponent* Anchor : { RodTip, Stand, Grip })
	{
		TestEqual(TEXT("canonical anchor attaches to SceneRoot"), Anchor->GetAttachParent(), SceneRoot);
		TestFalse(TEXT("canonical anchor cannot be edited when inherited"), Anchor->bEditableWhenInherited);
	}
	TestTrue(TEXT("VisualRoot remains editable when inherited"), VisualRoot->bEditableWhenInherited);

	Rod->SetActorTransform(FTransform(FRotator(0.0, 35.0, 0.0), FVector(100.0, 200.0, 300.0)));
	const FTransform ExpectedTip = Rod->GetRodTipWorldTransform();
	const FTransform ExpectedStand = Rod->GetStandWorldTransform();
	const FTransform ExpectedGrip = Rod->GetGripWorldTransform();
	VisualRoot->SetRelativeLocation(FVector(900.0, 0.0, 0.0));
	RodTip->SetRelativeLocation(FVector(-500.0, 0.0, 0.0));
	Stand->SetRelativeLocation(FVector(-400.0, 0.0, 0.0));
	Grip->SetRelativeLocation(FVector(-300.0, 0.0, 0.0));
	TestTrue(TEXT("tip getter ignores component movement"), Rod->GetRodTipWorldTransform().Equals(ExpectedTip));
	TestTrue(TEXT("stand getter ignores component movement"), Rod->GetStandWorldTransform().Equals(ExpectedStand));
	TestTrue(TEXT("grip getter ignores component movement"), Rod->GetGripWorldTransform().Equals(ExpectedGrip));
	Rod->SetActorLocation(FVector(700.0, 800.0, 900.0));
	TestTrue(TEXT("tip getter follows actor transform"), Rod->GetRodTipWorldTransform().Equals(Rod->GetActorTransform()));
	TestTrue(TEXT("stand getter follows actor transform"), Rod->GetStandWorldTransform().Equals(Rod->GetActorTransform()));
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
	TestNotNull(TEXT("identity actors spawn"), Rod);
	TestNotNull(TEXT("identity owner spawns"), Owner);
	if (!Rod || !Hook || !Fish || !Owner)
	{
		return false;
	}
	const FGuid RodId = FGuid::NewGuid();
	const FGuid SessionId = FGuid::NewGuid();
	const FGuid AttemptId = FGuid::NewGuid();
	TestFalse(TEXT("rod rejects invalid identity"), Rod->InitializeAuthoritativeIdentity(FGuid(), TEXT("Rod"), NAME_None, Owner, nullptr, false, false));
	TestTrue(TEXT("rod accepts first identity"), Rod->InitializeAuthoritativeIdentity(RodId, TEXT("Rod"), TEXT("SkinA"), Owner, nullptr, true, false));
	const FCatFishingRodPresentationState RodFirst = Rod->GetPresentationState();
	TestEqual(TEXT("rod starts at revision one"), RodFirst.RodActorRevision, int64{1});
	TestTrue(TEXT("rod exact identity replay succeeds"), Rod->InitializeAuthoritativeIdentity(RodId, TEXT("Rod"), TEXT("SkinB"), Owner, Owner, false, true));
	TestEqual(TEXT("rod replay preserves state"), Rod->GetPresentationState().RodSkinDefinitionId, RodFirst.RodSkinDefinitionId);
	TestFalse(TEXT("rod rejects changed immutable identity"), Rod->InitializeAuthoritativeIdentity(FGuid::NewGuid(), TEXT("Rod"), NAME_None, Owner, nullptr, false, false));
	TestTrue(TEXT("hook accepts first identity"), Hook->InitializeAuthoritativeIdentity(SessionId, AttemptId));
	TestTrue(TEXT("hook exact identity replay succeeds"), Hook->InitializeAuthoritativeIdentity(SessionId, AttemptId));
	TestFalse(TEXT("hook rejects same session and attempt ids"), Hook->InitializeAuthoritativeIdentity(SessionId, SessionId));
	TestTrue(TEXT("fish accepts first identity"), Fish->InitializeAuthoritativeIdentity(SessionId, AttemptId, TEXT("Fish"), 12.0));
	TestTrue(TEXT("fish exact identity replay succeeds"), Fish->InitializeAuthoritativeIdentity(SessionId, AttemptId, TEXT("Fish"), 99.0));
	TestEqual(TEXT("fish replay preserves line length"), Fish->GetPresentationState().CurrentLineLength, 12.0);
	const double NonFiniteLineLength = TNumericLimits<double>::Max() * 2.0;
	TestFalse(TEXT("fish rejects non-finite line length"), Fish->InitializeAuthoritativeIdentity(FGuid::NewGuid(), FGuid::NewGuid(), TEXT("Fish"), NonFiniteLineLength));
	ACatFishingHookActor* ClientHook = World->SpawnActor<ACatFishingHookActor>();
	ClientHook->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("client hook rejects authority initialization"), ClientHook->InitializeAuthoritativeIdentity(FGuid::NewGuid(), FGuid::NewGuid()));
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
	const ACatFishEncounterActor* FishCDO = GetDefault<ACatFishEncounterActor>();
	TestTrue(TEXT("fish replicates"), FishCDO->GetIsReplicated());
	TestTrue(TEXT("fish replicates movement"), FishCDO->IsReplicatingMovement());
	TestFalse(TEXT("fish does not tick"), FishCDO->PrimaryActorTick.bCanEverTick);
	TestFalse(TEXT("fish is not always relevant"), FishCDO->bAlwaysRelevant);
	TestFalse(TEXT("fish does not use owner relevancy"), FishCDO->bNetUseOwnerRelevancy);
	TestFalse(TEXT("fish is not owner-only"), FishCDO->bOnlyRelevantToOwner);
	const UScriptStruct* StateStruct = FCatFishEncounterPresentationState::StaticStruct();
	const TSet<FName> ExpectedFields = { TEXT("FishingSessionId"), TEXT("CastAttemptId"), TEXT("FishDefinitionId"), TEXT("MotionIntent"), TEXT("CurrentLineLength") };
	for (TFieldIterator<FProperty> It(StateStruct); It; ++It)
	{
		TestTrue(FString::Printf(TEXT("fish state field %s is whitelisted"), *It->GetName()), ExpectedFields.Contains(It->GetFName()));
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
	const UCatFishingLineComponent* Line = GetDefault<UCatFishingLineComponent>();
	TestFalse(TEXT("line component does not tick"), Line->PrimaryComponentTick.bCanEverTick);
	TestFalse(TEXT("line component does not replicate gameplay state"), Line->GetIsReplicated());
	ACatFishingHookActor* Hook = GetMutableDefault<ACatFishingHookActor>();
	const UCableComponent* Cable = Cast<UCableComponent>(Hook->GetDefaultSubobjectByName(TEXT("FishingLine")));
	TestNotNull(TEXT("hook owns a native fishing-line cable"), Cable);
	if (Cable)
	{
		TestFalse(TEXT("visual cable itself is not replicated"), Cable->GetIsReplicated());
		TestTrue(TEXT("visual cable has both endpoints fixed"), Cable->bAttachStart && Cable->bAttachEnd);
		TestFalse(TEXT("visual cable collision stays disabled"), Cable->bEnableCollision);
	}

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("create line presentation game world"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	if (UWorld* World = WorldWrapper.GetTestWorld())
	{
		ACatFishingRodActor* Rod = World->SpawnActor<ACatFishingRodActor>();
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = Rod;
		ACatFishingHookActor* RuntimeHook = World->SpawnActor<ACatFishingHookActor>(
			ACatFishingHookActor::StaticClass(), FTransform::Identity, SpawnParameters);
		TestNotNull(TEXT("spawn runtime hook for cable binding"), RuntimeHook);
		WorldWrapper.BeginPlayInTestWorld();
		if (RuntimeHook)
		{
			TestTrue(TEXT("authority identity publishes the line presentation state"),
				RuntimeHook->InitializeAuthoritativeIdentity(FGuid::NewGuid(), FGuid::NewGuid()));
			const UCableComponent* RuntimeCable = Cast<UCableComponent>(
				RuntimeHook->GetDefaultSubobjectByName(TEXT("FishingLine")));
			TestNotNull(TEXT("runtime hook keeps its cable"), RuntimeCable);
			if (RuntimeCable)
			{
				TestTrue(TEXT("runtime cable attaches to the replicated rod owner"),
					RuntimeCable->GetAttachedActor() == Rod);
			}
		}
	}
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
