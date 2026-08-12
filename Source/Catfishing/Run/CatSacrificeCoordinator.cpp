#include "Run/CatSacrificeCoordinator.h"

#include "CatGameplayTypes.h"
#include "Engine/World.h"
#include "Fishing/CatFishingService.h"
#include "GameFramework/PlayerState.h"
#include "Items/CatItemsService.h"
#include "Social/CatSocialService.h"

// 创建条件流程：只在 authority Game World 创建协议协调器；客户端不能预留鱼或增加额度。
bool UCatSacrificeCoordinator::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

// 反初始化流程：关闭命令并清协议；World 已销毁时不尝试跨进程恢复内存事务。
void UCatSacrificeCoordinator::Deinitialize()
{
	bCommandsOpen = false;
	Protocols.Reset();
	Super::Deinitialize();
}

// 献祭请求流程：同 RequestId 先重放或重试 ItemsCommitted；首次请求依次预留、Run 预检、Items 不可逆提交和 Run apply，预检失败只在 commit 前取消。
FCatSacrificeResult UCatSacrificeCoordinator::RequestSacrifice(AController* RequestingController,
	const FCatSacrificeCommand& Command)
{
	FCatSacrificeResult Rejected;
	Rejected.RequestId = Command.Context.RequestId;
	Rejected.Error = ECatDomainCommandError::InvalidPayload;
	const FString StableNetId = ResolveStableNetId(RequestingController);
	UCatItemsService* Items = GetWorld() ? GetWorld()->GetSubsystem<UCatItemsService>() : nullptr;
	ACatfishingGameModeBase* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	if (!bCommandsOpen)
	{
		Rejected.Error = ECatDomainCommandError::CommandsClosed;
		return Rejected;
	}
	if (!Command.Context.RequestId.IsValid() || !Command.FishInstanceId.IsValid() || !Command.ContainerId.IsValid()
		|| StableNetId.IsEmpty() || !Items || !GameMode)
	{
		return Rejected;
	}
	const FString ProtocolKey = MakeProtocolKey(StableNetId, Command.Context.RequestId);
	if (FProtocolRecord* Existing = Protocols.Find(ProtocolKey))
	{
		return Existing->Result.Stage == ECatSacrificeStage::ItemsCommitted
			? ApplyCommittedRecord(*Existing) : Existing->Result;
	}

	FProtocolRecord& Record = Protocols.Add(ProtocolKey);
	Record.Command = Command;
	Record.Command.Context.StableNetId = StableNetId;
	Record.Result.RequestId = Command.Context.RequestId;
	Record.Result.Stage = ECatSacrificeStage::Received;
	const FCatFishReservationResult Reservation = Items->ReserveFish(Record.Command);
	if (!Reservation.bReserved)
	{
		Record.Result.Stage = ECatSacrificeStage::Failed;
		Record.Result.Error = Reservation.Error;
		return Record.Result;
	}
	Record.Result.Stage = ECatSacrificeStage::Reserved;
	Record.Result.ItemsRevision = Reservation.ContainerRevision;
	Record.Result.AppliedContribution = Reservation.SacrificeContribution;
	FCatQuotaContributionCommand QuotaCommand;
	QuotaCommand.Context.RequestId = Record.Command.Context.RequestId;
	QuotaCommand.Context.ExpectedRevision = Record.Command.ExpectedRunRevision;
	QuotaCommand.Context.StableNetId = StableNetId;
	QuotaCommand.Contribution = Reservation.SacrificeContribution;
	const FCatRunCommandResult Validation = GameMode->ValidateCommittedQuotaContributionFromCoordinator(QuotaCommand);
	if (Validation.Error != ECatRunCommandError::None)
	{
		Items->CancelFishReservation(StableNetId, Record.Command.Context.RequestId, Record.Command.ContainerId);
		Record.Result.Stage = ECatSacrificeStage::Failed;
		Record.Result.Error = MapRunError(Validation.Error);
		Record.Result.RunRevision = Validation.Revision;
		return Record.Result;
	}
	Record.Result.Stage = ECatSacrificeStage::RunAccepted;
	const FCatFishReservationCommitResult ItemsCommit = Items->CommitFishReservation(
		StableNetId, Record.Command.Context.RequestId, Record.Command.ContainerId);
	if (!ItemsCommit.bCommitted)
	{
		Items->CancelFishReservation(StableNetId, Record.Command.Context.RequestId, Record.Command.ContainerId);
		Record.Result.Stage = ECatSacrificeStage::Failed;
		Record.Result.Error = ItemsCommit.Error;
		return Record.Result;
	}
	Record.Result.Stage = ECatSacrificeStage::ItemsCommitted;
	Record.Result.ItemsRevision = ItemsCommit.ContainerRevision;
	Record.Result.AppliedContribution = ItemsCommit.SacrificeContribution;
	return ApplyCommittedRecord(Record);
}
// Teardown 收口流程：先永久关献祭新入口，Received/Reserved 协议取消可逆锁，ItemsCommitted 之后的协议只重试 Run apply 而绝不还鱼；再终止 Fishing。Social 必须趁 Items 仍开放时清 Timer 并返还 theft escrow，成功后才关 Items/取消剩余预留；任一分支失败都返回 false，上层不得继续 DestroySession。
bool UCatSacrificeCoordinator::PrepareForRunTeardown()
{
	bCommandsOpen = false;
	UCatItemsService* Items = GetWorld() ? GetWorld()->GetSubsystem<UCatItemsService>() : nullptr;
	UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr;
	UCatSocialService* Social = GetWorld() ? GetWorld()->GetSubsystem<UCatSocialService>() : nullptr;
	bool bAllCommittedApplied = Items != nullptr && Fishing != nullptr && Social != nullptr;
	for (TPair<FString, FProtocolRecord>& Pair : Protocols)
	{
		FProtocolRecord& Record = Pair.Value;
		if (Record.Result.Stage == ECatSacrificeStage::Reserved || Record.Result.Stage == ECatSacrificeStage::RunAccepted)
		{
			if (Items)
			{
				Items->CancelFishReservation(Record.Command.Context.StableNetId,
					Record.Command.Context.RequestId, Record.Command.ContainerId);
				Record.Result.Stage = ECatSacrificeStage::Cancelled;
				Record.Result.Error = ECatDomainCommandError::Cancelled;
			}
			else
			{
				bAllCommittedApplied = false;
			}
		}
		else if (Record.Result.Stage == ECatSacrificeStage::ItemsCommitted)
		{
			bAllCommittedApplied &= ApplyCommittedRecord(Record).bCompleted;
		}
	}
	if (Fishing)
	{
		Fishing->CloseCommandsAndTerminateAll();
	}
	// Social 必须在 Items 命令门仍开放时清 Timer 并返还 theft escrow；失败会阻止 Host teardown 进入 ACK/Destroy，而不是留下半协议后继续关 Items。
	const bool bSocialResolved = Social && Social->CloseCommandsAndResolveAll();
	bAllCommittedApplied &= bSocialResolved;
	if (Items && bSocialResolved)
	{
		Items->CloseCommandsAndCancelReservations();
	}
	return bAllCommittedApplied;
}

