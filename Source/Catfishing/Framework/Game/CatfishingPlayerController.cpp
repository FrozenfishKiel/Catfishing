#include "Framework/Game/CatfishingPlayerController.h"

#include "Framework/Core/CatStableNetId.h"
#include "Framework/Game/CatfishingGameMode.h"
#include "Framework/Game/CatfishingGameState.h"
#include "Framework/Game/CatfishingPlayerState.h"

#include "Camp/CatCampHubActor.h"
#include "Character/CatCharacter.h"
#include "Collection/CatRunImprintService.h"
#include "Condition/CatConditionComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatTeamEquipmentLibrary.h"
#include "Fishing/CatFishingService.h"
#include "Integration/CatChumContributionCoordinator.h"
#include "Integration/CatFishConsumptionCoordinator.h"
#include "Logging/CatLog.h"
#include "Online/CatOnlineSubsystem.h"
#include "Profile/CatProfileSubsystem.h"
#include "Run/CatSacrificeCoordinator.h"
#include "ShopEconomy/CatShopOrderCoordinator.h"
#include "Social/CatSocialService.h"

namespace
{
	// 领域命令结果日志流程：四条 Camp RPC 和野外自救 RPC 的终态都只有 RequestId/Committed/Error/Revision 四项可说，统
	// 一在这里按项目 Event= 风格写一行；Camp 与 Condition 自己不打这类日志，所以这是这些命令被拒时唯一的可观察点。
	void LogCampCommandResult(const TCHAR* EventName, const FCatDomainCommandResult& Result)
	{
		UE_LOG(LogCatfishing, Log, TEXT("Event=%s RequestId=%s Committed=%s Error=%s Revision=%lld"), EventName,
			*Result.RequestId.ToString(EGuidFormats::DigitsWithHyphens), Result.bCommitted ? TEXT("true") : TEXT("false"),
			*UEnum::GetValueAsString(Result.Error), Result.Revision);
	}
}

// ---- 生命周期与统一玩法 gate ----

// 接管流程：先让父类建立 Pawn 所有权与输入链，再记录最终双方类型；不缓存 Pawn，也不从 Controller 复制 StableNetId 到 Character。
void ACatfishingPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	UE_LOG(LogCatfishing, Log, TEXT("Event=controller_possessed Controller=%s Pawn=%s"),
		*GetClass()->GetName(), InPawn ? *InPawn->GetClass()->GetName() : TEXT("None"));
}

// Controller 玩法 gate 流程：现取当前 World 的 authority GameMode 并委托唯一判断；不缓存 GameMode 或身份，旅行、
// Logout 与 teardown 后会立即 fail-closed。
// 被拒时在这里统一打一行日志：所有玩法 RPC 都从这道门早退，门关着时如果不出声，PIE 里点按钮就只剩"没反应"，只能靠读源
// 码反推是 GameMode 缺失、命令门已关还是身份不在 Active 名单。
bool ACatfishingPlayerController::CanForwardGameplayCommand() const
{
	const ACatfishingGameModeBase* GameMode = GetWorld()
		? GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	const bool bAccepted = GameMode && GameMode->CanAcceptGameplayCommand(this);
	if (!bAccepted)
	{
		UE_LOG(LogCatfishing, Log, TEXT("Event=gameplay_command_rejected_by_gate Controller=%s HasGameMode=%s PlayerStateUniqueId=%s"),
			*GetName(), GameMode ? TEXT("true") : TEXT("false"),
			PlayerState && PlayerState->GetUniqueId().IsValid() ? *PlayerState->GetUniqueId()->ToString() : TEXT("None"));
	}
	return bAccepted;
}

// ---- Run：翻天确认与结算收口 ----

// Ready RPC 流程：先过统一玩法 gate，再转发 RequestId/ExpectedRevision/意图布尔值；GameMode 决定资格、个人复制值和全员 StateTree 事件。
void ACatfishingPlayerController::ServerSetNextDayReady_Implementation(const FGuid RequestId,
	const int64 ExpectedRevision, const bool bReady)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	FCatNextDayReadyCommand Command;
	Command.Context.RequestId = RequestId;
	Command.Context.ExpectedRevision = ExpectedRevision;
	Command.bReady = bReady;
	ACatfishingGameModeBase* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	const FCatRunCommandResult Result = GameMode
		? GameMode->SubmitNextDayReady(this, Command)
		: FCatRunCommandResult();
	UE_LOG(LogCatRun, Log, TEXT("Event=ready_command_result RequestId=%s Committed=%s Error=%s Revision=%lld"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens), Result.bCommitted ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(Result.Error), Result.Revision);
}

