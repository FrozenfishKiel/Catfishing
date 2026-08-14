#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "UObject/UnrealType.h"

#include "Fishing/CatFishingTypes.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/Actor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingCommandComponentRpcContractTest,
	"Catfishing.Unit.Fishing.CommandComponent.ClientResultRpcDeclaresReliableOwningControllerContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingCommandComponentMailboxTest,
	"Catfishing.Unit.Fishing.CommandComponent.RequestResultsAreCorrelatedDeduplicatedAndBounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingCommandComponentNativeSubobjectTest,
	"Catfishing.Unit.Fishing.CommandComponent.ControllerCreatesExactlyOneNativeDefaultComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

namespace CatFishingCommandComponentTest
{
	static FCatFishingCommandResult MakeResult(const FGuid RequestId, const int64 Revision)
	{
		FCatFishingCommandResult Result;
		Result.CommandType = ECatFishingCommandType::BeginCast;
		Result.bCommitted = true;
		Result.Error = ECatFishingCommandError::None;
		Result.RequestId = RequestId;
		Result.Revision = Revision;
		return Result;
	}

	static ACatfishingPlayerController* SpawnProjectController(
		FAutomationTestBase& Test,
		FTestWorldWrapper& WorldWrapper)
	{
		Test.TestTrue(TEXT("create command component game world"),
			WorldWrapper.CreateTestWorld(EWorldType::Game));
		WorldWrapper.ForwardErrorMessages(&Test);
		UWorld* World = WorldWrapper.GetTestWorld();
		Test.TestNotNull(TEXT("command component world exists"), World);
		ACatfishingPlayerController* Controller = World
			? World->SpawnActor<ACatfishingPlayerController>()
			: nullptr;
		Test.TestNotNull(TEXT("project controller spawns"), Controller);
		return Controller;
	}
}

