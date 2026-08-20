#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

#include "Character/CatCharacter.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "OnlineSubsystemTypes.h"
#include "Framework/Core/CatFishingBoundaryContracts.h"
#include "Integration/Fishing/CatFishingBoundaryHash.h"
#include "Integration/Fishing/CatFishingOperationJournal.h"
#include "Integration/Fishing/CatFishingBoundarySubsystem.h"
#include "Integration/Fishing/CatFishingFightCursorLedger.h"

namespace CatFishingBoundaryContractTest
{
	// 请求头构造流程：用固定 Attempt、StableId 和 Revision 搭建可重复场景；调用方只替换 RequestId 来验证 Hash 是否错误绑定幂等键。
	static FCatFishingBoundaryRequestHeader MakeHeader(const FGuid RequestGuid)
	{
		FCatFishingBoundaryRequestHeader Header;
		Header.SchemaVersion = 1;
		Header.AttemptId.Value = FGuid(0x10000000, 0x20000000, 0x30000000, 0x40000000);
		Header.RequestId.Value = RequestGuid;
		Header.PrincipalId.CanonicalValue = TEXT("player:host-a");
		Header.ExpectedRevision = 7;
		return Header;
	}

	// 业务载荷构造流程：手写固定字节序列，避免测试期望复用 Hash 生产代码导致镜像断言。
	static TArray<uint8> MakeBusinessPayload(const uint8 LastByte)
	{
		return TArray<uint8>{0x43, 0x46, 0x53, LastByte};
	}

	// Hash 构造流程：生成 32 字节人工载荷，供 Journal 幂等测试直接比较，不依赖 Hash helper 的内部算法。
	static FCatFishingPayloadHash MakePayloadHash(const uint8 Seed)
	{
		FCatFishingPayloadHash Hash;
		Hash.Bytes.SetNum(32);
		for (int32 Index = 0; Index < Hash.Bytes.Num(); ++Index)
		{
			Hash.Bytes[Index] = static_cast<uint8>(Seed + Index);
		}
		return Hash;
	}

	// 展示流程：把 Hash 字节转成小写十六进制，专门服务 Golden 断言，避免测试只比较两个生产结果是否相等。
	static FString HashToLowerHex(const FCatFishingPayloadHash& Hash)
	{
		FString Hex;
		Hex.Reserve(Hash.Bytes.Num() * 2);
		for (const uint8 Byte : Hash.Bytes)
		{
			Hex += FString::Printf(TEXT("%02x"), Byte);
		}
		return Hex;
	}
	// 定义构造流程：创建一条只供本测试使用的运行期特殊饵定义，使 Boundary 能通过真实 EquipmentSettings 查询到消耗品。
	static UCatEquipmentDefinition* MakeSpecialBaitDefinition(const FName DefinitionId)
	{
		UCatEquipmentDefinition* Definition = NewObject<UCatEquipmentDefinition>(GetTransientPackage());
		if (Definition)
		{
			Definition->bEnableRuntimeDefinition = true;
			Definition->EquipmentDefinitionId = DefinitionId;
			Definition->Kind = ECatEquipmentKind::Bait;
			Definition->LoadoutSlotId = TEXT("BaitSlot");
			Definition->FunctionalRouteId = TEXT("BoundarySpecialBait");
			Definition->bRunConsumable = true;
			Definition->bSpecialBait = true;
		}
		return Definition;
	}
	// 耗材查询流程：按 DefinitionId 查找快照里的剩余数量；如果 Equipment 在数量归零后移除了栈，本测试把缺失视为 0，避免用数组下标假设内部存储形态。
	static int32 FindConsumableQuantityForTest(const FCatEquipmentLoadoutSnapshot& Snapshot, const FName DefinitionId)
	{
		for (const FCatRunConsumableStack& Stack : Snapshot.Consumables)
		{
			if (Stack.DefinitionId == DefinitionId)
			{
				return Stack.Quantity;
			}
		}
		return 0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingBoundaryHashIgnoresRequestIdTest,
	"Catfishing.FishingBoundary.Contract.PayloadHash.IgnoresRequestIdAndIncludesBusinessFields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingBoundaryJournalReplayTest,
	"Catfishing.FishingBoundary.Contract.OperationJournal.ReplaysSameRequestAndRejectsPayloadMismatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingBoundaryJournalCloseKeepsInboxTest,
	"Catfishing.FishingBoundary.Contract.OperationJournal.CloseRejectsOnlyNewRequestsAndKeepsAcceptedInbox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingBoundaryJournalCommitStabilityTest,
	"Catfishing.FishingBoundary.Contract.OperationJournal.CommitResultStabilizesTerminalInbox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingBoundaryStartCastSubsystemTest,
	"Catfishing.FishingBoundary.Contract.StartCast.SubsystemFailClosedContracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingBoundaryBaitAcceptedSubsystemTest,
	"Catfishing.FishingBoundary.Contract.BaitAccepted.SubsystemRejectsUnknownAttempt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingBoundaryBaitAcceptedReceiptSubsystemTest,
	"Catfishing.FishingBoundary.Contract.BaitAccepted.SubsystemIssuesOrdinaryReceiptAndReplays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingBoundarySpecialBaitConsumesOnceSubsystemTest,
	"Catfishing.FishingBoundary.Contract.BaitAccepted.SpecialBaitConsumesOnceAndReplays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingBoundaryFightAcceptedSubsystemTest,
	"Catfishing.FishingBoundary.Contract.FightAccepted.SubsystemRejectsUnknownAttempt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCatFishingBoundaryFightCursorLedgerTest,
	"Catfishing.FishingBoundary.Contract.Fight.CursorAndSealContracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
// 测试流程：
// 1. 构造两个只差 RequestId 的同语义请求，确认测试前提真的只改变幂等键。
// 2. 用同一 OperationKind 与同一业务载荷计算 Hash，断言 Hash 相等，证明 RequestId 没被错误纳入 canonical payload。
// 3. 改变业务载荷和 Revision 后分别重新计算，断言 Hash 改变，证明真正影响业务结果的字段仍会进入 Hash。
bool FCatFishingBoundaryHashIgnoresRequestIdTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FCatFishingBoundaryRequestHeader FirstHeader = CatFishingBoundaryContractTest::MakeHeader(
		FGuid(0x11111111, 0x22222222, 0x33333333, 0x44444444));
	FCatFishingBoundaryRequestHeader ReplayHeader = FirstHeader;
	ReplayHeader.RequestId.Value = FGuid(0x55555555, 0x66666666, 0x77777777, 0x88888888);
	TestNotEqual(TEXT("测试前提：两个请求使用不同 RequestId"), FirstHeader.RequestId.Value, ReplayHeader.RequestId.Value);

	const TArray<uint8> BusinessPayload = CatFishingBoundaryContractTest::MakeBusinessPayload(0x01);
	const FCatFishingPayloadHash FirstHash = FCatFishingBoundaryHash::HashOperation(
		ECatFishingBoundaryOperationKind::Start,
		FirstHeader,
		BusinessPayload);
	const FCatFishingPayloadHash ReplayHash = FCatFishingBoundaryHash::HashOperation(
		ECatFishingBoundaryOperationKind::Start,
		ReplayHeader,
		BusinessPayload);
	TestEqual(TEXT("Boundary PayloadHash 固定为 SHA-256 长度"), FirstHash.Bytes.Num(), 32);
	TestEqual(TEXT("固定 canonical 输入匹配外部 SHA-256 Golden"),
		CatFishingBoundaryContractTest::HashToLowerHex(FirstHash),
		FString(TEXT("a749992973f624e18b1b246da6dbd44f22a4e38135a3b73c0e33ecd17ff47584")));
	TestTrue(TEXT("RequestId 不影响同语义请求的 PayloadHash"), FirstHash == ReplayHash);

	const FCatFishingPayloadHash ChangedPayloadHash = FCatFishingBoundaryHash::HashOperation(
		ECatFishingBoundaryOperationKind::Start,
		FirstHeader,
		CatFishingBoundaryContractTest::MakeBusinessPayload(0x02));
	TestFalse(TEXT("业务载荷变化必须改变 PayloadHash"), FirstHash == ChangedPayloadHash);

	FCatFishingBoundaryRequestHeader ChangedRevisionHeader = FirstHeader;
	ChangedRevisionHeader.ExpectedRevision = 8;
	const FCatFishingPayloadHash ChangedRevisionHash = FCatFishingBoundaryHash::HashOperation(
		ECatFishingBoundaryOperationKind::Start,
		ChangedRevisionHeader,
		BusinessPayload);
	TestFalse(TEXT("Revision 变化必须改变 PayloadHash"), FirstHash == ChangedRevisionHash);
	return !HasAnyErrors();
}

// 测试流程：
// 1. 首次提交 Start operation，Journal 必须在副作用前分配 OperationId，并返回 Pending 供后续 Poll 或提交。
// 2. 原样重放同一 RequestId 与同一 PayloadHash，Journal 必须返回同一个 OperationId，并标记为重放。
// 3. 用同一 RequestId 携带不同 PayloadHash 重放，Journal 必须稳定拒绝 PayloadMismatch，不能生成第二个 OperationId。
// 4. 用同一 RequestId 更换 Principal，Journal 必须拒绝身份漂移，避免 Principal 被拼进 key 后绕开重放检查。
bool FCatFishingBoundaryJournalReplayTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCatFishingOperationJournal Journal;
	FCatFishingJournalRequest Request;
	Request.OperationKind = ECatFishingBoundaryOperationKind::Start;
	Request.Header = CatFishingBoundaryContractTest::MakeHeader(
		FGuid(0xAAAAAAAA, 0xBBBBBBBB, 0xCCCCCCCC, 0xDDDDDDDD));
	Request.PayloadHash = CatFishingBoundaryContractTest::MakePayloadHash(0x10);

