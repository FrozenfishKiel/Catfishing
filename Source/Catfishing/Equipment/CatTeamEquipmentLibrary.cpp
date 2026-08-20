#include "Equipment/CatTeamEquipmentLibrary.h"

#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"

// 创建条件流程：只允许服务器 Game World 拥有可写的团队装备库；客户端不生成第二份实例表。
bool UCatTeamEquipmentLibrary::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

// 反初始化流程：先关闭入库，再清空只属于本 World 的实物、订单索引和幂等缓存；装备库不带进下一局。
void UCatTeamEquipmentLibrary::Deinitialize()
{
	CloseCommands();
	Snapshot = FCatTeamEquipmentLibrarySnapshot();
	InstanceIdBySourceTransaction.Reset();
	TakenInstanceById.Reset();
	TerminalCache.Reset();
	TerminalPayloadByKey.Reset();
	Super::Deinitialize();
}

// 读取流程：返回服务器当前库存事实；写入只能通过入库命令发生。
const FCatTeamEquipmentLibrarySnapshot& UCatTeamEquipmentLibrary::GetSnapshot() const
{
	return Snapshot;
}

// 入库准入流程：定义 ID 非空 → 写口没关 → 定义在装备运行目录里查得到。三条的先后顺序就是入库写口原来的顺序，
// 同一种情况两边给出的错误码因此完全一致，商店订单在扣钱之前问到的答案不会和真正入库时的结果对不上。
ECatDomainCommandError UCatTeamEquipmentLibrary::ValidateShopOrderGrant(const FName DefinitionId) const
{
	if (DefinitionId.IsNone())
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	if (!bCommandsOpen)
	{
		return ECatDomainCommandError::CommandsClosed;
	}
	if (!GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(DefinitionId))
	{
		// 运行目录里查不到就不入库：让一件"名字对得上但定义不存在"的东西进库，
		// 等于在装备库里造了一件谁也用不了、还会一直占着订单交付回执的空壳。
		return ECatDomainCommandError::DependencyUnavailable;
	}
	return ECatDomainCommandError::None;
}

// 入库流程：载荷校验、重放判定、准入（写口 gate 与定义校验）、订单去重、版本前提，最后才创建实例并推进版本。
FCatTeamEquipmentGrantResult UCatTeamEquipmentLibrary::GrantFromShopOrder(const FCatTeamEquipmentGrantCommand& Command)
{
	FCatTeamEquipmentGrantResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	Result.Command.Revision = Snapshot.Revision;
	if (!Command.Context.RequestId.IsValid() || Command.Context.StableNetId.IsEmpty()
		|| !Command.SourceTransactionId.IsValid() || Command.DefinitionId.IsNone())
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	const FString TerminalKey = MakeTerminalKey(Command.Context.StableNetId, TEXT("TeamEquipmentGrant"), Command.Context.RequestId);
	const FString PayloadSignature = MakePayloadSignature(Command);
	switch (CatQueryTerminalReplay(TerminalCache, TerminalPayloadByKey, TerminalKey, PayloadSignature, Result,
		[](FCatTeamEquipmentGrantResult& Replayed) { MarkCommandReplayed(Replayed.Command); }))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
	}
	const auto CacheAndReturn = [this, &TerminalKey, &PayloadSignature](FCatTeamEquipmentGrantResult& Terminal)
	{
		Terminal.Command.Revision = Snapshot.Revision;
		TerminalCache.Add(TerminalKey, Terminal);
		TerminalPayloadByKey.Add(TerminalKey, PayloadSignature);
		return Terminal;
	};
	const ECatDomainCommandError GrantRejection = ValidateShopOrderGrant(Command.DefinitionId);
	const UCatEquipmentDefinition* Definition =
		GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(Command.DefinitionId);
	if (GrantRejection != ECatDomainCommandError::None || !Definition)
	{
		// 准入判据只有 ValidateShopOrderGrant 一份；这里再挡一次空指针，只是因为下面要读这条定义的 Kind。
		// 准入通过时它必然非空（两次查的是同一份只读运行目录），所以走到这一支时准入给的错误码就是真正的拒绝理由。
		Result.Command.Error = GrantRejection != ECatDomainCommandError::None
			? GrantRejection : ECatDomainCommandError::DependencyUnavailable;
		return CacheAndReturn(Result);
	}
	if (TryFindInstanceBySourceTransaction(Command.SourceTransactionId, Result.Instance))
	{
		Result.Command.Error = ECatDomainCommandError::AlreadyResolved;
		return CacheAndReturn(Result);
	}
	if (Command.Context.ExpectedRevision != Snapshot.Revision)
	{
		Result.Command.Error = ECatDomainCommandError::RevisionConflict;
		return CacheAndReturn(Result);
	}

	FCatTeamEquipmentInstance& Instance = Snapshot.Instances.AddDefaulted_GetRef();
	Instance.InstanceId = FGuid::NewGuid();
	Instance.DefinitionId = Command.DefinitionId;
	Instance.Kind = Definition->Kind;
	Instance.SourceTransactionId = Command.SourceTransactionId;
	++Snapshot.Revision;
	InstanceIdBySourceTransaction.Add(Command.SourceTransactionId, Instance.InstanceId);
	Result.Instance = Instance;
	Result.Command.bCommitted = true;
	Result.Command.Error = ECatDomainCommandError::None;
	CacheAndReturn(Result);
	OnLibraryChanged.Broadcast();
	return Result;
}

