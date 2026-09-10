#include "Social/CatSocialService.h"

#include "Camp/CatCampSettings.h"
#include "Character/CatCharacter.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "Logging/CatLog.h"
#include "Condition/CatConditionComponent.h"
#include "Collection/CatRunImprintService.h"
#include "Data/CatFishDefinition.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Inventory/CatInventoryAccessRules.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryStatics.h"
#include "Social/CatProtectionSignActor.h"
#include "Social/CatSocialSettings.h"
#include "TimerManager.h"

// 创建条件流程：只允许 authority Game World 持有权限、Timer 和偷鱼协议；客户端没有平行 Social 写状态。
bool UCatSocialService::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

// 反初始化流程：复用统一关门/返还路径，随后清一局缓存和索引；若 World 依赖已先失效，不伪造 returned，但仍释放本服务内存。
void UCatSocialService::Deinitialize()
{
	CloseCommandsAndResolveAll();
	ActiveThefts.Reset();
	ActiveTheftByThief.Reset();
	LastMischiefTimeByPlayer.Reset();
	LastManualHelpTimeByPlayer.Reset();
	CommandTerminalCache.Reset();
	TheftTerminalCache.Reset();
	ProtectionSignByPlayer.Reset();
	Super::Deinitialize();
}

// Teardown 关门流程：先永久拒绝新 Social 命令，再复制活跃 ProtocolId、逐条清唯一 Timer 并把鱼实例放回来源库存；失败协议保留并返回 false，调用方不得假装全部收口。
bool UCatSocialService::CloseCommandsAndResolveAll()
{
	bCommandsOpen = false;
	TArray<FGuid> Requests;
	ActiveThefts.GetKeys(Requests);
	bool bAllResolved = true;
	for (const FGuid& TheftProtocolId : Requests)
	{
		if (FActiveTheft* Theft = ActiveThefts.Find(TheftProtocolId); Theft && GetWorld())
		{
			GetWorld()->GetTimerManager().ClearTimer(Theft->EatingWindowTimer);
		}
		const FCatTheftResult ReturnResult = ReturnActiveTheft(TheftProtocolId);
		bAllResolved &= ReturnResult.bReturned || ReturnResult.bConsumed;
	}
	return bAllResolved && ActiveThefts.IsEmpty();
}

