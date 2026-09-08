#include "Items/CatContainerCommandCoordinator.h"

#include "Camp/CatCampInventoryActor.h"
#include "Camp/CatCampHubActor.h"
#include "Camp/CatCampSettings.h"
#include "Character/CatCharacter.h"
#include "Engine/World.h"
#include "Equipment/CatEquipmentComponent.h"
#include "EngineUtils.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"
#include "Inventory/CatInventoryComponent.h"
#include "Items/CatContainerAccessRules.h"
#include "Items/CatItemsService.h"
#include "Logging/CatLog.h"

bool UCatContainerCommandCoordinator::ShouldCreateSubsystem(UObject* Outer) const
{
	// 创建条件流程：只在服务器 Game World 建立容器命令协调器；客户端没有容器写口或距离裁决权。
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

FCatDomainCommandResult UCatContainerCommandCoordinator::TransferReachableObject(
	AController* RequestingController, ACatCharacter* ControlledCharacter, const FGuid RequestId,
	const ECatContainedObjectKind ObjectKind, const FGuid ObjectInstanceId, const FGuid SourceContainerId,
	const ECatContainerKind SourceContainerKind, const int32 SourceContainerSlotIndex,
	const int64 ExpectedSourceRevision, const FGuid TargetContainerId,
	const ECatContainerKind TargetContainerKind, const int32 TargetContainerSlotIndex,
	const int64 ExpectedTargetRevision)
{
	// 普通容器库存提交流程：
	// 1. 先验证玩法命令 gate、RPC 参数形状、当前 Character、Items 服务和服务器身份；客户端身份只作为请求来源。
	// 2. 再从 Items 重读源/目标容器宿主、种类、快照和源槽位对象，要求客户端提交的容器类型与注册事实一致。
	// 3. 接着只做容器宿主距离校验；地面鱼护箱子是外部箱子库存，不要求拖拽者拥有鱼护，也不进入 Social 偷鱼协议。
	// 4. 最后构造 Items 转移命令，由 Items 按容器策略和 Revision 原子提交，结果交回 Controller 走 owning-client 回执。
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	UWorld* World = GetWorld();
	const ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	UCatItemsService* Items = World ? World->GetSubsystem<UCatItemsService>() : nullptr;
	const APlayerState* CurrentPlayerState = RequestingController ? RequestingController->PlayerState : nullptr;
	if (!GameMode || !GameMode->CanAcceptGameplayCommand(RequestingController))
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (!RequestId.IsValid() || ObjectKind == ECatContainedObjectKind::Unknown || !ObjectInstanceId.IsValid()
		|| !SourceContainerId.IsValid() || !TargetContainerId.IsValid()
		|| SourceContainerSlotIndex == INDEX_NONE || TargetContainerSlotIndex == INDEX_NONE
		|| SourceContainerKind == ECatContainerKind::Unknown || TargetContainerKind == ECatContainerKind::Unknown)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (!ControlledCharacter || ControlledCharacter->GetWorld() != World || !Items || !CurrentPlayerState
		|| !CurrentPlayerState->GetUniqueId().IsValid())
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else
	{
		ECatContainerKind ActualSourceKind = ECatContainerKind::Unknown;
		ECatContainerKind ActualTargetKind = ECatContainerKind::Unknown;
		AActor* SourceHost = nullptr;
		AActor* TargetHost = nullptr;
		const UCatCampSettings* CampSettings = GetDefault<UCatCampSettings>();
		FCatContainerSnapshot SourceSnapshot;
		FCatContainerSnapshot TargetSnapshot;
		if (!Items->TryGetContainerHost(SourceContainerId, ActualSourceKind, SourceHost)
			|| !Items->TryGetContainerHost(TargetContainerId, ActualTargetKind, TargetHost)
			|| !Items->TryGetContainerSnapshot(SourceContainerId, SourceSnapshot)
			|| !Items->TryGetContainerSnapshot(TargetContainerId, TargetSnapshot))
		{
			Result.Error = ECatDomainCommandError::NotFound;
		}
		else if (ActualSourceKind != SourceContainerKind || ActualTargetKind != TargetContainerKind)
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
		}
		else if (!CatContainerAccessRules::IsHostReachable(SourceHost, ControlledCharacter, CampSettings)
			|| !CatContainerAccessRules::IsHostReachable(TargetHost, ControlledCharacter, CampSettings))
		{
			Result.Error = ECatDomainCommandError::PermissionDenied;
		}
		else
		{
			FCatContainedObjectInstance MatchedObject;
			const bool bSourceSlotStillMatches = CatItems::TryGetContainedObjectAt(SourceSnapshot,
				SourceContainerSlotIndex, MatchedObject)
				&& MatchedObject.ObjectKind == ObjectKind
				&& MatchedObject.ObjectInstanceId == ObjectInstanceId;
			const FCatContainedObjectInstance* Object = bSourceSlotStillMatches ? &MatchedObject : nullptr;
			FCatContainedObjectInstance TargetSlotObject;
			const bool bTargetSlotOccupied = CatItems::TryGetContainedObjectAt(TargetSnapshot,
				TargetContainerSlotIndex, TargetSlotObject);
			if (!Object)
			{
				Result.Error = ECatDomainCommandError::NotFound;
			}
			else if (bTargetSlotOccupied && TargetSlotObject.ObjectKind != ObjectKind)
			{
				Result.Error = ECatDomainCommandError::PolicyUndecided;
			}
			else
			{
				FCatContainerObjectTransferCommand Command;
				Command.Context.RequestId = RequestId;
				Command.Context.ExpectedRevision = ExpectedSourceRevision;
				Command.Context.StableNetId = CurrentPlayerState->GetUniqueId()->ToString();
				Command.ObjectKind = ObjectKind;
				Command.ObjectInstanceId = ObjectInstanceId;
				Command.SourceContainerId = SourceContainerId;
				Command.SourceContainerSlotIndex = SourceContainerSlotIndex;
				Command.TargetContainerId = TargetContainerId;
				Command.TargetContainerSlotIndex = TargetContainerSlotIndex;
				Command.ExpectedTargetRevision = ExpectedTargetRevision;
				Result = Items->TransferContainedObject(Command);
			}
		}
	}
	return Result;
}