// 结算完成 RPC 流程：现取 authority GameMode/Imprint 服务并检查当前 Run 的计划终态与 Grant ACK；通过后才调用 Run 唯一
// 协调入口，不让客户端布尔值直接结束结算夜。
void ACatfishingPlayerController::ServerRequestSettlementCompletion_Implementation(const FGuid RequestId,
	const int64 ExpectedRevision)
{
	ACatfishingGameModeBase* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	UCatRunImprintService* Imprint = GetWorld() ? GetWorld()->GetSubsystem<UCatRunImprintService>() : nullptr;
	FCatRunCommandResult Result;
	Result.RequestId = RequestId;
	if (GameMode && Imprint && Imprint->IsSettlementArchiveReady(GameMode->GetRunPublicState().Phase.RunId))
	{
		Result = GameMode->CompleteSettlementFromCoordinator(RequestId, ExpectedRevision);
	}
	else
	{
		Result.Error = ECatRunCommandError::TeardownFailed;
	}
	UE_LOG(LogCatRun, Log, TEXT("Event=settlement_completion_result RequestId=%s Committed=%s Error=%s Revision=%lld"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens), Result.bCommitted ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(Result.Error), Result.Revision);
}

// ---- Profile Grant 与成像 CapturePlan 回执 ----

// Profile Grant 客户端流程：从当前 LocalPlayer 现取唯一 Profile 子系统并执行两阶段 durable 应用；只有返回 AckAllowed
// 才调用服务器 ACK，保存失败保持待重投。
// 三种会改动鱼图鉴摘要的 Grant（钓起、剪影、抄获）落盘后立刻回传新摘要；抄获也在其中，因为它推进 ScoopedCount 这一轨。
void ACatfishingPlayerController::ClientReceiveProfileGrant_Implementation(const FCatProfileGrant& Grant)
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UCatProfileSubsystem* Profile = LocalPlayer ? LocalPlayer->GetSubsystem<UCatProfileSubsystem>() : nullptr;
	const FCatProfileApplyResult Result = Profile ? Profile->ApplyGrant(Grant) : FCatProfileApplyResult();
	if (Result.bAckAllowed)
	{
		ServerAcknowledgeProfileGrant(Grant.GrantId);
		if (Grant.Kind == ECatProfileGrantKind::FishRecorded || Grant.Kind == ECatProfileGrantKind::FishSilhouette
			|| Grant.Kind == ECatProfileGrantKind::FishScooped)
		{
			TArray<FCatFishCollectionRecord> Records;
			if (Profile->GetFishCollectionSnapshot(Records))
			{
				ServerPublishPublicFishCollection(Records);
			}
		}
		if (Grant.Kind == ECatProfileGrantKind::Unlock)
		{
			// 解锁落盘后立刻把新清单报给服务器，否则这局里刚解锁的装备还是装不上。
			TArray<FName> UnlockIds;
			if (Profile->GetUnlockIdsSnapshot(UnlockIds))
			{
				ServerPublishEquipmentUnlocks(UnlockIds);
			}
		}
	}
}

// Profile ACK 服务器流程：先让 RunImprintService 以当前 Controller 核对并推进独立 DeliveryRecord；真实 ACK 成功或已重
// 放后再通知 GameMode 复核 Host exit 统一等待。
void ACatfishingPlayerController::ServerAcknowledgeProfileGrant_Implementation(const FGuid GrantId)
{
	if (UCatRunImprintService* Service = GetWorld() ? GetWorld()->GetSubsystem<UCatRunImprintService>() : nullptr)
	{
		const FCatDomainCommandResult AckResult = Service->AcknowledgeGrant(this, GrantId);
		if (AckResult.bCommitted || AckResult.Error == ECatDomainCommandError::AlreadyResolved)
		{
			if (ACatfishingGameModeBase* GameMode = GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>())
			{
				GameMode->NotifyHostExitGrantAckProgress();
			}
		}
	}
}

// CapturePlan 客户端流程：把计划交给本 LocalPlayer Profile 的外部成像桥；桥或本地依赖拒绝时立即回报失败，使服务器把该
// 计划收口为终态而不是永久重投阻塞结算。
void ACatfishingPlayerController::ClientReceiveImprintCapturePlan_Implementation(const FCatCapturePlan& Plan)
{
	bool bAcceptedByBridge = false;
	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (UCatProfileSubsystem* Profile = LocalPlayer->GetSubsystem<UCatProfileSubsystem>())
		{
			bAcceptedByBridge = Profile->ReceiveCapturePlan(Plan);
		}
	}
	if (!bAcceptedByBridge && Plan.CapturePlanId.IsValid())
	{
		ServerReportImprintCaptureResult(Plan.CapturePlanId, false, FGuid());
	}
}

// 成像结果服务器流程：只转交计划 ID、结果布尔和真实 ImprintId；服务端通过当前 PlayerState 校验接收者，客户端不能指定 Grant 内容。
void ACatfishingPlayerController::ServerReportImprintCaptureResult_Implementation(const FGuid CapturePlanId,
	const bool bSucceeded, const FGuid ImprintId)
{
	if (UCatRunImprintService* Service = GetWorld() ? GetWorld()->GetSubsystem<UCatRunImprintService>() : nullptr)
	{
		Service->ReportCaptureResult(this, CapturePlanId, bSucceeded, ImprintId);
	}
}

// ---- Fishing：开局、协作、抢抄与遛鱼意图 ----