// 偷鱼开始流程：先按身份/操作/客户端 RequestId 重放，再验证来源库存宿主、偷取者、鱼实例原捕获者和距离；随后从库存槽真实移除鱼实例并开启唯一 Timer。
FCatTheftResult UCatSocialService::BeginTheft(AController* ThiefController, const FCatTheftCommand& Command)
{
	FCatTheftResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	Result.Body.RequestId = Command.Context.RequestId;
	const UCatSocialSettings* Settings = GetDefault<UCatSocialSettings>();
	const FString ThiefStableNetId = ResolveStableNetId(ThiefController);
	ACatCharacter* ThiefCharacter = ThiefController ? Cast<ACatCharacter>(ThiefController->GetPawn()) : nullptr;
	if (ThiefStableNetId.IsEmpty() || !Command.Context.RequestId.IsValid())
	{
		Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
		return Result;
	}
	const FString TerminalKey = MakeTerminalKey(ThiefStableNetId, TEXT("BeginTheft"), Command.Context.RequestId);
	if (const FCatTheftResult* Cached = TheftTerminalCache.Find(TerminalKey))
	{
		Result = *Cached;
		MarkCommandReplayed(Result.Command);
		return Result;
	}
	const auto Finish = [this, &TerminalKey](const FCatTheftResult& TerminalResult)
	{
		TheftTerminalCache.Add(TerminalKey, TerminalResult);
		return TerminalResult;
	};
	if (!bCommandsOpen)
	{
		Result.Command.Error = ECatDomainCommandError::CommandsClosed;
		return Finish(Result);
	}
	AActor* SourceInventoryHost = Command.SourceInventoryHost.Get();
	const UCatCampSettings* CampSettings = GetDefault<UCatCampSettings>();
	if (!Settings->IsTheftReady() || !IsCharacterSociallyActive(ThiefCharacter)
		|| !SourceInventoryHost || SourceInventoryHost->GetWorld() != GetWorld()
		|| !CatInventoryAccessRules::IsHostReachable(SourceInventoryHost, ThiefCharacter, CampSettings))
	{
		Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
		return Finish(Result);
	}
	if (ActiveTheftByThief.Contains(ThiefStableNetId))
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPhase;
		return Finish(Result);
	}
	TArray<UCatInventoryComponent*> SourceInventories;
	UCatInventoryStatics::AppendInventoryComponentsFromActor(SourceInventoryHost, SourceInventories);
	UCatInventoryComponent* SourceInventory = nullptr;
	const FCatInventoryEntry* SourceEntry = nullptr;
	UCatFishInventoryItemInstance* SourceFish = nullptr;
	for (UCatInventoryComponent* CandidateInventory : SourceInventories)
	{
		const FCatInventoryEntry* CandidateEntry = CandidateInventory
			? CandidateInventory->GetInventoryEntryAtSlot(Command.SourceInventorySlotIndex) : nullptr;
		UCatFishInventoryItemInstance* CandidateFish = CandidateEntry
			? Cast<UCatFishInventoryItemInstance>(CandidateEntry->Instance.Get()) : nullptr;
		if (CandidateFish && CandidateFish->GetItemInstanceId() == Command.FishItemInstanceId
			&& CandidateEntry->StackCount == 1)
		{
			SourceInventory = CandidateInventory;
			SourceEntry = CandidateEntry;
			SourceFish = CandidateFish;
			break;
		}
	}
	if (!SourceInventory || !SourceEntry || !SourceFish)
	{
		Result.Command.Error = ECatDomainCommandError::NotFound;
		return Finish(Result);
	}
	AController* FishOwnerController = FindControllerByStableNetId(SourceFish->GetFishOwnerStableNetId());
	const ACatCharacter* FishOwnerCharacter = FishOwnerController ? Cast<ACatCharacter>(FishOwnerController->GetPawn()) : nullptr;
	if (SourceFish->GetFishOwnerStableNetId().IsEmpty() || !IsCharacterSociallyActive(FishOwnerCharacter))
	{
		Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	Result.TheftProtocolId = FGuid::NewGuid();
	Result.FishInstanceId = SourceFish->GetItemInstanceId();
	FCatInventoryEntry RemovedEntry;
	if (!SourceInventory->RemoveInventoryEntryAtSlotFromAuthority(Command.SourceInventorySlotIndex, RemovedEntry)
		|| RemovedEntry.Instance.Get() != SourceFish)
	{
		Result.Command.Error = ECatDomainCommandError::NotFound;
		return Finish(Result);
	}
	Result.Command.bCommitted = true;
	Result.Command.Error = ECatDomainCommandError::None;
	// 库存已经移除同一个鱼实例后，Social 才建立活跃索引；客户端 RequestId 继续只用于 Begin 终态缓存。
	FActiveTheft& Theft = ActiveThefts.Add(Result.TheftProtocolId);
	Theft.TheftProtocolId = Result.TheftProtocolId;
	Theft.ClientRequestId = Command.Context.RequestId;
	Theft.ThiefStableNetId = ThiefStableNetId;
	Theft.VictimStableNetId = SourceFish->GetFishOwnerStableNetId();
	Theft.ThiefCharacter = ThiefCharacter;
	Theft.SourceInventory = SourceInventory;
	Theft.SourceInventorySlotIndex = Command.SourceInventorySlotIndex;
	Theft.FishItem = SourceFish;
	Theft.FishItemInstanceId = SourceFish->GetItemInstanceId();
	Theft.FishDefinitionId = SourceFish->GetFishDefinition()
		? SourceFish->GetFishDefinition()->GetInventoryDefinitionId() : NAME_None;
	Theft.Result = Result;
	Theft.Result.bRecoveryWindowOpen = true;
	Theft.Result.TheftProtocolId = Theft.TheftProtocolId;
	ActiveTheftByThief.Add(ThiefStableNetId, Theft.TheftProtocolId);
	FTimerDelegate TimerDelegate = FTimerDelegate::CreateUObject(this, &ThisClass::HandleTheftWindowExpired, Theft.TheftProtocolId);
	GetWorld()->GetTimerManager().SetTimer(Theft.EatingWindowTimer, TimerDelegate,
		static_cast<float>(Settings->TheftEatingWindowSeconds), false);
	UE_LOG(LogCatSocial, Log, TEXT("Event=social_theft_started ProtocolId=%s FishItem=%s SourceInventory=%s SourceSlot=%d WindowSeconds=%.3f"),
		*Theft.TheftProtocolId.ToString(EGuidFormats::DigitsWithHyphens),
		*Theft.FishItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*GetNameSafe(SourceInventory),
		Theft.SourceInventorySlotIndex,
		Settings->TheftEatingWindowSeconds);
	TheftTerminalCache.Add(TerminalKey, Theft.Result);
	return Theft.Result;
}

