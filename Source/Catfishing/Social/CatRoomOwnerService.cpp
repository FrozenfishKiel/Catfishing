#include "Social/CatRoomOwnerService.h"

#include "Engine/World.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingPlayerState.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Logging/CatLog.h"
#include "Logging/CatLogContext.h"

// 创建条件流程：只允许 authority Game World 持有加入顺序与房主身份；客户端读 PlayerState 上复制过去的房主标记。
bool UCatRoomOwnerService::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

// 初始化流程：订阅引擎的 PostLogin/Logout。
// 为什么走引擎委托而不是改 GameMode：房主是社交层的事，GameMode 已经拥有准入、存档与 teardown 三条协议，
// 再往它身上挂一份房间角色只会让那个类更难读；这两个委托正是引擎为「旁观登录生命周期」留的口子。
void UCatRoomOwnerService::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PostLoginHandle = FGameModeEvents::OnGameModePostLoginEvent().AddUObject(this, &ThisClass::HandlePostLogin);
	LogoutHandle = FGameModeEvents::OnGameModeLogoutEvent().AddUObject(this, &ThisClass::HandleLogout);
}

// 反初始化流程：成对解除订阅并清空本局顺序表与房主身份；这两个委托是全局静态的，漏解会让下一局收到旧 World 的回调。
void UCatRoomOwnerService::Deinitialize()
{
	FGameModeEvents::OnGameModePostLoginEvent().Remove(PostLoginHandle);
	FGameModeEvents::OnGameModeLogoutEvent().Remove(LogoutHandle);
	PostLoginHandle.Reset();
	LogoutHandle.Reset();
	JoinSequenceByPlayer.Reset();
	KickTerminalCache.Reset();
	RoomOwnerStableNetId.Reset();
	RoomOwnerPlayerState.Reset();
	Super::Deinitialize();
}

APlayerState* UCatRoomOwnerService::GetRoomOwnerPlayerState() const
{
	return RoomOwnerPlayerState.Get();
}

// 房主判定流程：比服务器私有身份，不比 PlayerState 指针——重连会换一个 PlayerState，但那还是同一只猫。
bool UCatRoomOwnerService::IsRoomOwner(const AController* Controller) const
{
	const FString StableNetId = ResolveStableNetId(Controller);
	return !StableNetId.IsEmpty() && StableNetId == RoomOwnerStableNetId;
}

// 世界隔离流程：FGameModeEvents 上那两个委托是全局静态的，PIE 多 World 或前台/玩法两张图会同时发。
bool UCatRoomOwnerService::IsSameWorld(const AGameModeBase* GameMode) const
{
	return GameMode && GameMode->GetWorld() == GetWorld();
}

// 登录流程：首次见到的身份分配一个加入序号；重连的人沿用旧序号，所以「最早加入者」不会因为掉线一次就换人。
// 房主空缺时由当前登录者接任——开局第一个登录的就是建局者（listen server 自己），于是房主初值天然正确。
void UCatRoomOwnerService::HandlePostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer)
{
	if (!IsSameWorld(GameMode))
	{
		return;
	}
	const FString StableNetId = ResolveStableNetId(NewPlayer);
	if (StableNetId.IsEmpty())
	{
		return;
	}
	const bool bFirstSeen = !JoinSequenceByPlayer.Contains(StableNetId);
	if (bFirstSeen)
	{
		JoinSequenceByPlayer.Add(StableNetId, NextJoinSequence++);
	}
	if (RoomOwnerStableNetId.IsEmpty() || RoomOwnerStableNetId == StableNetId)
	{
		// 重连回来的房主要重新拿到当前这份 PlayerState；身份没变，投影变了。
		PublishRoomOwner(NewPlayer->PlayerState,
			RoomOwnerStableNetId.IsEmpty() ? TEXT("FirstJoin") : TEXT("OwnerReconnected"));
	}
	UE_LOG(LogCatSocial, Log,
		TEXT("Event=room_member_joined JoinSequence=%lld FirstSeen=%s Members=%d IsRoomOwner=%s %s"),
		JoinSequenceByPlayer[StableNetId], bFirstSeen ? TEXT("true") : TEXT("false"),
		JoinSequenceByPlayer.Num(), IsRoomOwner(NewPlayer) ? TEXT("true") : TEXT("false"),
		*CatLogContext::BuildControllerFields(NewPlayer));
}