// 钓鱼开始 RPC 流程：先过统一玩法 gate，再转交当前 Controller 与 RequestId；FishingService 重读 Run、Environment、水
// 域和权威协作能力，RequestId 不作为抽鱼熵。结果无论成败都落一行结构化日志，服务缺失按默认 DependencyUnavailable 记。
void ACatfishingPlayerController::ServerStartFishingSession_Implementation(const FGuid RequestId)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr;
	const FCatFishingStartResult Result = Fishing ? Fishing->StartFishingSession(this, RequestId) : FCatFishingStartResult();
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_start_command_result RequestId=%s Started=%s Error=%s SessionId=%s"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens), Result.bStarted ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(Result.Error), *Result.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens));
}

// 搏斗协作 RPC 流程：先过统一玩法 gate，再转交会话键、幂等键与 ExpectedRevision；Session 继续验证 Giant 与 HookedFight。
void ACatfishingPlayerController::ServerAssistFishingSession_Implementation(const FGuid FishingSessionId,
	const FGuid RequestId, const int64 ExpectedRevision)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	if (UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr)
	{
		Fishing->SubmitFightAssist(FishingSessionId, this, RequestId, ExpectedRevision);
	}
}

// 抢抄 RPC 流程：先过统一玩法 gate，再清客户端身份并用当前 Pawn 的鱼护覆盖目标；FishingSession/Items 决定首个合法 Compare-and-Commit。
void ACatfishingPlayerController::ServerRequestScoop_Implementation(const FGuid FishingSessionId,
	FCatScoopCommand Command)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	Command.Context.StableNetId.Reset();
	const ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	Command.TargetGuardContainerId = ControlledCharacter ? ControlledCharacter->GetPersonalFishGuardId() : FGuid();
	UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr;
	FCatScoopResult Result;
	Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
	if (Fishing)
	{
		Result = Fishing->RequestScoop(FishingSessionId, this, Command);
	}
	// Session 内部只在找到会话后才记 fishing_scoop_terminal；这里补上会话找不到、服务缺失这两类在 RPC 层就被拒的结果。
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_scoop_command_result SessionId=%s RequestId=%s Committed=%s Error=%s"),
		*FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
		*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		Result.Command.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Command.Error));
}

// 遛鱼意图 RPC 流程：先过统一玩法 gate，再把意图交给 FishingService 按本人身份路由到活跃会话；结果落一行结构化日志（会话找不到记 NotFound）。
void ACatfishingPlayerController::ServerSetFishingFightIntent_Implementation(const ECatFishingFightIntent Intent)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr;
	const FCatDomainCommandResult Result = Fishing ? Fishing->SubmitFightIntent(this, Intent) : FCatDomainCommandResult();
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_fight_intent_result Intent=%s Committed=%s Error=%s Revision=%lld"),
		*UEnum::GetValueAsString(Intent), Result.bCommitted ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(Result.Error), Result.Revision);
}

// ---- 献祭：本局额度的唯一来源 ----

// 献祭 RPC 流程：先过统一玩法 gate，清客户端身份后只调用唯一 Coordinator；Items 预留/提交与 Run apply 顺序不在 Controller 复制实现。
void ACatfishingPlayerController::ServerRequestSacrifice_Implementation(FCatSacrificeCommand Command)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	Command.Context.StableNetId.Reset();
	if (UCatSacrificeCoordinator* Coordinator = GetWorld() ? GetWorld()->GetSubsystem<UCatSacrificeCoordinator>() : nullptr)
	{
		Coordinator->RequestSacrifice(this, Command);
	}
}

// ---- Camp：休息、篝火回看、转鱼入缸与救援 ----

// 营地休息 RPC 流程：先过统一玩法 gate，再调用当前 World 的 Camp Actor；Camp 现取 Pawn/距离后把身体写入交给
// ConditionComponent。Camp 不在本 World 时按默认 InvalidPayload 记日志，不再静默丢弃。
void ACatfishingPlayerController::ServerRequestCampRest_Implementation(ACatCampHubActor* Camp, const FGuid RequestId)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (Camp && Camp->GetWorld() == GetWorld())
	{
		Result = Camp->RequestRest(this, RequestId);
	}
	LogCampCommandResult(TEXT("camp_rest_command_result"), Result);
}

// 篝火回看 RPC 流程：先过统一玩法 gate，再把 Controller 与 RequestId 交给固定 Camp；Camp 重验范围、结算阶段和全员在场。结果落结构化日志。
void ACatfishingPlayerController::ServerRequestCampfirePlayback_Implementation(ACatCampHubActor* Camp,
	const FGuid RequestId)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (Camp && Camp->GetWorld() == GetWorld())
	{
		Result = Camp->RequestCampfirePlayback(this, RequestId);
	}
	LogCampCommandResult(TEXT("camp_campfire_playback_command_result"), Result);
}

// 鱼护入缸 RPC 流程：先过统一玩法 gate，再转交两个 Revision 和鱼 ID；Camp/Items 重建身份、容器和容量事实。结果落结构化日志。
void ACatfishingPlayerController::ServerTransferFishToTank_Implementation(ACatCampHubActor* Camp,
	const FGuid RequestId, const FGuid FishInstanceId, const int64 ExpectedGuardRevision, const int64 ExpectedTankRevision)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (Camp && Camp->GetWorld() == GetWorld())
	{
		Result = Camp->TransferFishToTank(this, RequestId, FishInstanceId, ExpectedGuardRevision, ExpectedTankRevision);
	}
	LogCampCommandResult(TEXT("camp_transfer_fish_command_result"), Result);
}

