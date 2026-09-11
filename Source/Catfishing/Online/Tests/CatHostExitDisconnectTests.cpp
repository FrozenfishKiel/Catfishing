#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Collection/CatRunImprintService.h"
#include "Engine/World.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "OnlineSubsystemTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatHostExitDisconnectTest,
	"Catfishing.Online.HostExit.DisconnectBeforeAck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCatHostExitDisconnectTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	TestWorld.ForwardErrorMessages(this);
	UWorld* World = TestWorld.GetTestWorld();
	UCatRunImprintService* Imprint = World->GetSubsystem<UCatRunImprintService>();
	if (!TestNotNull(TEXT("Production imprint service exists"), Imprint)) return false;

	for (int32 Scenario = 0; Scenario < 3; ++Scenario)
	{
		const bool bPendingGrant = Scenario == 2;
		ACatfishingGameModeBase* Mode = World->SpawnActor<ACatfishingGameModeBase>();
		if (!TestNotNull(TEXT("Production GameMode exists"), Mode)) return false;
		Mode->bRunCommandsOpen = false; // Reproduce the post-save, post-teardown waiting state.
		Mode->ActiveHostExitRequestId = FGuid::NewGuid();
		Mode->ActiveHostExitOperationEpoch = 7;
		int32 CompletionCount = 0;
		const FGuid RequestId = Mode->ActiveHostExitRequestId;
		const FDelegateHandle Handle = Mode->OnRunTeardownCompleted().AddLambda(
			[this, Mode, RequestId, &CompletionCount](const FCatRunTeardownResult& Result)
			{
				++CompletionCount;
				TestEqual(TEXT("Completion keeps the original request"), Result.RequestId, RequestId);
				TestEqual(TEXT("Completion keeps the original epoch"), Result.OperationEpoch, int64(7));
				TestEqual(TEXT("Completion is ready"), Result.Status, ECatRunTeardownStatus::Ready);
				TestTrue(TEXT("All departed identities removed before notification"), Mode->AdmissionRecords.IsEmpty());
			});
		auto AddPlayer = [World, Mode](const TCHAR* Id, const bool bRegister)
		{
			APlayerController* Controller = World->SpawnActor<APlayerController>();
			APlayerState* State = World->SpawnActor<APlayerState>();
			const FUniqueNetIdRef NetId = FUniqueNetIdString::Create(Id, FName(TEXT("CAT_TEST")));
			State->SetUniqueId(FUniqueNetIdRepl(NetId));
			Controller->PlayerState = State;
			if (bRegister)
			{
				auto& Record = Mode->AdmissionRecords.Add(Id);
				Record.Phase = ACatfishingGameModeBase::EAdmissionPhase::Active;
				Record.Controller = Controller;
				Mode->PendingHostExitRemoteStableNetIds.Add(Id);
			}
			return Controller;
		};
		APlayerController* First = AddPlayer(TEXT("HostExitFirst"), true);
		APlayerController* Second = AddPlayer(TEXT("HostExitSecond"), true);
		APlayerController* Stale = AddPlayer(TEXT("HostExitFirst"), false);
		if (bPendingGrant)
		{
			AddExpectedMessage(TEXT("Event=run_teardown_grants_pending"), EAutomationExpectedMessageFlags::Contains, 2);
			TestTrue(TEXT("Create a real unacknowledged grant"),
				Imprint->RecordCommittedUnlock(TEXT("HostExitTestUnlock"), TEXT("HostExitFirst")).IsValid());
		}

		Mode->AcknowledgeHostExitClient(First, FGuid::NewGuid());
		TestEqual(TEXT("Wrong request cannot remove a remote"), Mode->PendingHostExitRemoteStableNetIds.Num(), 2);
		Mode->AcknowledgeHostExitClient(Stale, RequestId);
		TestEqual(TEXT("Stale controller ACK cannot remove the active connection"), Mode->PendingHostExitRemoteStableNetIds.Num(), 2);
		Mode->CompleteHostExitWait();
		TestEqual(TEXT("Completion cannot bypass remaining remotes"), CompletionCount, 0);
		AddExpectedMessage(TEXT("Event=identity_release_ignored"), EAutomationExpectedMessageFlags::Contains, 1);
		Mode->Logout(Stale);
		TestEqual(TEXT("Old controller cannot remove replacement's wait"), Mode->PendingHostExitRemoteStableNetIds.Num(), 2);
		if (Scenario == 1)
		{
			Mode->AcknowledgeHostExitClient(First, RequestId);
			TestEqual(TEXT("Valid ACK still resolves that remote before logout"), Mode->PendingHostExitRemoteStableNetIds.Num(), 1);
		}
		Mode->Logout(First);
		TestEqual(TEXT("Real disconnect resolves only that remote without RPC"), Mode->PendingHostExitRemoteStableNetIds.Num(), 1);
		TestEqual(TEXT("Other remote still blocks completion"), CompletionCount, 0);
		AddExpectedMessage(TEXT("Event=identity_release_ignored"), EAutomationExpectedMessageFlags::Contains, 1);
		Mode->Logout(First);
		TestEqual(TEXT("Duplicate logout cannot resolve another remote"), Mode->PendingHostExitRemoteStableNetIds.Num(), 1);
		if (!bPendingGrant)
		{
			// This isolated World deliberately has no environment provider; it is unrelated to departure.
			AddExpectedMessage(TEXT("Event=environment_evaluation_failed"), EAutomationExpectedMessageFlags::Contains, 1);
		}
		Mode->Logout(Second);
		TestEqual(TEXT("Last real departure clears remote wait"), Mode->PendingHostExitRemoteStableNetIds.Num(), 0);
		TestEqual(TEXT("Durable grant gate decides completion"), CompletionCount, bPendingGrant ? 0 : 1);
		TestEqual(TEXT("Public teardown state follows durable gate"), Mode->GetRunPublicState().bTeardownComplete, !bPendingGrant);
		Mode->AcknowledgeHostExitClient(Second, RequestId);
		Mode->NotifyHostExitGrantAckProgress();
		TestEqual(TEXT("Late ACK and repeat progress do not duplicate completion"), CompletionCount, bPendingGrant ? 0 : 1);
		if (bPendingGrant)
		{
			Mode->CompleteHostExitWait();
			TestEqual(TEXT("Completion cannot bypass durable grants"), CompletionCount, 0);
			TestEqual(TEXT("Disconnect does not invent durable grant ACK"), Imprint->GetPendingGrantAckCount(), 1);
		}
		Mode->OnRunTeardownCompleted().Remove(Handle);
		Mode->Destroy();
	}
	return !HasAnyErrors();
}

#endif
