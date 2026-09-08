#include "Inventory/CatInventoryCommandCoordinator.h"

#include "Character/CatCharacter.h"
#include "Engine/World.h"
#include "Equipment/CatEquipmentComponent.h"
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
	// 通用库存 Use 入口流程：UI 只提供 RequestId、库存版本和槽位；协调器不识别物品种类，具体能否使用交给库存实例声明。
	return UseInventoryItemFromSlotInternal(RequestingController, ControlledCharacter, RequestId,
		ExpectedInventoryRevision, InventorySlotIndex);
}

FCatDomainCommandResult UCatInventoryCommandCoordinator::SelectFishingItemFromInventorySlot(
	AController* RequestingController, ACatCharacter* ControlledCharacter, const FGuid RequestId,
	const int64 ExpectedInventoryRevision, const int64,
	const int32 InventorySlotIndex)
{
	// 旧 SelectFishingItem 兼容入口流程：历史 RPC 仍携带 EquipmentRevision，但库存 Use 只按正式库存版本复核槽位；装备效果使用服务器当前选择快照作为基线。
	return UseInventoryItemFromSlotInternal(RequestingController, ControlledCharacter, RequestId,
		ExpectedInventoryRevision, InventorySlotIndex);
}

FCatDomainCommandResult UCatInventoryCommandCoordinator::UseInventoryItemFromSlotInternal(
	AController* RequestingController, ACatCharacter* ControlledCharacter, const FGuid RequestId,
	const int64 ExpectedInventoryRevision, const int32 InventorySlotIndex)
{
	// 随身库存物品使用流程：
	// 1. 先在服务器侧重读玩法 gate 和 RequestId，避免客户端新旧 Use 事件在不可提交阶段使用物品。
	// 2. 再解析当前玩家的正式 InventoryComponent；协调器不读取物品定义或装备类别，避免重新长出 UI 层特判。
	// 3. 组装库存 Use 上下文，只把库存版本、槽位、Pawn 和请求身份交给 InventoryComponent。
	// 4. InventoryComponent 会重读槽位并调用物品实例；成功可能改 Equipment 快照，也可能由未来实例改库存数量或其他领域事实。
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;

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
		if (!ControlledCharacter || ControlledCharacter->GetWorld() != World || !Inventory)
		{
			Result.Error = ECatDomainCommandError::DependencyUnavailable;
			UE_LOG(LogCatfishing, Warning,
				TEXT("Event=use_inventory_item_rejected Reason=MissingInventory Request=%s Slot=%d Character=%s"),
				*RequestId.ToString(EGuidFormats::DigitsWithHyphens), InventorySlotIndex,
				*GetNameSafe(ControlledCharacter));
		}
		else
		{
			FCatInventoryItemUseContext UseContext;
			UseContext.RequestId = RequestId;
			UseContext.RequestingController = RequestingController;
			UseContext.UserPawn = ControlledCharacter;
			UseContext.SourceInventory = Inventory;
			UseContext.ExpectedInventoryRevision = ExpectedInventoryRevision;
			UseContext.InventorySlotIndex = InventorySlotIndex;
			Result = Inventory->UseItemAtSlotFromAuthority(UseContext);
		}
	}

	UE_LOG(LogCatfishing, Log,
		TEXT("Event=use_inventory_item Committed=%s Error=%s ResultRevision=%lld Slot=%d ExpectedInventoryRevision=%lld"),
		Result.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Error),
		Result.Revision, InventorySlotIndex, ExpectedInventoryRevision);
	return Result;
}
