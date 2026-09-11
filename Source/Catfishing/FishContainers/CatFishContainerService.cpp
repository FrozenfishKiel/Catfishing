#include "FishContainers/CatFishContainerService.h"

#include "Camp/CatCampSettings.h"
#include "Character/CatCharacter.h"
#include "Condition/CatConditionComponent.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Logging/CatLog.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "FishContainers/CatContainerAccessRules.h"
#include "FishContainers/CatContainerReplicationComponent.h"
#include "FishContainers/CatFishGuardActor.h"
#include "FishContainers/CatFishTankActor.h"
#include "FishContainers/CatFishContainerSettings.h"

namespace
{
	// 有效鱼槽判断流程：服务器分配过 FishInstanceId 才代表真实鱼；默认构造项只是数组里的空格。
	bool IsValidFishSlot(const FCatFishInstance& Fish)
	{
		return Fish.FishInstanceId.IsValid();
	}

	// 槽位占用判断流程：先确认下标在数组内，再确认该位置有真实鱼；数组之外在容量内可视为空格。
	bool IsFishSlotOccupied(const FCatContainerSnapshot& Snapshot, const int32 SlotIndex)
	{
		return Snapshot.Fish.IsValidIndex(SlotIndex) && IsValidFishSlot(Snapshot.Fish[SlotIndex]);
	}

	// 鱼实例定位流程：按槽位数组线性查找真实鱼 ID，返回值就是该鱼当前所在的权威槽位下标。
	int32 FindFishSlotById(const FCatContainerSnapshot& Snapshot, const FGuid FishInstanceId)
	{
		return Snapshot.Fish.IndexOfByPredicate([FishInstanceId](const FCatFishInstance& Fish)
		{
			return Fish.FishInstanceId == FishInstanceId;
		});
	}

	// 鱼数量统计流程：只统计服务器分配过实例 ID 的鱼；中间空格和数组尾部占位都不消耗容量。
	int32 CountContainedFish(const FCatContainerSnapshot& Snapshot)
	{
		int32 Count = 0;
		for (const FCatFishInstance& Fish : Snapshot.Fish)
		{
			if (IsValidFishSlot(Fish))
			{
				++Count;
			}
		}
		return Count;
	}

	// 直接吃鱼载荷签名流程：把鱼实例绑定到 RequestId，阻止同一请求换鱼。
	FString MakeFishConsumePayloadSignature(const FCatFishConsumeCommand& Command)
	{
		return FString::Printf(TEXT("FishInstance=%s"),
			*Command.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens));
	}

	// 空格查找流程：在容器容量内寻找第一个没有真实鱼的槽位；容量未裁时返回 INDEX_NONE，防止写入无界数组。
	int32 FindFirstFreeFishSlot(const FCatContainerSnapshot& Snapshot, const int32 Capacity)
	{
		if (Capacity <= 0)
		{
			return INDEX_NONE;
		}
		for (int32 SlotIndex = 0; SlotIndex < Capacity; ++SlotIndex)
		{
			if (!IsFishSlotOccupied(Snapshot, SlotIndex))
			{
				return SlotIndex;
			}
		}
		return INDEX_NONE;
	}

	// 槽位扩展流程：只把数组扩到目标槽位可写，不预填整个容量；中间默认项就是可见空格。
	void EnsureFishSlot(FCatContainerSnapshot& Snapshot, const int32 SlotIndex)
	{
		if (SlotIndex >= Snapshot.Fish.Num())
		{
			Snapshot.Fish.SetNum(SlotIndex + 1);
		}
	}

	// 尾部清理流程：只删除数组末尾连续空槽，保留中间空格，这样拖到后排格子后 UI 仍能按容量显示正确位置。
	void TrimTrailingEmptyFishSlots(FCatContainerSnapshot& Snapshot)
	{
		while (!Snapshot.Fish.IsEmpty() && !IsValidFishSlot(Snapshot.Fish.Last()))
		{
			Snapshot.Fish.Pop(EAllowShrinking::No);
		}
	}

	// 鱼离开容器权限流程：地面鱼护按公共箱子处理，靠近并通过服务器容器校验的玩家都能用普通库存操作移动；共享鱼缸继续允许正式展示流转。
	bool CanFishLeaveContainer(const FCatFishInstance& Fish, const ECatContainerKind ContainerKind,
		const FString& StableNetId)
	{
		(void)Fish;
		(void)StableNetId;
		if (ContainerKind == ECatContainerKind::FishGuard)
		{
			return true;
		}
		return ContainerKind == ECatContainerKind::SharedFishTank;
	}

	// 鱼缸展示资格流程：共享展示容器只接收目录中明确允许展示的鱼，缺定义或未就绪都不能被客户端拖拽绕过。
	bool CanFishBeDisplayedInTank(const FCatFishInstance& Fish)
	{
		const UCatFishDefinition* Definition = GetDefault<UCatFishCatalogSettings>()->FindRuntimeDefinition(
			Fish.FishDefinitionId);
		return Definition && Definition->bTankDisplayEligible;
	}

	// 鱼进入容器权限流程：共享鱼缸校验展示资格，鱼护箱子只要求目标本身是鱼容器；其他未裁容器不接收鱼。
	ECatDomainCommandError ResolveFishEnterContainerError(const FCatFishInstance& Fish,
		const ECatContainerKind ContainerKind,
		const FString& StableNetId)
	{
		if (ContainerKind == ECatContainerKind::SharedFishTank)
		{
			return CanFishBeDisplayedInTank(Fish)
				? ECatDomainCommandError::None : ECatDomainCommandError::PolicyUndecided;
		}
		if (ContainerKind == ECatContainerKind::FishGuard)
		{
			return ECatDomainCommandError::None;
		}
		return ECatDomainCommandError::PolicyUndecided;
	}

	// 关卡键构造流程：按去掉 PIE 前缀的关卡包、Actor 名和组件名定位预放置宿主；动态对象由鱼容器服务单独分配可持久化实体键。
	bool TryMakePersistentContainerKey(const UCatContainerReplicationComponent* Component,
		const ECatContainerKind Kind, FString& OutKey)
	{
		OutKey.Reset();
		const AActor* Owner = Component ? Component->GetOwner() : nullptr;
		if (!Owner || !Owner->IsNetStartupActor()
			|| (Kind != ECatContainerKind::FishGuard && Kind != ECatContainerKind::SharedFishTank))
		{
			return false;
		}
		OutKey = FString::Printf(TEXT("level:%s|%s|%s|%d"),
			*UWorld::RemovePIEPrefix(Owner->GetLevel()->GetOutermost()->GetName()),
			*Owner->GetFName().ToString(), *Component->GetFName().ToString(), static_cast<int32>(Kind));
		return true;
	}

	// 宿主类型预检流程：持久化只支持领域内已实现注册生命周期的鱼护和鱼缸，拒绝抽象类、错误种类和不复制的宿主。
	bool IsPersistentContainerClassValid(UClass* HostClass, const ECatContainerKind Kind)
	{
		const bool bKnownClass = HostClass && !HostClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
			&& ((Kind == ECatContainerKind::FishGuard && HostClass->IsChildOf(ACatFishGuardActor::StaticClass()))
				|| (Kind == ECatContainerKind::SharedFishTank && HostClass->IsChildOf(ACatFishTankActor::StaticClass())));
		return bKnownClass && HostClass->GetDefaultObject<AActor>()->GetIsReplicated();
	}

	// 动态键读取流程：只接受鱼容器服务自己生成的正序号格式，恢复后据此继续编号，不接受网络 ContainerId 或随机 GUID。
	bool ParsePersistentContainerNumber(const FString& Key, int64& OutNumber)
	{
		OutNumber = 0;
		return Key.StartsWith(TEXT("runtime:")) && LexTryParseString(OutNumber, *Key.Mid(8))
			&& OutNumber > 0 && OutNumber < MAX_int64 && Key == FString::Printf(TEXT("runtime:%lld"), OutNumber);
	}

	// 持久化鱼校验流程：逐条确认实例、定义、所有权、重量和跨容器唯一性；坏鱼不能因恢复路径绕过正常捕获时的领域前提。
	bool ValidatePersistentFish(const FCatFishInstance& Fish, TSet<FGuid>& SeenFishInstanceIds,
		FText& OutFailure)
	{
		const UCatFishDefinition* Definition = GetDefault<UCatFishCatalogSettings>()->FindRuntimeDefinition(
			Fish.FishDefinitionId);
		if (!Fish.FishInstanceId.IsValid() || Fish.FishDefinitionId.IsNone() || Fish.OwnerStableNetId.IsEmpty()
			|| !FMath::IsFinite(Fish.WeightKilograms) || Fish.WeightKilograms <= 0.0
			|| !Definition || SeenFishInstanceIds.Contains(Fish.FishInstanceId))
		{
			OutFailure = FText::FromString(TEXT("世界鱼容器含有无效、缺定义或重复的鱼实例。"));
			return false;
		}
		SeenFishInstanceIds.Add(Fish.FishInstanceId);
		return true;
	}
}

