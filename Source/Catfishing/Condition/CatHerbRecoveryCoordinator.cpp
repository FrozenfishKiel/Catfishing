#include "Condition/CatHerbRecoveryCoordinator.h"

#include "Character/CatCharacter.h"
#include "Condition/CatConditionComponent.h"
#include "Condition/CatConditionSettings.h"
#include "Engine/World.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "GameFramework/Controller.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Logging/CatLog.h"

namespace CatHerbRecoveryCoordinatorPrivate
{
// 正式库存草药终态键只按 RequestId 分组；载荷差异交给签名检查，使网络重试和冲突请求能被明确区分。
FString MakeFormalHerbTerminalKey(const FGuid RequestId)
{
	return FString::Printf(TEXT("FormalHerbRecovery|%s"), *RequestId.ToString(EGuidFormats::DigitsWithHyphens));
}

// 正式库存草药载荷签名记录施药者、目标、库存版本和实例身份；同一个 RequestId 如果换目标、换药或换版本前提，会被视为非法重放。
FString MakeFormalHerbPayloadSignature(const AController* HelpingController, const ACatCharacter* TargetCharacter,
	const int64 ExpectedInventoryRevision, const FGuid HerbItemInstanceId)
{
	return FString::Printf(TEXT("Helper=%s|Target=%s|InventoryRevision=%lld|Herb=%s"),
		*GetPathNameSafe(HelpingController), *GetPathNameSafe(TargetCharacter), ExpectedInventoryRevision,
		*HerbItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens));
}
}