FCatDomainCommandResult UCatContainerCommandCoordinator::WithdrawCampInventoryItem(
	AController* RequestingController, ACatCharacter* ControlledCharacter, ACatCampInventoryActor* CampInventory,
	const FGuid RequestId, const int64 ExpectedCampInventoryRevision, const int32 SourceSlotIndex,
	const int32 Quantity, const int64 ExpectedInventoryRevision)
{
	// 营地公共仓库取物流程：
	// 1. 先在服务器侧重读玩法 gate、RequestId、仓库 World 和当前玩家正式 InventoryComponent；旧 Equipment 只作为可选投影刷新对象一并带下去。
	// 2. 再用仓库自己的交互距离裁决玩家是否仍触达公共仓库，避免 UI 旧引用直接授权移动。
	// 3. 通过后把公共仓库格、数量、营地版本和随身正式库存版本交给 CampInventoryActor 一次性提交。
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	UWorld* World = GetWorld();
	const ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	if (!GameMode || !GameMode->CanAcceptGameplayCommand(RequestingController))
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		return Result;
	}
	UCatInventoryComponent* PlayerInventory = ControlledCharacter ? ControlledCharacter->GetInventoryComponent() : nullptr;
	UCatEquipmentComponent* LegacyProjectionEquipment =
		ControlledCharacter ? ControlledCharacter->GetEquipmentComponent() : nullptr;
	const UCatCampSettings* CampSettings = GetDefault<UCatCampSettings>();
	if (!RequestId.IsValid() || !CampInventory || CampInventory->GetWorld() != World
		|| !ControlledCharacter || ControlledCharacter->GetWorld() != World || !PlayerInventory)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	if (!CatContainerAccessRules::IsHostReachable(CampInventory, ControlledCharacter, CampSettings))
	{
		Result.Error = ECatDomainCommandError::PermissionDenied;
		return Result;
	}
	return CampInventory->WithdrawToInventoryFromAuthority(RequestId, ExpectedCampInventoryRevision,
		SourceSlotIndex, Quantity, PlayerInventory, ExpectedInventoryRevision, LegacyProjectionEquipment);
}