// 取用准入流程：四项检查按固定顺序短路。顺序不是随手排的——载荷不合法时后面三项都无从谈起；写口关了就等于这局不再有取用这回事，
// 排在定位之前，免得给出"东西在库里"这种此刻没有意义的中间答案；定位不到时版本前提也没有比较对象。
// 只读预检和真正取走共用这一份判据，两条路径才不会给出不一样的答案。
ECatDomainCommandError UCatTeamEquipmentLibrary::EvaluateTakeAdmission(const FCatTeamEquipmentTakeCommand& Command,
	int32& OutInstanceIndex) const
{
	OutInstanceIndex = INDEX_NONE;
	if (!Command.Context.RequestId.IsValid() || Command.Context.StableNetId.IsEmpty() || !Command.InstanceId.IsValid())
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	if (!bCommandsOpen)
	{
		return ECatDomainCommandError::CommandsClosed;
	}
	const int32 InstanceIndex = Snapshot.Instances.IndexOfByPredicate(
		[&Command](const FCatTeamEquipmentInstance& Candidate)
	{
		return Candidate.InstanceId == Command.InstanceId;
	});
	if (InstanceIndex == INDEX_NONE)
	{
		return ECatDomainCommandError::NotFound;
	}
	if (Command.Context.ExpectedRevision != Snapshot.Revision)
	{
		return ECatDomainCommandError::RevisionConflict;
	}
	OutInstanceIndex = InstanceIndex;
	return ECatDomainCommandError::None;
}

// 取用预检流程：拿准入结论，通过就把那件实物拷给调用方，不通过就把输出清空。全程只读，库存、版本和终态缓存都不动。
ECatDomainCommandError UCatTeamEquipmentLibrary::ValidateTake(const FCatTeamEquipmentTakeCommand& Command,
	FCatTeamEquipmentInstance& OutInstance) const
{
	OutInstance = FCatTeamEquipmentInstance();
	int32 InstanceIndex = INDEX_NONE;
	const ECatDomainCommandError Admission = EvaluateTakeAdmission(Command, InstanceIndex);
	if (Admission == ECatDomainCommandError::None)
	{
		OutInstance = Snapshot.Instances[InstanceIndex];
	}
	return Admission;
}