	const FCatFishingBoundaryResultHeader FirstResult = Journal.AcceptOrReplay(Request);
	TestEqual(TEXT("首次接受的 operation 进入 Pending"), FirstResult.Disposition, ECatFishingBoundaryDisposition::Pending);
	TestEqual(TEXT("首次接受没有错误"), FirstResult.Error, ECatFishingBoundaryError::None);
	TestFalse(TEXT("首次接受不是重放"), FirstResult.bReplay);
	TestTrue(TEXT("首次接受会分配 OperationId"), FirstResult.Operation.OperationId.Value.IsValid());
	TestTrue(TEXT("首次接受保留 PayloadHash"), FirstResult.PayloadHash == Request.PayloadHash);

	const FCatFishingBoundaryResultHeader ReplayResult = Journal.AcceptOrReplay(Request);
	TestEqual(TEXT("原样重放仍返回 Pending"), ReplayResult.Disposition, ECatFishingBoundaryDisposition::Pending);
	TestEqual(TEXT("原样重放没有错误"), ReplayResult.Error, ECatFishingBoundaryError::None);
	TestTrue(TEXT("原样重放显式标记为 replay"), ReplayResult.bReplay);
	TestEqual(TEXT("原样重放返回同一 OperationId"),
		ReplayResult.Operation.OperationId.Value,
		FirstResult.Operation.OperationId.Value);
	TestTrue(TEXT("原样重放保留首次 PayloadHash"), ReplayResult.PayloadHash == FirstResult.PayloadHash);

	FCatFishingJournalRequest MismatchedPayloadRequest = Request;
	MismatchedPayloadRequest.PayloadHash = CatFishingBoundaryContractTest::MakePayloadHash(0x20);
	const FCatFishingBoundaryResultHeader MismatchResult = Journal.AcceptOrReplay(MismatchedPayloadRequest);
	TestEqual(TEXT("同 RequestId 不同 PayloadHash 被拒绝"),
		MismatchResult.Disposition,
		ECatFishingBoundaryDisposition::Rejected);
	TestEqual(TEXT("Payload 不一致返回 PayloadMismatch"),
		MismatchResult.Error,
		ECatFishingBoundaryError::PayloadMismatch);
	TestFalse(TEXT("PayloadMismatch 不分配新的 OperationId"), MismatchResult.Operation.OperationId.Value.IsValid());