// 搬运救援 RPC 流程：先过统一玩法 gate，再验证两个 Actor 属于当前 World 后交固定 Camp；Teleport 与倒地事实由 Camp/Condition 裁决。结果落结构化日志。
void ACatfishingPlayerController::ServerRescueCharacterToCamp_Implementation(ACatCampHubActor* Camp,
	ACatCharacter* TargetCharacter, const FGuid RequestId)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (Camp && TargetCharacter && Camp->GetWorld() == GetWorld() && TargetCharacter->GetWorld() == GetWorld())
	{
		Result = Camp->RescueToCamp(this, TargetCharacter, RequestId);
	}
	LogCampCommandResult(TEXT("camp_rescue_command_result"), Result);
}

// ---- Equipment 装配与 Items 直接进食 ----

// 装配 RPC 流程：先过统一玩法 gate，当前 Pawn 还必须是项目 Character；EquipmentComponent 用服务器目录验证三个定义并原子写入。
void ACatfishingPlayerController::ServerConfigureEquipment_Implementation(const FGuid RequestId,
	const int64 ExpectedRevision, const FName RodDefinitionId, const FName BaitDefinitionId, const FName FloatDefinitionId)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	if (UCatEquipmentComponent* Equipment = ControlledCharacter ? ControlledCharacter->GetEquipmentComponent() : nullptr)
	{
		Equipment->ConfigureLoadoutFromAuthority(RequestId, ExpectedRevision,
			RodDefinitionId, BaitDefinitionId, FloatDefinitionId);
	}
}

// 直接吃鱼 RPC 流程：先过统一玩法 gate，再确认客户端报的进食者确实是本 Controller 当前占有的 Pawn；
// 身份重建后整条链交给 Integration 的进食协调器。Controller 不读容器、不判共享缸距离、不碰 Condition，也不持有终态；
// 除下面这两条转发层自己就能判掉的以外，其余拒绝原因日志也都由协调器打。
void ACatfishingPlayerController::ServerConsumeFish_Implementation(ACatCharacter* EatingCharacter,
	FCatFishConsumeCommand Command)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	UCatFishConsumptionCoordinator* Coordinator = GetWorld()
		? GetWorld()->GetSubsystem<UCatFishConsumptionCoordinator>() : nullptr;
	if (EatingCharacter != GetPawn() || !Coordinator)
	{
		// 这两条是转发层自己就能判掉的拒绝，进不了协调器的终态缓存，所以在这里补上同一格式的可观察记录。
		UE_LOG(LogCatItems, Log, TEXT("Event=fish_consume_command_rejected RequestId=%s FishInstanceId=%s Reason=%s"),
			*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			*Command.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
			EatingCharacter != GetPawn() ? TEXT("EaterIsNotControlledPawn") : TEXT("CoordinatorUnavailable"));
		return;
	}
	Command.Context.StableNetId = CatResolveStableNetId(PlayerState);
	Coordinator->ConsumeFishFromContainer(Command, EatingCharacter);
}

// ---- Social：偷鱼协议、运行期权限与房主踢人 ----

// 偷鱼开始 RPC 流程：先过统一玩法 gate，清客户端身份后转交当前 Controller；Social 负责权限、单鱼上限、Timer 和 Items escrow。
void ACatfishingPlayerController::ServerBeginTheft_Implementation(FCatTheftCommand Command)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	Command.Context.StableNetId.Reset();
	if (UCatSocialService* Social = GetWorld() ? GetWorld()->GetSubsystem<UCatSocialService>() : nullptr)
	{
		ClientReceiveTheftResult(Social->BeginTheft(this, Command));
	}
}

// 偷鱼结果客户端流程：可靠接收服务器完整终态并整体替换本机读模型；ProtocolId、追回/吃掉/售出状态由此到达 UI，客户端不
// 能据本缓存修改 escrow、主人或钱包事实。
void ACatfishingPlayerController::ClientReceiveTheftResult_Implementation(const FCatTheftResult& Result)
{
	LastTheftResult = Result;
}

// 偷鱼结果读取流程：返回最近一次 Begin/Catch/Sell 的本机副本供界面取得 ProtocolId 和阶段；服务器授权仍重读当前 Controller/World 事实。
FCatTheftResult ACatfishingPlayerController::GetLastTheftResult() const
{
	return LastTheftResult;
}

// 偷鱼追回 RPC 流程：先过统一玩法 gate，再提交当前 Controller 与服务器 ProtocolId；客户端 RequestId 不参与定位
// escrow，Social 继续验证真实主人、状态和距离。
void ACatfishingPlayerController::ServerCatchTheft_Implementation(const FGuid TheftProtocolId)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	if (UCatSocialService* Social = GetWorld() ? GetWorld()->GetSubsystem<UCatSocialService>() : nullptr)
	{
		ClientReceiveTheftResult(Social->CatchTheft(this, TheftProtocolId));
	}
}