// 创建条件流程：只允许 Game/PIE 的 authority World 持有可写鱼容器服务；客户端 World 不创建第二份容器聚合。
bool UCatFishContainerService::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

// 反初始化流程：先关闭命令并归还可追回的偷鱼记录，再清组件弱引用、容器和终态缓存；局内实物不会进入下一张地图。
void UCatFishContainerService::Deinitialize()
{
	CloseCommandsFromAuthority();
	Containers.Reset();
	TheftEscrows.Reset();
	CaptureTerminalCache.Reset();
	CaptureByFishingSession.Reset();
	TransferTerminalCache.Reset();
	ConsumeTerminalCache.Reset();
	ConsumeTerminalPayloadByKey.Reset();
	TheftTerminalCache.Reset();
	Super::Deinitialize();
}

// 容器注册流程：先验证 authority 和恢复宿主配对，再建立网络记录与独立持久键并发布初始快照；恢复中意外宿主或序号耗尽会关闭命令，阻止外层提交。
bool UCatFishContainerService::RegisterContainer(UCatContainerReplicationComponent* ReplicationComponent, const FGuid ContainerId,
	const ECatContainerKind Kind, const int32 Capacity)
{
	AActor* Host = ReplicationComponent ? ReplicationComponent->GetOwner() : nullptr;
	if (bRestoringPersistentContainers && Host != ExpectedRestoreHost.Get())
	{
		bCommandsOpen = false;
		UE_LOG(LogCatFishContainers, Error, TEXT("Event=persistence_unexpected_container_registration Host=%s World=%s"),
			*GetNameSafe(Host), *GetNameSafe(GetWorld()));
		return false;
	}
	if (!bCommandsOpen || !Host || !Host->HasAuthority()
		|| Host->GetWorld() != GetWorld() || !ContainerId.IsValid() || Kind == ECatContainerKind::Unknown)
	{
		return false;
	}
	if (FContainerRecord* Existing = Containers.Find(ContainerId))
	{
		return Existing->ReplicationComponent.Get() == ReplicationComponent;
	}
	if (!Host->IsNetStartupActor() && (NextPersistentContainerNumber <= 0 || NextPersistentContainerNumber == MAX_int64))
	{
		bCommandsOpen = false;
		UE_LOG(LogCatFishContainers, Error, TEXT("Event=persistence_container_key_exhausted World=%s"), *GetNameSafe(GetWorld()));
		return false;
	}
	FContainerRecord& Record = Containers.Add(ContainerId);
	Record.Snapshot.ContainerId = ContainerId;
	Record.Snapshot.Kind = Kind;
	Record.Snapshot.Revision = 1;
	Record.Snapshot.Capacity = FMath::Max(0, Capacity);
	Record.Capacity = FMath::Max(0, Capacity);
	Record.ReplicationComponent = ReplicationComponent;
	Record.bRuntimeCreated = !Host->IsNetStartupActor();
	if (Record.bRuntimeCreated)
	{
		Record.PersistentKey = FString::Printf(TEXT("runtime:%lld"), NextPersistentContainerNumber++);
	}
	else
	{
		TryMakePersistentContainerKey(ReplicationComponent, Kind, Record.PersistentKey);
	}
	PublishContainer(Record);
	return true;
}

// 容器注销流程：先核对恢复时预期的销毁宿主，意外注销会关闭命令；再允许 pending-kill 弱引用仅作同一性比较，移除精确记录或保留 escrow 返还槽，前一个 Actor 不能删新登记。
void UCatFishContainerService::UnregisterContainer(UCatContainerReplicationComponent* ReplicationComponent)
{
	if (!ReplicationComponent)
	{
		return;
	}
	if (bRestoringPersistentContainers && ReplicationComponent->GetOwner() != ExpectedRestoreHost.Get(true))
	{
		bCommandsOpen = false;
		UE_LOG(LogCatFishContainers, Error, TEXT("Event=persistence_unexpected_container_unregistration Host=%s World=%s"),
			*GetNameSafe(ReplicationComponent->GetOwner()), *GetNameSafe(GetWorld()));
	}
	for (auto It = Containers.CreateIterator(); It; ++It)
	{
		if (It.Value().ReplicationComponent.Get(true) == ReplicationComponent)
		{
			if (CountPendingReturnSlots(It.Key()) > 0)
			{
				It.Value().ReplicationComponent.Reset();
			}
			else
			{
				It.RemoveCurrent();
			}
			return;
		}
	}
}

// 快照查询流程：按稳定容器 ID 复制公开 DTO；容量属于公开 UI 事实，服务端偷鱼窗口永远不写入输出。
bool UCatFishContainerService::TryGetContainerSnapshot(const FGuid ContainerId, FCatContainerSnapshot& OutSnapshot) const
{
	const FContainerRecord* Record = Containers.Find(ContainerId);
	if (!Record)
	{
		OutSnapshot = FCatContainerSnapshot();
		return false;
	}
	OutSnapshot = Record->Snapshot;
	return true;
}