FCatDomainCommandResult UCatContainerCommandCoordinator::MoveCampInventorySlot(
	AController* RequestingController, ACatCharacter* ControlledCharacter, ACatCampInventoryActor* CampInventory,
	const FGuid RequestId, const int64 ExpectedCampInventoryRevision, const int32 SourceSlotIndex,
	const int32 TargetSlotIndex)
{
	// 营地公共仓库整理流程：
	// 1. 先在服务器侧重读玩法 gate、RequestId、仓库 World 和当前玩家身份。
	// 2. 再用仓库自己的交互距离裁决触达；失败时记录可检索日志并返回公共领域错误。
	// 3. 通过后只把槽位和 Revision 交给 CampInventoryActor，移动/合并/交换规则不出 Items/Camp 系统。
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	UWorld* World = GetWorld();
	const ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	if (!GameMode || !GameMode->CanAcceptGameplayCommand(RequestingController))
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=move_camp_inventory_slot_rejected Reason=CommandsClosedOrInactive Request=%s Camp=%s Source=%d Target=%d"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(CampInventory),
			SourceSlotIndex, TargetSlotIndex);
		return Result;
	}
	const UCatCampSettings* CampSettings = GetDefault<UCatCampSettings>();
	if (!RequestId.IsValid() || !CampInventory || CampInventory->GetWorld() != World
		|| !ControlledCharacter || ControlledCharacter->GetWorld() != World)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=move_camp_inventory_slot_rejected Reason=DependencyUnavailable Request=%s Camp=%s Character=%s Source=%d Target=%d"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(CampInventory),
			*GetNameSafe(ControlledCharacter), SourceSlotIndex, TargetSlotIndex);
		return Result;
	}
	if (!CatContainerAccessRules::IsHostReachable(CampInventory, ControlledCharacter, CampSettings))
	{
		Result.Error = ECatDomainCommandError::PermissionDenied;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=move_camp_inventory_slot_rejected Reason=PermissionDenied Request=%s Camp=%s Character=%s Source=%d Target=%d"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(CampInventory),
			*GetNameSafe(ControlledCharacter), SourceSlotIndex, TargetSlotIndex);
		return Result;
	}
	Result = CampInventory->MoveInventorySlotFromAuthority(RequestId, ExpectedCampInventoryRevision,
		SourceSlotIndex, TargetSlotIndex);
	UE_LOG(LogCatfishing, Log,
		TEXT("Event=move_camp_inventory_slot Committed=%s Error=%s Revision=%lld Camp=%s Source=%d Target=%d"),
		Result.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Error),
		Result.Revision, *GetNameSafe(CampInventory), SourceSlotIndex, TargetSlotIndex);
	return Result;
}

FCatDomainCommandResult UCatContainerCommandCoordinator::DepositInventorySlotToCampInventory(
	AController* RequestingController, ACatCharacter* ControlledCharacter, ACatCampInventoryActor* CampInventory,
	const FGuid RequestId, const int64 ExpectedCampInventoryRevision, const int32 TargetCampSlotIndex,
	const int64 ExpectedInventoryRevision, const int32 SourceInventorySlotIndex)
{
	// 随身库存存入营地仓库流程：
	// 1. 先在服务器侧重读玩法 gate、RequestId、仓库 World 和当前玩家正式 InventoryComponent；旧 Equipment 只作为可选投影刷新对象一并带下去。
	// 2. 再按仓库交互距离确认玩家仍在公共仓库旁，避免远程拖拽旧 UI 数据。
	// 3. 最后由 CampInventoryActor 在同一事务中提交双方正式库存交换；旧 Equipment 读模型只跟随刷新。
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	UWorld* World = GetWorld();
	const ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	if (!GameMode || !GameMode->CanAcceptGameplayCommand(RequestingController))
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		return Result;
	}
	UCatInventoryComponent* PlayerInventory = ControlledCharacter ? ControlledCharacter->GetInventoryComponent() : nullptr;
	UCatEquipmentComponent* LegacyProjectionEquipment =
		ControlledCharacter ? ControlledCharacter->GetEquipmentComponent() : nullptr;
	const UCatCampSettings* CampSettings = GetDefault<UCatCampSettings>();
	if (!RequestId.IsValid() || !CampInventory || CampInventory->GetWorld() != World
		|| !ControlledCharacter || ControlledCharacter->GetWorld() != World || !PlayerInventory)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	if (!CatContainerAccessRules::IsHostReachable(CampInventory, ControlledCharacter, CampSettings))
	{
		Result.Error = ECatDomainCommandError::PermissionDenied;
		return Result;
	}
	return CampInventory->DepositFromInventorySlotFromAuthority(RequestId, ExpectedCampInventoryRevision,
		TargetCampSlotIndex, PlayerInventory, ExpectedInventoryRevision, SourceInventorySlotIndex,
		LegacyProjectionEquipment);
}