// 偷鱼售出 RPC 流程：先过统一玩法 gate，再交当前 Controller、ProtocolId、钱包版本和售价值；当前 Social 稳定 fail-closed，避免半事务删除鱼或写钱包。
void ACatfishingPlayerController::ServerSellStolenFish_Implementation(const FGuid TheftProtocolId,
	const FGuid RequestId, const int64 ExpectedWalletRevision)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	if (UCatSocialService* Social = GetWorld() ? GetWorld()->GetSubsystem<UCatSocialService>() : nullptr)
	{
		ClientReceiveTheftResult(Social->SellStolenFish(this, TheftProtocolId, RequestId, ExpectedWalletRevision));
	}
}
// Social 权限 RPC 流程：先过统一玩法 gate，再把两个开关值交给 Social；谁是局主由 Social 按已登记的房主身份判定。
// 只有真正改成功才把新策略推到 GameState：Social 在权限值没变时不推进版本，这里跟着它走，避免每次点击都发一份相同快照。
void ACatfishingPlayerController::ServerSetSocialPolicy_Implementation(const FGuid RequestId,
	const ECatDomainPolicy NewTheftPermission, const ECatDomainPolicy NewMischiefPermission)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	UCatSocialService* Social = GetWorld() ? GetWorld()->GetSubsystem<UCatSocialService>() : nullptr;
	if (!Social)
	{
		return;
	}
	const FCatDomainCommandResult Result = Social->SetSocialPolicy(this, RequestId, NewTheftPermission, NewMischiefPermission);
	if (!Result.bCommitted)
	{
		return;
	}
	if (ACatfishingGameState* CatGameState = GetWorld()->GetGameState<ACatfishingGameState>())
	{
		CatGameState->SetSocialPolicyFromAuthority(Social->GetSocialPolicy());
	}
}

// 踢人 RPC 流程：只做转发，全部裁决和收口都在 authority GameMode。
// 这里不复用 CanForwardGameplayCommand：那道 gate 会在 teardown 或结算夜关门，而房主此时仍然应该踢得动人；
// 请求者是不是 Active、是不是房主，GameMode 自己会重新验一遍。
void ACatfishingPlayerController::ServerRequestKickPlayer_Implementation(const FString& TargetStableNetId)
{
	ACatfishingGameModeBase* AuthGameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	if (!AuthGameMode)
	{
		return;
	}
	const ECatOnlineError KickError = AuthGameMode->RequestHostKick(this, TargetStableNetId);
	UE_LOG(LogCatOnline, Log, TEXT("Event=host_kick_rpc Result=%s"), *UEnum::GetValueAsString(KickError));
}

// ---- Shop：购买、免费自取与售鱼 ----

// 购买 RPC 流程：只声明这是付费订单，其余交给共用转发实现。
void ACatfishingPlayerController::ServerSubmitShopPurchase_Implementation(const FName EntryId,
	const FGuid RequestId, const int64 ExpectedWalletRevision)
{
	SubmitShopOrder(EntryId, RequestId, ExpectedWalletRevision, false);
}

// 免费自取 RPC 流程：只声明这是免费订单，其余交给共用转发实现。
void ACatfishingPlayerController::ServerClaimFreeShopEntry_Implementation(const FName EntryId,
	const FGuid RequestId, const int64 ExpectedWalletRevision)
{
	SubmitShopOrder(EntryId, RequestId, ExpectedWalletRevision, true);
}

// 商店订单转发流程：过玩法 gate、从本机 PlayerState 重建服务器身份、组装只含目录项 ID 的命令，最后交给订单协调器
// 必须走协调器而不是直接调商店服务：商店只会把订单记成 Pending，把东西真正放进团队装备库并回填交付状态的是协调器。
void ACatfishingPlayerController::SubmitShopOrder(const FName EntryId, const FGuid RequestId,
	const int64 ExpectedWalletRevision, const bool bFreeClaim)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	const APlayerState* CurrentPlayerState = PlayerState;
	UCatShopOrderCoordinator* Coordinator = GetWorld() ? GetWorld()->GetSubsystem<UCatShopOrderCoordinator>() : nullptr;
	if (!Coordinator || !CurrentPlayerState || !CurrentPlayerState->GetUniqueId().IsValid())
	{
		return;
	}
	FCatShopPurchaseCommand Command;
	Command.Context.RequestId = RequestId;
	Command.Context.ExpectedRevision = ExpectedWalletRevision;
	// 身份一律从服务器上的 PlayerState 重建，与 GameMode 的准入键同源；客户端提交的任何身份字段都不存在。
	Command.Context.StableNetId = CatResolveStableNetId(CurrentPlayerState);
	Command.EntryId = EntryId;
	// 耗材类订单要落到买家自己身上：Equipment 组件从本 Controller 当前 Pawn 上取，客户端没有指定收货人的入口。
	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	UCatEquipmentComponent* RecipientEquipment = ControlledCharacter ? ControlledCharacter->GetEquipmentComponent() : nullptr;
	const FCatShopOrderResult Result = bFreeClaim
		? Coordinator->SubmitFreeClaim(Command, RecipientEquipment)
		: Coordinator->SubmitPurchase(Command, RecipientEquipment);
	UE_LOG(LogCatfishing, Log,
		TEXT("Event=shop_order_submitted RequestId=%s EntryId=%s Free=%s Order=%s Delivery=%s"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens), *EntryId.ToString(),
		bFreeClaim ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(Result.Transaction.Command.Error),
		*UEnum::GetValueAsString(Result.Delivery.Error));
}