// 捕获提交流程：先验证请求、会话、鱼身份和重量，再检查终态、容器种类、恢复门及可用容量。
// 成功时将鱼放入空槽，发布新快照并缓存会话唯一捕获事实；重放不重复创建鱼，拒绝不写容器。
FCatCaptureCommitResult UCatFishContainerService::CommitCapture(const FCatCaptureCommitCommand& Command)
{
	FCatCaptureCommitResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	Result.Command.Error = ECatDomainCommandError::InvalidPayload;
	if (!Command.Context.RequestId.IsValid() || !Command.FishingSessionId.IsValid() || !Command.FishInstanceId.IsValid()
		|| !Command.TargetContainerId.IsValid()
		|| Command.Context.StableNetId.IsEmpty() || Command.FishDefinitionId.IsNone()
		|| !FMath::IsFinite(Command.WeightKilograms) || Command.WeightKilograms <= 0.0)
	{
		return Result;
	}
	const FString CacheKey = MakeTerminalKey(Command.Context.StableNetId, TEXT("Capture"),
		Command.FishingSessionId, Command.Context.RequestId);
	if (const FCatCaptureCommitResult* Cached = CaptureTerminalCache.Find(CacheKey))
	{
		Result = *Cached;
		MarkCommandReplayed(Result.Command);
		return Result;
	}
	// FishingSession 是捕获竞争的聚合作用域；服务级映射在任何容器写入前复核，防止旁路或不同 RequestId 为同一会话生成第二个 FishInstance。
	if (const FCatCaptureCommittedResult* ExistingCapture = CaptureByFishingSession.Find(Command.FishingSessionId))
	{
		Result.Command.Error = ECatDomainCommandError::AlreadyResolved;
		Result.Command.Revision = ExistingCapture->ContainerRevision;
		Result.Committed = *ExistingCapture;
		CaptureTerminalCache.Add(CacheKey, Result);
		return Result;
	}
	FContainerRecord* Target = Containers.Find(Command.TargetContainerId);
	if (!bCommandsOpen || bRestoringPersistentContainers)
	{
		Result.Command.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (!Target)
	{
		Result.Command.Error = ECatDomainCommandError::NotFound;
	}
	else if (Target->Snapshot.Kind != ECatContainerKind::FishGuard)
	{
		Result.Command.Error = ECatDomainCommandError::PermissionDenied;
	}
	else if (Target->Capacity <= 0)
	{
		Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
	}
	else if (CountContainedFish(Target->Snapshot) + CountPendingReturnSlots(Command.TargetContainerId)
		>= Target->Capacity)
	{
		Result.Command.Error = ECatDomainCommandError::CapacityExceeded;
	}
	else
	{
		const int32 TargetSlotIndex = FindFirstFreeFishSlot(Target->Snapshot, Target->Capacity);
		if (TargetSlotIndex == INDEX_NONE)
		{
			Result.Command.Error = ECatDomainCommandError::CapacityExceeded;
		}
		else
		{
			FCatFishInstance Fish;
			Fish.FishInstanceId = Command.FishInstanceId;
			Fish.FishDefinitionId = Command.FishDefinitionId;
			Fish.OwnerStableNetId = Command.Context.StableNetId;
			Fish.SourceFishingSessionId = Command.FishingSessionId;
			Fish.WeightKilograms = Command.WeightKilograms;
			EnsureFishSlot(Target->Snapshot, TargetSlotIndex);
			Target->Snapshot.Fish[TargetSlotIndex] = Fish;
			TrimTrailingEmptyFishSlots(Target->Snapshot);
			++Target->Snapshot.Revision;
			PublishContainer(*Target);
			Result.Command.bCommitted = true;
			Result.Command.Error = ECatDomainCommandError::None;
			Result.Command.Revision = Target->Snapshot.Revision;
			Result.Committed.CaptureRequestId = Command.Context.RequestId;
			Result.Committed.FishingSessionId = Command.FishingSessionId;
			Result.Committed.FishInstance = Fish;
			Result.Committed.ContainerId = Command.TargetContainerId;
			Result.Committed.ContainerRevision = Target->Snapshot.Revision;
			CaptureByFishingSession.Add(Command.FishingSessionId, Result.Committed);
		}
	}
	Result.Command.Revision = Target ? Target->Snapshot.Revision : 0;
	CaptureTerminalCache.Add(CacheKey, Result);
	UE_LOG(LogCatFishContainers, Log, TEXT("Event=fish_container_capture_terminal RequestId=%s SessionId=%s Committed=%s Error=%s ContainerRevision=%lld"),
		*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		*Command.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
		Result.Command.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Command.Error), Result.Command.Revision);
	return Result;
}