// 追回流程：按服务器 ProtocolId 定位协议并按 Catcher 身份重放，再验证真实受害者/共享策略、双方可交互状态与权威距离；成功后原位返还并移除协议。
FCatTheftResult UCatSocialService::CatchTheft(AController* CatchingController, const FGuid TheftProtocolId)
{
	FCatTheftResult Result;
	Result.TheftProtocolId = TheftProtocolId;
	const FString CatcherStableNetId = ResolveStableNetId(CatchingController);
	const UCatSocialSettings* Settings = GetDefault<UCatSocialSettings>();
	if (!TheftProtocolId.IsValid() || CatcherStableNetId.IsEmpty())
	{
		Result.Command.Error = ECatDomainCommandError::NotFound;
		return Result;
	}
	const FString TerminalKey = MakeTerminalKey(CatcherStableNetId, TEXT("CatchTheft"), TheftProtocolId);
	if (const FCatTheftResult* Cached = TheftTerminalCache.Find(TerminalKey))
	{
		Result = *Cached;
		MarkCommandReplayed(Result.Command);
		return Result;
	}
	const auto Finish = [this, &TerminalKey](const FCatTheftResult& TerminalResult)
	{
		// 空间或状态拒绝不是追回协议终态；只缓存库存返还成功的结果，使同一捕手重试不复制鱼，同时允许仍在窗口内重新接近。
		if (TerminalResult.bReturned || TerminalResult.bConsumed || TerminalResult.Command.bCommitted)
		{
			TheftTerminalCache.Add(TerminalKey, TerminalResult);
		}
		return TerminalResult;
	};
	if (!bCommandsOpen)
	{
		Result.Command.Error = ECatDomainCommandError::CommandsClosed;
		return Finish(Result);
	}
	FActiveTheft* Theft = ActiveThefts.Find(TheftProtocolId);
	if (!Theft)
	{
		Result.Command.Error = ECatDomainCommandError::NotFound;
		return Finish(Result);
	}
	ACatCharacter* CatcherCharacter = CatchingController ? Cast<ACatCharacter>(CatchingController->GetPawn()) : nullptr;
	ACatCharacter* ThiefCharacter = Theft->ThiefCharacter.Get();
	AController* VictimController = FindControllerByStableNetId(Theft->VictimStableNetId);
	const bool bVictimAuthorityMatches = VictimController && VictimController == CatchingController;
	const bool bAllowed = bVictimAuthorityMatches;
	if (!bAllowed || !Settings->IsTheftReady() || !IsCharacterSociallyActive(CatcherCharacter)
		|| !IsCharacterSociallyActive(ThiefCharacter)
		|| FVector::DistSquared(CatcherCharacter->GetActorLocation(), ThiefCharacter->GetActorLocation())
			> FMath::Square(Settings->TheftCatchRangeCentimeters))
	{
		Result.Command.Error = bAllowed ? ECatDomainCommandError::PolicyUndecided : ECatDomainCommandError::PermissionDenied;
		return Finish(Result);
	}
	GetWorld()->GetTimerManager().ClearTimer(Theft->EatingWindowTimer);
	const FActiveTheft Frozen = *Theft;
	Result = ReturnActiveTheft(TheftProtocolId);
	if (Result.bReturned)
	{
		SubmitCaughtImprint(Frozen, CatcherStableNetId);
		UE_LOG(LogCatSocial, Log, TEXT("Event=social_theft_caught ProtocolId=%s FishInstanceId=%s Returned=true"),
			*TheftProtocolId.ToString(EGuidFormats::DigitsWithHyphens), *Frozen.FishItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens));
	}
	return Finish(Result);
}