	FCatFishingJournalRequest ChangedPrincipalRequest = Request;
	ChangedPrincipalRequest.Header.PrincipalId.CanonicalValue = TEXT("player:other");
	ChangedPrincipalRequest.PayloadHash = CatFishingBoundaryContractTest::MakePayloadHash(0x21);
	const FCatFishingBoundaryResultHeader ChangedPrincipalResult = Journal.AcceptOrReplay(ChangedPrincipalRequest);
	TestEqual(TEXT("同 RequestId 更换 Principal 被拒绝"),
		ChangedPrincipalResult.Disposition,
		ECatFishingBoundaryDisposition::Rejected);
	TestEqual(TEXT("Principal 漂移返回 InvalidIdentity"),
		ChangedPrincipalResult.Error,
		ECatFishingBoundaryError::InvalidIdentity);
	TestFalse(TEXT("Principal 漂移不分配新的 OperationId"), ChangedPrincipalResult.Operation.OperationId.Value.IsValid());
	return !HasAnyErrors();
}

// 测试流程：
// 1. 先接受一条 Start operation，用非法 Disposition/Error 组合提交，要求拒绝且保留 Pending。
// 2. 再提交合法 Committed 终态，确认 Poll 看到稳定终态。
// 3. 继续尝试用 Pending 降级或 Rejected 覆盖同一个 Operation，要求已有终态仍不可被改写。
bool FCatFishingBoundaryJournalCommitStabilityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCatFishingOperationJournal Journal;
	FCatFishingJournalRequest Request;
	Request.OperationKind = ECatFishingBoundaryOperationKind::Start;
	Request.Header = CatFishingBoundaryContractTest::MakeHeader(
		FGuid(0x91919191, 0x92929292, 0x93939393, 0x94949494));
	Request.PayloadHash = CatFishingBoundaryContractTest::MakePayloadHash(0x50);

	const FCatFishingBoundaryResultHeader FirstResult = Journal.AcceptOrReplay(Request);
	TestEqual(TEXT("提交前 operation 处于 Pending"), FirstResult.Disposition, ECatFishingBoundaryDisposition::Pending);

	FCatFishingBoundaryResultHeader CommittedWithError = FirstResult;
	CommittedWithError.Disposition = ECatFishingBoundaryDisposition::Committed;
	CommittedWithError.Error = ECatFishingBoundaryError::InvalidRequest;
	TestFalse(TEXT("Committed 不能携带错误码"), Journal.CommitResult(FirstResult.Operation, CommittedWithError));

	FCatFishingBoundaryResultHeader RejectedWithoutError = FirstResult;
	RejectedWithoutError.Disposition = ECatFishingBoundaryDisposition::Rejected;
	RejectedWithoutError.Error = ECatFishingBoundaryError::None;
	TestFalse(TEXT("Rejected 不能使用 None 错误码"), Journal.CommitResult(FirstResult.Operation, RejectedWithoutError));

	FCatFishingBoundaryResultHeader UnknownDisposition = FirstResult;
	UnknownDisposition.Disposition = static_cast<ECatFishingBoundaryDisposition>(255);
	UnknownDisposition.Error = ECatFishingBoundaryError::None;
	TestFalse(TEXT("未知 Disposition 不能写入 Inbox"), Journal.CommitResult(FirstResult.Operation, UnknownDisposition));

	FCatFishingBoundaryResultHeader PolledAfterInvalidCommit;
	TestTrue(TEXT("非法组合被拒后仍可 Poll 到 Inbox"), Journal.TryPoll(FirstResult.Operation, PolledAfterInvalidCommit));
	TestEqual(TEXT("非法组合被拒后仍保持 Pending"), PolledAfterInvalidCommit.Disposition, ECatFishingBoundaryDisposition::Pending);
	TestEqual(TEXT("非法组合被拒后没有写入错误码"), PolledAfterInvalidCommit.Error, ECatFishingBoundaryError::None);

	FCatFishingBoundaryResultHeader CommittedResult = FirstResult;
	CommittedResult.Disposition = ECatFishingBoundaryDisposition::Committed;
	CommittedResult.Error = ECatFishingBoundaryError::None;
	CommittedResult.Revision = FirstResult.Revision + 1;
	TestTrue(TEXT("首次终态提交成功"), Journal.CommitResult(FirstResult.Operation, CommittedResult));

	FCatFishingBoundaryResultHeader PolledCommittedResult;
	TestTrue(TEXT("提交后可 Poll 到 Inbox"), Journal.TryPoll(FirstResult.Operation, PolledCommittedResult));
	TestEqual(TEXT("Poll 读取到 Committed 终态"), PolledCommittedResult.Disposition, ECatFishingBoundaryDisposition::Committed);
	TestEqual(TEXT("Poll 保留终态 Revision"), PolledCommittedResult.Revision, CommittedResult.Revision);
	TestTrue(TEXT("Poll 结果标记为 replay 视角"), PolledCommittedResult.bReplay);

	FCatFishingBoundaryResultHeader PendingDowngrade = FirstResult;
	PendingDowngrade.Disposition = ECatFishingBoundaryDisposition::Pending;
	PendingDowngrade.Revision = FirstResult.Revision;
	TestFalse(TEXT("终态不能被 Pending 降级"), Journal.CommitResult(FirstResult.Operation, PendingDowngrade));

	FCatFishingBoundaryResultHeader PolledAfterDowngrade;
	TestTrue(TEXT("降级被拒后仍可 Poll 到 Inbox"), Journal.TryPoll(FirstResult.Operation, PolledAfterDowngrade));
	TestEqual(TEXT("降级被拒后仍保持 Committed"), PolledAfterDowngrade.Disposition, ECatFishingBoundaryDisposition::Committed);
	TestEqual(TEXT("降级被拒后 Revision 不回退"), PolledAfterDowngrade.Revision, CommittedResult.Revision);

	FCatFishingBoundaryResultHeader RejectedOverwrite = FirstResult;
	RejectedOverwrite.Disposition = ECatFishingBoundaryDisposition::Rejected;
	RejectedOverwrite.Error = ECatFishingBoundaryError::CancelledBeforeCommit;
	RejectedOverwrite.Revision = FirstResult.Revision - 1;
	TestFalse(TEXT("终态不能被二次覆盖"), Journal.CommitResult(FirstResult.Operation, RejectedOverwrite));

	FCatFishingBoundaryResultHeader PolledAfterOverwrite;
	TestTrue(TEXT("二次覆盖被拒后仍可 Poll 到 Inbox"), Journal.TryPoll(FirstResult.Operation, PolledAfterOverwrite));
	TestEqual(TEXT("二次覆盖被拒后仍保持 Committed"), PolledAfterOverwrite.Disposition, ECatFishingBoundaryDisposition::Committed);
	TestEqual(TEXT("二次覆盖被拒后 Revision 不被改写"), PolledAfterOverwrite.Revision, CommittedResult.Revision);
	return !HasAnyErrors();
}