bool UCatHerbRecoveryCoordinator::ShouldCreateSubsystem(UObject* Outer) const
{
	// 创建条件流程：只在服务器 Game World 建立草药恢复协调器；客户端 UI 只能发起 RPC，不能自行改库存或身体。
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

FCatDomainCommandResult UCatHerbRecoveryCoordinator::UseHerbOnCharacter(AController* HelpingController,
	ACatCharacter* TargetCharacter, const FGuid RequestId, const int64 ExpectedInventoryRevision,
	const FGuid HerbItemInstanceId)
{
	// 草药协调流程：
	// 1. 先确认请求键、施药者当前 Pawn、双方组件、正式库存和目标 World；草药消耗没有正式库存时直接失败。
	// 2. 重放命中时直接返回首次库存和身体提交终态，避免网络重试再次扣草药或重新恢复目标。
	// 3. 不是重放时才检查玩法 gate、正式库存里的草药实例、施药者状态、范围和目标恢复预检，随后在库存组件扣草药并提交身体恢复。
	// 4. Equipment 只在正式扣药成功后刷新旧投影；它不再作为草药数量或幂等终态的备用库存权威。
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	UWorld* World = GetWorld();
	ACatCharacter* ControlledCharacter = HelpingController ? Cast<ACatCharacter>(HelpingController->GetPawn()) : nullptr;
	UCatEquipmentComponent* Equipment = ControlledCharacter ? ControlledCharacter->GetEquipmentComponent() : nullptr;
	UCatInventoryComponent* OwnerInventory = ControlledCharacter ? ControlledCharacter->GetInventoryComponent() : nullptr;
	UCatConditionComponent* SourceConditions = ControlledCharacter ? ControlledCharacter->GetConditionComponent() : nullptr;
	UCatConditionComponent* TargetConditions = TargetCharacter ? TargetCharacter->GetConditionComponent() : nullptr;
	if (!RequestId.IsValid() || !HerbItemInstanceId.IsValid() || !ControlledCharacter || !Equipment
		|| !SourceConditions || !TargetConditions || !TargetCharacter || TargetCharacter->GetWorld() != World)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	if (!OwnerInventory)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}

	FString FormalTerminalKey;
	FString FormalPayloadSignature;
	// 正式库存路径绕过 Equipment::Use，也就不能借它的幂等缓存；这里先查协调器缓存，确保重放不会重新进入扣药流程。
	FormalTerminalKey = CatHerbRecoveryCoordinatorPrivate::MakeFormalHerbTerminalKey(RequestId);
	FormalPayloadSignature = CatHerbRecoveryCoordinatorPrivate::MakeFormalHerbPayloadSignature(
		HelpingController, TargetCharacter, ExpectedInventoryRevision, HerbItemInstanceId);
	FCatDomainCommandResult CachedResult;
	const ECatTerminalReplayOutcome ReplayOutcome = CatQueryTerminalReplay(FormalHerbTerminalCache,
		FormalHerbPayloadByKey, FormalTerminalKey, FormalPayloadSignature, CachedResult,
		[](FCatDomainCommandResult& Cached)
		{
			MarkCommandReplayed(Cached);
		});
	if (ReplayOutcome == ECatTerminalReplayOutcome::Replayed)
	{
		return CachedResult;
	}
	if (ReplayOutcome == ECatTerminalReplayOutcome::PayloadMismatch)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		Result.Revision = OwnerInventory->GetInventoryRevision();
		return Result;
	}

	const auto StoreFormalTerminal = [&](const FCatDomainCommandResult& TerminalResult)
	{
		// 正式库存分支把失败和成功都缓存为同一个终态；后续同 RequestId 重试只回放结果，不再读取已经变化的库存格。
		FormalHerbTerminalCache.Add(FormalTerminalKey, TerminalResult);
		FormalHerbPayloadByKey.Add(FormalTerminalKey, FormalPayloadSignature);
		return TerminalResult;
	};
	const auto LogBodyFailure = [&](const int64 ItemRevision)
	{
		// 失败诊断读取正式库存版本；身体提交失败时不会再回退 Equipment 终态，只暴露这次已经扣掉或回滚后的库存事实。
		UE_LOG(LogCatCharacter, Error,
			TEXT("Event=herb_recovery_body_commit_failed RequestId=%s Helper=%s Target=%s ItemRevision=%lld BodyError=%s BodyReplay=%s BodyReplayError=%s BodyRevision=%lld"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			*GetNameSafe(ControlledCharacter), *GetNameSafe(TargetCharacter),
			ItemRevision,
			*UEnum::GetValueAsString(Result.Error),
			Result.bTerminalReplay ? TEXT("true") : TEXT("false"),
			*UEnum::GetValueAsString(Result.ReplayedTerminalError), Result.Revision);
	};

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
	int32 FormalHerbSlotIndex = INDEX_NONE;
	// 正式库存预检：
	// 1. 先按客户端提交的实例 ID 回到当前库存槽位，旧 Equipment 投影不能证明草药仍在身上。
	// 2. 再直接读取库存实例和定义资产，确认它仍是一份运行就绪、可由库存组件扣量的 Herb。
	// 3. 这里不再构造 FCatRunInventorySlot，也不再调用 UCatEquipmentDefinition::Use 做旧定义裁决；草药真实效果由 Condition 裁决，库存变化由 InventoryComponent 执行。
	FormalHerbSlotIndex = OwnerInventory->FindInventorySlotIndexFromInstanceId(HerbItemInstanceId);
	const FCatInventoryEntry* FormalHerbEntry =
		OwnerInventory->GetInventoryEntryAtSlot(FormalHerbSlotIndex);
	const UCatInventoryItemInstance* FormalHerbInstance =
		FormalHerbEntry != nullptr ? FormalHerbEntry->Instance.Get() : nullptr;
	Definition = FormalHerbInstance != nullptr
		? Cast<UCatEquipmentDefinition>(FormalHerbInstance->GetItemDefinition()) : nullptr;
	bHasCurrentHerb = FormalHerbEntry != nullptr
		&& FormalHerbEntry->StackCount > 0
		&& FormalHerbInstance != nullptr
		&& FormalHerbInstance->GetItemInstanceId() == HerbItemInstanceId
		&& Definition != nullptr
		&& Definition->IsRuntimeDefinitionReady()
		&& Definition->Kind == ECatEquipmentKind::Herb
		&& Definition->ConsumesInventoryQuantityOnUse();
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
	// 正式库存提交流程：
	// 1. 先复核客户端看到的 InventoryRevision，防止基于旧背包状态扣错草药。
	// 2. 草药实例和定义已经由正式库存 entry 证明，这里不再通过旧装备槽结构重复推导。
	// 3. 真正的数量变化只发生在 UCatInventoryComponent::ConsumeItemAtSlot，提交成功后才刷新 Equipment 的历史投影。
	// 4. 如果投影同步失败，立即把正式库存回滚到扣药前快照；身体恢复必须等库存和旧投影都收口后才提交。
	if (OwnerInventory->GetInventoryRevision() != ExpectedInventoryRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
		Result.Revision = OwnerInventory->GetInventoryRevision();
		return StoreFormalTerminal(Result);
	}

	const TArray<FCatInventoryEntry> SavedEntries = OwnerInventory->GetInventoryEntries();
	if (!OwnerInventory->ConsumeItemAtSlot(FormalHerbSlotIndex, 1))
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		Result.Revision = OwnerInventory->GetInventoryRevision();
		return StoreFormalTerminal(Result);
	}

	const int64 ConsumedInventoryRevision = OwnerInventory->GetInventoryRevision();
	if (!Equipment->RefreshInventoryProjectionFromInventoryComponentFromAuthority())
	{
		OwnerInventory->ReplaceInventoryEntriesFromAuthority(SavedEntries, SavedEntries.Num());
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		Result.Revision = OwnerInventory->GetInventoryRevision();
		return StoreFormalTerminal(Result);
	}

	UE_LOG(LogCatCharacter, Log,
		TEXT("Event=herb_inventory_consumed RequestId=%s Helper=%s Target=%s HerbItem=%s InventoryRevision=%lld"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		*GetNameSafe(ControlledCharacter), *GetNameSafe(TargetCharacter),
		*HerbItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), ConsumedInventoryRevision);

	Result = TargetConditions->ApplyCommittedHerbRecovery(HelpingController, RequestId);
	if (!CatIsAcceptedDomainCommandResult(Result))
	{
		LogBodyFailure(ConsumedInventoryRevision);
	}
	return StoreFormalTerminal(Result);
}