// 恶作剧权限流程：先按身份/操作/RequestId 重放，再忽略客户端位置并从双方权威 Pawn 验证状态、距离、冷却与目标保护牌；通过后才写允许终态。
FCatDomainCommandResult UCatSocialService::RequestMischief(AController* InstigatorController,
	AController* TargetController, const FGuid RequestId, const FVector InteractionLocation)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const UCatSocialSettings* Settings = GetDefault<UCatSocialSettings>();
	const FString InstigatorId = ResolveStableNetId(InstigatorController);
	const APlayerState* TargetPlayerState = TargetController ? TargetController->PlayerState : nullptr;
	const ACatCharacter* InstigatorCharacter = InstigatorController ? Cast<ACatCharacter>(InstigatorController->GetPawn()) : nullptr;
	const ACatCharacter* TargetCharacter = TargetController ? Cast<ACatCharacter>(TargetController->GetPawn()) : nullptr;
	(void)InteractionLocation;
	if (!RequestId.IsValid() || InstigatorId.IsEmpty())
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Result;
	}
	const FString TerminalKey = MakeTerminalKey(InstigatorId, TEXT("Mischief"), RequestId);
	if (const FCatDomainCommandResult* Cached = CommandTerminalCache.Find(TerminalKey))
	{
		Result = *Cached;
		MarkCommandReplayed(Result);
		return Result;
	}
	const auto Finish = [this, &TerminalKey](const FCatDomainCommandResult& TerminalResult)
	{
		CommandTerminalCache.Add(TerminalKey, TerminalResult);
		return TerminalResult;
	};
	if (!bCommandsOpen)
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		return Finish(Result);
	}
	if (!Settings->IsMischiefReady() || !TargetPlayerState || InstigatorController == TargetController
		|| !IsCharacterSociallyActive(InstigatorCharacter) || !IsCharacterSociallyActive(TargetCharacter)
		|| FVector::DistSquared(InstigatorCharacter->GetActorLocation(), TargetCharacter->GetActorLocation())
			> FMath::Square(Settings->MischiefInteractionRangeCentimeters))
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Finish(Result);
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (const double* LastTime = LastMischiefTimeByPlayer.Find(InstigatorId);
		LastTime && Now - *LastTime < Settings->MischiefCooldownSeconds)
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
		return Finish(Result);
	}
	for (TActorIterator<ACatProtectionSignActor> It(GetWorld()); It; ++It)
	{
		if (It->ProtectsMischiefAgainst(TargetPlayerState, TargetCharacter->GetActorLocation()))
		{
			Result.Error = ECatDomainCommandError::PermissionDenied;
			return Finish(Result);
		}
	}
	LastMischiefTimeByPlayer.Add(InstigatorId, Now);
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	return Finish(Result);
}