FCatDomainCommandResult UCatContainerCommandCoordinator::WithdrawCampInventoryItemToInventorySlot(
	AController* RequestingController, ACatCharacter* ControlledCharacter, ACatCampInventoryActor* CampInventory,
	const FGuid RequestId, const int64 ExpectedCampInventoryRevision, const int32 SourceCampSlotIndex,
	const int64 ExpectedInventoryRevision, const int32 TargetInventorySlotIndex)
{
	// 营地仓库拖入随身目标格流程：
	// 1. 先在服务器侧重读玩法 gate、RequestId、仓库 World 和当前玩家正式 InventoryComponent；旧 Equipment 只作为可选投影刷新对象一并带下去。
	// 2. 再按仓库交互距离确认玩家仍可触达公共仓库，客户端目标格只作为候选输入。
	// 3. 通过后由 CampInventoryActor 同时裁决公共仓库源格和随身正式库存目标格；旧 Equipment 只跟随刷新。
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	UWorld* World = GetWorld();
	const ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	if (!GameMode || !GameMode->CanAcceptGameplayCommand(RequestingController))
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		return Result;
	}
	UCatInventoryComponent* PlayerInventory = ControlledCharacter ? ControlledCharacter->GetInventoryComponent() : nullptr;
	UCatEquipmentComponent* LegacyProjectionEquipment =
		ControlledCharacter ? ControlledCharacter->GetEquipmentComponent() : nullptr;
	const UCatCampSettings* CampSettings = GetDefault<UCatCampSettings>();
	if (!RequestId.IsValid() || !CampInventory || CampInventory->GetWorld() != World
		|| !ControlledCharacter || ControlledCharacter->GetWorld() != World || !PlayerInventory)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	if (!CatContainerAccessRules::IsHostReachable(CampInventory, ControlledCharacter, CampSettings))
	{
		Result.Error = ECatDomainCommandError::PermissionDenied;
		return Result;
	}
	return CampInventory->WithdrawToInventorySlotFromAuthority(RequestId, ExpectedCampInventoryRevision,
		SourceCampSlotIndex, PlayerInventory, ExpectedInventoryRevision, TargetInventorySlotIndex,
		LegacyProjectionEquipment);
}

