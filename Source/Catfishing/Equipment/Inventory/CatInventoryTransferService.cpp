#include "Equipment/Inventory/CatInventoryTransferService.h"

#include "Engine/World.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Equipment/CatRunInventorySlotOperations.h"
#include "Equipment/Inventory/CatInventoryTransferEndpoint.h"
#include "GameFramework/Actor.h"
#include "Logging/CatLog.h"

DEFINE_LOG_CATEGORY_STATIC(LogCatInventoryTransfer, Log, All);

namespace
{
	bool SameEndpoint(const FCatInventoryEndpointRef& A, const FCatInventoryEndpointRef& B)
	{
		return A.Host.HasSameIndexAndSerialNumber(B.Host) && A.Channel == B.Channel && A.EntryId == B.EntryId;
	}

	// 签名只比较原请求；源槽转移后改变，不会影响同一请求的合法重放。
	bool SameRequest(const FCatInventoryTransferRequest& A, const FCatInventoryTransferRequest& B)
	{
		return SameEndpoint(A.Source, B.Source) && SameEndpoint(A.Target, B.Target)
			&& A.ExpectedSourceRevision == B.ExpectedSourceRevision
			&& A.ExpectedTargetRevision == B.ExpectedTargetRevision
			&& A.SourceSlotIndex == B.SourceSlotIndex && A.TargetSlotIndex == B.TargetSlotIndex
			&& A.Mode == B.Mode && A.Quantity == B.Quantity
			&& A.ExpectedSourceItemId == B.ExpectedSourceItemId;
	}

	const UCatEquipmentDefinition* FindDefinition(const FName DefinitionId)
	{
		return GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(DefinitionId);
	}

	int32 EffectiveStackLimit(const ICatInventoryTransferEndpoint& Endpoint, const FName DefinitionId)
	{
		const UCatEquipmentDefinition* Definition = FindDefinition(DefinitionId);
		const int32 EndpointLimit = Endpoint.GetInventoryTransferStackLimit(DefinitionId);
		if (!Definition || EndpointLimit <= 0) return 0;
		// 数量型资格属于物品定义；宿主自定义 MaxStackSize 不能把有独立耐久的装备变成数量栈。
		return Definition->bRunConsumable ? EndpointLimit : 1;
	}

	bool ValidateSlots(const FCatInventoryEndpointSnapshot& State,
		const ICatInventoryTransferEndpoint& Endpoint, TSet<FGuid>& SeenInstances)
	{
		if (State.Revision < 0 || State.Revision == MAX_int64 || State.Capacity < 0) return false;
		for (const FCatRunInventorySlot& Slot : State.Slots)
		{
			if (!CatRunInventorySlotOperations::IsInventorySlotOccupied(Slot))
			{
				if (!Slot.DefinitionId.IsNone() || Slot.Quantity != 0 || Slot.ItemInstanceId.IsValid()) return false;
				continue;
			}
			const int32 StackLimit = EffectiveStackLimit(Endpoint, Slot.DefinitionId);
			if (!Slot.ItemInstanceId.IsValid() || SeenInstances.Contains(Slot.ItemInstanceId)
				|| StackLimit <= 0 || Slot.Quantity > StackLimit
				|| !FMath::IsFinite(Slot.RodDurability) || Slot.RodDurability < 0.0) return false;
			SeenInstances.Add(Slot.ItemInstanceId);
		}
		return true;
	}