// 放牌流程：先按身份/操作/RequestId 重放，再验证 Pawn、显式范围和有限位置；首次提交复用每人唯一 Actor 并配置保护，不写偷鱼或装备规则。
FCatDomainCommandResult UCatSocialService::PlaceProtectionSign(AController* RequestingController,
	const FGuid RequestId, const FVector SignLocation)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const UCatSocialSettings* Settings = GetDefault<UCatSocialSettings>();
	const FString StableNetId = ResolveStableNetId(RequestingController);
	APawn* Pawn = RequestingController ? RequestingController->GetPawn() : nullptr;
	APlayerState* PlayerState = RequestingController ? RequestingController->PlayerState : nullptr;
	if (!RequestId.IsValid() || StableNetId.IsEmpty())
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Result;
	}
	const FString TerminalKey = MakeTerminalKey(StableNetId, TEXT("PlaceProtectionSign"), RequestId);
	if (const FCatDomainCommandResult* Cached = CommandTerminalCache.Find(TerminalKey))
	{
		Result = *Cached;
		MarkCommandReplayed(Result);
		return Result;
	}
	const auto Finish = [this, &TerminalKey](const FCatDomainCommandResult& TerminalResult)
	{
		CommandTerminalCache.Add(TerminalKey, TerminalResult);
		return TerminalResult;
	};
	if (!bCommandsOpen)
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		return Finish(Result);
	}
	if (!Settings->IsMischiefReady() || !Pawn || !PlayerState
		|| SignLocation.ContainsNaN() || !FMath::IsFinite(Settings->ProtectionSignRadiusCentimeters)
		|| Settings->ProtectionSignRadiusCentimeters <= 0.0
		|| !FMath::IsFinite(Settings->ProtectionSignPlacementRangeCentimeters)
		|| Settings->ProtectionSignPlacementRangeCentimeters <= 0.0
		|| FVector::DistSquared(Pawn->GetActorLocation(), SignLocation)
			> FMath::Square(Settings->ProtectionSignPlacementRangeCentimeters))
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Finish(Result);
	}
	ACatProtectionSignActor* Sign = ProtectionSignByPlayer.FindRef(StableNetId).Get();
	if (!Sign)
	{
		Sign = GetWorld()->SpawnActor<ACatProtectionSignActor>(ACatProtectionSignActor::StaticClass(),
			SignLocation, FRotator::ZeroRotator);
		ProtectionSignByPlayer.Add(StableNetId, Sign);
	}
	else
	{
		Sign->SetActorLocation(SignLocation, false, nullptr, ETeleportType::TeleportPhysics);
	}
	if (!Sign || !Sign->ConfigureProtection(PlayerState, Settings->ProtectionSignRadiusCentimeters))
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	return Finish(Result);
}

// 手动求助流程：先按身份/操作/RequestId 重放，再校验两种 Manual 类型、范围、冷却和 Pawn；首次成功才递增 Revision 并发布 nearby 信号。
FCatDomainCommandResult UCatSocialService::RequestManualHelp(AController* RequestingController,
	const FGuid RequestId, const ECatHelpSignalKind Kind)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const UCatSocialSettings* Settings = GetDefault<UCatSocialSettings>();
	const FString StableNetId = ResolveStableNetId(RequestingController);
	const APawn* Pawn = RequestingController ? RequestingController->GetPawn() : nullptr;
	ACatfishingGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
	if (!RequestId.IsValid() || StableNetId.IsEmpty())
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Result;
	}
	const FString TerminalKey = MakeTerminalKey(StableNetId, TEXT("ManualHelp"), RequestId);
	if (const FCatDomainCommandResult* Cached = CommandTerminalCache.Find(TerminalKey))
	{
		Result = *Cached;
		MarkCommandReplayed(Result);
		return Result;
	}
	const auto Finish = [this, &TerminalKey](const FCatDomainCommandResult& TerminalResult)
	{
		CommandTerminalCache.Add(TerminalKey, TerminalResult);
		return TerminalResult;
	};
	if (!bCommandsOpen)
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		return Finish(Result);
	}
	if (!Settings->IsManualHelpReady() || !Pawn || !GameState
		|| (Kind != ECatHelpSignalKind::ManualFishing && Kind != ECatHelpSignalKind::ManualDowned))
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Finish(Result);
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (const double* LastTime = LastManualHelpTimeByPlayer.Find(StableNetId);
		LastTime && Now - *LastTime < Settings->ManualHelpCooldownSeconds)
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
		return Finish(Result);
	}
	FCatHelpSignalSnapshot Signal;
	Signal.SignalId = RequestId;
	Signal.Kind = Kind;
	Signal.SourceLocation = Pawn->GetActorLocation();
	Signal.RadiusCentimeters = Settings->ManualHelpRadiusCentimeters;
	Signal.bGlobal = false;
	Signal.Revision = ++HelpSignalRevision;
	GameState->SetHelpSignalFromAuthority(Signal);
	LastManualHelpTimeByPlayer.Add(StableNetId, Now);
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	UE_LOG(LogCatSocial, Log, TEXT("Event=social_manual_help RequestId=%s Kind=%s Revision=%lld RadiusCm=%.3f"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens), *UEnum::GetValueAsString(Kind), Signal.Revision,
		Signal.RadiusCentimeters);
	return Finish(Result);
}