// 测试流程：
// 1. 在真实 Game World 中取得 Boundary WorldSubsystem，证明 Start/Cast facade 有正式生命周期入口。
// 2. 对缺身份 Start 提交有效 RequestId，要求在副作用前稳定拒绝 InvalidIdentity，且不伪造 Attempt。
// 3. 对缺 Attempt Cast 提交有效 RequestId，要求在冻结 EncounterSpec 前拒绝 InvalidAttempt。
bool FCatFishingBoundaryStartCastSubsystemTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 Boundary 测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	UCatFishingBoundarySubsystem* Boundary = World ? World->GetSubsystem<UCatFishingBoundarySubsystem>() : nullptr;
	TestNotNull(TEXT("Boundary Subsystem 在 Game World 中创建"), Boundary);
	if (!Boundary)
	{
		return false;
	}

	const FGuid StartRequestId = FGuid::NewGuid();
	const FCatFishingBoundaryStartResult StartResult = Boundary->Start(nullptr, StartRequestId);
	TestEqual(TEXT("缺身份 Start 在副作用前拒绝"), StartResult.Header.Disposition, ECatFishingBoundaryDisposition::Rejected);
	TestEqual(TEXT("缺身份 Start 返回 InvalidIdentity"), StartResult.Header.Error, ECatFishingBoundaryError::InvalidIdentity);
	TestEqual(TEXT("Start 拒绝保留 RequestId"), StartResult.Context.RequestId, StartRequestId);
	TestFalse(TEXT("Start 拒绝不伪造 AttemptId"), StartResult.Context.AttemptId.Value.IsValid());

	FCatFishingCastAcceptedRequest CastRequest;
	CastRequest.RequestId = FGuid::NewGuid();
	const FCatFishingBoundaryCastResult CastResult = Boundary->CastAccepted(nullptr, CastRequest);
	TestEqual(TEXT("缺 Attempt Cast 在冻结 EncounterSpec 前拒绝"), CastResult.Header.Disposition, ECatFishingBoundaryDisposition::Rejected);
	TestEqual(TEXT("缺 Attempt Cast 返回 InvalidAttempt"), CastResult.Header.Error, ECatFishingBoundaryError::InvalidAttempt);
	TestTrue(TEXT("Cast 拒绝不伪造鱼种"), CastResult.EncounterSpec.FishDefinitionId.IsNone());
	return !HasAnyErrors();
}

// 测试流程：
// 1. 在真实 Game World 中取得 Boundary WorldSubsystem，证明 BaitAccepted façade 走正式 Subsystem 生命周期。
// 2. 提交格式有效但未由 Start 接受过的 Attempt，要求在任何 Equipment 或普通饵 Receipt 前拒绝。
// 3. 拒绝结果必须保留 Attempt/Revision 诊断信息，但不能伪造 Receipt。
bool FCatFishingBoundaryBaitAcceptedSubsystemTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 BaitAccepted 测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	UCatFishingBoundarySubsystem* Boundary = World ? World->GetSubsystem<UCatFishingBoundarySubsystem>() : nullptr;
	TestNotNull(TEXT("Boundary Subsystem 在 BaitAccepted 测试 World 中创建"), Boundary);
	if (!Boundary)
	{
		return false;
	}

	FCatFishingBiteAcceptedRequest Request;
	Request.RequestId = FGuid::NewGuid();
	Request.AttemptId.Value = FGuid(0x91910001, 0x91910002, 0x91910003, 0x91910004);
	Request.PrincipalId.CanonicalValue = TEXT("player:bait-unknown-attempt");
	Request.ExpectedRevision = 11;
	Request.BaitDefinitionId = TEXT("BasicBait");
	Request.BiteToken = FGuid::NewGuid();

	const FCatFishingBaitResult Result = Boundary->BaitAccepted(Request);
	TestEqual(TEXT("未知 Attempt 的 BaitAccepted 在副作用前拒绝"),
		Result.Header.Disposition,
		ECatFishingBoundaryDisposition::Rejected);
	TestEqual(TEXT("未知 Attempt 的 BaitAccepted 返回 InvalidAttempt"),
		Result.Header.Error,
		ECatFishingBoundaryError::InvalidAttempt);
	TestEqual(TEXT("BaitAccepted 拒绝保留 Attempt"),
		Result.Header.Operation.AttemptId.Value,
		Request.AttemptId.Value);
	TestEqual(TEXT("BaitAccepted 拒绝保留调用方 Revision"),
		Result.Header.Revision,
		Request.ExpectedRevision);
	TestFalse(TEXT("BaitAccepted 拒绝不伪造 Receipt"),
		Result.Receipt.ReceiptId.Value.IsValid());
	return !HasAnyErrors();
}

