#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

#include "Fishing/CatFishingGameplayTags.h"
#include "Fishing/CatFishingTypes.h"
#include "Fishing/Integration/CatFishingCommandTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingNativeEventTagsContractTest,
	"Catfishing.Unit.Fishing.Contracts.NativeEventTagsAreRegisteredAndExact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Protects the StateTree event boundary: a renamed or unregistered native tag would silently stop transitions.
bool FCatFishingNativeEventTagsContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	struct FExpectedTag
	{
		const TCHAR* Name;
		const FNativeGameplayTag* Tag;
	};

	const FExpectedTag ExpectedTags[] = {
		{ TEXT("Cat.Fishing.Event.CastLanded"), &CatFishingGameplayTags::CastLanded },
		{ TEXT("Cat.Fishing.Event.CastFailed"), &CatFishingGameplayTags::CastFailed },
		{ TEXT("Cat.Fishing.Event.ProbeTriggered"), &CatFishingGameplayTags::ProbeTriggered },
		{ TEXT("Cat.Fishing.Event.ProbeCompleted"), &CatFishingGameplayTags::ProbeCompleted },
		{ TEXT("Cat.Fishing.Event.FishSelectionFailed"), &CatFishingGameplayTags::FishSelectionFailed },
		{ TEXT("Cat.Fishing.Event.EarlyHook"), &CatFishingGameplayTags::EarlyHook },
		{ TEXT("Cat.Fishing.Event.HookAccepted"), &CatFishingGameplayTags::HookAccepted },
		{ TEXT("Cat.Fishing.Event.WindowExpired"), &CatFishingGameplayTags::WindowExpired },
		{ TEXT("Cat.Fishing.Event.FishStaminaDepleted"), &CatFishingGameplayTags::FishStaminaDepleted },
		{ TEXT("Cat.Fishing.Event.CatStaminaDepleted"), &CatFishingGameplayTags::CatStaminaDepleted },
		{ TEXT("Cat.Fishing.Event.CatOverpowered"), &CatFishingGameplayTags::CatOverpowered },
		{ TEXT("Cat.Fishing.Event.RodBroken"), &CatFishingGameplayTags::RodBroken },
		{ TEXT("Cat.Fishing.Event.AutoHaulReachedShore"), &CatFishingGameplayTags::AutoHaulReachedShore },
		{ TEXT("Cat.Fishing.Event.AutoHaulFailed"), &CatFishingGameplayTags::AutoHaulFailed },
		{ TEXT("Cat.Fishing.Event.ScoopCommitted"), &CatFishingGameplayTags::ScoopCommitted },
		{ TEXT("Cat.Fishing.Event.Interrupted"), &CatFishingGameplayTags::Interrupted },
	};

	for (const FExpectedTag& Expected : ExpectedTags)
	{
		TestTrue(FString::Printf(TEXT("%s is registered"), Expected.Name), Expected.Tag->GetTag().IsValid());
		TestEqual(FString::Printf(TEXT("%s is exact"), Expected.Name), Expected.Tag->GetTag().ToString(), FString(Expected.Name));
	}

	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingDefaultsContractTest,
	"Catfishing.Unit.Fishing.Contracts.PhaseOutcomeAndCommandDefaultsAreFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Protects fail-closed construction: new commands and results must never look committed or ready by default.
bool FCatFishingDefaultsContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FCatFishingSessionSnapshot Snapshot;
	const FCatFishingCommandResult Result;
	TestEqual(TEXT("Session defaults to Created"), Snapshot.Phase, ECatFishingPhase::Created);
	TestEqual(TEXT("Outcome None ordinal is zero"), ECatFishingOutcome::None, static_cast<ECatFishingOutcome>(0));
	TestFalse(TEXT("Result is not committed by default"), Result.bCommitted);
	TestEqual(TEXT("Result defaults to no command"), Result.CommandType, ECatFishingCommandType::None);
	TestEqual(TEXT("Result defaults to dependency unavailable"), Result.Error, ECatFishingCommandError::DependencyUnavailable);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingConcurrencyFieldsContractTest,
	"Catfishing.Unit.Fishing.Contracts.BeginCastDiscreteAndReelingCommandsExposeDifferentConcurrencyFields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Protects command authority shape: cast uses rod/equipment revisions while reeling uses an ordered input stream.
bool FCatFishingConcurrencyFieldsContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FCatBeginCastCommand BeginCast;
	const FCatSetReelingCommand SetReeling;
	const FCatRodCommandContext RodContext;
	const FCatFishingSessionCommandContext SessionContext;
	const UScriptStruct* SessionCommandStructs[] = {
		FCatRequestHookCommand::StaticStruct(), FCatCancelFishingCommand::StaticStruct(),
		FCatRequestScoopCommand::StaticStruct(), FCatAssistFightCommand::StaticStruct()
	};
	const TCHAR* ReelingIdentityFields[] = {
		TEXT("RequestId"), TEXT("FishingSessionId"), TEXT("CastAttemptId"),
		TEXT("ActivationCorrelationId"), TEXT("InputSequence")
	};
	TestFalse(TEXT("Begin cast does not carry a session id"), FindFProperty<FProperty>(FCatBeginCastCommand::StaticStruct(), TEXT("FishingSessionId")) != nullptr);
	TestNotNull(TEXT("Begin cast has an equipment revision"), FindFProperty<FProperty>(FCatBeginCastCommand::StaticStruct(), TEXT("ExpectedEquipmentRevision")));
	TestNotNull(TEXT("Rod command context has a rod actor revision"), FindFProperty<FProperty>(FCatRodCommandContext::StaticStruct(), TEXT("ExpectedRodActorRevision")));
	TestEqual(TEXT("Discrete rod command revision defaults to zero"), RodContext.ExpectedEquipmentRevision, int64{ 0 });
	TestNotNull(TEXT("Fishing session command context has an expected revision"), FindFProperty<FProperty>(FCatFishingSessionCommandContext::StaticStruct(), TEXT("ExpectedRevision")));
	TestEqual(TEXT("Fishing session command expected revision defaults to zero"), SessionContext.ExpectedRevision, int64{ 0 });
	for (const UScriptStruct* CommandStruct : SessionCommandStructs)
	{
		TestNotNull(FString::Printf(TEXT("%s carries session command context"), *CommandStruct->GetName()), FindFProperty<FProperty>(CommandStruct, TEXT("Context")));
	}
	for (const TCHAR* Field : ReelingIdentityFields)
	{
		TestNotNull(FString::Printf(TEXT("Reeling carries %s"), Field), FindFProperty<FProperty>(FCatSetReelingCommand::StaticStruct(), Field));
	}
	TestFalse(TEXT("Reeling does not carry an expected revision"), FindFProperty<FProperty>(FCatSetReelingCommand::StaticStruct(), TEXT("ExpectedRevision")) != nullptr);
	TestEqual(TEXT("Reeling input sequence defaults to zero"), SetReeling.InputSequence, int64{ 0 });
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingLegacyScoopAdapterContractTest,
	"Catfishing.Unit.Fishing.Contracts.LegacyScoopAdapterCarriesExplicitSessionIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Protects legacy Scoop routing: its session id is supplied by the old API, not stored in FCatScoopCommand.
bool FCatFishingLegacyScoopAdapterContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCatScoopCommand LegacyCommand;
	LegacyCommand.Context.RequestId = FGuid::NewGuid();
	LegacyCommand.Context.ExpectedRevision = 17;
	const FGuid FishingSessionId = FGuid::NewGuid();
	const FCatFishingSessionCommandContext Context = MakeFishingSessionCommandContext(FishingSessionId, LegacyCommand);
	TestEqual(TEXT("Adapter preserves legacy Scoop request id"), Context.RequestId, LegacyCommand.Context.RequestId);
	TestEqual(TEXT("Adapter preserves legacy Scoop expected revision"), Context.ExpectedRevision, LegacyCommand.Context.ExpectedRevision);
	TestEqual(TEXT("Adapter carries explicit legacy Scoop session id"), Context.FishingSessionId, FishingSessionId);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingStructuredResultContractTest,
	"Catfishing.Unit.Fishing.Contracts.StructuredResultCarriesCurrentServerConcurrencyIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Protects retry and reconciliation: the unified result must carry every current server concurrency identity.
bool FCatFishingStructuredResultContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FCatFishingCommandResult Result;
	const UScriptStruct* ResultStruct = FCatFishingCommandResult::StaticStruct();
	const TCHAR* RequiredFields[] = {
		TEXT("FishingSessionId"), TEXT("Revision"), TEXT("SnapshotSequence"), TEXT("PhaseEpoch"),
		TEXT("CastAttemptId"), TEXT("RodActorId"), TEXT("RodActorRevision"), TEXT("EquipmentRevision"),
		TEXT("SuggestedFishingSessionId")
	};
	for (const TCHAR* Field : RequiredFields)
	{
		TestNotNull(FString::Printf(TEXT("Result exposes %s"), Field), FindFProperty<FProperty>(ResultStruct, Field));
	}
	TestFalse(TEXT("Default result has no suggested session"), Result.SuggestedFishingSessionId.IsValid());
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