// Catches losing the reliable owning-client RPC declaration, accepting an arbitrary Actor owner,
// or leaking the private result channel into the public Session snapshot.
bool FCatFishingCommandComponentRpcContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const UFunction* ClientRpc = UCatFishingCommandComponent::StaticClass()->FindFunctionByName(
		TEXT("ClientReceiveFishingCommandResult"));
	TestNotNull(TEXT("client result RPC is reflected"), ClientRpc);
	if (ClientRpc)
	{
		TestTrue(TEXT("client result RPC targets the owning client"),
			ClientRpc->HasAnyFunctionFlags(FUNC_NetClient));
		TestTrue(TEXT("client result RPC is reliable"),
			ClientRpc->HasAnyFunctionFlags(FUNC_NetReliable));
	}

	const UCatFishingCommandComponent* ComponentCDO = GetDefault<UCatFishingCommandComponent>();
	TestTrue(TEXT("command component replicates by default"), ComponentCDO->GetIsReplicated());
	TestFalse(TEXT("command component never ticks"), ComponentCDO->PrimaryComponentTick.bCanEverTick);

	FTestWorldWrapper WorldWrapper;
	ACatfishingPlayerController* Controller =
		CatFishingCommandComponentTest::SpawnProjectController(*this, WorldWrapper);
	UCatFishingCommandComponent* Component = Controller
		? Controller->GetFishingCommandComponent()
		: nullptr;
	TestNotNull(TEXT("project controller exposes its native command component"), Component);
	if (Component && Controller)
	{
		TestEqual(TEXT("native command component is owned by the project controller"),
			Component->GetOwner(), static_cast<AActor*>(Controller));
	}

	UWorld* World = WorldWrapper.GetTestWorld();
	UCatFishingCommandComponent* InvalidRequestRelay = Controller
		? NewObject<UCatFishingCommandComponent>(Controller, TEXT("InvalidRequestRelay"))
		: nullptr;
	TestNotNull(TEXT("invalid request relay exists"), InvalidRequestRelay);
	if (Controller && Component && InvalidRequestRelay)
	{
		Controller->AddInstanceComponent(InvalidRequestRelay);
		InvalidRequestRelay->RegisterComponent();
		FScriptDelegate InvalidRelayDelegate;
		InvalidRelayDelegate.BindUFunction(InvalidRequestRelay,
			TEXT("ClientReceiveFishingCommandResult"));
		TestTrue(TEXT("invalid request relay binds"), InvalidRelayDelegate.IsBound());
		Component->OnResultReceived.Add(InvalidRelayDelegate);

		const FCatFishingCommandResult InvalidResult =
			CatFishingCommandComponentTest::MakeResult(FGuid(), 8);
		Component->DeliverResultFromAuthority(InvalidResult);
		FCatFishingCommandResult InvalidReadBack;
		InvalidReadBack.bCommitted = true;
		InvalidReadBack.Error = ECatFishingCommandError::None;
		TestFalse(TEXT("invalid request id is not stored by the authority component"),
			Component->TryGetResult(FGuid(), InvalidReadBack));
		TestFalse(TEXT("invalid request id does not appear in the relay mailbox"),
			InvalidRequestRelay->TryGetResult(FGuid(), InvalidReadBack));
		Component->OnResultReceived.Remove(InvalidRelayDelegate);
	}

	ACatfishingPlayerController* SimulatedController = World
		? World->SpawnActor<ACatfishingPlayerController>()
		: nullptr;
	UCatFishingCommandComponent* SimulatedComponent = SimulatedController
		? SimulatedController->GetFishingCommandComponent()
		: nullptr;
	UCatFishingCommandComponent* SimulatedRelay = SimulatedController
		? NewObject<UCatFishingCommandComponent>(SimulatedController, TEXT("SimulatedProxyRelay"))
		: nullptr;
	TestNotNull(TEXT("simulated proxy controller exists"), SimulatedController);
	TestNotNull(TEXT("simulated proxy native component exists"), SimulatedComponent);
	TestNotNull(TEXT("simulated proxy relay exists"), SimulatedRelay);
	if (SimulatedController && SimulatedComponent && SimulatedRelay)
	{
		SimulatedController->SetRole(ROLE_SimulatedProxy);
		SimulatedController->AddInstanceComponent(SimulatedRelay);
		SimulatedRelay->RegisterComponent();
		FScriptDelegate SimulatedRelayDelegate;
		SimulatedRelayDelegate.BindUFunction(SimulatedRelay,
			TEXT("ClientReceiveFishingCommandResult"));
		TestTrue(TEXT("simulated proxy relay binds"), SimulatedRelayDelegate.IsBound());
		SimulatedComponent->OnResultReceived.Add(SimulatedRelayDelegate);

		const FGuid SimulatedRequestId = FGuid::NewGuid();
		SimulatedComponent->DeliverResultFromAuthority(
			CatFishingCommandComponentTest::MakeResult(SimulatedRequestId, 9));
		FCatFishingCommandResult SimulatedReadBack;
		TestFalse(TEXT("simulated proxy cannot store an authority delivery"),
			SimulatedComponent->TryGetResult(SimulatedRequestId, SimulatedReadBack));
		TestFalse(TEXT("simulated proxy authority rejection does not broadcast"),
			SimulatedRelay->TryGetResult(SimulatedRequestId, SimulatedReadBack));
	}

	AActor* PlainActor = World ? World->SpawnActor<AActor>() : nullptr;
	UCatFishingCommandComponent* UnsupportedComponent = PlainActor
		? NewObject<UCatFishingCommandComponent>(PlainActor, TEXT("UnsupportedFishingCommandComponent"))
		: nullptr;
	TestNotNull(TEXT("temporary non-controller command component exists"), UnsupportedComponent);
	if (PlainActor && UnsupportedComponent)
	{
		PlainActor->AddInstanceComponent(UnsupportedComponent);
		UnsupportedComponent->RegisterComponent();
		const FGuid RequestId = FGuid::NewGuid();
		UnsupportedComponent->DeliverResultFromAuthority(
			CatFishingCommandComponentTest::MakeResult(RequestId, 7));
		FCatFishingCommandResult ReadBack;
		ReadBack.bCommitted = true;
		ReadBack.Error = ECatFishingCommandError::None;
		TestFalse(TEXT("non-controller owner cannot store a result"),
			UnsupportedComponent->TryGetResult(RequestId, ReadBack));
		TestFalse(TEXT("failed lookup resets committed state"), ReadBack.bCommitted);
		TestEqual(TEXT("failed lookup resets to fail-closed error"),
			ReadBack.Error, ECatFishingCommandError::DependencyUnavailable);
	}

	const UScriptStruct* SnapshotStruct = FCatFishingSessionSnapshot::StaticStruct();
	for (TFieldIterator<FProperty> It(SnapshotStruct); It; ++It)
	{
		const FProperty* Property = *It;
		TestNotEqual(TEXT("public snapshot has no command component field name"),
			Property->GetFName(), FName(TEXT("FishingCommandComponent")));
		const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property);
		TestFalse(FString::Printf(TEXT("public snapshot field %s is not a command component object"),
			*Property->GetName()),
			ObjectProperty && ObjectProperty->PropertyClass
				&& ObjectProperty->PropertyClass->IsChildOf<UCatFishingCommandComponent>());
	}
	return !HasAnyErrors();
}