// Giant 提示流程：要求 Social 总 gate、有效会话 ID 和当前 Pawn；直接发布全局系统信号，不占用玩家手动求助冷却。
void UCatSocialService::BroadcastGiantFishingPrompt(AController* FisherController, const FGuid FishingSessionId)
{
	const UCatSocialSettings* Settings = GetDefault<UCatSocialSettings>();
	const APawn* Pawn = FisherController ? FisherController->GetPawn() : nullptr;
	ACatfishingGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
	if (!bCommandsOpen || !Settings->bEnableSocialRuntime || !FishingSessionId.IsValid() || !Pawn || !GameState)
	{
		return;
	}
	FCatHelpSignalSnapshot Signal;
	Signal.SignalId = FishingSessionId;
	Signal.Kind = ECatHelpSignalKind::GiantFishSystem;
	Signal.SourceLocation = Pawn->GetActorLocation();
	Signal.bGlobal = true;
	Signal.Revision = ++HelpSignalRevision;
	GameState->SetHelpSignalFromAuthority(Signal);
	UE_LOG(LogCatSocial, Log, TEXT("Event=social_giant_prompt SessionId=%s Revision=%lld Global=true"),
		*FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), Signal.Revision);
}

// Character 取消流程：只匹配作为小偷的弱 Character；逐条清 Timer 并原位返还，避免掉线/倒地把负面影响扩大成永久丢鱼。
void UCatSocialService::CancelTheftsForCharacter(const ACatCharacter* Character)
{
	if (!Character)
	{
		return;
	}
	TArray<FGuid> Requests;
	for (const TPair<FGuid, FActiveTheft>& Pair : ActiveThefts)
	{
		if (Pair.Value.ThiefCharacter.Get() == Character)
		{
			Requests.Add(Pair.Key);
		}
	}
	for (const FGuid& TheftProtocolId : Requests)
	{
		if (FActiveTheft* Theft = ActiveThefts.Find(TheftProtocolId))
		{
			GetWorld()->GetTimerManager().ClearTimer(Theft->EatingWindowTimer);
		}
		ReturnActiveTheft(TheftProtocolId);
	}
}