	ECatDomainCommandError PrepareTransfer(const FCatInventoryTransferRequest& Request,
		ICatInventoryTransferEndpoint& SourceEndpoint, ICatInventoryTransferEndpoint& TargetEndpoint,
		FCatInventoryEndpointSnapshot& Source, FCatInventoryEndpointSnapshot& Target,
		const bool bSameEndpoint, FCatInventoryTransferResult& Result)
	{
		if (!Source.bCanExtract || !Target.bCanReceive) return ECatDomainCommandError::PermissionDenied;
		TSet<FGuid> SeenInstances;
		if (!ValidateSlots(Source, SourceEndpoint, SeenInstances)
			|| (!bSameEndpoint && !ValidateSlots(Target, TargetEndpoint, SeenInstances)))
			return ECatDomainCommandError::InvalidPayload;
		Source.Slots.SetNum(FMath::Max(Source.Capacity, Source.Slots.Num()));
		if (!bSameEndpoint) Target.Slots.SetNum(FMath::Max(Target.Capacity, Target.Slots.Num()));
		TArray<FCatRunInventorySlot>& TargetSlots = bSameEndpoint ? Source.Slots : Target.Slots;
		if (!Source.Slots.IsValidIndex(Request.SourceSlotIndex)) return ECatDomainCommandError::InvalidPayload;
		const FCatRunInventorySlot OriginalItem = Source.Slots[Request.SourceSlotIndex];
		if (!CatRunInventorySlotOperations::IsInventorySlotOccupied(OriginalItem)) return ECatDomainCommandError::NotFound;
		if (Request.ExpectedSourceItemId.IsValid() && Request.ExpectedSourceItemId != OriginalItem.ItemInstanceId)
			return ECatDomainCommandError::RevisionConflict;
		const UCatEquipmentDefinition* SourceDefinition = FindDefinition(OriginalItem.DefinitionId);
		const int32 TargetLimit = EffectiveStackLimit(TargetEndpoint, OriginalItem.DefinitionId);
		if (TargetLimit <= 0) return ECatDomainCommandError::InvalidPayload;
		int32 TargetIndex = Request.TargetSlotIndex;
		if (Request.Mode == ECatInventoryTransferMode::TransferQuantity)
		{
			if (Request.Quantity <= 0 || Request.Quantity > OriginalItem.Quantity
				|| !SourceDefinition || (!SourceDefinition->bRunConsumable && Request.Quantity != 1))
				return ECatDomainCommandError::InvalidPayload;
			if (Request.Quantity < OriginalItem.Quantity && (!Source.bAllowPartial || !Target.bAllowPartial))
				return ECatDomainCommandError::InvalidPhase;
			if (Request.Quantity > TargetLimit) return ECatDomainCommandError::CapacityExceeded;
			if (TargetIndex == INDEX_NONE)
			{
				TargetIndex = TargetSlots.IndexOfByPredicate([](const FCatRunInventorySlot& Slot)
				{
					return !CatRunInventorySlotOperations::IsInventorySlotOccupied(Slot);
				});
				if (TargetIndex == INDEX_NONE) return ECatDomainCommandError::CapacityExceeded;
			}
			if (!TargetSlots.IsValidIndex(TargetIndex)
				|| (bSameEndpoint && TargetIndex == Request.SourceSlotIndex))
				return ECatDomainCommandError::InvalidPayload;
			FCatRunInventorySlot& TargetSlot = TargetSlots[TargetIndex];
			if (CatRunInventorySlotOperations::IsInventorySlotOccupied(TargetSlot))
			{
				if (!SourceDefinition->bRunConsumable || TargetSlot.DefinitionId != OriginalItem.DefinitionId
					|| Request.Quantity > FMath::Max(0, TargetLimit - TargetSlot.Quantity))
					return ECatDomainCommandError::CapacityExceeded;
				TargetSlot.Quantity += Request.Quantity;
				Result.Item = OriginalItem;
				Result.Item.ItemInstanceId = TargetSlot.ItemInstanceId;
			}
			else
			{
				TargetSlot = OriginalItem;
				TargetSlot.Quantity = Request.Quantity;
				if (Request.Quantity < OriginalItem.Quantity) TargetSlot.ItemInstanceId = FGuid::NewGuid();
				Result.Item = TargetSlot;
			}
			FCatRunInventorySlot& SourceSlot = Source.Slots[Request.SourceSlotIndex];
			SourceSlot.Quantity -= Request.Quantity;
			if (SourceSlot.Quantity == 0) SourceSlot = FCatRunInventorySlot();
			Result.MovedQuantity = Request.Quantity;
			Result.Item.Quantity = Request.Quantity;
			return ECatDomainCommandError::None;
		}
		if (Request.Mode != ECatInventoryTransferMode::DragToSlot || !TargetSlots.IsValidIndex(TargetIndex)
			|| (bSameEndpoint && TargetIndex == Request.SourceSlotIndex))
			return ECatDomainCommandError::InvalidPayload;
		const FCatRunInventorySlot PreviousTarget = TargetSlots[TargetIndex];
		const bool bTargetOccupied = CatRunInventorySlotOperations::IsInventorySlotOccupied(PreviousTarget);
		const bool bSwap = bTargetOccupied && PreviousTarget.DefinitionId != OriginalItem.DefinitionId;
		if (bSwap)
		{
			if (!Source.bAllowSwap || !Target.bAllowSwap || !Source.bCanReceive || !Target.bCanExtract)
				return ECatDomainCommandError::PermissionDenied;
			const int32 ReverseLimit = EffectiveStackLimit(SourceEndpoint, PreviousTarget.DefinitionId);
			if (ReverseLimit <= 0) return ECatDomainCommandError::InvalidPayload;
			if (PreviousTarget.Quantity > ReverseLimit || OriginalItem.Quantity > TargetLimit)
				return ECatDomainCommandError::CapacityExceeded;
		}
		else if (!bTargetOccupied && OriginalItem.Quantity > TargetLimit)
		{
			return ECatDomainCommandError::CapacityExceeded;
		}
		else if (bTargetOccupied)
		{
			const int32 Room = FMath::Max(0, TargetLimit - PreviousTarget.Quantity);
			if (Room == 0) return ECatDomainCommandError::AlreadyResolved;
			if (Room < OriginalItem.Quantity && (!Source.bAllowPartial || !Target.bAllowPartial))
				return ECatDomainCommandError::CapacityExceeded;
		}
		const auto Move = CatRunInventorySlotOperations::MoveItemBetweenSlotArrays(Source.Slots,
			Request.SourceSlotIndex, TargetSlots, TargetIndex,
			[&TargetEndpoint](const FName DefinitionId) { return EffectiveStackLimit(TargetEndpoint, DefinitionId); });
		if (!Move.bChanged) return Move.Error;
		Result.MovedQuantity = bTargetOccupied && !bSwap
			? TargetSlots[TargetIndex].Quantity - PreviousTarget.Quantity : OriginalItem.Quantity;
		Result.Item = TargetSlots[TargetIndex];
		Result.Item.Quantity = Result.MovedQuantity;
		return ECatDomainCommandError::None;
	}