// 可触达容器进食流程：先校验服务器身份、鱼定义和触达距离，再预检身体效果；
// 容器移除成功后提交身体效果，已有终态按原结果重放，身体提交失败会保留明确错误和日志。
FCatFishConsumeResult UCatFishContainerService::ConsumeReachableFish(AController* RequestingController,
	ACatCharacter* EatingCharacter, FCatFishConsumeCommand Command)
{
	FCatFishConsumeResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	Result.Body.RequestId = Command.Context.RequestId;
	UWorld* World = GetWorld();
	const ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	UCatConditionComponent* Conditions = EatingCharacter ? EatingCharacter->GetConditionComponent() : nullptr;
	const APlayerState* CurrentPlayerState = RequestingController ? RequestingController->PlayerState : nullptr;
	if (!RequestingController || EatingCharacter != RequestingController->GetPawn()
		|| !Command.Context.RequestId.IsValid() || !Command.FishInstanceId.IsValid()
		|| !Command.SourceContainerId.IsValid())
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	if (!Conditions || !CurrentPlayerState || !CurrentPlayerState->GetUniqueId().IsValid()
		|| EatingCharacter->GetWorld() != World)
	{
		Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	Command.Context.StableNetId = CurrentPlayerState->GetUniqueId()->ToString();
	const auto SubmitBodyFromDefinition = [&](UCatFishDefinition* Definition)
	{
		if (!Definition)
		{
			Result.Body.Error = ECatDomainCommandError::PolicyUndecided;
			return;
		}
		Result.Body = Conditions->ConsumeCommittedFish(Command.Context.RequestId, Definition);
		if (!CatIsAcceptedDomainCommandResult(Result.Body))
		{
			UE_LOG(LogCatFishContainers, Error,
				TEXT("Event=fish_container_consume_body_commit_failed RequestId=%s FishInstanceId=%s ContainerId=%s FishContainerRevision=%lld BodyError=%s BodyReplay=%s BodyReplayError=%s BodyRevision=%lld"),
				*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
				*Command.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
				*Command.SourceContainerId.ToString(EGuidFormats::DigitsWithHyphens),
				Result.Command.Revision,
				*UEnum::GetValueAsString(Result.Body.Error),
				Result.Body.bTerminalReplay ? TEXT("true") : TEXT("false"),
				*UEnum::GetValueAsString(Result.Body.ReplayedTerminalError), Result.Body.Revision);
		}
	};
	FCatFishConsumeResult ReplayResult;
	if (TryReplayFishConsumeTerminal(Command, ReplayResult))
	{
		Result = ReplayResult;
		Result.Body.RequestId = Command.Context.RequestId;
		if (CatIsAcceptedDomainCommandResult(Result.Command))
		{
			UCatFishDefinition* ReplayDefinition =
				GetDefault<UCatFishCatalogSettings>()->FindRuntimeDefinition(Result.Fish.FishDefinitionId);
			SubmitBodyFromDefinition(ReplayDefinition);
		}
		return Result;
	}
	if (!GameMode || !GameMode->CanAcceptGameplayCommand(RequestingController))
	{
		Result.Command.Error = ECatDomainCommandError::CommandsClosed;
		return Result;
	}
	FCatContainerSnapshot Source;
	if (!TryGetContainerSnapshot(Command.SourceContainerId, Source))
	{
		Result.Command.Error = ECatDomainCommandError::NotFound;
		return Result;
	}
	Result.Command.Revision = Source.Revision;
	ECatContainerKind SourceKind = ECatContainerKind::Unknown;
	AActor* SourceHost = nullptr;
	if (Conditions->GetSnapshot().bDowned)
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPhase;
		return Result;
	}
	if (!TryGetContainerHost(Command.SourceContainerId, SourceKind, SourceHost))
	{
		Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	if (SourceKind != ECatContainerKind::FishGuard && SourceKind != ECatContainerKind::SharedFishTank)
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	const UCatCampSettings* CampSettings = GetDefault<UCatCampSettings>();
	if (!SourceHost)
	{
		Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	if (!CatContainerAccessRules::IsHostReachable(SourceHost, EatingCharacter, CampSettings))
	{
		Result.Command.Error = ECatDomainCommandError::PermissionDenied;
		return Result;
	}
	const FCatFishInstance* Fish = Source.Fish.FindByPredicate([&Command](const FCatFishInstance& Candidate)
	{
		return Candidate.FishInstanceId == Command.FishInstanceId;
	});
	if (!Fish)
	{
		Result.Command.Error = ECatDomainCommandError::NotFound;
		return Result;
	}
	UCatFishDefinition* Definition = GetDefault<UCatFishCatalogSettings>()->FindRuntimeDefinition(Fish->FishDefinitionId);
	if (!Definition)
	{
		Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
		return Result;
	}
	Result.Command.Error = Conditions->ValidateFishConsumption(Definition);
	if (Result.Command.Error != ECatDomainCommandError::None)
	{
		return Result;
	}
	Result = ConsumeFish(Command);
	Result.Body.RequestId = Command.Context.RequestId;
	if (CatIsAcceptedDomainCommandResult(Result.Command))
	{
		SubmitBodyFromDefinition(Definition);
	}
	return Result;
}

// 原子转移流程：先重放终态，再拒绝恢复窗口并校验两容器、鱼归属、槽位和容量；提交时只交换或移动数组槽位，最后发布权威快照。
FCatDomainCommandResult UCatFishContainerService::TransferOwnedFish(const FCatFishTransferCommand& Command)
{
	FCatDomainCommandResult Result;
	Result.RequestId = Command.Context.RequestId;
	Result.Error = ECatDomainCommandError::InvalidPayload;
	if (!Command.Context.RequestId.IsValid() || Command.Context.StableNetId.IsEmpty() || !Command.FishInstanceId.IsValid()
		|| !Command.SourceContainerId.IsValid() || !Command.TargetContainerId.IsValid()
		|| Command.SourceContainerSlotIndex == INDEX_NONE || Command.TargetContainerSlotIndex == INDEX_NONE)
	{
		return Result;
	}
	const FString CacheKey = MakeTerminalKey(Command.Context.StableNetId, TEXT("Transfer"),
		Command.SourceContainerId, Command.Context.RequestId);
	if (const FCatDomainCommandResult* Cached = TransferTerminalCache.Find(CacheKey))
	{
		Result = *Cached;
		MarkCommandReplayed(Result);
		return Result;
	}
	FContainerRecord* Source = Containers.Find(Command.SourceContainerId);
	const bool bSameContainer = Command.SourceContainerId == Command.TargetContainerId;
	FContainerRecord* Target = bSameContainer ? Source : Containers.Find(Command.TargetContainerId);
	if (!bCommandsOpen || bRestoringPersistentContainers)
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (!Source || !Target)
	{
		Result.Error = ECatDomainCommandError::NotFound;
	}
	else if (Source->Capacity <= 0 || Target->Capacity <= 0)
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
	}
	else if (Command.SourceContainerSlotIndex < 0 || Command.SourceContainerSlotIndex >= Source->Capacity)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (Command.TargetContainerSlotIndex < 0 || Command.TargetContainerSlotIndex >= Target->Capacity)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else
	{
		const int32 FishIndex = FindFishSlotById(Source->Snapshot, Command.FishInstanceId);
		if (FishIndex == INDEX_NONE)
		{
			Result.Error = ECatDomainCommandError::NotFound;
		}
		else if (FishIndex != Command.SourceContainerSlotIndex)
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
		}
		else
		{
			const FCatFishInstance Fish = Source->Snapshot.Fish[FishIndex];
			const bool bTargetOccupied = IsFishSlotOccupied(Target->Snapshot, Command.TargetContainerSlotIndex);
			const FCatFishInstance TargetFish = bTargetOccupied
				? Target->Snapshot.Fish[Command.TargetContainerSlotIndex] : FCatFishInstance();
			const bool bFishMayLeave = CanFishLeaveContainer(Fish, Source->Snapshot.Kind,
				Command.Context.StableNetId);
			const ECatDomainCommandError FishEnterError = ResolveFishEnterContainerError(Fish,
				Target->Snapshot.Kind, Command.Context.StableNetId);
			const bool bTargetFishMayLeave = !bTargetOccupied || CanFishLeaveContainer(TargetFish,
				Target->Snapshot.Kind, Command.Context.StableNetId);
			const ECatDomainCommandError TargetFishEnterError = bTargetOccupied
				? ResolveFishEnterContainerError(TargetFish, Source->Snapshot.Kind, Command.Context.StableNetId)
				: ECatDomainCommandError::None;
			if (!bFishMayLeave || !bTargetFishMayLeave)
			{
				Result.Error = ECatDomainCommandError::PermissionDenied;
			}
			else if (FishEnterError != ECatDomainCommandError::None)
			{
				Result.Error = FishEnterError;
			}
			else if (TargetFishEnterError != ECatDomainCommandError::None)
			{
				Result.Error = TargetFishEnterError;
			}
			else if (!bSameContainer && !bTargetOccupied
				&& CountContainedFish(Target->Snapshot) + CountPendingReturnSlots(Command.TargetContainerId)
					>= Target->Capacity)
			{
				Result.Error = ECatDomainCommandError::CapacityExceeded;
			}
			else if (bSameContainer && Command.SourceContainerSlotIndex == Command.TargetContainerSlotIndex)
			{
				Result.Error = ECatDomainCommandError::AlreadyResolved;
				Result.Revision = Source->Snapshot.Revision;
			}
			else
			{
				if (bSameContainer)
				{
					EnsureFishSlot(Source->Snapshot, Command.TargetContainerSlotIndex);
					Source->Snapshot.Fish.Swap(Command.SourceContainerSlotIndex, Command.TargetContainerSlotIndex);
					TrimTrailingEmptyFishSlots(Source->Snapshot);
					++Source->Snapshot.Revision;
					PublishContainer(*Source);
				}
				else
				{
					EnsureFishSlot(Target->Snapshot, Command.TargetContainerSlotIndex);
					Target->Snapshot.Fish[Command.TargetContainerSlotIndex] = Fish;
					Source->Snapshot.Fish[Command.SourceContainerSlotIndex] = TargetFish;
					TrimTrailingEmptyFishSlots(Source->Snapshot);
					TrimTrailingEmptyFishSlots(Target->Snapshot);
					++Source->Snapshot.Revision;
					++Target->Snapshot.Revision;
					PublishContainer(*Source);
					PublishContainer(*Target);
				}
				Result.bCommitted = true;
				Result.Error = ECatDomainCommandError::None;
				Result.Revision = Source->Snapshot.Revision;
			}
		}
	}
	Result.Revision = Source ? Source->Snapshot.Revision : 0;
	TransferTerminalCache.Add(CacheKey, Result);
	UE_LOG(LogCatFishContainers, Log, TEXT("Event=fish_container_transfer_terminal RequestId=%s Committed=%s Error=%s SourceRevision=%lld"),
		*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens), Result.bCommitted ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(Result.Error), Result.Revision);
	return Result;
}

