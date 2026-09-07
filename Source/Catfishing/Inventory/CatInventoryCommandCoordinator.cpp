#include "Inventory/CatInventoryCommandCoordinator.h"

#include "Character/CatCharacter.h"
#include "Engine/World.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "GameFramework/Controller.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Logging/CatLog.h"

bool UCatInventoryCommandCoordinator::ShouldCreateSubsystem(UObject* Outer) const
{
	// 创建条件流程：只在服务器 Game World 建立库存命令协调器；客户端 UI 只能发 RPC，不能直接改正式库存。
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

FCatDomainCommandResult UCatInventoryCommandCoordinator::MoveInventorySlot(AController* RequestingController,
	ACatCharacter* ControlledCharacter, const FGuid RequestId, const int64 ExpectedRevision,
	const int32 SourceSlotIndex, const int32 TargetSlotIndex)
{
	// 随身库存整理流程：
	// 1. 先在服务器侧重读玩法 gate 和 RequestId，避免无效局状态继续修改背包。
	// 2. 再解析当前玩家的正式 InventoryComponent；不存在正式库存时只返回依赖错误，不在 Controller 里补规则。
	// 3. 通过后把槽位和 Revision 交给 InventoryComponent，移动/合并/交换由正式库存提交。
	// 4. 提交成功后通知 Equipment 刷新旧库存投影；这个投影只服务钓鱼选择、存档和迁移期旧消费者。
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	UWorld* World = GetWorld();
	const ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	if (!GameMode || !GameMode->CanAcceptGameplayCommand(RequestingController))
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=move_inventory_slot_rejected Reason=CommandsClosedOrInactive Request=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	}
	else if (!RequestId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=move_inventory_slot_rejected Reason=InvalidRequest Request=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	}
	else
	{
		UCatInventoryComponent* Inventory = ControlledCharacter ? ControlledCharacter->GetInventoryComponent() : nullptr;
		if (!ControlledCharacter || ControlledCharacter->GetWorld() != World || !Inventory)
		{
			Result.Error = ECatDomainCommandError::DependencyUnavailable;
			UE_LOG(LogCatfishing, Warning,
				TEXT("Event=move_inventory_slot_rejected Reason=NoInventoryComponent Request=%s"),
				*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
		}
		else
		{
			Result = Inventory->MoveInventorySlotFromAuthority(RequestId, ExpectedRevision,
				SourceSlotIndex, TargetSlotIndex);
			UCatEquipmentComponent* Equipment = ControlledCharacter->GetEquipmentComponent();
			if (Result.bCommitted && Equipment
				&& !Equipment->RefreshInventoryProjectionFromInventoryComponentFromAuthority())
			{
				UE_LOG(LogCatfishing, Warning,
					TEXT("Event=move_inventory_slot_projection_sync_failed Request=%s InventoryRevision=%lld Character=%s"),
					*RequestId.ToString(EGuidFormats::DigitsWithHyphens), Result.Revision,
					*GetNameSafe(ControlledCharacter));
			}
		}
	}
	UE_LOG(LogCatfishing, Log,
		TEXT("Event=move_inventory_slot Committed=%s Error=%s Revision=%lld Source=%d Target=%d"),
		Result.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Error),
		Result.Revision, SourceSlotIndex, TargetSlotIndex);
	return Result;
}

FCatDomainCommandResult UCatInventoryCommandCoordinator::UseInventoryItemFromSlot(
	AController* RequestingController, ACatCharacter* ControlledCharacter, const FGuid RequestId,
	const int64 ExpectedInventoryRevision, const int32 InventorySlotIndex)
{
	// 通用库存 Use 入口流程：UI 只提供 RequestId、库存版本和槽位；内部按服务器当前下游状态决定这件物品能不能使用。
	return UseInventoryItemFromSlotInternal(RequestingController, ControlledCharacter, RequestId,
		ExpectedInventoryRevision, InventorySlotIndex, false, 0);
}

FCatDomainCommandResult UCatInventoryCommandCoordinator::SelectFishingItemFromInventorySlot(
	AController* RequestingController, ACatCharacter* ControlledCharacter, const FGuid RequestId,
	const int64 ExpectedInventoryRevision, const int64 ExpectedEquipmentRevision,
	const int32 InventorySlotIndex)
{
	// 旧钓具选择入口流程：历史 RPC 仍携带 EquipmentRevision，本层保留这份严格校验后复用通用库存 Use 实现。
	return UseInventoryItemFromSlotInternal(RequestingController, ControlledCharacter, RequestId,
		ExpectedInventoryRevision, InventorySlotIndex, true, ExpectedEquipmentRevision);
}