// 取用流程：先算准入结论，载荷不合法的直接退回不进缓存；再做重放判定；确认是首次之后，准入不通过的把结论写进终态缓存，
// 通过的才从库里移除、记到已取走表、推进版本并广播。
FCatTeamEquipmentGrantResult UCatTeamEquipmentLibrary::TakeInstance(const FCatTeamEquipmentTakeCommand& Command)
{
	FCatTeamEquipmentGrantResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	Result.Command.Revision = Snapshot.Revision;
	int32 InstanceIndex = INDEX_NONE;
	const ECatDomainCommandError Admission = EvaluateTakeAdmission(Command, InstanceIndex);
	if (Admission == ECatDomainCommandError::InvalidPayload)
	{
		// 载荷不合法就没有幂等键可算（键要用 StableNetId 和 RequestId），所以这一条必须在重放判定之前退回，也不进终态缓存。
		Result.Command.Error = Admission;
		return Result;
	}
	const FString TerminalKey = MakeTerminalKey(Command.Context.StableNetId, TEXT("TeamEquipmentTake"), Command.Context.RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("Expected=%lld|Instance=%s"), Command.Context.ExpectedRevision,
		*Command.InstanceId.ToString(EGuidFormats::DigitsWithHyphens));
	switch (CatQueryTerminalReplay(TerminalCache, TerminalPayloadByKey, TerminalKey, PayloadSignature, Result,
		[](FCatTeamEquipmentGrantResult& Replayed) { MarkCommandReplayed(Replayed.Command); }))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
	}
	const auto CacheAndReturn = [this, &TerminalKey, &PayloadSignature](FCatTeamEquipmentGrantResult& Terminal)
	{
		Terminal.Command.Revision = Snapshot.Revision;
		TerminalCache.Add(TerminalKey, Terminal);
		TerminalPayloadByKey.Add(TerminalKey, PayloadSignature);
		return Terminal;
	};
	if (Admission != ECatDomainCommandError::None)
	{
		Result.Command.Error = Admission;
		return CacheAndReturn(Result);
	}
	Result.Instance = Snapshot.Instances[InstanceIndex];
	Snapshot.Instances.RemoveAt(InstanceIndex);
	TakenInstanceById.Add(Result.Instance.InstanceId, Result.Instance);
	++Snapshot.Revision;
	Result.Command.bCommitted = true;
	Result.Command.Error = ECatDomainCommandError::None;
	CacheAndReturn(Result);
	OnLibraryChanged.Broadcast();
	return Result;
}

// 查找流程：先清输出，再按订单索引取实例 ID，最后先在库里、再在已取走表里定位那件实物；两边都没有时按没有入过库处理。
bool UCatTeamEquipmentLibrary::TryFindInstanceBySourceTransaction(const FGuid SourceTransactionId,
	FCatTeamEquipmentInstance& OutInstance) const
{
	OutInstance = FCatTeamEquipmentInstance();
	const FGuid* InstanceId = InstanceIdBySourceTransaction.Find(SourceTransactionId);
	if (!InstanceId)
	{
		return false;
	}
	const FCatTeamEquipmentInstance* Instance = Snapshot.Instances.FindByPredicate(
		[InstanceId](const FCatTeamEquipmentInstance& Candidate)
	{
		return Candidate.InstanceId == *InstanceId;
	});
	if (!Instance)
	{
		Instance = TakenInstanceById.Find(*InstanceId);
	}
	if (!Instance)
	{
		return false;
	}
	OutInstance = *Instance;
	return true;
}

// 关闭流程：只把入库 gate 置否，不清空库存和缓存；这样收摊之后仍读得到这局买过什么，重试也仍拿得回首次终态。
void UCatTeamEquipmentLibrary::CloseCommands()
{
	bCommandsOpen = false;
}

// 幂等键流程：只组合服务器身份、操作名和 RequestId；订单、实例与定义留给载荷签名比对。
FString UCatTeamEquipmentLibrary::MakeTerminalKey(const FString& StableNetId, const TCHAR* Operation, const FGuid RequestId)
{
	return FString::Printf(TEXT("%s|%s|%s"), *StableNetId, Operation,
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
}

// 载荷签名流程：冻结订单、定义和版本前提；同 RequestId 改任一项都不是同一次入库意图。
FString UCatTeamEquipmentLibrary::MakePayloadSignature(const FCatTeamEquipmentGrantCommand& Command)
{
	return FString::Printf(TEXT("Expected=%lld|Transaction=%s|Definition=%s"), Command.Context.ExpectedRevision,
		*Command.SourceTransactionId.ToString(EGuidFormats::DigitsWithHyphens), *Command.DefinitionId.ToString());
}