// Catches mailbox correlation regressions: overwriting the first result, rebroadcasting a duplicate,
// retaining more than 32 results, or broadcasting Consume/Reset side effects.
bool FCatFishingCommandComponentMailboxTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	ACatfishingPlayerController* Controller =
		CatFishingCommandComponentTest::SpawnProjectController(*this, WorldWrapper);
	UCatFishingCommandComponent* Component = Controller
		? Controller->GetFishingCommandComponent()
		: nullptr;
	if (!Controller || !Component)
	{
		return false;
	}

	TestEqual(TEXT("mailbox test uses standalone routing"), Controller->GetNetMode(), NM_Standalone);
	TestTrue(TEXT("mailbox test controller has authority"), Controller->HasAuthority());

	const FMulticastDelegateProperty* DelegateProperty =
		FindFProperty<FMulticastDelegateProperty>(UCatFishingCommandComponent::StaticClass(),
			GET_MEMBER_NAME_CHECKED(UCatFishingCommandComponent, OnResultReceived));
	const UFunction* ClientRpc = UCatFishingCommandComponent::StaticClass()->FindFunctionByName(
		TEXT("ClientReceiveFishingCommandResult"));
	TestNotNull(TEXT("result delegate property is reflected"), DelegateProperty);
	TestNotNull(TEXT("relay target RPC is reflected"), ClientRpc);
	if (!DelegateProperty || !DelegateProperty->SignatureFunction || !ClientRpc)
	{
		return false;
	}
	TFieldIterator<FProperty> DelegateParameterIt(DelegateProperty->SignatureFunction);
	TFieldIterator<FProperty> RpcParameterIt(ClientRpc);
	const FProperty* DelegateParameter = DelegateParameterIt ? *DelegateParameterIt : nullptr;
	const FProperty* RpcParameter = RpcParameterIt ? *RpcParameterIt : nullptr;
	TestNotNull(TEXT("delegate exposes its result parameter"), DelegateParameter);
	TestNotNull(TEXT("RPC exposes its result parameter"), RpcParameter);
	if (!DelegateParameter || !RpcParameter)
	{
		return false;
	}
	TestTrue(TEXT("delegate result is a const reference parameter"),
		DelegateParameter->HasAllPropertyFlags(CPF_Parm | CPF_ReferenceParm | CPF_ConstParm));
	TestTrue(TEXT("RPC result is a const reference parameter"),
		RpcParameter->HasAllPropertyFlags(CPF_Parm | CPF_ReferenceParm | CPF_ConstParm));
	TestTrue(TEXT("UHT marks the dynamic delegate reference as OutParm"),
		DelegateParameter->HasAnyPropertyFlags(CPF_OutParm));
	TestFalse(TEXT("UHT keeps the network RPC input free of OutParm"),
		RpcParameter->HasAnyPropertyFlags(CPF_OutParm));
	const FStructProperty* DelegateStructParameter = CastField<FStructProperty>(DelegateParameter);
	const FStructProperty* RpcStructParameter = CastField<FStructProperty>(RpcParameter);
	TestTrue(TEXT("delegate result uses the fishing command result struct"),
		DelegateStructParameter
			&& DelegateStructParameter->Struct == FCatFishingCommandResult::StaticStruct());
	TestTrue(TEXT("RPC result uses the fishing command result struct"),
		RpcStructParameter && RpcStructParameter->Struct == FCatFishingCommandResult::StaticStruct());
	const uint64 ParameterFlagDifference =
		(DelegateParameter->GetPropertyFlags() ^ RpcParameter->GetPropertyFlags()) & CPF_ParmFlags;
	TestEqual(TEXT("UHT delegate-versus-network-input flags differ only by OutParm"),
		ParameterFlagDifference, static_cast<uint64>(CPF_OutParm));
	++DelegateParameterIt;
	++RpcParameterIt;
	TestFalse(TEXT("delegate has exactly one parameter"),
		DelegateParameterIt && DelegateParameterIt->HasAnyPropertyFlags(CPF_Parm));
	TestFalse(TEXT("RPC has exactly one parameter"),
		RpcParameterIt && RpcParameterIt->HasAnyPropertyFlags(CPF_Parm));
	const uint64 SignatureIgnoreFlags =
		UFunction::GetDefaultIgnoredSignatureCompatibilityFlags() | CPF_OutParm;
	TestTrue(TEXT("delegate and RPC signatures are reflection-compatible after the required UHT OutParm distinction"),
		DelegateProperty->SignatureFunction->IsSignatureCompatibleWith(ClientRpc, SignatureIgnoreFlags));

	UCatFishingCommandComponent* RelayComponent = NewObject<UCatFishingCommandComponent>(
		Controller, TEXT("FishingCommandResultRelay"));
	TestNotNull(TEXT("second command component relay exists"), RelayComponent);
	if (!RelayComponent)
	{
		return false;
	}
	Controller->AddInstanceComponent(RelayComponent);
	RelayComponent->RegisterComponent();
	FScriptDelegate Relay;
	Relay.BindUFunction(RelayComponent, TEXT("ClientReceiveFishingCommandResult"));
	TestTrue(TEXT("reflection relay binds to the private client RPC"), Relay.IsBound());
	Component->OnResultReceived.Add(Relay);

	const FGuid RelayRequestId = FGuid::NewGuid();
	Component->DeliverResultFromAuthority(
		CatFishingCommandComponentTest::MakeResult(RelayRequestId, 1));
	FCatFishingCommandResult ReadBack;
	TestTrue(TEXT("first delivery synchronously enters the relay mailbox"),
		RelayComponent->TryGetResult(RelayRequestId, ReadBack));
	TestEqual(TEXT("relay preserves the delivered revision"), ReadBack.Revision, int64{ 1 });
	RelayComponent->ConsumeResult(RelayRequestId);
	Component->DeliverResultFromAuthority(
		CatFishingCommandComponentTest::MakeResult(RelayRequestId, 999));
	TestFalse(TEXT("duplicate delivery does not broadcast into the emptied relay mailbox"),
		RelayComponent->TryGetResult(RelayRequestId, ReadBack));
	TestTrue(TEXT("duplicate delivery preserves the first source result"),
		Component->TryGetResult(RelayRequestId, ReadBack));
	TestEqual(TEXT("first source result wins"), ReadBack.Revision, int64{ 1 });

	Component->ResetTransientCommandState();
	RelayComponent->ResetTransientCommandState();
	TArray<FGuid> RequestIds;
	RequestIds.Reserve(33);
	for (int32 Index = 0; Index < 33; ++Index)
	{
		const FGuid RequestId = FGuid::NewGuid();
		RequestIds.Add(RequestId);
		Component->DeliverResultFromAuthority(
			CatFishingCommandComponentTest::MakeResult(RequestId, 100 + Index));
	}

	ReadBack.bCommitted = true;
	ReadBack.Error = ECatFishingCommandError::None;
	TestFalse(TEXT("the thirty-third insert evicts the first result"),
		Component->TryGetResult(RequestIds[0], ReadBack));
	TestFalse(TEXT("evicted lookup resets committed state"), ReadBack.bCommitted);
	TestEqual(TEXT("evicted lookup resets fail-closed error"),
		ReadBack.Error, ECatFishingCommandError::DependencyUnavailable);
	for (int32 Index = 1; Index < RequestIds.Num(); ++Index)
	{
		TestTrue(FString::Printf(TEXT("result %d remains correlated"), Index + 1),
			Component->TryGetResult(RequestIds[Index], ReadBack));
		TestEqual(FString::Printf(TEXT("result %d preserves its value"), Index + 1),
			ReadBack.Revision, int64{ 100 + Index });
	}

	const int32 DuplicateIndex = 10;
	RelayComponent->ConsumeResult(RequestIds[DuplicateIndex]);
	Component->DeliverResultFromAuthority(
		CatFishingCommandComponentTest::MakeResult(RequestIds[DuplicateIndex], 5000));
	TestFalse(TEXT("duplicate bounded result does not rebroadcast"),
		RelayComponent->TryGetResult(RequestIds[DuplicateIndex], ReadBack));
	TestTrue(TEXT("duplicate bounded result remains readable at source"),
		Component->TryGetResult(RequestIds[DuplicateIndex], ReadBack));
	TestEqual(TEXT("duplicate bounded result cannot overwrite first value"),
		ReadBack.Revision, int64{ 100 + DuplicateIndex });

	const int32 ConsumedIndex = 11;
	RelayComponent->ConsumeResult(RequestIds[ConsumedIndex]);
	Component->ConsumeResult(RequestIds[ConsumedIndex]);
	TestFalse(TEXT("consumed source result is unavailable"),
		Component->TryGetResult(RequestIds[ConsumedIndex], ReadBack));
	TestFalse(TEXT("consume does not broadcast into relay"),
		RelayComponent->TryGetResult(RequestIds[ConsumedIndex], ReadBack));

	const int32 ResetIndex = 12;
	RelayComponent->ResetTransientCommandState();
	Component->ResetTransientCommandState();
	TestFalse(TEXT("reset clears all source results"),
		Component->TryGetResult(RequestIds[ResetIndex], ReadBack));
	TestFalse(TEXT("reset does not broadcast into relay"),
		RelayComponent->TryGetResult(RequestIds[ResetIndex], ReadBack));
	return !HasAnyErrors();
}