FCatDomainCommandResult UCatInventoryCommandCoordinator::UseInventoryItemFromSlotInternal(
	AController* RequestingController, ACatCharacter* ControlledCharacter, const FGuid RequestId,
	const int64 ExpectedInventoryRevision, const int32 InventorySlotIndex,
	const bool bRequireEquipmentRevision, const int64 ExpectedEquipmentRevision)
{
	// 随身库存物品使用流程：
	// 1. 先在服务器侧重读玩法 gate 和 RequestId，避免客户端旧 UI 事件在不可提交阶段改选择。
	// 2. 再用 InventoryRevision 和槽位下标回到正式 InventoryComponent，定义、实例 ID 和物品类别都不信任客户端拼装。
	// 3. 当前已接入的可使用库存物品是钓具；这里按正式库存实例重建选择，并基于服务器 Equipment 快照补齐其他槽。
	// 4. 新版入口不把 EquipmentRevision 暴露给 UI；只有旧 RPC 明确携带版本时才额外拒绝并发冲突。
	// 5. 最后把服务端重建出的完整选择交给 EquipmentComponent；库存事实仍留在 InventoryComponent，Equipment 只保存钓鱼选择和运行态。
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	FName SelectedDefinitionId = NAME_None;
	FGuid SelectedItemInstanceId;
	ECatEquipmentKind SelectedKind = ECatEquipmentKind::Unknown;
	int64 ObservedInventoryRevision = 0;
	int64 ObservedEquipmentRevision = 0;

	UWorld* World = GetWorld();
	const ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	if (!GameMode || !GameMode->CanAcceptGameplayCommand(RequestingController))
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=use_inventory_item_rejected Reason=CommandsClosedOrInactive Request=%s Slot=%d"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens), InventorySlotIndex);
	}
	else if (!RequestId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=use_inventory_item_rejected Reason=InvalidRequest Request=%s Slot=%d"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens), InventorySlotIndex);
	}
	else
	{
		UCatInventoryComponent* Inventory = ControlledCharacter ? ControlledCharacter->GetInventoryComponent() : nullptr;
		UCatEquipmentComponent* Equipment = ControlledCharacter ? ControlledCharacter->GetEquipmentComponent() : nullptr;
		if (!ControlledCharacter || ControlledCharacter->GetWorld() != World || !Inventory || !Equipment)
		{
			Result.Error = ECatDomainCommandError::DependencyUnavailable;
			UE_LOG(LogCatfishing, Warning,
				TEXT("Event=use_inventory_item_rejected Reason=MissingInventoryOrEquipment Request=%s Slot=%d Character=%s"),
				*RequestId.ToString(EGuidFormats::DigitsWithHyphens), InventorySlotIndex,
				*GetNameSafe(ControlledCharacter));
		}
		else
		{
			ObservedInventoryRevision = Inventory->GetInventoryRevision();
			ObservedEquipmentRevision = Equipment->GetSnapshot().Revision;
			if (ObservedInventoryRevision != ExpectedInventoryRevision)
			{
				Result.Error = ECatDomainCommandError::RevisionConflict;
				Result.Revision = ObservedInventoryRevision;
				UE_LOG(LogCatfishing, Warning,
					TEXT("Event=use_inventory_item_rejected Reason=InventoryRevisionConflict Request=%s Slot=%d ExpectedInventoryRevision=%lld InventoryRevision=%lld"),
					*RequestId.ToString(EGuidFormats::DigitsWithHyphens), InventorySlotIndex,
					ExpectedInventoryRevision, ObservedInventoryRevision);
			}
			else if (bRequireEquipmentRevision && ObservedEquipmentRevision != ExpectedEquipmentRevision)
			{
				Result.Error = ECatDomainCommandError::RevisionConflict;
				Result.Revision = ObservedEquipmentRevision;
				UE_LOG(LogCatfishing, Warning,
					TEXT("Event=use_inventory_item_rejected Reason=EquipmentRevisionConflict Request=%s Slot=%d ExpectedEquipmentRevision=%lld EquipmentRevision=%lld"),
					*RequestId.ToString(EGuidFormats::DigitsWithHyphens), InventorySlotIndex,
					ExpectedEquipmentRevision, ObservedEquipmentRevision);
			}
			else
			{
				const FCatInventoryEntry* Entry = Inventory->GetInventoryEntryAtSlot(InventorySlotIndex);
				const UCatInventoryItemInstance* Instance = Entry != nullptr ? Entry->Instance.Get() : nullptr;
				const UCatEquipmentDefinition* Definition = Instance != nullptr
					? Cast<UCatEquipmentDefinition>(Instance->GetItemDefinition()) : nullptr;
				if (Entry == nullptr || Instance == nullptr || Entry->StackCount <= 0
					|| !Instance->GetItemInstanceId().IsValid())
				{
					Result.Error = ECatDomainCommandError::NotFound;
					Result.Revision = ObservedInventoryRevision;
				}
				else if (Definition == nullptr || !Definition->IsRuntimeDefinitionReady())
				{
					Result.Error = ECatDomainCommandError::InvalidPayload;
					Result.Revision = ObservedInventoryRevision;
				}
				else
				{
					SelectedDefinitionId = Definition->EquipmentDefinitionId;
					SelectedItemInstanceId = Instance->GetItemInstanceId();
					SelectedKind = Definition->Kind;

					const FCatEquipmentLoadoutSnapshot& Snapshot = Equipment->GetSnapshot();
					FName RodDefinitionId = Snapshot.RodDefinitionId;
					FName BaitDefinitionId = Snapshot.BaitDefinitionId;
					FName FloatDefinitionId = Snapshot.FloatDefinitionId;
					FName ScoopNetDefinitionId = Snapshot.ScoopNetDefinitionId;
					FGuid RodItemInstanceId = Snapshot.RodItemInstanceId;
					FGuid BaitItemInstanceId = Snapshot.BaitItemInstanceId;
					FGuid FloatItemInstanceId = Snapshot.FloatItemInstanceId;
					FGuid ScoopNetItemInstanceId = Snapshot.ScoopNetItemInstanceId;
					bool bSelectedKindSupported = true;

					switch (SelectedKind)
					{
					case ECatEquipmentKind::Rod:
						RodDefinitionId = SelectedDefinitionId;
						RodItemInstanceId = SelectedItemInstanceId;
						break;
					case ECatEquipmentKind::Bait:
						BaitDefinitionId = SelectedDefinitionId;
						BaitItemInstanceId = SelectedItemInstanceId;
						break;
					case ECatEquipmentKind::Float:
						FloatDefinitionId = SelectedDefinitionId;
						FloatItemInstanceId = SelectedItemInstanceId;
						break;
					case ECatEquipmentKind::ScoopNet:
						ScoopNetDefinitionId = SelectedDefinitionId;
						ScoopNetItemInstanceId = SelectedItemInstanceId;
						break;
					default:
						bSelectedKindSupported = false;
						break;
					}

					if (!bSelectedKindSupported)
					{
						// 非钓具条目不能进入 Equipment 选择；保留库存版本作为拒绝证据，方便 UI 重读被点击格。
						Result.Error = ECatDomainCommandError::InvalidPayload;
						Result.Revision = ObservedInventoryRevision;
					}
					else if (RodDefinitionId.IsNone() || BaitDefinitionId.IsNone() || FloatDefinitionId.IsNone())
					{
						Result.Error = ECatDomainCommandError::InvalidPayload;
						Result.Revision = ObservedEquipmentRevision;
					}
					else
					{
						const int64 EffectiveExpectedEquipmentRevision = bRequireEquipmentRevision
							? ExpectedEquipmentRevision : ObservedEquipmentRevision;
						Result = Equipment->ConfigureLoadoutFromAuthority(RequestId, EffectiveExpectedEquipmentRevision,
							RodDefinitionId, BaitDefinitionId, FloatDefinitionId, ScoopNetDefinitionId, NAME_None,
							RodItemInstanceId, BaitItemInstanceId, FloatItemInstanceId, ScoopNetItemInstanceId);
						ObservedEquipmentRevision = Equipment->GetSnapshot().Revision;
					}
				}
			}
		}
	}

	UE_LOG(LogCatfishing, Log,
		TEXT("Event=use_inventory_item Committed=%s Error=%s ResultRevision=%lld Slot=%d Definition=%s Item=%s Kind=%s ExpectedInventoryRevision=%lld InventoryRevision=%lld ExpectedEquipmentRevision=%lld EquipmentRevision=%lld StrictEquipmentRevision=%s"),
		Result.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Error),
		Result.Revision, InventorySlotIndex, *SelectedDefinitionId.ToString(),
		*SelectedItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*UEnum::GetValueAsString(SelectedKind), ExpectedInventoryRevision, ObservedInventoryRevision,
		ExpectedEquipmentRevision, ObservedEquipmentRevision, bRequireEquipmentRevision ? TEXT("true") : TEXT("false"));
	return Result;
}