// 直接进食流程：先重放终态，再拒绝恢复窗口并校验身份、当前容器和目标鱼；地面鱼护作为公共容器，成功后才发布移除结果。
FCatFishConsumeResult UCatFishContainerService::ConsumeFish(const FCatFishConsumeCommand& Command)
{
	FCatFishConsumeResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	if (!Command.Context.RequestId.IsValid() || Command.Context.StableNetId.IsEmpty()
		|| !Command.FishInstanceId.IsValid() || !Command.SourceContainerId.IsValid())
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	const FString CacheKey = MakeTerminalKey(Command.Context.StableNetId, TEXT("ConsumeFish"),
		Command.SourceContainerId, Command.Context.RequestId);
	const FString PayloadSignature = MakeFishConsumePayloadSignature(Command);
	if (const FCatFishConsumeResult* Cached = ConsumeTerminalCache.Find(CacheKey))
	{
		const FString* CachedPayload = ConsumeTerminalPayloadByKey.Find(CacheKey);
		if (!CachedPayload || *CachedPayload != PayloadSignature)
		{
			Result.Command.Error = ECatDomainCommandError::InvalidPayload;
			return Result;
		}
		Result = *Cached;
		MarkCommandReplayed(Result.Command);
		return Result;
	}
	FContainerRecord* Source = Containers.Find(Command.SourceContainerId);
	int32 FishIndex = INDEX_NONE;
	if (!bCommandsOpen || bRestoringPersistentContainers)
	{
		Result.Command.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (!Source)
	{
		Result.Command.Error = ECatDomainCommandError::NotFound;
	}
	else
	{
		FishIndex = FindFishSlotById(Source->Snapshot, Command.FishInstanceId);
		if (FishIndex == INDEX_NONE)
		{
			Result.Command.Error = ECatDomainCommandError::NotFound;
		}
		else
		{
			Result.Fish = Source->Snapshot.Fish[FishIndex];
			Source->Snapshot.Fish[FishIndex] = FCatFishInstance();
			TrimTrailingEmptyFishSlots(Source->Snapshot);
			++Source->Snapshot.Revision;
			PublishContainer(*Source);
			Result.Command.bCommitted = true;
			Result.Command.Error = ECatDomainCommandError::None;
		}
	}
	Result.Command.Revision = Source ? Source->Snapshot.Revision : 0;
	ConsumeTerminalCache.Add(CacheKey, Result);
	ConsumeTerminalPayloadByKey.Add(CacheKey, PayloadSignature);
	UE_LOG(LogCatFishContainers, Log, TEXT("Event=fish_container_consume_terminal RequestId=%s Committed=%s Error=%s Revision=%lld"),
		*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens), Result.Command.bCommitted ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(Result.Command.Error), Result.Command.Revision);
	return Result;
}

bool UCatFishContainerService::TryReplayFishConsumeTerminal(const FCatFishConsumeCommand& Command,
	FCatFishConsumeResult& OutResult) const
{
	// 直接吃鱼重放查询流程：
	// 1. 用 ConsumeFish 的身份、容器和 RequestId 终态键查询缓存，再核对 FishInstanceId 签名。
	// 2. 未命中返回 false，让调用方继续首次提交 preflight；签名漂移返回 true+InvalidPayload，命中则返回可诊断鱼容器服务终态。
	// 3. 这个入口不写容器，也不替代 ConsumeFish 的首次提交校验。
	OutResult = FCatFishConsumeResult();
	OutResult.Command.RequestId = Command.Context.RequestId;
	if (!Command.Context.RequestId.IsValid() || Command.Context.StableNetId.IsEmpty()
		|| !Command.FishInstanceId.IsValid() || !Command.SourceContainerId.IsValid())
	{
		return false;
	}
	const FString CacheKey = MakeTerminalKey(Command.Context.StableNetId, TEXT("ConsumeFish"),
		Command.SourceContainerId, Command.Context.RequestId);
	const FCatFishConsumeResult* Cached = ConsumeTerminalCache.Find(CacheKey);
	if (!Cached)
	{
		return false;
	}
	const FString PayloadSignature = MakeFishConsumePayloadSignature(Command);
	const FString* CachedPayload = ConsumeTerminalPayloadByKey.Find(CacheKey);
	if (!CachedPayload || *CachedPayload != PayloadSignature)
	{
		OutResult.Command.Error = ECatDomainCommandError::InvalidPayload;
		return true;
	}
	OutResult = *Cached;
	MarkCommandReplayed(OutResult.Command);
	return true;
}

