#include "Camp/CatCampHubActor.h"

#include "Camp/CatCampInventoryActor.h"
#include "Camp/CatCampSettings.h"
#include "Character/CatCharacter.h"
#include "CollisionQueryParams.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "Condition/CatConditionComponent.h"
#include "Collection/CatRunImprintService.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Items/CatFishTankActor.h"
#include "Items/CatItemsService.h"
#include "Logging/CatLog.h"

namespace
{
	/** 营地玩家出生环的默认半径，单位厘米；它让初始 Pawn 离开营地中心和 PlayerStart 胶囊，同时仍处在常规营地交互半径内。 */
	constexpr double CatCampPlayerEntryRingRadiusCentimeters = 300.0;

	/** 营地出生地面探测的上方余量，单位厘米；它允许营地实例按地面设施或 PlayerStart 中心两种编辑器高度摆放。 */
	constexpr double CatCampPlayerEntryGroundProbeUpCentimeters = 1200.0;

	/** 营地出生地面探测的下方距离，单位厘米；它覆盖 TestMap 这类旧地图里营地 Z 与可站地面高度不一致的情况。 */
	constexpr double CatCampPlayerEntryGroundProbeDownCentimeters = 4000.0;

	/** Pawn 胶囊底部离地的最小余量，单位厘米；它防止精确贴地时被地面碰撞误判为初始穿插。 */
	constexpr double CatCampPlayerEntryFloorClearanceCentimeters = 2.0;

	// 营地候选点落地流程：用候选 XY 垂直扫描 WorldStatic 地面；命中后把地面接触点转换成 Pawn 根胶囊中心点，不把营地 Actor 自身高度直接当出生高度。
	bool TryProjectCampEntryCandidateToGround(UWorld* World, const AActor* Camp,
		const FVector& CandidateAnchorLocation, const double PawnHalfHeight, FVector& OutSpawnLocation)
	{
		OutSpawnLocation = FVector::ZeroVector;
		if (!World || !Camp || PawnHalfHeight <= 0.0)
		{
			return false;
		}

		const FVector TraceStart =
			CandidateAnchorLocation + FVector(0.0, 0.0, CatCampPlayerEntryGroundProbeUpCentimeters);
		const FVector TraceEnd =
			CandidateAnchorLocation - FVector(0.0, 0.0, CatCampPlayerEntryGroundProbeDownCentimeters);
		FHitResult GroundHit;
		const FCollisionObjectQueryParams GroundObjectQueryParams(ECC_WorldStatic);
		const FCollisionQueryParams GroundQueryParams(SCENE_QUERY_STAT(CatCampPlayerEntryGroundTrace), false, Camp);
		if (!World->LineTraceSingleByObjectType(GroundHit, TraceStart, TraceEnd, GroundObjectQueryParams,
			GroundQueryParams) || !GroundHit.bBlockingHit)
		{
			return false;
		}

		OutSpawnLocation = GroundHit.ImpactPoint
			+ FVector(0.0, 0.0, PawnHalfHeight + CatCampPlayerEntryFloorClearanceCentimeters);
		return true;
	}
}

// 构造流程：先保留 APlayerStart 从 ANavigationObjectBase 建立的胶囊根，再把项目营地根挂在它下面；随后创建救援子节点、开启复制并关闭 Tick。PlayerStartTag 只表达营地出生点身份，不参与客户端自选 Portal。
ACatCampHubActor::ACatCampHubActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = false;
	PlayerStartTag = TEXT("Camp");
	CampRoot = CreateDefaultSubobject<USceneComponent>(TEXT("CampRoot"));
	CampRoot->SetupAttachment(GetRootComponent());
	RescuePoint = CreateDefaultSubobject<USceneComponent>(TEXT("RescuePoint"));
	RescuePoint->SetupAttachment(CampRoot);
}