// 进食到期流程：按服务器 ProtocolId 验证协议、Character、鱼实例定义并完成身体 preflight；任一依赖失效就返还，全部有效才用原客户端 RequestId 关联身体效果。
void UCatSocialService::HandleTheftWindowExpired(const FGuid TheftProtocolId)
{
	FActiveTheft* Theft = ActiveThefts.Find(TheftProtocolId);
	// ClearTimer 后若回调已进入队列，关门事实仍优先把 escrow 返还，不能在 Host teardown 中继续执行吃鱼副作用。
	if (!bCommandsOpen)
	{
		ReturnActiveTheft(TheftProtocolId);
		return;
	}
	ACatCharacter* Character = Theft ? Theft->ThiefCharacter.Get() : nullptr;
	UCatConditionComponent* Conditions = Character ? Character->GetConditionComponent() : nullptr;
	UCatFishInventoryItemInstance* FishItem = Theft ? Theft->FishItem.Get() : nullptr;
	UCatFishDefinition* Definition = FishItem ? FishItem->GetFishDefinition() : nullptr;
	if (!Theft || !FishItem || !Conditions || !Definition
		|| Conditions->ValidateFishConsumption(Definition) != ECatDomainCommandError::None)
	{
		ReturnActiveTheft(TheftProtocolId);
		return;
	}
	Theft->Result.Body = Conditions->ConsumeCommittedFish(Theft->ClientRequestId, Definition);
	if (!CatIsAcceptedDomainCommandResult(Theft->Result.Body))
	{
		UE_LOG(LogCatSocial, Error,
			TEXT("Event=social_theft_body_commit_failed ProtocolId=%s RequestId=%s FishInstanceId=%s BodyError=%s BodyReplay=%s BodyReplayError=%s BodyRevision=%lld"),
			*TheftProtocolId.ToString(EGuidFormats::DigitsWithHyphens),
			*Theft->ClientRequestId.ToString(EGuidFormats::DigitsWithHyphens),
			*Theft->FishItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
			*UEnum::GetValueAsString(Theft->Result.Body.Error),
			Theft->Result.Body.bTerminalReplay ? TEXT("true") : TEXT("false"),
			*UEnum::GetValueAsString(Theft->Result.Body.ReplayedTerminalError), Theft->Result.Body.Revision);
	}
	Theft->Result.Command.RequestId = Theft->ClientRequestId;
	Theft->Result.Command.bCommitted = true;
	Theft->Result.Command.Error = ECatDomainCommandError::None;
	Theft->Result.bRecoveryWindowOpen = false;
	Theft->Result.bConsumed = true;
	ACatfishingPlayerController* ThiefPlayerController = Cast<ACatfishingPlayerController>(Character->GetController());
	const FCatTheftResult ConsumedResult = Theft->Result;
	// 协议即将从活跃表移除，先把 Begin 请求的重放值推进到 consumed 终态；后续网络重试不会退回 NotFound 或再次消费。
	TheftTerminalCache.Add(MakeTerminalKey(Theft->ThiefStableNetId, TEXT("BeginTheft"), Theft->ClientRequestId), ConsumedResult);
	ActiveTheftByThief.Remove(Theft->ThiefStableNetId);
	ActiveThefts.Remove(TheftProtocolId);
	if (ThiefPlayerController)
	{
		ThiefPlayerController->ClientReceiveTheftResult(ConsumedResult);
	}
	UE_LOG(LogCatSocial, Log, TEXT("Event=social_theft_consumed ProtocolId=%s FishInstanceId=%s"),
		*TheftProtocolId.ToString(EGuidFormats::DigitsWithHyphens), *ConsumedResult.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens));
}

// 返还辅助流程：按服务器 ProtocolId 把同一个鱼实例放回来源库存；成功后用原客户端 RequestId 更新 Begin 终态，再移除 thief 索引/协议。
FCatTheftResult UCatSocialService::ReturnActiveTheft(const FGuid TheftProtocolId)
{
	FCatTheftResult Result;
	Result.TheftProtocolId = TheftProtocolId;
	FActiveTheft* Theft = ActiveThefts.Find(TheftProtocolId);
	Result.Command.RequestId = Theft ? Theft->ClientRequestId : FGuid();
	UCatInventoryComponent* SourceInventory = Theft ? Theft->SourceInventory.Get() : nullptr;
	UCatFishInventoryItemInstance* FishItem = Theft ? Theft->FishItem.Get() : nullptr;
	if (!Theft || !SourceInventory || !FishItem)
	{
		Result.Command.Error = ECatDomainCommandError::NotFound;
		return Result;
	}
	Result = Theft->Result;
	Result.Command.RequestId = Theft->ClientRequestId;
	FCatInventoryReceiveBatch ReturnBatch;
	FCatInventoryInstanceEntry& ReturnEntry = ReturnBatch.InstanceEntries.AddDefaulted_GetRef();
	ReturnEntry.ItemInstance = FishItem;
	ReturnEntry.Count = 1;
	if (SourceInventory->TryAddInventoryBatch(ReturnBatch))
	{
		Result.Command.bCommitted = true;
		Result.Command.Error = ECatDomainCommandError::None;
		Result.bRecoveryWindowOpen = false;
		Result.bReturned = true;
		// 返还成功是该 Begin 请求的最终事实；必须在移除私有协议前冻结身份键与完整 returned 结果。
		TheftTerminalCache.Add(MakeTerminalKey(Theft->ThiefStableNetId, TEXT("BeginTheft"), Theft->ClientRequestId), Result);
		ActiveTheftByThief.Remove(Theft->ThiefStableNetId);
		ActiveThefts.Remove(TheftProtocolId);
	}
	else
	{
		Result.Command.Error = ECatDomainCommandError::CapacityExceeded;
	}
	return Result;
}