// Catches replacing the native default subobject with a runtime component, changing its stable name,
// or creating more than one native Fishing command component per project Controller.
bool FCatFishingCommandComponentNativeSubobjectTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	ACatfishingPlayerController* ControllerCDO = GetMutableDefault<ACatfishingPlayerController>();
	UCatFishingCommandComponent* CDOComponent = ControllerCDO
		? ControllerCDO->GetFishingCommandComponent()
		: nullptr;
	TestNotNull(TEXT("controller CDO owns a command component"), CDOComponent);
	if (CDOComponent)
	{
		TestEqual(TEXT("controller CDO component is native"),
			CDOComponent->CreationMethod, EComponentCreationMethod::Native);
		TestEqual(TEXT("controller CDO component has the stable name"),
			CDOComponent->GetFName(), FName(TEXT("FishingCommandComponent")));
		TestEqual(TEXT("named CDO default subobject matches the getter"),
			ControllerCDO->GetDefaultSubobjectByName(TEXT("FishingCommandComponent")),
			static_cast<UObject*>(CDOComponent));
	}

	FTestWorldWrapper WorldWrapper;
	ACatfishingPlayerController* Controller =
		CatFishingCommandComponentTest::SpawnProjectController(*this, WorldWrapper);
	UCatFishingCommandComponent* Component = Controller
		? Controller->GetFishingCommandComponent()
		: nullptr;
	TestNotNull(TEXT("spawned controller owns a command component"), Component);
	if (!Controller || !Component)
	{
		return false;
	}
	TestEqual(TEXT("spawned controller component is native"),
		Component->CreationMethod, EComponentCreationMethod::Native);
	TestEqual(TEXT("spawned controller component has the stable name"),
		Component->GetFName(), FName(TEXT("FishingCommandComponent")));
	TestEqual(TEXT("spawned named default subobject matches the getter"),
		Controller->GetDefaultSubobjectByName(TEXT("FishingCommandComponent")),
		static_cast<UObject*>(Component));

	TInlineComponentArray<UCatFishingCommandComponent*> Components(Controller);
	int32 NativeComponentCount = 0;
	for (const UCatFishingCommandComponent* Candidate : Components)
	{
		if (Candidate && Candidate->CreationMethod == EComponentCreationMethod::Native)
		{
			++NativeComponentCount;
		}
	}
	TestEqual(TEXT("controller enumerates exactly one native command component"), NativeComponentCount, 1);
	TestEqual(TEXT("controller has exactly one command component before test relays"), Components.Num(), 1);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
