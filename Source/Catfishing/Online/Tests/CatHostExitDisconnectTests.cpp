#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Collection/CatRunImprintService.h"
#include "Engine/World.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "GameFramework/PlayerState.h"
#include "OnlineSubsystemTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatHostExitDisconnectTest,
	"Catfishing.Online.HostExit.NoAcknowledgementWait",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 退出边界回归：在真实 World 服务中制造未确认记录，分别覆盖本地和有远端玩家的清理。
// 请求必须同步 Ready、关闭玩法并保留记录的未确认事实；重复请求不得重复改变 Revision，非法请求不得清理。
bool FCatHostExitDisconnectTest::RunTest(const FString& Parameters)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game)) return false;
	TestWorld.ForwardErrorMessages(this);
	UWorld* World = TestWorld.GetTestWorld();
	UCatRunImprintService* Imprint = World->GetSubsystem<UCatRunImprintService>();
	if (!TestNotNull(TEXT("Production imprint service exists"), Imprint)) return false;
	TestTrue(TEXT("Create an actual unacknowledged grant"),
		Imprint->RecordCommittedUnlock(TEXT("HostExitTestUnlock"), TEXT("HostExitRecipient")).IsValid());

	for (int32 Scenario = 0; Scenario < 2; ++Scenario)
	{
		ACatfishingGameModeBase* Mode = World->SpawnActor<ACatfishingGameModeBase>();
		if (!TestNotNull(TEXT("Production GameMode exists"), Mode)) return false;
		Mode->bRunCommandsOpen = true;
		ACatfishingPlayerController* Remote = nullptr;
		if (Scenario == 1)
		{
			Remote = World->SpawnActor<ACatfishingPlayerController>();
			APlayerState* State = World->SpawnActor<APlayerState>();
			const FUniqueNetIdRef NetId = FUniqueNetIdString::Create(TEXT("HostExitRemote"), FName(TEXT("CAT_TEST")));
			State->SetUniqueId(FUniqueNetIdRepl(NetId));
			Remote->PlayerState = State;
			auto& Record = Mode->AdmissionRecords.Add(TEXT("HostExitRemote"));
			Record.Phase = ACatfishingGameModeBase::EAdmissionPhase::Active;
			Record.Controller = Remote;
		}
		FCatRunTeardownRequest Request;
		TestEqual(TEXT("Invalid request rejected"), Mode->RequestRunTeardown(Request).Status, ECatRunTeardownStatus::Failed);
		TestTrue(TEXT("Invalid request does not close gameplay"), Mode->bRunCommandsOpen);
		Request.RequestId = FGuid::NewGuid();
		Request.OperationEpoch = 7;
		AddExpectedMessage(TEXT("Event=run_teardown_unconfirmed_grants"), EAutomationExpectedMessageFlags::Contains, 1);
		const FCatRunTeardownResult Result = Mode->RequestRunTeardown(Request);
		TestEqual(TEXT("Unconfirmed grant and remote never block exit"), Result.Status, ECatRunTeardownStatus::Ready);
		TestEqual(TEXT("Result carries original request"), Result.RequestId, Request.RequestId);
		TestEqual(TEXT("Result carries original epoch"), Result.OperationEpoch, Request.OperationEpoch);
		TestFalse(TEXT("Gameplay commands closed"), Mode->bRunCommandsOpen);
		TestTrue(TEXT("Teardown complete published"), Mode->GetRunPublicState().bTeardownComplete);
		TestEqual(TEXT("Exit never fabricates durable grant ACK"), Imprint->GetPendingGrantAckCount(), 1);
		const int64 Revision = Mode->GetRunPublicState().Revision;
		TestEqual(TEXT("Same request replays ready"), Mode->RequestRunTeardown(Request).Status, ECatRunTeardownStatus::Ready);
		Request.RequestId = FGuid::NewGuid();
		++Request.OperationEpoch;
		TestEqual(TEXT("Travel retry reuses completed cleanup"), Mode->RequestRunTeardown(Request).Status, ECatRunTeardownStatus::Ready);
		TestEqual(TEXT("Retries do not repeat cleanup"), Mode->GetRunPublicState().Revision, Revision);
		Mode->Destroy();
		if (Remote) Remote->Destroy();
	}
	return !HasAnyErrors();
}

#endif