// 售鱼 RPC 流程：过玩法 gate、重建服务器身份，再把鱼、容器和两个版本前提交给订单协调器跑完整条链。
// 这里刻意不传价格和重量：价格由服务器查体重轴现算，重量取自服务器上的鱼实例，客户端报什么都不会被读。
void ACatfishingPlayerController::ServerSellFish_Implementation(const FGuid FishInstanceId, const FGuid ContainerId,
	const int64 ExpectedContainerRevision, const ECatShopFishSaleSource SourceKind, const FGuid RequestId,
	const int64 ExpectedWalletRevision)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	const APlayerState* CurrentPlayerState = PlayerState;
	UCatShopOrderCoordinator* Coordinator = GetWorld() ? GetWorld()->GetSubsystem<UCatShopOrderCoordinator>() : nullptr;
	if (!Coordinator || !CurrentPlayerState || !CurrentPlayerState->GetUniqueId().IsValid())
	{
		return;
	}
	FCatShopFishSaleOrderCommand Command;
	Command.Context.RequestId = RequestId;
	Command.Context.ExpectedRevision = ExpectedWalletRevision;
	Command.Context.StableNetId = CatResolveStableNetId(CurrentPlayerState);
	Command.FishInstanceId = FishInstanceId;
	Command.ContainerId = ContainerId;
	Command.ExpectedContainerRevision = ExpectedContainerRevision;
	Command.SourceKind = SourceKind;
	const FCatShopOrderResult Result = Coordinator->SubmitFishSale(Command);
	UE_LOG(LogCatfishing, Log,
		TEXT("Event=shop_fish_sale_submitted RequestId=%s FishInstanceId=%s Source=%s Wallet=%s Items=%s"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		*FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*UEnum::GetValueAsString(SourceKind),
		*UEnum::GetValueAsString(Result.Transaction.Command.Error),
		*UEnum::GetValueAsString(Result.Delivery.Error));
}

// ---- Social：求助、恶作剧与防骚扰牌 ----

// 手动求助 RPC 流程：先过统一玩法 gate，再转交 Controller、RequestId 和 Manual 类型；Social 拒绝客户端伪造 Giant 提示。
void ACatfishingPlayerController::ServerRequestManualHelp_Implementation(const FGuid RequestId,
	const ECatHelpSignalKind Kind)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	if (UCatSocialService* Social = GetWorld() ? GetWorld()->GetSubsystem<UCatSocialService>() : nullptr)
	{
		Social->RequestManualHelp(this, RequestId, Kind);
	}
}

// 恶作剧 RPC 流程：先过统一玩法 gate，再从当前 World 按 PlayerState 定位目标；找到后交 Social 做权限、双方距离与
// ProtectionSign 裁决。Social 不做频率限制，这里也不预先节流。
void ACatfishingPlayerController::ServerRequestMischief_Implementation(APlayerState* TargetPlayerState,
	const FGuid RequestId)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	APlayerController* TargetController = nullptr;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* Candidate = It->Get(); Candidate && Candidate->PlayerState == TargetPlayerState)
		{
			TargetController = Candidate;
			break;
		}
	}
	if (UCatSocialService* Social = GetWorld() ? GetWorld()->GetSubsystem<UCatSocialService>() : nullptr)
	{
		// Social 的裁决全部读服务器上双方 Pawn 的权威位置，没有任何一步消费客户端坐标。
		Social->RequestMischief(this, TargetController, RequestId);
	}
}

// 放牌 RPC 流程：先过统一玩法 gate，再转交 Controller、RequestId 和期望位置；Social 重读 Pawn、配置范围并保证每人唯一 Actor。
void ACatfishingPlayerController::ServerPlaceProtectionSign_Implementation(const FGuid RequestId,
	const FVector SignLocation)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	if (UCatSocialService* Social = GetWorld() ? GetWorld()->GetSubsystem<UCatSocialService>() : nullptr)
	{
		Social->PlaceProtectionSign(this, RequestId, SignLocation);
	}
}

// ---- Condition：抖水与野外自救 ----

// 抖水完成流程：先过统一玩法 gate，再取得当前 Character 并验证 RequestId/身体组件；通过后只清 Wet，保留其他身体事实。
void ACatfishingPlayerController::ServerCompleteShakeDry_Implementation(const FGuid RequestId)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	if (RequestId.IsValid() && ControlledCharacter && ControlledCharacter->GetConditionComponent())
	{
		ControlledCharacter->GetConditionComponent()->SetWetFromAuthority(false);
	}
}

// 野外自救 RPC 流程：先过统一玩法 gate，再把当前 Pawn 的 ConditionComponent 作为唯一写口提交自救；没有 Pawn 或身体组
// 件时按默认 InvalidPayload 记日志，不静默丢弃。
// 倒地与否、阈值是否已裁、重放都由 Condition 裁决，Controller 不重复判断。
void ACatfishingPlayerController::ServerRequestFieldSelfRecovery_Implementation(const FGuid RequestId)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	if (UCatConditionComponent* Conditions = ControlledCharacter ? ControlledCharacter->GetConditionComponent() : nullptr)
	{
		Result = Conditions->RequestFieldSelfRecovery(this, RequestId);
	}
	LogCampCommandResult(TEXT("condition_field_self_recovery_command_result"), Result);
}