	FCatInventoryEndpointWrite MakeWrite(const FCatInventoryEndpointRef& Ref,
		TArray<FCatRunInventorySlot>&& Slots)
	{
		FCatInventoryEndpointWrite Write;
		Write.Channel = Ref.Channel;
		Write.EntryId = Ref.EntryId;
		Write.Slots = MoveTemp(Slots);
		return Write;
	}
}

bool UCatInventoryTransferService::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

FCatInventoryTransferResult UCatInventoryTransferService::TransferFromAuthority(const FCatInventoryTransferRequest& Request)
{
	FCatInventoryTransferResult Result;
	Result.RequestId = Request.RequestId;
	UWorld* World = GetWorld();
	const auto LogResult = [&Request, World](const FCatInventoryTransferResult& Completed)
	{
		const bool bRejected = !Completed.bReplayed && Completed.Error != ECatDomainCommandError::None
			&& Completed.Error != ECatDomainCommandError::AlreadyResolved;
		const bool bAuthority = IsValid(Request.Initiator) && Request.Initiator->HasAuthority();
		const FString Message = FString::Printf(
			TEXT("Event=%s RequestId=%s World=%s NetMode=%d Authority=%d LocalRole=%d Initiator=%s Source=%s SourceChannel=%s SourceEntryId=%s Target=%s TargetChannel=%s TargetEntryId=%s ItemInstanceId=%s Quantity=%d SourceRevision=%lld TargetRevision=%lld Committed=%d Replayed=%d Error=%s"),
			bRejected ? TEXT("inventory_transfer_rejected") : TEXT("inventory_transfer_result"),
			*Request.RequestId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(World), World ? static_cast<int32>(World->GetNetMode()) : -1,
			bAuthority, IsValid(Request.Initiator) ? static_cast<int32>(Request.Initiator->GetLocalRole()) : -1, *GetNameSafe(Request.Initiator),
			*GetPathNameSafe(Request.Source.Host.Get()), *Request.Source.Channel.ToString(), *Request.Source.EntryId.ToString(),
			*GetPathNameSafe(Request.Target.Host.Get()), *Request.Target.Channel.ToString(), *Request.Target.EntryId.ToString(),
			*Completed.Item.ItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), Completed.MovedQuantity,
			Completed.SourceRevision, Completed.TargetRevision, Completed.bCommitted, Completed.bReplayed,
			*UEnum::GetValueAsString(Completed.Error));
		if (bRejected)
		{
			UE_LOG(LogCatInventoryTransfer, Warning, TEXT("%s"), *Message);
		}
		else
		{
			UE_LOG(LogCatInventoryTransfer, Log, TEXT("%s"), *Message);
		}
	};
	if (bClosing || !World)
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		LogResult(Result);
		return Result;
	}
	if (!IsInGameThread() || World->GetNetMode() == NM_Client || !Request.RequestId.IsValid()
		|| !IsValid(Request.Initiator) || !Request.Initiator->HasAuthority() || Request.Initiator->GetWorld() != World)
	{
		LogResult(Result);
		return Result;
	}
	UE_LOG(LogCatInventoryTransfer, Log,
		TEXT("Event=inventory_transfer_requested RequestId=%s World=%s NetMode=%d Authority=true LocalRole=%d Initiator=%s Source=%s SourceChannel=%s Target=%s TargetChannel=%s SourceSlot=%d TargetSlot=%d Quantity=%d Mode=%d ExpectedSourceRevision=%lld ExpectedTargetRevision=%lld"),
		*Request.RequestId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(World), static_cast<int32>(World->GetNetMode()),
		static_cast<int32>(Request.Initiator->GetLocalRole()), *GetNameSafe(Request.Initiator),
		*GetPathNameSafe(Request.Source.Host.Get()), *Request.Source.Channel.ToString(),
		*GetPathNameSafe(Request.Target.Host.Get()), *Request.Target.Channel.ToString(), Request.SourceSlotIndex, Request.TargetSlotIndex,
		Request.Quantity, static_cast<int32>(Request.Mode), Request.ExpectedSourceRevision, Request.ExpectedTargetRevision);
	const FRequestKey Key{FObjectKey(Request.Initiator), Request.RequestId};
	if (const FTerminalRecord* Cached = TerminalRecords.Find(Key))
	{
		Result.SourceRevision = Cached->Result.SourceRevision;
		Result.TargetRevision = Cached->Result.TargetRevision;
		if (SameRequest(Cached->Request, Request))
		{
			Result = Cached->Result;
			Result.bReplayed = true;
			if (Result.bCommitted) Result.Error = ECatDomainCommandError::AlreadyResolved;
			Result.bCommitted = false;
		}
		LogResult(Result);
		return Result;
	}
	const auto Finish = [this, &Key, &Request, &LogResult](const FCatInventoryTransferResult& Completed)
	{
		TerminalRecords.Add(Key, FTerminalRecord{Request, Completed});
		LogResult(Completed);
		return Completed;
	};
	UObject* SourceHost = Request.Source.Host.Get();
	UObject* TargetHost = Request.Target.Host.Get();
	ICatInventoryTransferEndpoint* SourceEndpoint = Cast<ICatInventoryTransferEndpoint>(SourceHost);
	ICatInventoryTransferEndpoint* TargetEndpoint = Cast<ICatInventoryTransferEndpoint>(TargetHost);
	const AActor* SourceAuthority = SourceEndpoint ? SourceEndpoint->GetInventoryTransferAuthorityActor() : nullptr;
	const AActor* TargetAuthority = TargetEndpoint ? TargetEndpoint->GetInventoryTransferAuthorityActor() : nullptr;
	if (!IsValid(SourceHost) || !IsValid(TargetHost) || !IsValid(SourceAuthority) || !IsValid(TargetAuthority)
		|| !SourceAuthority->HasAuthority() || !TargetAuthority->HasAuthority()
		|| SourceAuthority->GetWorld() != World || TargetAuthority->GetWorld() != World
		|| Request.Source.Channel.IsNone() || Request.Target.Channel.IsNone()
		|| Request.ExpectedSourceRevision < -1 || Request.ExpectedTargetRevision < -1)
		return Finish(Result);
	FCatInventoryEndpointSnapshot Source;
	FCatInventoryEndpointSnapshot Target;
	const bool bSameEndpoint = SameEndpoint(Request.Source, Request.Target);
	Result.Error = SourceEndpoint->ReadInventoryTransferEndpoint(Request.Source.Channel, Request.Source.EntryId, Source);
	Result.SourceRevision = Source.Revision;
	if (SourceHost == TargetHost) Result.TargetRevision = Source.Revision;
	if (Source.Slots.IsValidIndex(Request.SourceSlotIndex)) Result.Item = Source.Slots[Request.SourceSlotIndex];
	if (Result.Error != ECatDomainCommandError::None) return Finish(Result);
	if (bSameEndpoint) Target = Source;
	else Result.Error = TargetEndpoint->ReadInventoryTransferEndpoint(Request.Target.Channel, Request.Target.EntryId, Target);
	Result.TargetRevision = Target.Revision;
	if (Result.Error != ECatDomainCommandError::None) return Finish(Result);
	if ((Request.ExpectedSourceRevision != -1 && Request.ExpectedSourceRevision != Source.Revision)
		|| (Request.ExpectedTargetRevision != -1 && Request.ExpectedTargetRevision != Target.Revision))
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
		return Finish(Result);
	}
	if (SourceHost == TargetHost && Source.Revision != Target.Revision)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Finish(Result);
	}
	Result.Error = PrepareTransfer(Request, *SourceEndpoint, *TargetEndpoint, Source, Target, bSameEndpoint, Result);
	if (Result.Error != ECatDomainCommandError::None) return Finish(Result);
	TArray<FCatInventoryEndpointWrite> SourceWrites;
	SourceWrites.Add(MakeWrite(Request.Source, MoveTemp(Source.Slots)));
	TArray<FCatInventoryEndpointWrite> TargetWrites;
	if (!bSameEndpoint)
	{
		if (SourceHost == TargetHost) SourceWrites.Add(MakeWrite(Request.Target, MoveTemp(Target.Slots)));
		else TargetWrites.Add(MakeWrite(Request.Target, MoveTemp(Target.Slots)));
	}
	Result.SourceRevision = Source.Revision + 1;
	Result.TargetRevision = Target.Revision + 1;
	SourceEndpoint->ApplyInventoryTransferWritesSilently(SourceWrites, Result.SourceRevision);
	if (SourceHost != TargetHost) TargetEndpoint->ApplyInventoryTransferWritesSilently(TargetWrites, Result.TargetRevision);
	Result.bCommitted = true;
	// 两端已提交，先冻结结果与幂等终态。观察者可重入，但不能重新执行这一笔请求。
	const FCatInventoryTransferResult CommittedResult = Finish(Result);
	SourceEndpoint->PublishInventoryTransfer();
	if (SourceHost != TargetHost && IsValid(TargetHost)) TargetEndpoint->PublishInventoryTransfer();
	return CommittedResult;
}

void UCatInventoryTransferService::Deinitialize()
{
	bClosing = true;
	TerminalRecords.Reset();
	Super::Deinitialize();
}