// 被抓印记流程：先要求正式事件、Run 和两名不同参与者，再提交一份共同 Candidate；批量接口会先为双方建齐并索引 Planned 记录，确认没有部分缺失后才逐条投递，暂时离线的接收者沿用原计划重试。该成像链是返还成功后的可选副作用，不会改写 theft returned 终态。
void UCatSocialService::SubmitCaughtImprint(const FActiveTheft& Theft, const FString& CatcherStableNetId)
{
	const UCatSocialSettings* Settings = GetDefault<UCatSocialSettings>();
	const ACatfishingGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
	UCatRunImprintService* Imprint = GetWorld() ? GetWorld()->GetSubsystem<UCatRunImprintService>() : nullptr;
	if (!Settings || Settings->TheftCaughtImprintEventId.IsNone() || !GameState || !Imprint
		|| Theft.ThiefStableNetId.IsEmpty() || CatcherStableNetId.IsEmpty()
		|| Theft.ThiefStableNetId == CatcherStableNetId)
	{
		return;
	}
	FCatImprintCandidate Candidate;
	Candidate.CandidateId = FGuid::NewGuid();
	Candidate.RunId = GameState->GetRunPublicState().Phase.RunId;
	Candidate.EventType = Settings->TheftCaughtImprintEventId;
	Candidate.SubjectId = Theft.FishItemInstanceId;
	Candidate.FishDefinitionId = Theft.FishDefinitionId;
	Candidate.ParticipantStableNetIds = {Theft.ThiefStableNetId, CatcherStableNetId};
	Candidate.ParticipantCount = Candidate.ParticipantStableNetIds.Num();
	if (Imprint->SubmitImprintCandidate(Candidate))
	{
		TArray<FCatCapturePlan> CapturePlans;
		Imprint->CreateCapturePlansForParticipants(Candidate.CandidateId,
			Candidate.ParticipantStableNetIds, false, CapturePlans);
	}
}

// 终态键构造流程：显式拼接服务器身份、操作域与标准 GUID；操作名隔离相同 RequestId 的不同语义，原始身份只留在服务私有映射中。
FString UCatSocialService::MakeTerminalKey(const FString& StableNetId, const TCHAR* Operation, const FGuid RequestId)
{
	return FString::Printf(TEXT("%s|%s|%s"), *StableNetId, Operation,
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
}

// 身份解析流程：只读取 Controller PlayerState 的继承 UniqueId；原始值只存在 Social 私有协议和冷却键。
FString UCatSocialService::ResolveStableNetId(const AController* Controller)
{
	const APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
	return PlayerState && PlayerState->GetUniqueId().IsValid() ? PlayerState->GetUniqueId()->ToString() : FString();
}

// Controller 定位流程：只遍历当前 World 并比较继承 UniqueId；不使用客户端提供的身份字符串，也不保留跨 World 强引用。
AController* UCatSocialService::FindControllerByStableNetId(const FString& StableNetId) const
{
	if (!GetWorld() || StableNetId.IsEmpty())
	{
		return nullptr;
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AController* Controller = It->Get();
		if (ResolveStableNetId(Controller) == StableNetId)
		{
			return Controller;
		}
	}
	return nullptr;
}

// Social 状态检查流程：要求项目 Character 和 Condition 均有效且未倒地；缺组件或倒地都不能发起、被授权或完成空间交互。
bool UCatSocialService::IsCharacterSociallyActive(const ACatCharacter* Character)
{
	const UCatConditionComponent* Conditions = Character ? Character->GetConditionComponent() : nullptr;
	return Conditions && !Conditions->GetSnapshot().bDowned;
}