// 身份解析流程：只读取当前 PlayerState::UniqueId；协调器随后把字符串冻结在服务器协议记录中，玩家掉线后仍可补 Run。
FString UCatSacrificeCoordinator::ResolveStableNetId(const AController* Controller)
{
	const APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
	return PlayerState && PlayerState->GetUniqueId().IsValid() ? PlayerState->GetUniqueId()->ToString() : FString();
}

// 协议键构造流程：固定命令类别并加入服务器适配身份；裸 RequestId 不再允许一个玩家命中另一个玩家的献祭记录。
FString UCatSacrificeCoordinator::MakeProtocolKey(const FString& StableNetId, const FGuid& RequestId)
{
	return FString::Printf(TEXT("%s|Sacrifice|%s"), *StableNetId,
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
}

// 错误映射流程：把 Run 的身份/阶段/Revision/依赖/关闭语义映射到跨领域公共错误，未分类拒绝保持 DependencyUnavailable。
ECatDomainCommandError UCatSacrificeCoordinator::MapRunError(const ECatRunCommandError Error)
{
	switch (Error)
	{
	case ECatRunCommandError::None: return ECatDomainCommandError::None;
	case ECatRunCommandError::PolicyUndecided: return ECatDomainCommandError::PolicyUndecided;
	case ECatRunCommandError::CommandsClosed: return ECatDomainCommandError::CommandsClosed;
	case ECatRunCommandError::InvalidPhase: return ECatDomainCommandError::InvalidPhase;
	case ECatRunCommandError::InvalidIdentity: return ECatDomainCommandError::InvalidIdentity;
	case ECatRunCommandError::InvalidPayload: return ECatDomainCommandError::InvalidPayload;
	case ECatRunCommandError::RevisionConflict: return ECatDomainCommandError::RevisionConflict;
	case ECatRunCommandError::AlreadyResolved: return ECatDomainCommandError::AlreadyResolved;
	default: return ECatDomainCommandError::DependencyUnavailable;
	}
}

// Run apply 流程：以冻结身份、外部 RequestId、最新保存的预期 Revision 和已 committed 贡献调用唯一 GameMode 写口；失败保持 ItemsCommitted，成功推进 RunApplied→Completed。
FCatSacrificeResult UCatSacrificeCoordinator::ApplyCommittedRecord(FProtocolRecord& Record)
{
	ACatfishingGameModeBase* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	if (!GameMode || Record.Result.Stage != ECatSacrificeStage::ItemsCommitted)
	{
		Record.Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Record.Result;
	}
	FCatQuotaContributionCommand QuotaCommand;
	QuotaCommand.Context.RequestId = Record.Command.Context.RequestId;
	// Items 已不可逆提交后不能因旧 Revision 丢鱼；重试以当前 Run Revision 重新校验阶段/写口，并继续复用同一 RequestId 防止重复额度。
	QuotaCommand.Context.ExpectedRevision = GameMode->GetRunPublicState().Revision;
	QuotaCommand.Context.StableNetId = Record.Command.Context.StableNetId;
	QuotaCommand.Contribution = Record.Result.AppliedContribution;
	const FCatRunCommandResult RunResult = GameMode->SubmitCommittedQuotaContributionFromCoordinator(QuotaCommand);
	Record.Result.RunRevision = RunResult.Revision;
	if (!RunResult.bCommitted && RunResult.Error != ECatRunCommandError::AlreadyResolved)
	{
		Record.Result.Error = MapRunError(RunResult.Error);
		return Record.Result;
	}
	Record.Result.Stage = ECatSacrificeStage::RunApplied;
	Record.Result.Error = ECatDomainCommandError::None;
	Record.Result.Stage = ECatSacrificeStage::Completed;
	Record.Result.bCompleted = true;
	return Record.Result;
}