// 偷鱼开始流程：先重放终态，再拒绝恢复窗口并校验命令、源容器、目标鱼和捕获者差异；成功把鱼移入 escrow，清源槽并发布快照序号和返还槽位。
FCatFishTheftResult UCatFishContainerService::BeginFishTheft(const FCatFishTheftCommand& Command)
{
	FCatFishTheftResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	Result.TheftProtocolId = Command.TheftProtocolId;
	if (!Command.Context.RequestId.IsValid() || Command.Context.StableNetId.IsEmpty()
		|| !Command.TheftProtocolId.IsValid() || !Command.FishInstanceId.IsValid() || !Command.SourceContainerId.IsValid())
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	const FString CacheKey = MakeTerminalKey(Command.Context.StableNetId, TEXT("Theft"),
		Command.SourceContainerId, Command.Context.RequestId);
	if (const FCatFishTheftResult* Cached = TheftTerminalCache.Find(CacheKey))
	{
		Result = *Cached;
		MarkCommandReplayed(Result.Command);
		return Result;
	}
	FContainerRecord* Source = Containers.Find(Command.SourceContainerId);
	int32 FishIndex = INDEX_NONE;
	if (!bCommandsOpen || bRestoringPersistentContainers)
	{
		Result.Command.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (!Source)
	{
		Result.Command.Error = ECatDomainCommandError::NotFound;
	}
	else
	{
		FishIndex = FindFishSlotById(Source->Snapshot, Command.FishInstanceId);
		if (FishIndex == INDEX_NONE)
		{
			Result.Command.Error = ECatDomainCommandError::NotFound;
		}
		else if (Source->Snapshot.Fish[FishIndex].OwnerStableNetId.IsEmpty()
			|| Source->Snapshot.Fish[FishIndex].OwnerStableNetId == Command.Context.StableNetId)
		{
			Result.Command.Error = ECatDomainCommandError::PermissionDenied;
		}
		else if (TheftEscrows.Contains(Command.TheftProtocolId))
		{
			// ProtocolId 由 Social 生成且全局唯一；碰撞时不得覆盖另一玩家 escrow，也不能退回使用客户端 RequestId。
			Result.Command.Error = ECatDomainCommandError::AlreadyResolved;
		}
		else
		{
			FTheftEscrowRecord& Escrow = TheftEscrows.Add(Command.TheftProtocolId);
			Escrow.TheftProtocolId = Command.TheftProtocolId;
			Escrow.ClientRequestId = Command.Context.RequestId;
			Escrow.SourceContainerId = Command.SourceContainerId;
			Escrow.SourceContainerSlotIndex = FishIndex;
			Escrow.Fish = Source->Snapshot.Fish[FishIndex];
			Escrow.ThiefStableNetId = Command.Context.StableNetId;
			Source->Snapshot.Fish[FishIndex] = FCatFishInstance();
			TrimTrailingEmptyFishSlots(Source->Snapshot);
			++Source->Snapshot.Revision;
			PublishContainer(*Source);
			Result.Command.bCommitted = true;
			Result.Command.Error = ECatDomainCommandError::None;
			Result.Fish = Escrow.Fish;
			Result.SourceContainerId = Escrow.SourceContainerId;
			Result.TheftProtocolId = Escrow.TheftProtocolId;
		}
	}
	Result.Command.Revision = Source ? Source->Snapshot.Revision : 0;
	TheftTerminalCache.Add(CacheKey, Result);
	return Result;
}

// 偷鱼追回流程：按服务器 ProtocolId 定位精确 escrow 与仍存源容器，优先把鱼放回原槽位并发布一次快照序号；随后删除 escrow，客户端 RequestId 永远不能命中别人的鱼。
FCatFishTheftResult UCatFishContainerService::ReturnStolenFish(const FGuid TheftProtocolId)
{
	FCatFishTheftResult Result;
	Result.TheftProtocolId = TheftProtocolId;
	FTheftEscrowRecord* Escrow = TheftEscrows.Find(TheftProtocolId);
	Result.Command.RequestId = Escrow ? Escrow->ClientRequestId : FGuid();
	FContainerRecord* Source = Escrow ? Containers.Find(Escrow->SourceContainerId) : nullptr;
	if (!Escrow || !Source)
	{
		Result.Command.Error = ECatDomainCommandError::NotFound;
		return Result;
	}
	Result.Fish = Escrow->Fish;
	Result.SourceContainerId = Escrow->SourceContainerId;
	Result.TheftProtocolId = Escrow->TheftProtocolId;
	const int32 ReturnSlotIndex = Escrow->SourceContainerSlotIndex >= 0 && Escrow->SourceContainerSlotIndex < Source->Capacity
		&& !IsFishSlotOccupied(Source->Snapshot, Escrow->SourceContainerSlotIndex)
		? Escrow->SourceContainerSlotIndex : FindFirstFreeFishSlot(Source->Snapshot, Source->Capacity);
	if (ReturnSlotIndex == INDEX_NONE)
	{
		Result.Command.Error = ECatDomainCommandError::CapacityExceeded;
		return Result;
	}
	EnsureFishSlot(Source->Snapshot, ReturnSlotIndex);
	Source->Snapshot.Fish[ReturnSlotIndex] = Escrow->Fish;
	TrimTrailingEmptyFishSlots(Source->Snapshot);
	++Source->Snapshot.Revision;
	PublishContainer(*Source);
	Result.Command.bCommitted = true;
	Result.Command.Error = ECatDomainCommandError::None;
	Result.Command.Revision = Source->Snapshot.Revision;
	TheftEscrows.Remove(TheftProtocolId);
	return Result;
}

// 偷鱼吃掉流程：按服务器 ProtocolId 定位 escrow 后复制不可变鱼事实并删除；鱼已在 Begin 时离开源数组，因此这里保持容器快照不变，也不执行第二次删除。
FCatFishTheftResult UCatFishContainerService::CommitStolenFishConsumption(const FGuid TheftProtocolId)
{
	FCatFishTheftResult Result;
	Result.TheftProtocolId = TheftProtocolId;
	FTheftEscrowRecord* Escrow = TheftEscrows.Find(TheftProtocolId);
	Result.Command.RequestId = Escrow ? Escrow->ClientRequestId : FGuid();
	if (!Escrow)
	{
		Result.Command.Error = ECatDomainCommandError::NotFound;
		return Result;
	}
	Result.Fish = Escrow->Fish;
	Result.SourceContainerId = Escrow->SourceContainerId;
	Result.TheftProtocolId = Escrow->TheftProtocolId;
	Result.Command.bCommitted = true;
	Result.Command.Error = ECatDomainCommandError::None;
	if (const FContainerRecord* Source = Containers.Find(Escrow->SourceContainerId))
	{
		Result.Command.Revision = Source->Snapshot.Revision;
	}
	TheftEscrows.Remove(TheftProtocolId);
	return Result;
}

// Teardown 流程：永久关闭新命令，并在组件仍有效时把可追回的偷鱼记录放回源槽；容器数组随后由 World 生命周期释放。
void UCatFishContainerService::CloseCommandsFromAuthority()
{
	bCommandsOpen = false;
	TArray<FGuid> OpenThefts;
	TheftEscrows.GetKeys(OpenThefts);
	for (const FGuid& TheftProtocolId : OpenThefts)
	{
		ReturnStolenFish(TheftProtocolId);
	}
}

// 容器宿主读取流程：从服务器记录返回真实种类，并把复制组件所属 Actor 作为空间宿主；任一记录或宿主失效都整体失败。
bool UCatFishContainerService::TryGetContainerHost(const FGuid ContainerId, ECatContainerKind& OutKind,
	AActor*& OutAuthorityActor) const
{
	const FContainerRecord* Record = Containers.Find(ContainerId);
	UCatContainerReplicationComponent* Component = Record ? Record->ReplicationComponent.Get() : nullptr;
	AActor* AuthorityActor = Component ? Component->GetOwner() : nullptr;
	if (!Record || !AuthorityActor)
	{
		OutKind = ECatContainerKind::Unknown;
		OutAuthorityActor = nullptr;
		return false;
	}
	OutKind = Record->Snapshot.Kind;
	OutAuthorityActor = AuthorityActor;
	return true;
}

// 世界鱼容器导出流程：
// 1. 先拒绝尚未收口的偷鱼 escrow，避免把已从数组取出但未提交目标容器的鱼误当库存落盘。
// 2. 导出所有地图和动态容器，空箱同样记录；动态宿主保留正式类、位置和可延续实体键。
// 3. 复用鱼容器服务私有恢复校验检查定义、容量、空格与实例唯一性；任一容器无法恢复就整体失败。
bool UCatFishContainerService::ExportPersistedWorldFishContainers(TArray<FCatPersistentContainerSnapshot>& OutContainers,
	FText& OutFailure) const
{
	OutContainers.Reset();
	OutFailure = FText::GetEmpty();
	if (!bCommandsOpen || !GetWorld() || GetWorld()->GetNetMode() == NM_Client || bRestoringPersistentContainers
		|| !TheftEscrows.IsEmpty())
	{
		OutFailure = FText::FromString(TEXT("世界鱼容器存在客户端上下文或未收口的可逆事务。"));
		return false;
	}
	for (const TPair<FGuid, FContainerRecord>& Pair : Containers)
	{
		const FContainerRecord& Record = Pair.Value;
		UCatContainerReplicationComponent* Component = Record.ReplicationComponent.Get();
		AActor* Host = Component ? Component->GetOwner() : nullptr;
		if (!IsValid(Host) || !Host->HasAuthority() || Record.PersistentKey.IsEmpty())
		{
			OutFailure = FText::FromString(TEXT("世界容器的真实宿主或持久实体键已经失效。"));
			OutContainers.Reset();
			return false;
		}
		FCatPersistentContainerSnapshot& Saved = OutContainers.AddDefaulted_GetRef();
		Saved.PersistentKey = Record.PersistentKey;
		Saved.bRuntimeCreated = Record.bRuntimeCreated;
		Saved.HostClass = Host->GetClass();
		Saved.HostTransform = Host->GetActorTransform();
		Saved.ComponentName = Component->GetFName();
		Saved.Kind = Record.Snapshot.Kind;
		Saved.Fish = Record.Snapshot.Fish;
	}
	if (!ValidatePersistedWorldFishContainersForRestore(OutContainers, OutFailure))
	{
		OutContainers.Reset();
		return false;
	}
	return true;
}

// 世界鱼容器恢复私有校验流程：
// 1. 先确认新 World 没有残留事务，再建立当前地图稳定键到鱼容器记录的只读映射。
// 2. 地图对象必须全部原位匹配；动态对象只接受真实领域宿主类、组件名和合法 Transform，提前读取现行容量。
// 3. 校验全部鱼定义、鱼缸资格、空格残留和跨容器重复 ID；这是恢复入口内部使用的只读步骤，不创建 Actor、不改数组或发布复制。
bool UCatFishContainerService::ValidatePersistedWorldFishContainersForRestore(
	const TArray<FCatPersistentContainerSnapshot>& SavedContainers, FText& OutFailure) const
{
	OutFailure = FText::GetEmpty();
	if (!bCommandsOpen || !GetWorld() || GetWorld()->GetNetMode() == NM_Client || bRestoringPersistentContainers
		|| !TheftEscrows.IsEmpty())
	{
		OutFailure = FText::FromString(TEXT("世界鱼容器恢复上下文不可用或仍有残留事务。"));
		return false;
	}
	TMap<FString, FGuid> CurrentByPersistentKey;
	for (const TPair<FGuid, FContainerRecord>& Pair : Containers)
	{
		const FContainerRecord& Record = Pair.Value;
		const UCatContainerReplicationComponent* Component = Record.ReplicationComponent.Get();
		const AActor* Host = Component ? Component->GetOwner() : nullptr;
		if (!IsValid(Host) || !Host->HasAuthority() || Host->IsActorBeingDestroyed() || Host->IsActorBeginningPlay())
		{
			OutFailure = FText::FromString(TEXT("当前容器宿主正在生成、销毁或已失去权威，不能恢复。"));
			return false;
		}
		if (Record.bRuntimeCreated)
		{
			continue;
		}
		if (Record.PersistentKey.IsEmpty() || CurrentByPersistentKey.Contains(Record.PersistentKey)
			|| !Record.ReplicationComponent.IsValid())
		{
			OutFailure = FText::FromString(TEXT("当前地图存在重复的世界鱼容器稳定键。"));
			return false;
		}
		CurrentByPersistentKey.Add(Record.PersistentKey, Pair.Key);
	}
	TSet<FString> SeenSavedKeys;
	TSet<FGuid> SeenFishInstanceIds;
	for (const FCatPersistentContainerSnapshot& Saved : SavedContainers)
	{
		UClass* HostClass = Saved.HostClass.LoadSynchronous();
		int32 Capacity = GetDefault<UCatFishContainerSettings>()->GetContainerCapacity(static_cast<uint8>(Saved.Kind));
		int64 PersistentNumber = 0;
		if (Saved.PersistentKey.IsEmpty() || SeenSavedKeys.Contains(Saved.PersistentKey)
			|| !IsPersistentContainerClassValid(HostClass, Saved.Kind) || Saved.ComponentName.IsNone()
			|| !Saved.HostTransform.IsValid() || Saved.HostTransform.GetScale3D().GetMin() <= 0.0
			|| (Saved.bRuntimeCreated && !ParsePersistentContainerNumber(Saved.PersistentKey, PersistentNumber)))
		{
			OutFailure = FText::FromString(TEXT("世界容器的实体键、宿主类、组件或位置无效。"));
			return false;
		}
		if (!Saved.bRuntimeCreated)
		{
			const FGuid* CurrentId = CurrentByPersistentKey.Find(Saved.PersistentKey);
			const FContainerRecord* Current = CurrentId ? Containers.Find(*CurrentId) : nullptr;
			const UCatContainerReplicationComponent* Component = Current ? Current->ReplicationComponent.Get() : nullptr;
			if (!Component || Component->GetFName() != Saved.ComponentName || Component->GetOwner()->GetClass() != HostClass
				|| Current->Snapshot.Kind != Saved.Kind)
			{
				OutFailure = FText::FromString(TEXT("存档的关卡容器宿主缺失或已变更。"));
				return false;
			}
			Capacity = Current->Capacity;
		}
		if (Capacity <= 0 || Saved.Fish.Num() > Capacity)
		{
			OutFailure = FText::FromString(TEXT("保存的鱼容器超过当前领域容量或容量未配置。"));
			return false;
		}
		for (const FCatFishInstance& Fish : Saved.Fish)
		{
			if (!IsValidFishSlot(Fish))
			{
				if (!Fish.FishDefinitionId.IsNone() || !Fish.OwnerStableNetId.IsEmpty() || Fish.SourceFishingSessionId.IsValid()
					|| Fish.WeightKilograms != 0.0)
				{
					OutFailure = FText::FromString(TEXT("鱼容器空格包含残留实例字段。"));
					return false;
				}
				continue;
			}
			if (!ValidatePersistentFish(Fish, SeenFishInstanceIds, OutFailure))
			{
				return false;
			}
			if (Saved.Kind == ECatContainerKind::SharedFishTank && !CanFishBeDisplayedInTank(Fish))
			{
				OutFailure = FText::FromString(TEXT("存档含有当前鱼缸不允许展示的鱼定义。"));
				return false;
			}
		}
		SeenSavedKeys.Add(Saved.PersistentKey);
	}
	for (const TPair<FString, FGuid>& Pair : CurrentByPersistentKey)
	{
		if (!SeenSavedKeys.Contains(Pair.Key))
		{
			OutFailure = FText::FromString(TEXT("当前地图包含存档未记录的关卡鱼容器，不能部分恢复。"));
			return false;
		}
	}
	return true;
}

// 世界鱼容器恢复入口流程：
// 1. 只允许一层恢复；入口内部先执行私有校验，通过后冻结普通命令，并在生成前登记预期宿主，逐个核对真实 BeginPlay 注册和容量。
// 2. 再复核原记录未被回调改写，确认原动态宿主全部销毁且注销后，才提交整批鱼数组；跨回调仅保存 ID/弱引用，不保留 TMap 元素指针。
// 3. 发布后再次检查重入和宿主登记。任何异常都关闭本 World 写口并尝试清理本次新宿主；原宿主销毁不可回滚，调用方必须阻止进局且不得声称世界未改变。
bool UCatFishContainerService::RestorePersistedWorldFishContainers(
	const TArray<FCatPersistentContainerSnapshot>& SavedContainers)
{
	if (bRestoringPersistentContainers)
	{
		bCommandsOpen = false;
		UE_LOG(LogCatFishContainers, Error, TEXT("Event=persistence_container_restore_reentered World=%s"), *GetNameSafe(GetWorld()));
		return false;
	}
	FText Failure;
	if (!ValidatePersistedWorldFishContainersForRestore(SavedContainers, Failure))
	{
		bCommandsOpen = false;
		UE_LOG(LogCatFishContainers, Warning, TEXT("Event=persistence_world_fish_restore_rejected Reason=%s"), *Failure.ToString());
		return false;
	}
	TGuardValue<bool> RestoreGuard(bRestoringPersistentContainers, true);
	TGuardValue<TWeakObjectPtr<AActor>> HostGuard(ExpectedRestoreHost, nullptr);
	const TMap<FGuid, FContainerRecord> OriginalRecords = Containers;
	TMap<FString, FGuid> CurrentByPersistentKey;
	TArray<TWeakObjectPtr<AActor>> PreviousDynamicHosts;
	for (const TPair<FGuid, FContainerRecord>& Pair : Containers)
	{
		if (Pair.Value.bRuntimeCreated)
		{
			UCatContainerReplicationComponent* Component = Pair.Value.ReplicationComponent.Get();
			if (Component && Component->GetOwner())
			{
				PreviousDynamicHosts.AddUnique(Component->GetOwner());
			}
		}
		else
		{
			CurrentByPersistentKey.Add(Pair.Value.PersistentKey, Pair.Key);
		}
	}
	TArray<TWeakObjectPtr<AActor>> CreatedHosts;
	// 失败收口不伪造回滚：关闭写口，逐个检查新宿主销毁回执；清理失败保留诊断，整个 World 只能退出，不能再采样覆盖磁盘。
	const auto AbortRestore = [this, &CreatedHosts](const TCHAR* Reason)
	{
		bCommandsOpen = false;
		bool bCleaned = true;
		for (const TWeakObjectPtr<AActor>& WeakHost : CreatedHosts)
		{
			if (AActor* Host = WeakHost.Get())
			{
				ExpectedRestoreHost = Host;
				bCleaned &= Host->Destroy();
			}
		}
		ExpectedRestoreHost.Reset();
		UE_LOG(LogCatFishContainers, Error, TEXT("Event=persistence_container_restore_aborted World=%s NetMode=%d Reason=%s CreatedHostsCleaned=%d CommandsOpen=0"),
			*GetNameSafe(GetWorld()), GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : INDEX_NONE, Reason, bCleaned);
		return false;
	};
	for (const FCatPersistentContainerSnapshot& Saved : SavedContainers)
	{
		if (!Saved.bRuntimeCreated)
		{
			continue;
		}
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		// 引擎在组件初始化和 BeginPlay 前调用此处，精确配对本次宿主，拒绝构造/发布回调顺手注册其他容器。
		SpawnParameters.CustomPreSpawnInitialization = [this, &CreatedHosts](AActor* Host)
		{
			ExpectedRestoreHost = Host;
			CreatedHosts.Add(Host);
		};
		AActor* Host = GetWorld()->SpawnActor<AActor>(Saved.HostClass.Get(), Saved.HostTransform, SpawnParameters);
		ExpectedRestoreHost.Reset();
		if (!bCommandsOpen || !IsValid(Host) || Host->IsActorBeingDestroyed())
		{
			return AbortRestore(TEXT("SpawnFailedOrInterrupted"));
		}
		FGuid RegisteredId;
		int32 HostContainerCount = 0;
		for (TPair<FGuid, FContainerRecord>& Pair : Containers)
		{
			UCatContainerReplicationComponent* Component = Pair.Value.ReplicationComponent.Get();
			if (Component && Component->GetOwner() == Host)
			{
				++HostContainerCount;
				if (Component->GetFName() == Saved.ComponentName && Pair.Value.Snapshot.Kind == Saved.Kind
					&& Pair.Value.Capacity >= Saved.Fish.Num())
				{
					RegisteredId = Pair.Key;
				}
			}
		}
		if (!RegisteredId.IsValid() || HostContainerCount != 1)
		{
			return AbortRestore(TEXT("RegistrationMismatch"));
		}
		CurrentByPersistentKey.Add(Saved.PersistentKey, RegisteredId);
	}
	for (const TPair<FGuid, FContainerRecord>& Original : OriginalRecords)
	{
		const FContainerRecord* Current = Containers.Find(Original.Key);
		const UCatContainerReplicationComponent* Component = Current ? Current->ReplicationComponent.Get() : nullptr;
		const AActor* Host = Component ? Component->GetOwner() : nullptr;
		if (!bCommandsOpen || !Current || !IsValid(Host) || Host->IsActorBeingDestroyed()
			|| Current->ReplicationComponent != Original.Value.ReplicationComponent
			|| Current->Snapshot.Revision != Original.Value.Snapshot.Revision || Current->Capacity != Original.Value.Capacity
			|| Current->PersistentKey != Original.Value.PersistentKey)
		{
			return AbortRestore(TEXT("OriginalContainerChangedDuringSpawn"));
		}
	}
	TArray<TPair<FGuid, FCatContainerSnapshot>> PreparedSnapshots;
	PreparedSnapshots.Reserve(SavedContainers.Num());
	for (const FCatPersistentContainerSnapshot& Saved : SavedContainers)
	{
		const FGuid* Current = CurrentByPersistentKey.Find(Saved.PersistentKey);
		FContainerRecord* Record = Current ? Containers.Find(*Current) : nullptr;
		if (!Record || !Record->ReplicationComponent.IsValid())
		{
			return AbortRestore(TEXT("PreparedContainerMissing"));
		}
		FCatContainerSnapshot Restored = Record->Snapshot;
		Restored.Fish = Saved.Fish;
		Restored.Revision = FMath::Max<int64>(1, Restored.Revision + 1);
		PreparedSnapshots.Emplace(*Current, MoveTemp(Restored));
	}
	for (const TWeakObjectPtr<AActor>& PreviousHost : PreviousDynamicHosts)
	{
		AActor* Host = PreviousHost.Get();
		ExpectedRestoreHost = Host;
		const bool bDestroyed = Host && Host->Destroy();
		ExpectedRestoreHost.Reset();
		if (!bDestroyed || !bCommandsOpen)
		{
			return AbortRestore(TEXT("PreviousHostDestroyRejectedOrInterrupted"));
		}
	}
	if (Containers.Num() != PreparedSnapshots.Num())
	{
		return AbortRestore(TEXT("UnexpectedOrUnregisteredContainerCount"));
	}
	for (const TPair<FGuid, FCatContainerSnapshot>& Prepared : PreparedSnapshots)
	{
		const FContainerRecord* Record = Containers.Find(Prepared.Key);
		const UCatContainerReplicationComponent* Component = Record ? Record->ReplicationComponent.Get() : nullptr;
		const AActor* Host = Component ? Component->GetOwner() : nullptr;
		if (!IsValid(Host) || Host->IsActorBeingDestroyed())
		{
			return AbortRestore(TEXT("PreparedHostLostDuringDestruction"));
		}
	}
	for (const FCatPersistentContainerSnapshot& Saved : SavedContainers)
	{
		FContainerRecord& Record = Containers.FindChecked(CurrentByPersistentKey.FindChecked(Saved.PersistentKey));
		Record.PersistentKey = Saved.PersistentKey;
		int64 SavedNumber = 0;
		if (Saved.bRuntimeCreated && ParsePersistentContainerNumber(Saved.PersistentKey, SavedNumber))
		{
			NextPersistentContainerNumber = FMath::Max(NextPersistentContainerNumber, SavedNumber + 1);
		}
	}
	for (TPair<FGuid, FCatContainerSnapshot>& Prepared : PreparedSnapshots)
	{
		Containers.FindChecked(Prepared.Key).Snapshot = MoveTemp(Prepared.Value);
	}
	CaptureTerminalCache.Reset();
	CaptureByFishingSession.Reset();
	TransferTerminalCache.Reset();
	ConsumeTerminalCache.Reset();
	ConsumeTerminalPayloadByKey.Reset();
	TheftTerminalCache.Reset();
	for (const TPair<FGuid, FCatContainerSnapshot>& Prepared : PreparedSnapshots)
	{
		FContainerRecord* Record = Containers.Find(Prepared.Key);
		if (!bCommandsOpen || !Record || !Record->ReplicationComponent.IsValid())
		{
			return AbortRestore(TEXT("PublishInterrupted"));
		}
		PublishContainer(*Record);
	}
	if (!bCommandsOpen || Containers.Num() != PreparedSnapshots.Num())
	{
		return AbortRestore(TEXT("PublishReentered"));
	}
	UE_LOG(LogCatFishContainers, Log, TEXT("Event=persistence_world_fish_restored World=%s NetMode=%d Containers=%d DynamicHosts=%d"),
		*GetNameSafe(GetWorld()), static_cast<int32>(GetWorld()->GetNetMode()), PreparedSnapshots.Num(), CreatedHosts.Num());
	return true;
}

// 快照发布流程：把服务端组合 DTO 写入精确弱组件；宿主已销毁时只保留服务器事实，绝不回滚已提交事务。
void UCatFishContainerService::PublishContainer(FContainerRecord& Record)
{
	if (UCatContainerReplicationComponent* Component = Record.ReplicationComponent.Get())
	{
		Component->SetSnapshotFromAuthority(Record.Snapshot);
	}
}

// 终态键流程：仅在服务器内组合身份、操作、聚合 ID 和 RequestId；相同 RequestId 可安全用于另一容器，而同一聚合重试仍只读首次终态。
FString UCatFishContainerService::MakeTerminalKey(const FString& StableNetId, const TCHAR* Operation,
	const FGuid& AggregateId, const FGuid& RequestId)
{
	return FString::Printf(TEXT("%s|%s|%s|%s"), *StableNetId, Operation,
		*AggregateId.ToString(EGuidFormats::DigitsWithHyphens),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
}

// 返还槽位统计流程：遍历仍在进行的 escrow 并计算精确源容器数量；容量检查把这些鱼视为仍占位，保证追回无需挤掉别的鱼。
int32 UCatFishContainerService::CountPendingReturnSlots(const FGuid ContainerId) const
{
	int32 Count = 0;
	for (const TPair<FGuid, FTheftEscrowRecord>& Pair : TheftEscrows)
	{
		if (Pair.Value.SourceContainerId == ContainerId)
		{
			++Count;
		}
	}
	return Count;
}