// ---- Environment：玩家投窝 ----

// 玩家窝料 RPC 流程：先过统一玩法 gate，再把客户端给的落点、耗材 ID 和两个版本前提装进命令，
// 身份从本机 PlayerState 重建、投掷者取本 Controller 当前占有的 Pawn；
// "预留耗材 → 写入窝点 → 提交消耗"整条链和它的幂等缓存都在 Integration 的投窝协调器里，Controller 不做落点判定。
void ACatfishingPlayerController::ServerContributeChum_Implementation(const FVector DropLocation,
	const FGuid RequestId, const int64 ExpectedEquipmentRevision, const int64 ExpectedChumRevision,
	const FName ChumDefinitionId)
{
	if (!CanForwardGameplayCommand())
	{
		return;
	}
	UCatChumContributionCoordinator* Coordinator = GetWorld()
		? GetWorld()->GetSubsystem<UCatChumContributionCoordinator>() : nullptr;
	if (!Coordinator)
	{
		UE_LOG(LogCatItems, Warning, TEXT("Event=player_chum_coordinator_unavailable RequestId=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
		return;
	}
	FCatChumContributionCommand Command;
	Command.Context.RequestId = RequestId;
	Command.Context.ExpectedRevision = ExpectedChumRevision;
	Command.Context.StableNetId = CatResolveStableNetId(PlayerState);
	Command.DropLocation = DropLocation;
	Command.ChumDefinitionId = ChumDefinitionId;
	Command.ExpectedEquipmentRevision = ExpectedEquipmentRevision;
	Coordinator->ContributePlayerChum(Command, Cast<ACatCharacter>(GetPawn()));
}

// ---- Online / HostExit：主动离局标记与退出回执 ----

// 主动离局 RPC 流程：只把当前 Controller 交给 authority GameMode；标记不销毁 Session、不旅行，并由随后 Logout 精确消费。
void ACatfishingPlayerController::ServerMarkVoluntaryLeave_Implementation()
{
	if (ACatfishingGameModeBase* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr)
	{
		GameMode->MarkVoluntaryLeave(this);
	}
}

// Host exit 客户端流程：从本地 GameInstance 取得唯一 Online 子系统并提交服务器关联 RequestId；子系统只在本地
// DestroySession 成功后回 ACK，失败由 Host 有界超时收口。
void ACatfishingPlayerController::ClientPrepareForHostExit_Implementation(const FGuid RequestId)
{
	UGameInstance* GameInstance = GetGameInstance();
	if (UCatOnlineSubsystem* Online = GameInstance ? GameInstance->GetSubsystem<UCatOnlineSubsystem>() : nullptr)
	{
		Online->RequestRemoteHostExit(RequestId);
	}
}

// Host exit ACK 服务器流程：只把当前 Controller 与关联键交给 authority GameMode；RPC 自身不销毁 Session、不旅行或更改 Run。
void ACatfishingPlayerController::ServerAcknowledgeHostExit_Implementation(const FGuid RequestId)
{
	if (ACatfishingGameModeBase* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr)
	{
		GameMode->AcknowledgeHostExitClient(this, RequestId);
	}
}

// ---- Profile 投影上报：公开鱼图鉴与装备解锁 ----

// 公开投影刷新客户端流程：从当前 LocalPlayer durable Profile 只读取 FishCollection 与 UnlockIds；各自读取成功才提交服务器，绝不附带相册或 Journal。
void ACatfishingPlayerController::ClientRefreshPublicFishCollection_Implementation()
{
	TArray<FCatFishCollectionRecord> Records;
	TArray<FName> UnlockIds;
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UCatProfileSubsystem* Profile = LocalPlayer ? LocalPlayer->GetSubsystem<UCatProfileSubsystem>() : nullptr;
	if (Profile && Profile->GetFishCollectionSnapshot(Records))
	{
		ServerPublishPublicFishCollection(Records);
	}
	if (Profile && Profile->GetUnlockIdsSnapshot(UnlockIds))
	{
		ServerPublishEquipmentUnlocks(UnlockIds);
	}
}

// 公开图鉴发布服务器流程：只允许当前 Controller 自己的项目 PlayerState 接收，并让 PlayerState 完整校验后整体复制。
void ACatfishingPlayerController::ServerPublishPublicFishCollection_Implementation(const TArray<FCatFishCollectionRecord>& Records)
{
	if (ACatfishingPlayerState* CatPlayerState = GetPlayerState<ACatfishingPlayerState>())
	{
		CatPlayerState->SetPublicFishCollectionFromAuthority(Records);
	}
}

// 解锁清单发布服务器流程：只允许当前 Controller 自己的项目 PlayerState 接收（没有指定别人 PlayerState 的入口），由
// PlayerState 校验后持有并复制；结果只记日志。
void ACatfishingPlayerController::ServerPublishEquipmentUnlocks_Implementation(const TArray<FName>& UnlockIds)
{
	ACatfishingPlayerState* CatPlayerState = GetPlayerState<ACatfishingPlayerState>();
	const bool bAccepted = CatPlayerState && CatPlayerState->SetAuthorizedEquipmentUnlocksFromAuthority(UnlockIds);
	UE_LOG(LogCatfishing, Log, TEXT("Event=equipment_unlocks_published Count=%d Accepted=%s"),
		UnlockIds.Num(), bAccepted ? TEXT("true") : TEXT("false"));
}

// ---- 团队装备库：取用并装配（跨聚合 saga，见类注释） ----

// 取用团队装备 RPC 流程：过玩法 gate 和本 Controller 的幂等缓存；首次执行先让装备库只读预检这条取用命令，
// 预检通过（并顺带拿到那件实物）才让本人 Equipment 装配，装配成功后再真正从库里取走。
// 装配是这条链上的不可逆点，所以"库同不同意"必须问在它前面：装备库的写口会在结算夜被关掉，
// 而玩法命令 gate 那时仍然开着，先装后问就会出现东西已经在猫身上、库里那件却还在，别人可以把同一件再取一次。
// 装备库没有归还写口（归还要凭空造实例、且飞书未裁取用/归还规则），这种分叉出现后无法自愈，因此只能靠前置预检堵住。
// 预检通过之后取走那一步不会再失败：服务器单线程，两次调用之间没有别的写入能插进来；万一真发生就记 Error 留痕，不回滚装配。
void ACatfishingPlayerController::ServerTakeTeamEquipment_Implementation(const FGuid InstanceId, const FGuid RequestId,
	const int64 ExpectedLibraryRevision, const int64 ExpectedEquipmentRevision)
{
	if (!CanForwardGameplayCommand() || !RequestId.IsValid())
	{
		return;
	}
	const FString PayloadSignature = FString::Printf(TEXT("Instance=%s|LibraryRevision=%lld|EquipmentRevision=%lld"),
		*InstanceId.ToString(EGuidFormats::DigitsWithHyphens), ExpectedLibraryRevision, ExpectedEquipmentRevision);
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	switch (CatQueryTerminalReplay(TakeTeamEquipmentTerminalCache, TakeTeamEquipmentPayloadByRequest, RequestId,
		PayloadSignature, Result, MarkCommandReplayed))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		UE_LOG(LogCatfishing, Warning, TEXT("Event=team_equipment_take_payload_mismatch RequestId=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
		return;
	case ECatTerminalReplayOutcome::Replayed:
		UE_LOG(LogCatfishing, Log, TEXT("Event=team_equipment_take_replayed RequestId=%s Error=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens), *UEnum::GetValueAsString(Result.Error));
		return;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
	}
	ACatCharacter* ControlledCharacter = Cast<ACatCharacter>(GetPawn());
	UCatEquipmentComponent* Equipment = ControlledCharacter ? ControlledCharacter->GetEquipmentComponent() : nullptr;
	UCatTeamEquipmentLibrary* Library = GetWorld() ? GetWorld()->GetSubsystem<UCatTeamEquipmentLibrary>() : nullptr;
	const APlayerState* CurrentPlayerState = PlayerState;
	if (!Equipment || !Library || !CurrentPlayerState || !CurrentPlayerState->GetUniqueId().IsValid())
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else
	{
		// 这条命令原样用于预检和取走两次调用；两次载荷一致，装备库的重放判定才不会把真正取走那次当成载荷漂移。
		FCatTeamEquipmentTakeCommand Take;
		Take.Context.RequestId = RequestId;
		Take.Context.ExpectedRevision = ExpectedLibraryRevision;
		Take.Context.StableNetId = CatResolveStableNetId(CurrentPlayerState);
		Take.InstanceId = InstanceId;
		// 实例在不在库里、库版本对不对得上、写口还开不开着，全部由装备库自己回答；Controller 不再抄一份同样的规则，
		// 否则两处判据一旦漂移，就会重新出现"Controller 认为能取、装备库其实不给"的分叉。
		FCatTeamEquipmentInstance Instance;
		const ECatDomainCommandError TakeAdmission = Library->ValidateTake(Take, Instance);
		if (TakeAdmission != ECatDomainCommandError::None)
		{
			Result.Error = TakeAdmission;
		}
		else
		{
			Result = Equipment->EquipFromTeamLibraryFromAuthority(RequestId, ExpectedEquipmentRevision, Instance);
			if (Result.bCommitted)
			{
				const FCatTeamEquipmentGrantResult Taken = Library->TakeInstance(Take);
				if (!Taken.Command.bCommitted)
				{
					UE_LOG(LogCatfishing, Error, TEXT("Event=team_equipment_take_failed_after_equip RequestId=%s Error=%s"),
						*RequestId.ToString(EGuidFormats::DigitsWithHyphens), *UEnum::GetValueAsString(Taken.Command.Error));
				}
			}
		}
	}
	TakeTeamEquipmentTerminalCache.Add(RequestId, Result);
	TakeTeamEquipmentPayloadByRequest.Add(RequestId, PayloadSignature);
	UE_LOG(LogCatfishing, Log, TEXT("Event=team_equipment_take_terminal RequestId=%s Instance=%s Committed=%s Error=%s EquipmentRevision=%lld"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens), *InstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		Result.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Error), Result.Revision);
}