// 玩家出生 Transform 解析流程：
// 1. 先要求 authority、有效 World、Pawn 原型和 0-3 的当前玩家序号，超过容量时直接拒绝，避免第 5 人复用已有落点。
// 2. 再读取 Pawn 默认半高，确认后续能把营地附近地面点转换成 Pawn 根胶囊中心点；拿不到有效碰撞高度时 fail-closed。
// 3. 以营地水平朝向排出前、右、左、后的四个固定候选方向，并从 PreferredEntryIndex 开始循环尝试，保证当前玩家队列决定优先点但不保存重连槽位。
// 4. 每个候选点先垂直投射到 WorldStatic 地面，再交给 World::FindTeleportSpot 按 Pawn 碰撞做附近调整；只有 UE 确认不阻挡的 Transform 才返回，所有失败都写入可检索日志。
bool ACatCampHubActor::TryResolvePlayerEntryTransform(const int32 PreferredEntryIndex, const APawn* PawnToFit,
	FTransform& OutTransform) const
{
	OutTransform = FTransform::Identity;
	UWorld* World = GetWorld();
	if (!HasAuthority() || !World || !PawnToFit)
	{
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=camp_player_entry_rejected Camp=%s Reason=AuthorityWorldOrPawnUnavailable World=%s NetMode=%d Authority=%s LocalRole=%d"),
			*GetNameSafe(this), *GetNameSafe(World), World ? static_cast<int32>(World->GetNetMode()) : INDEX_NONE,
			HasAuthority() ? TEXT("true") : TEXT("false"), static_cast<int32>(GetLocalRole()));
		return false;
	}
	if (PreferredEntryIndex < 0 || PreferredEntryIndex >= CatGameplayPlayerLimits::MaxCampSpawnPlayers)
	{
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=camp_player_entry_rejected Camp=%s Reason=PlayerCapacityExceeded PreferredIndex=%d Capacity=%d World=%s NetMode=%d Authority=true LocalRole=%d"),
			*GetNameSafe(this), PreferredEntryIndex, CatGameplayPlayerLimits::MaxCampSpawnPlayers,
			*GetNameSafe(World), static_cast<int32>(World->GetNetMode()), static_cast<int32>(GetLocalRole()));
		return false;
	}
	const double PawnHalfHeight = PawnToFit->GetDefaultHalfHeight();
	if (PawnHalfHeight <= 0.0)
	{
		UE_LOG(LogCatfishing, Error,
			TEXT("Event=camp_player_entry_rejected Camp=%s Reason=PawnCollisionUnavailable PreferredIndex=%d PawnClass=%s World=%s NetMode=%d Authority=true LocalRole=%d"),
			*GetNameSafe(this), PreferredEntryIndex, *GetNameSafe(PawnToFit->GetClass()),
			*GetNameSafe(World), static_cast<int32>(World->GetNetMode()), static_cast<int32>(GetLocalRole()));
		return false;
	}

	const FRotator SpawnRotation(0.0, GetActorRotation().Yaw, 0.0);
	const FRotationMatrix SpawnBasis(SpawnRotation);
	const FVector Forward = SpawnBasis.GetScaledAxis(EAxis::X);
	const FVector Right = SpawnBasis.GetScaledAxis(EAxis::Y);
	const FVector CandidateDirections[CatGameplayPlayerLimits::MaxCampSpawnPlayers] =
	{
		Forward,
		Right,
		-Right,
		-Forward
	};

	for (int32 AttemptIndex = 0; AttemptIndex < CatGameplayPlayerLimits::MaxCampSpawnPlayers; ++AttemptIndex)
	{
		const int32 CandidateIndex =
			(PreferredEntryIndex + AttemptIndex) % CatGameplayPlayerLimits::MaxCampSpawnPlayers;
		const FVector CandidateAnchorLocation =
			GetActorLocation() + CandidateDirections[CandidateIndex] * CatCampPlayerEntryRingRadiusCentimeters;
		FVector CandidateLocation = FVector::ZeroVector;
		if (!TryProjectCampEntryCandidateToGround(World, this, CandidateAnchorLocation, PawnHalfHeight,
			CandidateLocation))
		{
			UE_LOG(LogCatfishing, Warning,
				TEXT("Event=camp_player_entry_candidate_rejected Camp=%s Reason=GroundProbeMiss PreferredIndex=%d SlotIndex=%d Attempt=%d Anchor=%s PawnHalfHeight=%.2f World=%s NetMode=%d Authority=true LocalRole=%d"),
				*GetNameSafe(this), PreferredEntryIndex, CandidateIndex, AttemptIndex,
				*CandidateAnchorLocation.ToCompactString(), PawnHalfHeight, *GetNameSafe(World),
				static_cast<int32>(World->GetNetMode()), static_cast<int32>(GetLocalRole()));
			continue;
		}
		if (World->FindTeleportSpot(PawnToFit, CandidateLocation, SpawnRotation))
		{
			OutTransform = FTransform(SpawnRotation, CandidateLocation);
			UE_LOG(LogCatfishing, Log,
				TEXT("Event=camp_player_entry_resolved Camp=%s PreferredIndex=%d SlotIndex=%d Attempt=%d Transform=%s World=%s NetMode=%d Authority=true LocalRole=%d"),
				*GetNameSafe(this), PreferredEntryIndex, CandidateIndex, AttemptIndex,
				*OutTransform.ToHumanReadableString(), *GetNameSafe(World), static_cast<int32>(World->GetNetMode()),
				static_cast<int32>(GetLocalRole()));
			return true;
		}
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=camp_player_entry_candidate_rejected Camp=%s Reason=PawnEncroached PreferredIndex=%d SlotIndex=%d Attempt=%d Location=%s PawnHalfHeight=%.2f World=%s NetMode=%d Authority=true LocalRole=%d"),
			*GetNameSafe(this), PreferredEntryIndex, CandidateIndex, AttemptIndex,
			*CandidateLocation.ToCompactString(), PawnHalfHeight, *GetNameSafe(World),
			static_cast<int32>(World->GetNetMode()), static_cast<int32>(GetLocalRole()));
	}

	UE_LOG(LogCatfishing, Error,
		TEXT("Event=camp_player_entry_rejected Camp=%s Reason=NoValidNearbySpot PreferredIndex=%d Capacity=%d World=%s NetMode=%d Authority=true LocalRole=%d"),
		*GetNameSafe(this), PreferredEntryIndex, CatGameplayPlayerLimits::MaxCampSpawnPlayers,
		*GetNameSafe(World), static_cast<int32>(World->GetNetMode()), static_cast<int32>(GetLocalRole()));
	return false;
}