// 退出流程：只有房主离开才触发移交，其他人离开什么也不动。
// **这里不收口本局**：Run、Fishing、Camp 一条都不关——房主离开只换房主（09-09 账本六问⑤「只换房主、不收口、本局接着打」）。
// 真正会把本局带走的只有 listen server 进程退出，那条路在 Online 的 Host teardown 上，与本服务无关。
void UCatRoomOwnerService::HandleLogout(AGameModeBase* GameMode, AController* Exiting)
{
	if (!IsSameWorld(GameMode) || !IsRoomOwner(Exiting))
	{
		return;
	}
	TransferRoomOwnershipToEarliestJoiner(Exiting);
}

// 移交流程：在仍然连着的玩家里挑加入序号最小的接任。
// 序号并列（理论上不会发生，序号单调且不复用）时按 PlayerId 取小，保证结果确定可复现而不是跟着遍历顺序走。
void UCatRoomOwnerService::TransferRoomOwnershipToEarliestJoiner(const AController* DepartingController)
{
	UWorld* World = GetWorld();
	const ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	APlayerState* BestPlayerState = nullptr;
	int64 BestSequence = TNumericLimits<int64>::Max();
	int32 BestPlayerId = TNumericLimits<int32>::Max();
	if (World)
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Candidate = It->Get();
			// Logout 事件在引擎清理之前发出，离开者这一刻仍在迭代器里；按 Controller 指针把他排除掉。
			if (!Candidate || Candidate == DepartingController || !Candidate->PlayerState)
			{
				continue;
			}
			if (GameMode && !GameMode->IsControllerActive(Candidate))
			{
				continue;
			}
			const FString CandidateId = ResolveStableNetId(Candidate);
			const int64* Sequence = CandidateId.IsEmpty() ? nullptr : JoinSequenceByPlayer.Find(CandidateId);
			if (!Sequence)
			{
				continue;
			}
			const int32 CandidatePlayerId = Candidate->PlayerState->GetPlayerId();
			if (*Sequence < BestSequence || (*Sequence == BestSequence && CandidatePlayerId < BestPlayerId))
			{
				BestSequence = *Sequence;
				BestPlayerId = CandidatePlayerId;
				BestPlayerState = Candidate->PlayerState;
			}
		}
	}
	// 一个人都不剩：房主空缺，不伪造一个已经离开的房主。下一个登录的人（包括原房主重连）会接任。
	PublishRoomOwner(BestPlayerState, BestPlayerState ? TEXT("OwnerLeft") : TEXT("RoomEmpty"));
	UE_LOG(LogCatSocial, Log,
		TEXT("Event=room_owner_transferred Reason=OwnerLeft NewOwnerJoinSequence=%s Result=%s %s"),
		BestPlayerState ? *LexToString(BestSequence) : TEXT("None"),
		BestPlayerState ? TEXT("Transferred") : TEXT("RoomEmpty"),
		*CatLogContext::BuildControllerFields(DepartingController));
}

// 房主写入流程：先更新服务器私有身份，再把公开标记写到新旧两个 PlayerState 上；相同结果重复写入直接返回。
void UCatRoomOwnerService::PublishRoomOwner(APlayerState* NewOwnerPlayerState, const TCHAR* Reason)
{
	const FString NewOwnerId = NewOwnerPlayerState && NewOwnerPlayerState->GetUniqueId().IsValid()
		? NewOwnerPlayerState->GetUniqueId()->ToString() : FString();
	if (NewOwnerId == RoomOwnerStableNetId && RoomOwnerPlayerState.Get() == NewOwnerPlayerState)
	{
		return;
	}
	// 先摘旧的再挂新的：复制载体是每个 PlayerState 上的一个布尔，全场同时最多一个 true 由这里保证。
	// 载体放在 PlayerState 而不是 GameState，是因为房间页读的就是成员列表，一行一个人，读法最直；
	// 也避免在 GameState 上再加一个「当前谁」的独立字段，让两处身份有机会对不上。
	if (ACatfishingPlayerState* PreviousOwner = Cast<ACatfishingPlayerState>(RoomOwnerPlayerState.Get()))
	{
		PreviousOwner->SetRoomOwnerFromAuthority(false);
	}
	RoomOwnerStableNetId = NewOwnerId;
	RoomOwnerPlayerState = NewOwnerPlayerState;
	if (ACatfishingPlayerState* NewOwner = Cast<ACatfishingPlayerState>(NewOwnerPlayerState))
	{
		NewOwner->SetRoomOwnerFromAuthority(true);
	}
	UE_LOG(LogCatSocial, Log, TEXT("Event=room_owner_changed Reason=%s Owner=%s"),
		Reason, *CatLogContext::BuildStableNetIdValue(NewOwnerPlayerState));
}

