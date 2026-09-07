#include "Equipment/CatEquipmentCommandCoordinator.h"

#include "Camp/CatCampHubActor.h"
#include "Character/CatCharacter.h"
#include "Engine/World.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "GameFramework/Controller.h"
#include "Inventory/CatInventoryComponent.h"
#include "Logging/CatLog.h"

bool UCatEquipmentCommandCoordinator::ShouldCreateSubsystem(UObject* Outer) const
{
	// 创建条件流程：只在服务器 Game World 建立装备命令协调器；客户端只通过复制快照和 owning-client 回执观察结果。
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

FCatDomainCommandResult UCatEquipmentCommandCoordinator::ConfigureLoadout(AController* RequestingController,
	ACatCharacter* ControlledCharacter, const FGuid RequestId, const int64 ExpectedRevision,
	const FName RodDefinitionId, const FName BaitDefinitionId, const FName FloatDefinitionId,
	const FName ScoopNetDefinitionId, const FGuid RodItemInstanceId, const FGuid BaitItemInstanceId,
	const FGuid FloatItemInstanceId, const FGuid ScoopNetItemInstanceId)
{
	// 装备选择提交流程：
	// 1. 先在服务器侧重读 GameMode gate 和 RequestId，拒绝关闭局或非法请求。
	// 2. 再从当前玩家 Pawn 解析 EquipmentComponent；Controller 传入的只是请求来源，不决定装备业务。
	// 3. 最后把定义、实例和 Revision 原样交给 Equipment 聚合裁决，并记录同一条诊断事件供打包日志回放。
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	UWorld* World = GetWorld();
	const ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	if (!GameMode || !GameMode->CanAcceptGameplayCommand(RequestingController))
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=configure_equipment_rejected Reason=CommandsClosedOrInactive Request=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	}
	else if (!RequestId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=configure_equipment_rejected Reason=InvalidRequest Request=%s"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	}
	else
	{
		UCatEquipmentComponent* Equipment = ControlledCharacter ? ControlledCharacter->GetEquipmentComponent() : nullptr;
		if (!ControlledCharacter || ControlledCharacter->GetWorld() != World || !Equipment)
		{
			Result.Error = ECatDomainCommandError::DependencyUnavailable;
			UE_LOG(LogCatfishing, Warning,
				TEXT("Event=configure_equipment_rejected Reason=NoEquipmentComponent Request=%s"),
				*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
		}
		else
		{
			Result = Equipment->ConfigureLoadoutFromAuthority(RequestId, ExpectedRevision,
				RodDefinitionId, BaitDefinitionId, FloatDefinitionId, ScoopNetDefinitionId, NAME_None,
				RodItemInstanceId, BaitItemInstanceId, FloatItemInstanceId, ScoopNetItemInstanceId);
		}
	}
	UE_LOG(LogCatfishing, Log,
		TEXT("Event=configure_equipment Committed=%s Error=%s Revision=%lld Rod=%s RodItem=%s Bait=%s BaitItem=%s Float=%s FloatItem=%s Net=%s NetItem=%s"),
		Result.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Error),
		Result.Revision, *RodDefinitionId.ToString(), *RodItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*BaitDefinitionId.ToString(), *BaitItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*FloatDefinitionId.ToString(), *FloatItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*ScoopNetDefinitionId.ToString(), *ScoopNetItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens));
	return Result;
}

FCatDomainCommandResult UCatEquipmentCommandCoordinator::MoveInventorySlot(AController* RequestingController,
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

FCatDomainCommandResult UCatEquipmentCommandCoordinator::RepairRodAtCamp(AController* RequestingController,
	ACatCharacter* ControlledCharacter, ACatCampHubActor* Camp, const FGuid RequestId,
	const int64 ExpectedEquipmentRevision)
{
	// 营地修竿流程：
	// 1. 先沿用服务器 GameMode gate；局状态关闭时不触碰装备组件。
	// 2. 再确认营地属于当前 World，并从当前玩家 Pawn 取得 EquipmentComponent。
	// 3. 最后让 Camp 判断玩家是否仍在营地范围内，由 Equipment 提交浮木消耗和鱼竿耐久恢复。
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	UWorld* World = GetWorld();
	const ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	if (!GameMode || !GameMode->CanAcceptGameplayCommand(RequestingController))
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		return Result;
	}
	UCatEquipmentComponent* Equipment = ControlledCharacter ? ControlledCharacter->GetEquipmentComponent() : nullptr;
	if (!Camp || Camp->GetWorld() != World || !ControlledCharacter || ControlledCharacter->GetWorld() != World
		|| !Equipment)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	return Equipment->RepairRodAtCamp(RequestId, ExpectedEquipmentRevision, Camp->IsControllerInCamp(RequestingController));
}
