#include "Items/CatItemsService.h"

#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Logging/CatLog.h"
#include "Engine/World.h"
#include "Items/CatContainerReplicationComponent.h"

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

	// 鱼离开容器权限流程：地面鱼护里的鱼只能由捕获者通过普通库存操作移出；其他玩家必须走 Social 偷鱼协议。
	// 旧 PersonalGuard 明确拒绝，避免角色随身鱼护路径复活。
	bool CanFishLeaveContainer(const FCatFishInstance& Fish, const ECatContainerKind ContainerKind,
		const FString& StableNetId)
	{
		if (ContainerKind == ECatContainerKind::FishGuard)
		{
			return Fish.OwnerStableNetId == StableNetId;
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

	// 鱼进入容器权限流程：旧 PersonalGuard 拒绝，共享鱼缸校验展示资格，鱼护箱子只要求目标本身是鱼容器。
	ECatDomainCommandError ResolveFishEnterContainerError(const FCatFishInstance& Fish,
		const ECatContainerKind ContainerKind,
		const FString& StableNetId)
	{
		if (ContainerKind == ECatContainerKind::PersonalGuard)
		{
			return ECatDomainCommandError::PolicyUndecided;
		}
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
}

// 创建条件流程：只允许 Game/PIE 的 authority World 持有可写 Items；客户端 World 不创建第二份容器聚合。
bool UCatItemsService::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

// 反初始化流程：先关闭命令并取消可逆预留，再清组件弱引用、容器和终态缓存；局内实物不会进入下一张地图。
void UCatItemsService::Deinitialize()
{
	CloseCommandsAndCancelReservations();
	Containers.Reset();
	ReservationByFish.Reset();
	Reservations.Reset();
	TheftEscrows.Reset();
	CaptureTerminalCache.Reset();
	CaptureByFishingSession.Reset();
	TransferTerminalCache.Reset();
	ConsumeTerminalCache.Reset();
	TheftTerminalCache.Reset();
	Super::Deinitialize();
}

// 容器注册流程：验证 authority 期间传入的宿主、ID 和世界容器类型，再把初始 Revision、容量和复制组件写入服务端记录并发布初始快照。
bool UCatItemsService::RegisterContainer(UCatContainerReplicationComponent* ReplicationComponent, const FGuid ContainerId,
	const ECatContainerKind Kind, const FString& OwnerStableNetId, const int32 Capacity)
{
	(void)OwnerStableNetId;
	if (!bCommandsOpen || !ReplicationComponent || !ContainerId.IsValid()
		|| Kind == ECatContainerKind::Unknown || Kind == ECatContainerKind::PersonalGuard)
	{
		return false;
	}
	if (FContainerRecord* Existing = Containers.Find(ContainerId))
	{
		return Existing->ReplicationComponent.Get() == ReplicationComponent;
	}
	FContainerRecord& Record = Containers.Add(ContainerId);
	Record.Snapshot.ContainerId = ContainerId;
	Record.Snapshot.Kind = Kind;
	Record.Snapshot.Revision = 1;
	Record.Snapshot.Capacity = FMath::Max(0, Capacity);
	Record.OwnerStableNetId.Reset();
	Record.Capacity = FMath::Max(0, Capacity);
	Record.ReplicationComponent = ReplicationComponent;
	PublishContainer(Record);
	return true;
}

// 容器注销流程：只移除弱引用精确匹配的宿主记录；迟到的旧 Actor 不能删除同 ID 的新注册容器。
void UCatItemsService::UnregisterContainer(UCatContainerReplicationComponent* ReplicationComponent)
{
	if (!ReplicationComponent)
	{
		return;
	}
	for (auto It = Containers.CreateIterator(); It; ++It)
	{
		if (It.Value().ReplicationComponent.Get() == ReplicationComponent)
		{
			if (CountReservedReturnSlots(It.Key()) > 0)
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

// 快照查询流程：按稳定容器 ID 复制公开 DTO；容量属于公开 UI 事实，服务端私有身份和预留永远不写入输出。
bool UCatItemsService::TryGetContainerSnapshot(const FGuid ContainerId, FCatContainerSnapshot& OutSnapshot) const
{
	const FContainerRecord* Record = Containers.Find(ContainerId);
	if (!Record)
	{
		OutSnapshot = FCatContainerSnapshot();
		return false;
	}
	OutSnapshot = Record->Snapshot;
	CatItems::RebuildContainedObjectsFromFish(OutSnapshot);
	return true;
}

// 捕获提交流程：先读取终态缓存，再验证命令、预分配鱼 ID、地面鱼护、Revision、容量和冻结定义值；
// 全部满足时把嘴叼鱼写入命中鱼护的第一个空槽，并发布同 Revision 的不可变 Committed DTO。
FCatCaptureCommitResult UCatItemsService::CommitCapture(const FCatCaptureCommitCommand& Command)
{
	FCatCaptureCommitResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	Result.Command.Error = ECatDomainCommandError::InvalidPayload;
	if (!Command.Context.RequestId.IsValid() || !Command.FishingSessionId.IsValid() || !Command.FishInstanceId.IsValid()
		|| !Command.TargetContainerId.IsValid()
		|| Command.Context.StableNetId.IsEmpty() || Command.FishDefinitionId.IsNone()
		|| !FMath::IsFinite(Command.WeightKilograms) || Command.WeightKilograms <= 0.0 || Command.SacrificeContribution <= 0)
	{
		return Result;
	}
	const FString CacheKey = MakeTerminalKey(Command.Context.StableNetId, TEXT("Capture"),
		Command.FishingSessionId, Command.Context.RequestId);
	if (const FCatCaptureCommitResult* Cached = CaptureTerminalCache.Find(CacheKey))
	{
		Result = *Cached;
		Result.Command.bCommitted = false;
		Result.Command.Error = ECatDomainCommandError::AlreadyResolved;
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
	if (!bCommandsOpen)
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
	else if (Target->Snapshot.Revision != Command.Context.ExpectedRevision)
	{
		Result.Command.Error = ECatDomainCommandError::RevisionConflict;
	}
	else if (Target->Capacity <= 0)
	{
		Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
	}
	else if (CatItems::GetContainedObjectCount(Target->Snapshot) + CountReservedReturnSlots(Command.TargetContainerId)
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
			Fish.SacrificeContribution = Command.SacrificeContribution;
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
	UE_LOG(LogCatItems, Log, TEXT("Event=items_capture_terminal RequestId=%s SessionId=%s Committed=%s Error=%s ContainerRevision=%lld"),
		*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		*Command.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
		Result.Command.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Command.Error), Result.Command.Revision);
	return Result;
}

// 通用容器物体转移流程：先校验对象级载荷和源/目标槽位意图，再按 ObjectKind 进入集中领域适配；当前只有鱼对象复用原子转移并真正提交，装备或耗材会明确返回策略未裁。
FCatDomainCommandResult UCatItemsService::TransferContainedObject(const FCatContainerObjectTransferCommand& Command)
{
	FCatDomainCommandResult Result;
	Result.RequestId = Command.Context.RequestId;
	Result.Error = ECatDomainCommandError::InvalidPayload;
	if (!Command.Context.RequestId.IsValid() || Command.Context.StableNetId.IsEmpty()
		|| Command.ObjectKind == ECatContainedObjectKind::Unknown || !Command.ObjectInstanceId.IsValid()
		|| !Command.SourceContainerId.IsValid() || !Command.TargetContainerId.IsValid()
		|| Command.SourceContainerSlotIndex == INDEX_NONE || Command.TargetContainerSlotIndex == INDEX_NONE)
	{
		return Result;
	}
	switch (Command.ObjectKind)
	{
	case ECatContainedObjectKind::Fish:
		{
			FCatFishTransferCommand FishCommand;
			FishCommand.Context = Command.Context;
			FishCommand.FishInstanceId = Command.ObjectInstanceId;
			FishCommand.SourceContainerId = Command.SourceContainerId;
			FishCommand.SourceContainerSlotIndex = Command.SourceContainerSlotIndex;
			FishCommand.TargetContainerId = Command.TargetContainerId;
			FishCommand.TargetContainerSlotIndex = Command.TargetContainerSlotIndex;
			FishCommand.ExpectedTargetRevision = Command.ExpectedTargetRevision;
			return TransferOwnedFish(FishCommand);
		}
	case ECatContainedObjectKind::Equipment:
	case ECatContainedObjectKind::Consumable:
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Result;
	case ECatContainedObjectKind::Unknown:
	default:
		return Result;
	}
}

// 原子转移流程：先重放终态，再同时校验两容器、个人归属、双 Revision、源/目标槽位、锁和容量；提交时只交换或移动数组槽位，最后发布一次权威快照。
FCatDomainCommandResult UCatItemsService::TransferOwnedFish(const FCatFishTransferCommand& Command)
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
		Result.bCommitted = false;
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	FContainerRecord* Source = Containers.Find(Command.SourceContainerId);
	const bool bSameContainer = Command.SourceContainerId == Command.TargetContainerId;
	FContainerRecord* Target = bSameContainer ? Source : Containers.Find(Command.TargetContainerId);
	if (!bCommandsOpen)
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (!Source || !Target)
	{
		Result.Error = ECatDomainCommandError::NotFound;
	}
	else if (Source->Snapshot.Kind == ECatContainerKind::PersonalGuard
		|| Target->Snapshot.Kind == ECatContainerKind::PersonalGuard)
	{
		Result.Error = ECatDomainCommandError::PermissionDenied;
	}
	else if (Source->Snapshot.Revision != Command.Context.ExpectedRevision || Target->Snapshot.Revision != Command.ExpectedTargetRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
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
		else if (ReservationByFish.Contains(Command.FishInstanceId))
		{
			Result.Error = ECatDomainCommandError::InvalidPhase;
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
			else if (bTargetOccupied && ReservationByFish.Contains(TargetFish.FishInstanceId))
			{
				Result.Error = ECatDomainCommandError::InvalidPhase;
			}
			else if (!bSameContainer && !bTargetOccupied
				&& CatItems::GetContainedObjectCount(Target->Snapshot) + CountReservedReturnSlots(Command.TargetContainerId)
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
	UE_LOG(LogCatItems, Log, TEXT("Event=items_transfer_terminal RequestId=%s Committed=%s Error=%s SourceRevision=%lld"),
		*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens), Result.bCommitted ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(Result.Error), Result.Revision);
	return Result;
}

// 直接进食流程：先重放终态，再校验身份、容器 Revision、个人归属/共享缸、未预留与目标鱼；成功清空所在槽位并发布一次 Revision。
FCatFishConsumeResult UCatItemsService::ConsumeFish(const FCatFishConsumeCommand& Command)
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
	if (const FCatFishConsumeResult* Cached = ConsumeTerminalCache.Find(CacheKey))
	{
		Result = *Cached;
		Result.Command.bCommitted = false;
		Result.Command.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	FContainerRecord* Source = Containers.Find(Command.SourceContainerId);
	int32 FishIndex = INDEX_NONE;
	if (!bCommandsOpen)
	{
		Result.Command.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (!Source)
	{
		Result.Command.Error = ECatDomainCommandError::NotFound;
	}
	else if (Source->Snapshot.Revision != Command.Context.ExpectedRevision)
	{
		Result.Command.Error = ECatDomainCommandError::RevisionConflict;
	}
	else if (ReservationByFish.Contains(Command.FishInstanceId))
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPhase;
	}
	else
	{
		FishIndex = FindFishSlotById(Source->Snapshot, Command.FishInstanceId);
		if (FishIndex == INDEX_NONE)
		{
			Result.Command.Error = ECatDomainCommandError::NotFound;
		}
		else if (Source->Snapshot.Kind == ECatContainerKind::PersonalGuard
			|| (Source->Snapshot.Kind == ECatContainerKind::FishGuard
				&& Source->Snapshot.Fish[FishIndex].OwnerStableNetId != Command.Context.StableNetId))
		{
			Result.Command.Error = ECatDomainCommandError::PermissionDenied;
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
	UE_LOG(LogCatItems, Log, TEXT("Event=items_consume_terminal RequestId=%s Committed=%s Error=%s Revision=%lld"),
		*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens), Result.Command.bCommitted ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(Result.Command.Error), Result.Command.Revision);
	return Result;
}

// 献祭预留流程：按 RequestId 幂等读取既有记录，再校验身份、容器 Revision、鱼归属与未锁定；成功只增加容器 Revision 和锁，不提前删除复制数组。
FCatFishReservationResult UCatItemsService::ReserveFish(const FCatSacrificeCommand& Command)
{
	FCatFishReservationResult Result;
	Result.ReservationId = Command.Context.RequestId;
	const FString ReservationKey = MakeReservationKey(Command.Context.StableNetId, Command.ContainerId,
		Command.Context.RequestId);
	if (const FReservationRecord* Existing = Reservations.Find(ReservationKey))
	{
		Result.bReserved = true;
		Result.Error = ECatDomainCommandError::None;
		Result.ContainerRevision = Existing->bCommitted ? Existing->CommittedRevision
			: Containers.FindRef(Existing->ContainerId).Snapshot.Revision;
		Result.SacrificeContribution = Existing->Fish.SacrificeContribution;
		return Result;
	}
	FContainerRecord* Container = Containers.Find(Command.ContainerId);
	if (!bCommandsOpen)
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		return Result;
	}
	if (!Container)
	{
		Result.Error = ECatDomainCommandError::NotFound;
		return Result;
	}
	if (!Command.Context.RequestId.IsValid() || !Command.FishInstanceId.IsValid() || Command.Context.StableNetId.IsEmpty())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	if (Container->Snapshot.Revision != Command.Context.ExpectedRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
		Result.ContainerRevision = Container->Snapshot.Revision;
		return Result;
	}
	const int32 FishIndex = FindFishSlotById(Container->Snapshot, Command.FishInstanceId);
	if (FishIndex == INDEX_NONE)
	{
		Result.Error = ECatDomainCommandError::NotFound;
		return Result;
	}
	const FCatFishInstance& Fish = Container->Snapshot.Fish[FishIndex];
	const bool bOwnerMayReserve = Container->Snapshot.Kind == ECatContainerKind::SharedFishTank
		|| Fish.OwnerStableNetId == Command.Context.StableNetId;
	if (!bOwnerMayReserve)
	{
		Result.Error = ECatDomainCommandError::PermissionDenied;
		return Result;
	}
	if (ReservationByFish.Contains(Command.FishInstanceId))
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
		return Result;
	}
	FReservationRecord& Reservation = Reservations.Add(ReservationKey);
	Reservation.RequestId = Command.Context.RequestId;
	Reservation.ContainerId = Command.ContainerId;
	Reservation.Fish = Fish;
	ReservationByFish.Add(Fish.FishInstanceId, ReservationKey);
	++Container->Snapshot.Revision;
	PublishContainer(*Container);
	Result.bReserved = true;
	Result.Error = ECatDomainCommandError::None;
	Result.ContainerRevision = Container->Snapshot.Revision;
	Result.SacrificeContribution = Fish.SacrificeContribution;
	return Result;
}

// 预留取消流程：只消费未提交且容器匹配的记录，移除 Fish 锁并增加容器 Revision；已提交记录返回 AlreadyResolved 且不恢复鱼。
FCatDomainCommandResult UCatItemsService::CancelFishReservation(const FString& StableNetId, const FGuid RequestId,
	const FGuid ContainerId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString ReservationKey = MakeReservationKey(StableNetId, ContainerId, RequestId);
	FReservationRecord* Reservation = Reservations.Find(ReservationKey);
	FContainerRecord* Container = Containers.Find(ContainerId);
	if (!Reservation || !Container || Reservation->ContainerId != ContainerId)
	{
		Result.Error = ECatDomainCommandError::NotFound;
		return Result;
	}
	if (Reservation->bCommitted)
	{
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		Result.Revision = Reservation->CommittedRevision;
		return Result;
	}
	ReservationByFish.Remove(Reservation->Fish.FishInstanceId);
	Reservations.Remove(ReservationKey);
	++Container->Snapshot.Revision;
	PublishContainer(*Container);
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::Cancelled;
	Result.Revision = Container->Snapshot.Revision;
	return Result;
}

// 预留提交流程：重复提交原样返回；首次提交确认鱼仍存在且锁属于 RequestId，再不可逆清空槽位、释放锁、记录提交 Revision 并发布。
FCatFishReservationCommitResult UCatItemsService::CommitFishReservation(const FString& StableNetId,
	const FGuid RequestId, const FGuid ContainerId)
{
	FCatFishReservationCommitResult Result;
	const FString ReservationKey = MakeReservationKey(StableNetId, ContainerId, RequestId);
	FReservationRecord* Reservation = Reservations.Find(ReservationKey);
	FContainerRecord* Container = Containers.Find(ContainerId);
	if (!Reservation || !Container || Reservation->ContainerId != ContainerId)
	{
		Result.Error = ECatDomainCommandError::NotFound;
		return Result;
	}
	if (Reservation->bCommitted)
	{
		Result.bCommitted = true;
		Result.Error = ECatDomainCommandError::None;
		Result.ContainerRevision = Reservation->CommittedRevision;
		Result.SacrificeContribution = Reservation->Fish.SacrificeContribution;
		return Result;
	}
	const FString* LockOwner = ReservationByFish.Find(Reservation->Fish.FishInstanceId);
	const int32 FishIndex = FindFishSlotById(Container->Snapshot, Reservation->Fish.FishInstanceId);
	if (!LockOwner || *LockOwner != ReservationKey || FishIndex == INDEX_NONE)
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
		return Result;
	}
	Container->Snapshot.Fish[FishIndex] = FCatFishInstance();
	TrimTrailingEmptyFishSlots(Container->Snapshot);
	ReservationByFish.Remove(Reservation->Fish.FishInstanceId);
	++Container->Snapshot.Revision;
	Reservation->bCommitted = true;
	Reservation->CommittedRevision = Container->Snapshot.Revision;
	PublishContainer(*Container);
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	Result.ContainerRevision = Reservation->CommittedRevision;
	Result.SacrificeContribution = Reservation->Fish.SacrificeContribution;
	return Result;
}

// 偷鱼开始流程：先重放终态，再校验命令、源容器、Revision、目标鱼、非本人和未预留；成功把唯一鱼移入 escrow、源槽位清空并发布一次 Revision，同时记录返还槽位。
FCatFishTheftResult UCatItemsService::BeginFishTheft(const FCatFishTheftCommand& Command)
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
		Result.Command.bCommitted = false;
		Result.Command.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	FContainerRecord* Source = Containers.Find(Command.SourceContainerId);
	int32 FishIndex = INDEX_NONE;
	if (!bCommandsOpen)
	{
		Result.Command.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (!Source)
	{
		Result.Command.Error = ECatDomainCommandError::NotFound;
	}
	else if (Source->Snapshot.Revision != Command.Context.ExpectedRevision)
	{
		Result.Command.Error = ECatDomainCommandError::RevisionConflict;
	}
	else if (ReservationByFish.Contains(Command.FishInstanceId))
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPhase;
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

// 偷鱼追回流程：按服务器 ProtocolId 定位精确 escrow 与仍存源容器，优先把鱼放回原槽位并发布一次 Revision；随后删除 escrow，客户端 RequestId 永远不能命中别人的鱼。
FCatFishTheftResult UCatItemsService::ReturnStolenFish(const FGuid TheftProtocolId)
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

// 偷鱼吃掉流程：按服务器 ProtocolId 定位 escrow 后复制不可变鱼事实并删除；鱼已在 Begin 时离开源数组，因此这里不再改容器 Revision 或执行第二次删除。
FCatFishTheftResult UCatItemsService::CommitStolenFishConsumption(const FGuid TheftProtocolId)
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

// Teardown 流程：永久关闭新命令，逐个释放尚未提交的 Fish 锁并清除其预留记录；ItemsCommitted 记录保留给协调器补 Run，容器数组随后由 World 生命周期释放。
void UCatItemsService::CloseCommandsAndCancelReservations()
{
	bCommandsOpen = false;
	TArray<FGuid> OpenThefts;
	TheftEscrows.GetKeys(OpenThefts);
	for (const FGuid& TheftProtocolId : OpenThefts)
	{
		ReturnStolenFish(TheftProtocolId);
	}
	TArray<FString> CancelledRequests;
	for (const TPair<FString, FReservationRecord>& Pair : Reservations)
	{
		if (!Pair.Value.bCommitted)
		{
			ReservationByFish.Remove(Pair.Value.Fish.FishInstanceId);
			CancelledRequests.Add(Pair.Key);
		}
	}
	for (const FString& RequestKey : CancelledRequests)
	{
		Reservations.Remove(RequestKey);
	}
}

// 容器权威上下文读取流程：从服务器私有记录返回真实种类/主人，并把复制组件的 Owner 作为空间宿主；任一记录或宿主失效都整体失败，绝不从公开 DTO 猜主人。
bool UCatItemsService::TryGetContainerHost(const FGuid ContainerId, ECatContainerKind& OutKind,
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

// 容器权威上下文读取流程：先复用不泄露身份的宿主查询，再仅从同一服务器记录补充私有主人；任一实体失效都整体失败。
bool UCatItemsService::TryGetContainerAuthorityContext(const FGuid ContainerId, ECatContainerKind& OutKind,
	FString& OutOwnerStableNetId, AActor*& OutAuthorityActor) const
{
	const FContainerRecord* Record = Containers.Find(ContainerId);
	if (!Record || !TryGetContainerHost(ContainerId, OutKind, OutAuthorityActor))
	{
		OutOwnerStableNetId.Reset();
		return false;
	}
	OutOwnerStableNetId = Record->OwnerStableNetId;
	return true;
}

// 快照发布流程：把服务端组合 DTO 写入精确弱组件；宿主已销毁时只保留服务器事实，绝不回滚已提交事务。
void UCatItemsService::PublishContainer(FContainerRecord& Record)
{
	CatItems::RebuildContainedObjectsFromFish(Record.Snapshot);
	if (UCatContainerReplicationComponent* Component = Record.ReplicationComponent.Get())
	{
		Component->SetSnapshotFromAuthority(Record.Snapshot);
	}
}

// 终态键流程：仅在服务器内组合身份、操作、聚合 ID 和 RequestId；相同 RequestId 可安全用于另一容器，而同一聚合重试仍只读首次终态。
FString UCatItemsService::MakeTerminalKey(const FString& StableNetId, const TCHAR* Operation,
	const FGuid& AggregateId, const FGuid& RequestId)
{
	return FString::Printf(TEXT("%s|%s|%s|%s"), *StableNetId, Operation,
		*AggregateId.ToString(EGuidFormats::DigitsWithHyphens),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
}

// 预留键流程：把可逆锁精确绑定到服务器身份、容器聚合与 RequestId；提交和取消必须同时持有三者，不能用裸 RequestId 解锁他人记录。
FString UCatItemsService::MakeReservationKey(const FString& StableNetId, const FGuid& ContainerId,
	const FGuid& RequestId)
{
	return FString::Printf(TEXT("%s|%s|%s"), *StableNetId,
		*ContainerId.ToString(EGuidFormats::DigitsWithHyphens),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
}

// 返还槽位统计流程：遍历仍在进行的 escrow 并计算精确源容器数量；容量检查把这些鱼视为仍占位，保证追回无需挤掉别的鱼。
int32 UCatItemsService::CountReservedReturnSlots(const FGuid ContainerId) const
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