// 踢人流程：先按身份/RequestId 重放，再逐条校验房主资格、目标有效性与目标是不是远端连接；
// 通过后按主动离局标记并交给引擎 Kick，清理仍由 GameMode::Logout 那一条现成路径做。
FCatDomainCommandResult UCatRoomOwnerService::RequestKickPlayer(AController* RequestingController,
	APlayerState* TargetPlayerState, const FGuid RequestId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString RequesterId = ResolveStableNetId(RequestingController);
	if (!RequestId.IsValid() || RequesterId.IsEmpty())
	{
		Result.Error = ECatDomainCommandError::InvalidIdentity;
		return Result;
	}
	const FString TerminalKey = FString::Printf(TEXT("%s|Kick|%s"), *RequesterId,
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	if (const FCatDomainCommandResult* Cached = KickTerminalCache.Find(TerminalKey))
	{
		Result = *Cached;
		MarkCommandReplayed(Result);
		return Result;
	}
	const auto Finish = [this, &TerminalKey](const FCatDomainCommandResult& TerminalResult)
	{
		KickTerminalCache.Add(TerminalKey, TerminalResult);
		return TerminalResult;
	};
	UWorld* World = GetWorld();
	ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	if (!GameMode || !GameMode->GameSession)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	if (!IsRoomOwner(RequestingController) || !GameMode->IsControllerActive(RequestingController))
	{
		// 踢人是房主唯一的权力，也是本局里唯一一处按身份分叉的入口；机制层其余部分仍然没有房主（09-07 决策点⑩）。
		Result.Error = ECatDomainCommandError::PermissionDenied;
		return Finish(Result);
	}
	APlayerController* TargetController = TargetPlayerState
		? Cast<APlayerController>(TargetPlayerState->GetOwner()) : nullptr;
	if (!TargetController || TargetController == RequestingController)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Finish(Result);
	}
	if (!GameMode->IsControllerActive(TargetController))
	{
		Result.Error = ECatDomainCommandError::NotFound;
		return Finish(Result);
	}
	if (!TargetController->GetNetConnection())
	{
		// 目标没有网络连接＝他就是本进程（listen server 那台机器上的本地玩家）。
		// 引擎的 KickPlayer 对本地玩家是空操作，与其静默失败不如明说：房主换人之后仍然踢不动开服的那台机器，
		// 世界跑在他的进程里。要让「踢掉房东」成立，得先有真正的 Host Migration，见本批交接说明。
		Result.Error = ECatDomainCommandError::PermissionDenied;
		UE_LOG(LogCatSocial, Warning,
			TEXT("Event=room_kick_rejected Reason=TargetIsListenServerHost RequestId=%s Target=%s %s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			*CatLogContext::BuildStableNetIdValue(TargetPlayerState),
			*CatLogContext::BuildControllerFields(RequestingController));
		return Finish(Result);
	}
	// 复用现成的 Logout 双删：主动离局标记让 GameMode::Logout 既移除准入记录，又不写重连记录
	// （VoluntaryLeaveRecovery=Disabled 时被标记者拿不到重连凭据）。这里不另建一套清理，也不碰任何跟人走的档案——
	// 「被踢者跟人走的资产无损」是自动成立的：那三样都在 durable Profile 里，本入口一个字都不写。
	GameMode->MarkVoluntaryLeave(TargetController);
	GameMode->GameSession->KickPlayer(TargetController,
		NSLOCTEXT("Catfishing", "RoomOwnerKick", "房主请你先离开这一局"));
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	UE_LOG(LogCatSocial, Log,
		TEXT("Event=room_kick_committed RequestId=%s Target=%s %s"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		*CatLogContext::BuildStableNetIdValue(TargetPlayerState),
		*CatLogContext::BuildControllerFields(RequestingController));
	return Finish(Result);
}

// 身份解析流程：只读取 Controller PlayerState 的继承 UniqueId；原始值只留在本服务私有的顺序表与房主字段里。
FString UCatRoomOwnerService::ResolveStableNetId(const AController* Controller)
{
	const APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
	return PlayerState && PlayerState->GetUniqueId().IsValid() ? PlayerState->GetUniqueId()->ToString() : FString();
}