// 测试流程：
// 1. 用自动化专用入口登记一条已接受 Start 上下文，避免为普通饵 Receipt 测试搭完整 PlayerController/ASC/Run fixture。
// 2. 提交普通饵 BaitAccepted 请求，要求立即提交 Committed 并发行 BaitAccepted Receipt。
// 3. 原样重放同一请求，要求返回同一个 OperationId 和 ReceiptId，不产生第二次语义提交。
bool FCatFishingBoundaryBaitAcceptedReceiptSubsystemTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 BaitAccepted Receipt 测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	UCatFishingBoundarySubsystem* Boundary = World ? World->GetSubsystem<UCatFishingBoundarySubsystem>() : nullptr;
	TestNotNull(TEXT("Boundary Subsystem 在 BaitAccepted Receipt 测试 World 中创建"), Boundary);
	if (!Boundary)
	{
		return false;
	}

	FCatFishingStartContext Context;
	Context.RequestId = FGuid(0x90900001, 0x90900002, 0x90900003, 0x90900004);
	Context.AttemptId.Value = FGuid(0x90910001, 0x90910002, 0x90910003, 0x90910004);
	Context.PrincipalId.CanonicalValue = TEXT("player:bait-receipt");
	Context.RunRevision = 23;
	Context.FisherGuardContainerId = FGuid(0x90920001, 0x90920002, 0x90920003, 0x90920004);
	Boundary->AddAcceptedStartContextForAutomation(Context);

	FCatFishingBiteAcceptedRequest Request;
	Request.RequestId = FGuid(0x90930001, 0x90930002, 0x90930003, 0x90930004);
	Request.AttemptId = Context.AttemptId;
	Request.PrincipalId = Context.PrincipalId;
	Request.ExpectedRevision = 23;
	Request.BaitDefinitionId = TEXT("BasicBait");
	Request.BiteToken = FGuid(0x90940001, 0x90940002, 0x90940003, 0x90940004);
	Request.bConsumesSpecialBait = false;

	const FCatFishingBaitResult First = Boundary->BaitAccepted(Request);
	TestEqual(TEXT("普通饵 BaitAccepted 提交成功"), First.Header.Disposition, ECatFishingBoundaryDisposition::Committed);
	TestEqual(TEXT("普通饵 BaitAccepted 无错误"), First.Header.Error, ECatFishingBoundaryError::None);
	TestTrue(TEXT("普通饵 BaitAccepted 分配 OperationId"), First.Header.Operation.OperationId.Value.IsValid());
	TestTrue(TEXT("普通饵 BaitAccepted 发行 Receipt"), First.Receipt.ReceiptId.Value.IsValid());
	TestEqual(TEXT("普通饵 Receipt 类型正确"), First.Receipt.Kind, ECatFishingReceiptKind::BaitAccepted);

	const FCatFishingBaitResult Replay = Boundary->BaitAccepted(Request);
	TestTrue(TEXT("普通饵 BaitAccepted 重放标记 replay"), Replay.Header.bReplay);
	TestEqual(TEXT("普通饵 BaitAccepted 重放保持 OperationId"),
		Replay.Header.Operation.OperationId.Value,
		First.Header.Operation.OperationId.Value);
	TestEqual(TEXT("普通饵 BaitAccepted 重放保持 ReceiptId"),
		Replay.Receipt.ReceiptId.Value,
		First.Receipt.ReceiptId.Value);
	return !HasAnyErrors();
}