FCatDomainCommandResult UCatContainerCommandCoordinator::StoreFishInReachableSharedTank(
	AController* RequestingController, ACatCharacter* ControlledCharacter, const FGuid RequestId,
	const FGuid FishInstanceId, const FGuid SourceContainerId, const int32 SourceContainerSlotIndex,
	const int64 ExpectedSourceRevision)
{
	// 一键存缸提交流程：
	// 1. 先验证按钮请求形状、玩法命令 gate、当前 Character 和 Items 服务，避免在无效局状态下扫描营地。
	// 2. 再从当前 World 的固定营地中寻找已配置、已注册且玩家可触达的 SharedFishTank，并选第一个空鱼格作为目标。
	// 3. 找到目标后立即复用普通容器转移入口；源鱼护身份、同一条鱼、双容器距离、Revision 和幂等仍由通用路径与 Items 裁决。
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	UWorld* World = GetWorld();
	const ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	UCatItemsService* Items = World ? World->GetSubsystem<UCatItemsService>() : nullptr;
	if (!GameMode || !GameMode->CanAcceptGameplayCommand(RequestingController))
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
	}
	else if (!RequestId.IsValid() || !FishInstanceId.IsValid() || !SourceContainerId.IsValid()
		|| SourceContainerSlotIndex == INDEX_NONE)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (!World || !ControlledCharacter || ControlledCharacter->GetWorld() != World || !Items)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else
	{
		const UCatCampSettings* CampSettings = GetDefault<UCatCampSettings>();
		AActor* TargetHost = nullptr;
		FCatContainerSnapshot TargetSnapshot;
		int32 TargetSlotIndex = INDEX_NONE;
		bool bFoundSharedTank = false;
		bool bFoundReachableSharedTank = false;
		bool bFoundPolicyClosedSharedTank = false;
		for (TActorIterator<ACatCampHubActor> It(World); It; ++It)
		{
			ACatCampHubActor* Camp = *It;
			FCatContainerSnapshot CandidateSnapshot;
			if (!IsValid(Camp) || !Camp->TryGetSharedFishTankSnapshot(CandidateSnapshot)
				|| CandidateSnapshot.Kind != ECatContainerKind::SharedFishTank)
			{
				continue;
			}
			ECatContainerKind CandidateHostKind = ECatContainerKind::Unknown;
			AActor* CandidateHost = nullptr;
			if (!Items->TryGetContainerHost(CandidateSnapshot.ContainerId, CandidateHostKind, CandidateHost)
				|| CandidateHostKind != ECatContainerKind::SharedFishTank)
			{
				continue;
			}
			bFoundSharedTank = true;
			if (!CatContainerAccessRules::IsHostReachable(CandidateHost, ControlledCharacter, CampSettings))
			{
				continue;
			}
			bFoundReachableSharedTank = true;
			if (CandidateSnapshot.Capacity <= 0)
			{
				bFoundPolicyClosedSharedTank = true;
				continue;
			}
			const int32 CandidateSlotIndex = CatContainerAccessRules::FindFirstFreeSlot(CandidateSnapshot);
			if (CandidateSlotIndex == INDEX_NONE)
			{
				continue;
			}
			TargetHost = CandidateHost;
			TargetSnapshot = CandidateSnapshot;
			TargetSlotIndex = CandidateSlotIndex;
			break;
		}
		if (!TargetHost)
		{
			Result.Error = !bFoundSharedTank
				? ECatDomainCommandError::DependencyUnavailable
				: (!bFoundReachableSharedTank
					? ECatDomainCommandError::PermissionDenied
					: (bFoundPolicyClosedSharedTank
						? ECatDomainCommandError::PolicyUndecided : ECatDomainCommandError::CapacityExceeded));
		}
		else
		{
			UE_LOG(LogCatItems, Log,
				TEXT("Event=fish_guard_store_shared_tank_resolved Request=%s Fish=%s SourceContainer=%s SourceSlot=%d TargetTank=%s TargetContainer=%s TargetSlot=%d TargetRevision=%lld"),
				*RequestId.ToString(EGuidFormats::DigitsWithHyphens),
				*FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
				*SourceContainerId.ToString(EGuidFormats::DigitsWithHyphens), SourceContainerSlotIndex,
				*GetNameSafe(TargetHost),
				*TargetSnapshot.ContainerId.ToString(EGuidFormats::DigitsWithHyphens), TargetSlotIndex,
				TargetSnapshot.Revision);
			Result = TransferReachableObject(RequestingController, ControlledCharacter, RequestId,
				ECatContainedObjectKind::Fish, FishInstanceId, SourceContainerId, ECatContainerKind::FishGuard,
				SourceContainerSlotIndex, ExpectedSourceRevision, TargetSnapshot.ContainerId,
				ECatContainerKind::SharedFishTank, TargetSlotIndex, TargetSnapshot.Revision);
		}
	}
	if (Result.Error != ECatDomainCommandError::None)
	{
		UE_LOG(LogCatItems, Warning,
			TEXT("Event=fish_guard_store_shared_tank_rejected Request=%s Fish=%s SourceContainer=%s SourceSlot=%d Error=%s Revision=%lld"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			*FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
			*SourceContainerId.ToString(EGuidFormats::DigitsWithHyphens), SourceContainerSlotIndex,
			*UEnum::GetValueAsString(Result.Error), Result.Revision);
	}
	return Result;
}