// 休息流程：现取本人 Character 并验证固定范围；随后只调用 Condition 的 CampRest 写口，营地不保存第二份身体数值。
FCatDomainCommandResult ACatCampHubActor::RequestRest(AController* RequestingController, const FGuid RequestId)
{
	ACatCharacter* Character = ResolveCharacterInCamp(RequestingController);
	UCatConditionComponent* Conditions = Character ? Character->GetConditionComponent() : nullptr;
	if (Conditions)
	{
		return Conditions->RequestCampRest(RequestingController, RequestId, true);
	}
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	Result.Error = ECatDomainCommandError::PolicyUndecided;
	return Result;
}

// 救援流程：先用救援者身份与 RequestId 重放成功终态，再从两个 Character 读服务器位置并要求救援者可行动、目标已倒地、距离不超营地显式交互范围；TeleportTo 成功后才提交 CarriedToCamp。
FCatDomainCommandResult ACatCampHubActor::RescueToCamp(AController* HelpingController, ACatCharacter* TargetCharacter,
	const FGuid RequestId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const APlayerState* HelpingPlayerState = HelpingController ? HelpingController->PlayerState : nullptr;
	ACatCharacter* HelpingCharacter = HelpingController ? Cast<ACatCharacter>(HelpingController->GetPawn()) : nullptr;
	UCatConditionComponent* HelpingConditions = HelpingCharacter ? HelpingCharacter->GetConditionComponent() : nullptr;
	const UCatCampSettings* Settings = GetDefault<UCatCampSettings>();
	if (!HasAuthority() || !HelpingPlayerState || !HelpingPlayerState->GetUniqueId().IsValid()
		|| !HelpingCharacter || !HelpingConditions || HelpingConditions->GetSnapshot().bDowned
		|| !TargetCharacter || TargetCharacter->GetWorld() != GetWorld() || !RequestId.IsValid()
		|| !RescuePoint || !TargetCharacter->GetConditionComponent() || !Settings || !Settings->IsRuntimeReady())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	const FString CacheKey = FString::Printf(TEXT("%s|Rescue|%s"),
		*HelpingPlayerState->GetUniqueId()->ToString(), *RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	if (const FCatDomainCommandResult* Cached = RescueTerminalCache.Find(CacheKey))
	{
		Result = *Cached;
		Result.bCommitted = false;
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	if (!TargetCharacter->GetConditionComponent()->GetSnapshot().bDowned
		|| FVector::DistSquared(HelpingCharacter->GetActorLocation(), TargetCharacter->GetActorLocation())
			> FMath::Square(Settings->InteractionRadiusCentimeters))
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
		return Result;
	}
	if (!TargetCharacter->TeleportTo(RescuePoint->GetComponentLocation(), RescuePoint->GetComponentRotation(), false, true))
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	Result = TargetCharacter->GetConditionComponent()->CompleteCarryToCamp(HelpingController, RequestId, true);
	if (Result.bCommitted)
	{
		RescueTerminalCache.Add(CacheKey, Result);
	}
	return Result;
}

// 共享鱼缸读取流程：只通过 Items 公开快照口读取当前固定鱼缸；缺 Items、缺鱼缸或容器未注册都返回 false 并清空输出。
bool ACatCampHubActor::TryGetSharedFishTankSnapshot(FCatContainerSnapshot& OutSnapshot) const
{
	OutSnapshot = FCatContainerSnapshot();
	const UCatItemsService* Items = GetWorld() ? GetWorld()->GetSubsystem<UCatItemsService>() : nullptr;
	return Items && SharedFishTank && Items->TryGetContainerSnapshot(SharedFishTank->GetTankContainerId(), OutSnapshot);
}

// 鱼缸归属判断流程：只比较关卡显式引用，不按位置或标签猜测；鱼缸交互因此不会误投到另一座营地。
bool ACatCampHubActor::IsSharedFishTank(const ACatFishTankActor* Candidate) const
{
	return Candidate && SharedFishTank == Candidate;
}

// 商店发货仓库判断流程：
// 1. 只在服务器侧回答，避免客户端把本地引用当成发货事实。
// 2. 只接受本营地显式配置且同 World 的 PublicInventory；没有配置时返回空，由 PlayerController 回送 DependencyUnavailable。
// 3. 本函数不执行入库，入库容量、堆叠和幂等仍由 ACatCampInventoryActor 自己裁决。
ACatCampInventoryActor* ACatCampHubActor::ResolvePublicInventoryForShopOrder() const
{
	return HasAuthority() && PublicInventory && PublicInventory->HasAuthority()
		&& PublicInventory->GetWorld() == GetWorld()
		? PublicInventory : nullptr;
}

// 篝火回看流程：先由服务器 UniqueId 与 RequestId 重放首次终态，再验证固定营地范围、结算夜和封面事件配置。随后逐个确认 GameState 玩家仍有有效身份、Controller 和营地内 Character，提交全员 Candidate，并通过批量接口先建齐全部 Planned 记录、再尝试投递；任一前置或落盘失败都会缓存拒绝且不发网络表现。全部事实成立后才用 Reliable NetMulticast 把原 RequestId 送到相关客户端，并缓存首次成功；本流程不写 next-day ready、不等待客户端播放完成，也不保存补播状态。
FCatDomainCommandResult ACatCampHubActor::RequestCampfirePlayback(AController* RequestingController, const FGuid RequestId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const APlayerState* RequestingPlayerState = RequestingController ? RequestingController->PlayerState : nullptr;
	const FString StableNetId = RequestingPlayerState && RequestingPlayerState->GetUniqueId().IsValid()
		? RequestingPlayerState->GetUniqueId()->ToString() : FString();
	if (!RequestId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	if (StableNetId.IsEmpty())
	{
		Result.Error = ECatDomainCommandError::InvalidIdentity;
		return Result;
	}
	const FString TerminalKey = FString::Printf(TEXT("%s|CampfirePlayback|%s"), *StableNetId,
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	if (const FCatDomainCommandResult* Cached = CampfirePlaybackTerminalCache.Find(TerminalKey))
	{
		Result = *Cached;
		Result.bCommitted = false;
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	const auto Finish = [this, &TerminalKey](const FCatDomainCommandResult& TerminalResult)
	{
		CampfirePlaybackTerminalCache.Add(TerminalKey, TerminalResult);
		return TerminalResult;
	};
	if (!ResolveCharacterInCamp(RequestingController))
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Finish(Result);
	}
	ACatfishingGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
	if (!GameState)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	const UCatCampSettings* Settings = GetDefault<UCatCampSettings>();
	const bool bSettlementNight =
		GameState->GetRunPublicState().Phase.Phase == ECatRunPhase::FailureSettlementNight
		|| GameState->GetRunPublicState().Phase.Phase == ECatRunPhase::SuccessSettlementNight;
	if (!bSettlementNight)
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
		return Finish(Result);
	}
	if (!Settings || Settings->CampfireCoverEventId.IsNone())
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Finish(Result);
	}

	FCatImprintCandidate Candidate;
	Candidate.CandidateId = RequestId;
	Candidate.RunId = GameState->GetRunPublicState().Phase.RunId;
	Candidate.EventType = Settings->CampfireCoverEventId;
	Candidate.SubjectId = Candidate.RunId;
	for (APlayerState* PlayerState : GameState->PlayerArray)
	{
		APlayerController* Controller = PlayerState ? PlayerState->GetPlayerController() : nullptr;
		if (!Controller || !PlayerState->GetUniqueId().IsValid() || !ResolveCharacterInCamp(Controller))
		{
			Result.Error = ECatDomainCommandError::InvalidPhase;
			return Finish(Result);
		}
		Candidate.ParticipantStableNetIds.Add(PlayerState->GetUniqueId()->ToString());
	}
	Candidate.ParticipantCount = Candidate.ParticipantStableNetIds.Num();
	Candidate.bAllActivePlayersPresent = Candidate.ParticipantCount > 0;
	UCatRunImprintService* Imprint = GetWorld()->GetSubsystem<UCatRunImprintService>();
	if (!Imprint || !Candidate.bAllActivePlayersPresent || !Imprint->SubmitImprintCandidate(Candidate))
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	TArray<FCatCapturePlan> CapturePlans;
	if (!Imprint->CreateCapturePlansForParticipants(Candidate.CandidateId,
		Candidate.ParticipantStableNetIds, true, CapturePlans))
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	MulticastCampfirePlaybackRequested(RequestId);
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	return Finish(Result);
}

// 篝火网络表现流程：NetMulticast 已由引擎把服务器确认的 RequestId 分发到该 Actor 的相关连接；每个收到调用的进程只广播一次既有本地委托，不写 ACK、ready、补播队列或持久状态。
void ACatCampHubActor::MulticastCampfirePlaybackRequested_Implementation(const FGuid RequestId)
{
	OnCampfirePlaybackRequested.Broadcast(RequestId);
}

// 营地只读判断流程：复用唯一范围解析并仅返回是否命中；不广播篝火、不恢复身体也不修改任何 Revision。
bool ACatCampHubActor::IsControllerInCamp(AController* Controller) const
{
	return ResolveCharacterInCamp(Controller) != nullptr;
}

// 营地范围流程：读取当前 Controller Pawn，验证 authority、显式配置和与固定 Actor 的世界距离；客户端不能提交自报位置。
ACatCharacter* ACatCampHubActor::ResolveCharacterInCamp(AController* Controller) const
{
	const UCatCampSettings* Settings = GetDefault<UCatCampSettings>();
	ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr;
	return HasAuthority() && Settings && Settings->IsRuntimeReady() && Character
		&& FVector::DistSquared(Character->GetActorLocation(), GetActorLocation())
			<= FMath::Square(Settings->InteractionRadiusCentimeters)
		? Character : nullptr;
}