// 测试流程：
// 1. 在真实 Game World 中生成 PlayerController、项目 PlayerState、项目 Character 和 EquipmentComponent，并写入测试稳定身份。
// 2. 临时注册一条运行期特殊饵定义，通过 EquipmentComponent 公开授予入口给角色一份库存。
// 3. BaitAccepted 以特殊饵提交时必须调用真实 Equipment 消费一次并发行 Receipt；同 RequestId 重放只返回首次 Receipt，不再扣第二次。
bool FCatFishingBoundarySpecialBaitConsumesOnceSubsystemTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建特殊饵 Boundary 测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	UCatFishingBoundarySubsystem* Boundary = World ? World->GetSubsystem<UCatFishingBoundarySubsystem>() : nullptr;
	TestNotNull(TEXT("特殊饵测试 World 可用"), World);
	TestNotNull(TEXT("Boundary Subsystem 在特殊饵测试 World 中创建"), Boundary);
	if (!World || !Boundary)
	{
		return false;
	}

	const FName SpecialBaitId(TEXT("BoundarySpecialBaitA"));
	UCatEquipmentDefinition* SpecialBait = CatFishingBoundaryContractTest::MakeSpecialBaitDefinition(SpecialBaitId);
	UCatEquipmentSettings* Settings = GetMutableDefault<UCatEquipmentSettings>();
	// Settings 保护流程：保存默认目录的版本、修订、来源戳、维修引用和定义清单，测试结束后恢复，避免影响同进程后续用例。
	const int32 SavedContentSchemaVersion = Settings->ContentSchemaVersion;
	const int64 SavedDataRevision = Settings->DataRevision;
	const FCatDataCatalogSourceStamp SavedSourceStamp = Settings->SourceStamp;
	const FName SavedDriftwoodDefinitionId = Settings->DriftwoodDefinitionId;
	const FName SavedStarterRodDefinitionId = Settings->StarterRodDefinitionId;
	const FName SavedStarterBaitDefinitionId = Settings->StarterBaitDefinitionId;
	const FName SavedStarterFloatDefinitionId = Settings->StarterFloatDefinitionId;
	const TArray<TSoftObjectPtr<UCatEquipmentDefinition>> SavedDefinitions = Settings->Definitions;
	Settings->ContentSchemaVersion = UCatEquipmentSettings::CurrentContentSchemaVersion;
	Settings->DataRevision = 1;
	Settings->SourceStamp.SourceKind = TEXT("AutomationEquipmentDoc");
	Settings->SourceStamp.SourceNodeToken = TEXT("BoundarySpecialBaitTest");
	Settings->SourceStamp.SourceRevision = 652;
	Settings->SourceStamp.SourceSliceName.Reset();
	Settings->DriftwoodDefinitionId = NAME_None;
	Settings->StarterRodDefinitionId = NAME_None;
	Settings->StarterBaitDefinitionId = NAME_None;
	Settings->StarterFloatDefinitionId = NAME_None;
	Settings->Definitions.Reset();
	Settings->Definitions.Add(SpecialBait);

	APlayerController* Controller = World->SpawnActor<APlayerController>();
	ACatfishingPlayerState* PlayerState = World->SpawnActor<ACatfishingPlayerState>();
	ACatCharacter* Character = World->SpawnActor<ACatCharacter>();
	TestNotNull(TEXT("特殊饵测试 Controller 可创建"), Controller);
	TestNotNull(TEXT("特殊饵测试 PlayerState 可创建"), PlayerState);
	TestNotNull(TEXT("特殊饵测试 Character 可创建"), Character);
	TestNotNull(TEXT("特殊饵定义可创建"), SpecialBait);

	const FString StableNetId(TEXT("player:boundary-special-bait"));
	if (Controller && PlayerState && Character)
	{
		const FUniqueNetIdRef StableUniqueId = FUniqueNetIdString::Create(StableNetId, TEXT("CAT_TEST"));
		PlayerState->SetUniqueId(FUniqueNetIdRepl(StableUniqueId));
		Controller->SetPlayerState(PlayerState);
		// Boundary 按 World 的 PlayerController 列表反查 StableId；手动生成的测试 Controller 必须显式注册。
		World->AddController(Controller);
		Controller->Possess(Character);
	}
	UCatEquipmentComponent* Equipment = Character ? Character->GetEquipmentComponent() : nullptr;
	TestNotNull(TEXT("特殊饵测试 Character 暴露 EquipmentComponent"), Equipment);

	FCatDomainCommandResult GrantResult;
	if (Equipment)
	{
		GrantResult = Equipment->GrantRunConsumableFromAuthority(
			FGuid(0xBABA0001, 0xBABA0002, 0xBABA0003, 0xBABA0004),
			Equipment->GetSnapshot().Revision,
			SpecialBaitId,
			1);
	}
	TestTrue(TEXT("测试夹具能通过真实 Equipment 授予特殊饵"), GrantResult.bCommitted);
	TestEqual(TEXT("特殊饵授予后 Equipment Revision 推进"), GrantResult.Revision, static_cast<int64>(1));

	FCatFishingStartContext Context;
	Context.RequestId = FGuid(0xBABB0001, 0xBABB0002, 0xBABB0003, 0xBABB0004);
	Context.AttemptId.Value = FGuid(0xBABC0001, 0xBABC0002, 0xBABC0003, 0xBABC0004);
	Context.PrincipalId.CanonicalValue = StableNetId;
	Context.RunRevision = 31;
	Context.FisherGuardContainerId = FGuid(0xBABD0001, 0xBABD0002, 0xBABD0003, 0xBABD0004);
	Boundary->AddAcceptedStartContextForAutomation(Context);

	FCatFishingBiteAcceptedRequest Request;
	Request.RequestId = FGuid(0xBABE0001, 0xBABE0002, 0xBABE0003, 0xBABE0004);
	Request.AttemptId = Context.AttemptId;
	Request.PrincipalId = Context.PrincipalId;
	Request.ExpectedRevision = GrantResult.Revision;
	Request.BaitDefinitionId = SpecialBaitId;
	Request.BiteToken = FGuid(0xBABF0001, 0xBABF0002, 0xBABF0003, 0xBABF0004);
	Request.bConsumesSpecialBait = true;

	const FCatFishingBaitResult First = Boundary->BaitAccepted(Request);
	TestEqual(TEXT("特殊饵 BaitAccepted 提交成功"), First.Header.Disposition, ECatFishingBoundaryDisposition::Committed);
	TestEqual(TEXT("特殊饵 BaitAccepted 无错误"), First.Header.Error, ECatFishingBoundaryError::None);
	TestTrue(TEXT("特殊饵 BaitAccepted 发行 Receipt"), First.Receipt.ReceiptId.Value.IsValid());
	TestEqual(TEXT("特殊饵 Receipt 记录消费后的 Equipment Revision"), First.Receipt.DomainRevision, static_cast<int64>(2));
	if (Equipment)
	{
		TestEqual(TEXT("特殊饵首次提交只扣一份库存"), CatFishingBoundaryContractTest::FindConsumableQuantityForTest(Equipment->GetSnapshot(), SpecialBaitId), 0);
		TestEqual(TEXT("特殊饵首次提交推进 Equipment Revision"), Equipment->GetSnapshot().Revision, static_cast<int64>(2));
	}

	const FCatFishingBaitResult Replay = Boundary->BaitAccepted(Request);
	TestTrue(TEXT("特殊饵 BaitAccepted 重放标记 replay"), Replay.Header.bReplay);
	TestEqual(TEXT("特殊饵 BaitAccepted 重放保持 ReceiptId"),
		Replay.Receipt.ReceiptId.Value,
		First.Receipt.ReceiptId.Value);
	TestEqual(TEXT("特殊饵 BaitAccepted 重放保持 DomainRevision"), Replay.Receipt.DomainRevision, First.Receipt.DomainRevision);
	if (Equipment)
	{
		TestEqual(TEXT("特殊饵重放不二次扣库存"), CatFishingBoundaryContractTest::FindConsumableQuantityForTest(Equipment->GetSnapshot(), SpecialBaitId), 0);
		TestEqual(TEXT("特殊饵重放不推进 Equipment Revision"), Equipment->GetSnapshot().Revision, static_cast<int64>(2));
	}

	Settings->Definitions = SavedDefinitions;
	Settings->ContentSchemaVersion = SavedContentSchemaVersion;
	Settings->DataRevision = SavedDataRevision;
	Settings->SourceStamp = SavedSourceStamp;
	Settings->DriftwoodDefinitionId = SavedDriftwoodDefinitionId;
	Settings->StarterRodDefinitionId = SavedStarterRodDefinitionId;
	Settings->StarterBaitDefinitionId = SavedStarterBaitDefinitionId;
	Settings->StarterFloatDefinitionId = SavedStarterFloatDefinitionId;
	return !HasAnyErrors();
}
// 测试流程：
// 1. 在真实 Game World 中取得 Boundary WorldSubsystem，证明 FightAccepted façade 走正式 Subsystem 生命周期。
// 2. 提交格式有效但未由 Start 接受过的 Attempt，要求在 GAS、Equipment 或 Session 状态写入前拒绝。
// 3. 拒绝结果必须保留 Attempt/Revision 诊断信息，但不能伪造 FightResourcesApplied Receipt。
bool FCatFishingBoundaryFightAcceptedSubsystemTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTestWorldWrapper WorldWrapper;
	TestTrue(TEXT("创建 FightAccepted 测试 Game World"), WorldWrapper.CreateTestWorld(EWorldType::Game));
	WorldWrapper.ForwardErrorMessages(this);
	UWorld* World = WorldWrapper.GetTestWorld();
	UCatFishingBoundarySubsystem* Boundary = World ? World->GetSubsystem<UCatFishingBoundarySubsystem>() : nullptr;
	TestNotNull(TEXT("Boundary Subsystem 在 FightAccepted 测试 World 中创建"), Boundary);
	if (!Boundary)
	{
		return false;
	}

	FCatFishingFightExchangeRequest Request;
	Request.RequestId = FGuid::NewGuid();
	Request.AttemptId.Value = FGuid(0x92920001, 0x92920002, 0x92920003, 0x92920004);
	Request.PrincipalId.CanonicalValue = TEXT("player:fight-unknown-attempt");
	Request.Cursor = 1;
	Request.ExpectedRevision = 17;
	Request.FishStaminaCost = 2.0;
	Request.ParticipantStaminaCost = 1.0;
	Request.RodDurabilityCost = 0.5;

	const FCatFishingFightResult Result = Boundary->FightAccepted(Request);
	TestEqual(TEXT("未知 Attempt 的 FightAccepted 在副作用前拒绝"),
		Result.Header.Disposition,
		ECatFishingBoundaryDisposition::Rejected);
	TestEqual(TEXT("未知 Attempt 的 FightAccepted 返回 InvalidAttempt"),
		Result.Header.Error,
		ECatFishingBoundaryError::InvalidAttempt);
	TestEqual(TEXT("FightAccepted 拒绝保留 Attempt"),
		Result.Header.Operation.AttemptId.Value,
		Request.AttemptId.Value);
	TestEqual(TEXT("FightAccepted 拒绝保留调用方 Revision"),
		Result.Header.Revision,
		Request.ExpectedRevision);
	TestEqual(TEXT("FightAccepted 拒绝不伪造 Receipt"),
		Result.Receipts.Num(),
		0);
	return !HasAnyErrors();
}
// 测试流程：
// 1. 先接受一条 Start operation，取得首次 Pending 和 OperationKey。
// 2. 关闭同一个 Attempt 后原样重放首次请求，要求仍返回首次 Pending，证明 Close 只拒绝新 operation。
// 3. 用首次 OperationKey Poll，要求仍能读到 Inbox；再提交同 Attempt 的新 RequestId，要求返回 AttemptClosed。
bool FCatFishingBoundaryJournalCloseKeepsInboxTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCatFishingOperationJournal Journal;
	FCatFishingJournalRequest Request;
	Request.OperationKind = ECatFishingBoundaryOperationKind::Start;
	Request.Header = CatFishingBoundaryContractTest::MakeHeader(
		FGuid(0xABABABAB, 0xCDCDCDCD, 0xEFEFEFEF, 0x12121212));
	Request.PayloadHash = CatFishingBoundaryContractTest::MakePayloadHash(0x30);

	const FCatFishingBoundaryResultHeader FirstResult = Journal.AcceptOrReplay(Request);
	TestEqual(TEXT("首次请求进入 Pending"), FirstResult.Disposition, ECatFishingBoundaryDisposition::Pending);
	TestTrue(TEXT("首次请求取得 OperationId"), FirstResult.Operation.OperationId.Value.IsValid());

	Journal.CloseAttempt(Request.Header.AttemptId);

	const FCatFishingBoundaryResultHeader ReplayAfterClose = Journal.AcceptOrReplay(Request);
	TestEqual(TEXT("Close 后同请求重放仍返回首次 Pending"), ReplayAfterClose.Disposition, ECatFishingBoundaryDisposition::Pending);
	TestEqual(TEXT("Close 后同请求重放没有 AttemptClosed 错误"), ReplayAfterClose.Error, ECatFishingBoundaryError::None);
	TestTrue(TEXT("Close 后同请求重放标记 replay"), ReplayAfterClose.bReplay);
	TestEqual(TEXT("Close 后同请求重放保留首次 OperationId"),
		ReplayAfterClose.Operation.OperationId.Value,
		FirstResult.Operation.OperationId.Value);

	FCatFishingBoundaryResultHeader PolledResult;
	TestTrue(TEXT("Close 后仍可 Poll 已接受 operation"), Journal.TryPoll(FirstResult.Operation, PolledResult));
	TestEqual(TEXT("Close 后 Poll 保留 Pending"), PolledResult.Disposition, ECatFishingBoundaryDisposition::Pending);
	TestEqual(TEXT("Close 后 Poll 保留首次 OperationId"),
		PolledResult.Operation.OperationId.Value,
		FirstResult.Operation.OperationId.Value);

	FCatFishingJournalRequest NewRequestAfterClose = Request;
	NewRequestAfterClose.Header.RequestId.Value = FGuid(0x13131313, 0x14141414, 0x15151515, 0x16161616);
	NewRequestAfterClose.PayloadHash = CatFishingBoundaryContractTest::MakePayloadHash(0x40);
	const FCatFishingBoundaryResultHeader NewResultAfterClose = Journal.AcceptOrReplay(NewRequestAfterClose);
	TestEqual(TEXT("Close 后新请求被拒绝"), NewResultAfterClose.Disposition, ECatFishingBoundaryDisposition::Rejected);
	TestEqual(TEXT("Close 后新请求返回 AttemptClosed"), NewResultAfterClose.Error, ECatFishingBoundaryError::AttemptClosed);
	TestFalse(TEXT("Close 后新请求不分配 OperationId"), NewResultAfterClose.Operation.OperationId.Value.IsValid());
	return !HasAnyErrors();
}

