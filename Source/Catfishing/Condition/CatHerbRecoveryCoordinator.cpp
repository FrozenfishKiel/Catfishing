#include "Condition/CatHerbRecoveryCoordinator.h"

#include "Character/CatCharacter.h"
#include "Condition/CatConditionComponent.h"
#include "Condition/CatConditionSettings.h"
#include "Engine/World.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Equipment/CatEquipmentTypes.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "GameFramework/Controller.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Logging/CatLog.h"

bool UCatHerbRecoveryCoordinator::ShouldCreateSubsystem(UObject* Outer) const
{
	// 创建条件流程：只在服务器 Game World 建立草药恢复协调器；客户端 UI 只能发起 RPC，不能自行改库存或身体。
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

FCatDomainCommandResult UCatHerbRecoveryCoordinator::UseHerbOnCharacter(AController* HelpingController,
	ACatCharacter* TargetCharacter, const FGuid RequestId, const int64 ExpectedEquipmentRevision,
	const FGuid HerbItemInstanceId)
{
	// 草药协调流程：
	// 1. 先确认请求键、施药者当前 Pawn、双方组件和目标 World，再只读查询 Equipment 终态重放。
	// 2. 重放命中时按首次库存成功或失败收口；成功重放继续补查身体终态，不被当前倒地、距离或背包格变化截断。
	// 3. 不是重放时才检查玩法 gate、正式库存里的草药实例、施药者状态、范围和目标恢复预检，随后扣草药并提交身体恢复。
	// 4. 没有正式库存组件的旧宿主才读取 Equipment 快照，避免迁移期测试夹具或临时 Actor 直接失效。
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	UWorld* World = GetWorld();
	ACatCharacter* ControlledCharacter = HelpingController ? Cast<ACatCharacter>(HelpingController->GetPawn()) : nullptr;
	UCatEquipmentComponent* Equipment = ControlledCharacter ? ControlledCharacter->GetEquipmentComponent() : nullptr;
	UCatConditionComponent* SourceConditions = ControlledCharacter ? ControlledCharacter->GetConditionComponent() : nullptr;
	UCatConditionComponent* TargetConditions = TargetCharacter ? TargetCharacter->GetConditionComponent() : nullptr;
	if (!RequestId.IsValid() || !HerbItemInstanceId.IsValid() || !ControlledCharacter || !Equipment
		|| !SourceConditions || !TargetConditions || !TargetCharacter || TargetCharacter->GetWorld() != World)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}

	const auto LogBodyFailure = [&](const FCatInventoryItemUseResult& AcceptedUseResult)
	{
		UE_LOG(LogCatCharacter, Error,
			TEXT("Event=herb_recovery_body_commit_failed RequestId=%s Helper=%s Target=%s EquipmentRevision=%lld BodyError=%s BodyReplay=%s BodyReplayError=%s BodyRevision=%lld"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			*GetNameSafe(ControlledCharacter), *GetNameSafe(TargetCharacter),
			AcceptedUseResult.EquipmentRevision,
			*UEnum::GetValueAsString(Result.Error),
			Result.bTerminalReplay ? TEXT("true") : TEXT("false"),
			*UEnum::GetValueAsString(Result.ReplayedTerminalError), Result.Revision);
	};
	FCatInventoryItemUseResult ReplayUseResult;
	if (Equipment->TryReplayInventoryItemUseTerminal(RequestId, ExpectedEquipmentRevision,
		HerbItemInstanceId, 1, ReplayUseResult))
	{
		if (!CatIsAcceptedInventoryItemUseResult(ReplayUseResult))
		{
			Result.Error = ReplayUseResult.bTerminalReplay
				? ReplayUseResult.ReplayedTerminalError : ReplayUseResult.Error;
			Result.Revision = ReplayUseResult.EquipmentRevision;
			return Result;
		}
		Result = TargetConditions->ApplyCommittedHerbRecovery(HelpingController, RequestId);
		if (!CatIsAcceptedDomainCommandResult(Result))
		{
			LogBodyFailure(ReplayUseResult);
		}
		return Result;
	}

	const ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	const UCatConditionSettings* ConditionSettings = GetDefault<UCatConditionSettings>();
	if (!GameMode || !GameMode->CanAcceptGameplayCommand(HelpingController))
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		return Result;
	}
	if (SourceConditions->GetSnapshot().bDowned || !ConditionSettings
		|| !FMath::IsFinite(ConditionSettings->HerbUseRangeCentimeters)
		|| ConditionSettings->HerbUseRangeCentimeters <= 0.0
		|| FVector::DistSquared(ControlledCharacter->GetActorLocation(), TargetCharacter->GetActorLocation())
			> FMath::Square(ConditionSettings->HerbUseRangeCentimeters))
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}

	UCatEquipmentDefinition* Definition = nullptr;
	bool bHasCurrentHerb = false;
	if (const UCatInventoryComponent* OwnerInventory = ControlledCharacter->GetInventoryComponent())
	{
		// 正式库存预检：救援命令只信任当前背包里的实例和正数量；旧 Equipment 投影不能证明草药仍在身上。
		const int32 FormalHerbSlotIndex =
			OwnerInventory->FindInventorySlotIndexFromInstanceId(HerbItemInstanceId);
		const FCatInventoryEntry* FormalHerbEntry =
			OwnerInventory->GetInventoryEntryAtSlot(FormalHerbSlotIndex);
		const UCatInventoryItemInstance* FormalHerbInstance =
			FormalHerbEntry != nullptr ? FormalHerbEntry->Instance : nullptr;
		Definition = FormalHerbInstance != nullptr
			? Cast<UCatEquipmentDefinition>(FormalHerbInstance->GetItemDefinition()) : nullptr;
		bHasCurrentHerb = FormalHerbEntry != nullptr
			&& FormalHerbEntry->StackCount > 0
			&& Definition != nullptr
			&& Definition->Kind == ECatEquipmentKind::Herb
			&& Definition->ConsumesInventoryQuantityOnUse();
	}
	else
	{
		const FCatRunInventorySlot* HerbSlot = nullptr;
		for (const FCatRunInventorySlot& Slot : Equipment->GetSnapshot().InventorySlots)
		{
			if (Slot.ItemInstanceId == HerbItemInstanceId)
			{
				HerbSlot = &Slot;
				break;
			}
		}
		Definition = HerbSlot
			? GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(HerbSlot->DefinitionId) : nullptr;
		bHasCurrentHerb = HerbSlot && HerbSlot->Quantity > 0
			&& Definition && Definition->Kind == ECatEquipmentKind::Herb
			&& Definition->ConsumesInventoryQuantityOnUse();
	}
	if (!bHasCurrentHerb)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	Result.Error = TargetConditions->ValidateHerbRecovery(HelpingController);
	if (Result.Error != ECatDomainCommandError::None)
	{
		return Result;
	}
	const FCatInventoryItemUseResult UseResult =
		Equipment->Use(RequestId, ExpectedEquipmentRevision, HerbItemInstanceId);
	if (!CatIsAcceptedInventoryItemUseResult(UseResult))
	{
		Result.Error = UseResult.bTerminalReplay ? UseResult.ReplayedTerminalError : UseResult.Error;
		Result.Revision = UseResult.EquipmentRevision;
		return Result;
	}
	Result = TargetConditions->ApplyCommittedHerbRecovery(HelpingController, RequestId);
	if (!CatIsAcceptedDomainCommandResult(Result))
	{
		LogBodyFailure(UseResult);
	}
	return Result;
}