// 测试流程：
// 1. 首次提交 cursor=1 的 Fight 请求，要求得到一个可恢复的 OperationKey。
// 2. cursor=2 作为 Last+1 可以继续接受；cursor=4 跳过中间帧必须返回 CursorGap。
// 3. cursor=2 使用不同业务 Hash 重放必须返回 PayloadMismatch，不能开第二条副作用。
// 4. seal 最终 cursor 后，更大的 cursor 必须被拒绝，Capture 之后不能再补搏斗资源帧。
bool FCatFishingBoundaryFightCursorLedgerTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCatFishingFightCursorLedger Ledger;
	FCatFishingFightExchangeRequest FirstRequest;
	FirstRequest.AttemptId.Value = FGuid(0x21212121, 0x22222222, 0x23232323, 0x24242424);
	FirstRequest.RequestId = FGuid(0x31313131, 0x32323232, 0x33333333, 0x34343434);
	FirstRequest.PrincipalId.CanonicalValue = TEXT("player:fight-cursor");
	FirstRequest.Cursor = 1;
	FirstRequest.ExpectedRevision = 41;
	FirstRequest.FishStaminaCost = 2.0;
	FirstRequest.ParticipantStaminaCost = 1.0;
	FirstRequest.RodDurabilityCost = 0.5;

	const FCatFishingPayloadHash FirstHash = CatFishingBoundaryContractTest::MakePayloadHash(0x70);
	const FCatFishingBoundaryResultHeader FirstResult = Ledger.AcceptOrReplay(FirstRequest, FirstHash);
	TestEqual(TEXT("Fight cursor=1 首次接受进入 Pending"), FirstResult.Disposition, ECatFishingBoundaryDisposition::Pending);
	TestEqual(TEXT("Fight cursor=1 首次接受无错误"), FirstResult.Error, ECatFishingBoundaryError::None);
	TestTrue(TEXT("Fight cursor=1 分配 OperationId"), FirstResult.Operation.OperationId.Value.IsValid());

	const FCatFishingFightResult ResourceResult = Ledger.CommitResourcesApplied(FirstResult.Operation, 42, false);
	TestEqual(TEXT("Fight 资源写入提交为 Committed"), ResourceResult.Header.Disposition, ECatFishingBoundaryDisposition::Committed);
	TestEqual(TEXT("Fight 资源写入无错误"), ResourceResult.Header.Error, ECatFishingBoundaryError::None);
	TestEqual(TEXT("Fight 资源写入只发行一份 Receipt"), ResourceResult.Receipts.Num(), 1);
	if (ResourceResult.Receipts.Num() == 1)
	{
		TestEqual(TEXT("Fight Receipt 类型为 FightResourcesApplied"), ResourceResult.Receipts[0].Kind, ECatFishingReceiptKind::FightResourcesApplied);
		TestEqual(TEXT("Fight Receipt 绑定首次 OperationId"),
			ResourceResult.Receipts[0].Operation.OperationId.Value,
			FirstResult.Operation.OperationId.Value);
		TestTrue(TEXT("Fight Receipt 保留首次 PayloadHash"), ResourceResult.Receipts[0].PayloadHash == FirstHash);
		TestEqual(TEXT("Fight Receipt 记录资源提交后的 Revision"), ResourceResult.Receipts[0].DomainRevision, static_cast<int64>(42));
	}

	const FCatFishingFightResult ResourceReplay = Ledger.CommitResourcesApplied(FirstResult.Operation, 43, true);
	TestTrue(TEXT("Fight 资源重复提交标记 replay"), ResourceReplay.Header.bReplay);
	TestEqual(TEXT("Fight 资源重复提交不改写 Revision"), ResourceReplay.Receipts[0].DomainRevision, static_cast<int64>(42));
	TestFalse(TEXT("Fight 资源重复提交不伪造 RodBroken"), ResourceReplay.bRodBroken);

	FCatFishingFightExchangeRequest SecondRequest = FirstRequest;
	SecondRequest.RequestId = FGuid(0x41414141, 0x42424242, 0x43434343, 0x44444444);
	SecondRequest.Cursor = 2;
	const FCatFishingPayloadHash SecondHash = CatFishingBoundaryContractTest::MakePayloadHash(0x71);
	const FCatFishingBoundaryResultHeader SecondResult = Ledger.AcceptOrReplay(SecondRequest, SecondHash);
	TestEqual(TEXT("Fight cursor=Last+1 可以接受"), SecondResult.Disposition, ECatFishingBoundaryDisposition::Pending);
	TestTrue(TEXT("Fight cursor=Last+1 分配独立 OperationId"), SecondResult.Operation.OperationId.Value.IsValid());
	TestNotEqual(TEXT("不同 cursor 使用不同 OperationId"), FirstResult.Operation.OperationId.Value, SecondResult.Operation.OperationId.Value);

	FCatFishingFightExchangeRequest GapRequest = FirstRequest;
	GapRequest.RequestId = FGuid(0x51515151, 0x52525252, 0x53535353, 0x54545454);
	GapRequest.Cursor = 4;
	const FCatFishingBoundaryResultHeader GapResult = Ledger.AcceptOrReplay(
		GapRequest,
		CatFishingBoundaryContractTest::MakePayloadHash(0x72));
	TestEqual(TEXT("Fight cursor 跳号被拒绝"), GapResult.Disposition, ECatFishingBoundaryDisposition::Rejected);
	TestEqual(TEXT("Fight cursor 跳号返回 CursorGap"), GapResult.Error, ECatFishingBoundaryError::CursorGap);
	TestFalse(TEXT("CursorGap 不分配 OperationId"), GapResult.Operation.OperationId.Value.IsValid());

	FCatFishingFightExchangeRequest MismatchedSecondRequest = SecondRequest;
	MismatchedSecondRequest.RequestId = FGuid(0x61616161, 0x62626262, 0x63636363, 0x64646464);
	const FCatFishingBoundaryResultHeader MismatchResult = Ledger.AcceptOrReplay(
		MismatchedSecondRequest,
		CatFishingBoundaryContractTest::MakePayloadHash(0x90));
	TestEqual(TEXT("同 cursor 不同 payload 被拒绝"), MismatchResult.Disposition, ECatFishingBoundaryDisposition::Rejected);
	TestEqual(TEXT("同 cursor 不同 payload 返回 PayloadMismatch"), MismatchResult.Error, ECatFishingBoundaryError::PayloadMismatch);
	TestFalse(TEXT("PayloadMismatch 不分配第二个 OperationId"), MismatchResult.Operation.OperationId.Value.IsValid());

	FCatFishingFinalFightCursor FinalCursor;
	FinalCursor.AttemptId = FirstRequest.AttemptId;
	FinalCursor.Cursor = 2;
	const FCatFishingBoundaryResultHeader SealResult = Ledger.SealFinalCursor(
		FinalCursor,
		FGuid(0x71717171, 0x72727272, 0x73737373, 0x74747474));
	TestEqual(TEXT("FinalFightCursor seal 成功提交"), SealResult.Disposition, ECatFishingBoundaryDisposition::Committed);
	TestEqual(TEXT("FinalFightCursor seal 无错误"), SealResult.Error, ECatFishingBoundaryError::None);

	FCatFishingFightExchangeRequest AfterSealRequest = FirstRequest;
	AfterSealRequest.RequestId = FGuid(0x81818181, 0x82828282, 0x83838383, 0x84848484);
	AfterSealRequest.Cursor = 3;
	const FCatFishingBoundaryResultHeader AfterSealResult = Ledger.AcceptOrReplay(
		AfterSealRequest,
		CatFishingBoundaryContractTest::MakePayloadHash(0x73));
	TestEqual(TEXT("FinalFightCursor seal 后拒绝更大 cursor"), AfterSealResult.Disposition, ECatFishingBoundaryDisposition::Rejected);
	TestEqual(TEXT("seal 后更大 cursor 返回 AlreadySettled"), AfterSealResult.Error, ECatFishingBoundaryError::AlreadySettled);
	return !HasAnyErrors();
}
#endif // WITH_DEV_AUTOMATION_TESTS

