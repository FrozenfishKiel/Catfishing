#include "Environment/CatChumPlacementService.h"

#include "Character/CatCharacter.h"
#include "Condition/CatConditionComponent.h"
#include "Engine/World.h"
#include "Environment/CatChumFieldSettings.h"
#include "Environment/CatChumFieldSubsystem.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Logging/CatLog.h"

namespace CatChumPlacementServicePrivate
{
	// 错误结果构造流程：只把请求 ID 和打窝错误码写入返回值，不触碰库存、窝点状态或幂等缓存。
	static FCatPlaceChumResult MakeError(const FGuid RequestId, const ECatChumFieldError Error)
	{
		FCatPlaceChumResult Result;
		Result.RequestId = RequestId;
		Result.Error = Error;
		return Result;
	}

	// 水域错误映射流程：保留 StaleGeometry 的可区分诊断，其余水域查询失败统一收敛成非法水目标，不产生任何库存或窝点副作用。
	static ECatChumFieldError MapWaterError(const ECatWaterQueryError Error)
	{
		return Error == ECatWaterQueryError::StaleGeometry
			? ECatChumFieldError::StaleGeometry : ECatChumFieldError::InvalidWaterTarget;
	}

}

FCatPlaceChumResult UCatChumPlacementService::PlaceChum(APlayerController* RequestingController,
	const FCatPlaceChumCommand& Command)
{
	// 打窝服务流程：
	// 1. 先验证服务器、玩家身份、命令幂等和玩法 gate，再用 ChumFieldSubsystem 重放首次终态。
	// 2. 玩家窝料事实只从正式库存按实例 ID 读取，并用正式库存版本裁决并发；没有正式库存组件时不再回退 Equipment Snapshot。
	// 3. 水域、距离和视线通过后先准备窝点，再直接提交正式库存扣量；若旧 Equipment 投影存在，它只跟随正式库存刷新给旧 UI/存档读取。
	// 4. 库存提交成功后才激活并复制窝点，保证世界影响不会脱离真实物品消耗单独成立。
	using namespace CatChumPlacementServicePrivate;
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !RequestingController
		|| !RequestingController->HasAuthority())
	{
		return MakeError(Command.RequestId, ECatChumFieldError::DependencyUnavailable);
	}
	const APlayerState* PlayerState = RequestingController->PlayerState;
	const FString StableNetId = PlayerState && PlayerState->GetUniqueId().IsValid()
		? PlayerState->GetUniqueId()->ToString() : FString();
	if (StableNetId.IsEmpty())
	{
		return MakeError(Command.RequestId, ECatChumFieldError::InvalidIdentity);
	}
	if (!Command.RequestId.IsValid())
	{
		return MakeError(Command.RequestId, ECatChumFieldError::InvalidPayload);
	}
	UCatChumFieldSubsystem* Fields = World->GetSubsystem<UCatChumFieldSubsystem>();
	if (!Fields)
	{
		return MakeError(Command.RequestId, ECatChumFieldError::DependencyUnavailable);
	}
	FCatPlaceChumResult Replay;
	if (Fields->TryGetTerminalResult(StableNetId, Command.RequestId, Replay))
	{
		return Replay;
	}
	auto FinalizeFirstResult = [Fields, &StableNetId](const FCatPlaceChumResult& Candidate)
	{
		Fields->StoreTerminalResult(StableNetId, Candidate);
		FCatPlaceChumResult Frozen;
		return Fields->TryGetTerminalResult(StableNetId, Candidate.RequestId, Frozen) ? Frozen : Candidate;
	};
	const ACatfishingGameModeBase* GameMode = World->GetAuthGameMode<ACatfishingGameModeBase>();
	if (!GameMode || !GameMode->CanAcceptFishingCommand(RequestingController))
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, ECatChumFieldError::CommandsClosed));
	}
	const UCatChumFieldSettings* Settings = GetDefault<UCatChumFieldSettings>();
	if (!Settings->IsRuntimeReady())
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, ECatChumFieldError::FeatureDisabled));
	}
	if (!Command.ExpectedWaterRegionHandle.IsValid() || !Command.ChumItemInstanceId.IsValid()
		|| Command.Quantity <= 0 || !FMath::IsFinite(Command.ClientCandidateWorldPoint.X)
		|| !FMath::IsFinite(Command.ClientCandidateWorldPoint.Y)
		|| !FMath::IsFinite(Command.ClientCandidateWorldPoint.Z))
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, ECatChumFieldError::InvalidPayload));
	}
	ACatCharacter* Character = Cast<ACatCharacter>(RequestingController->GetPawn());
	const UCatConditionComponent* Conditions = Character ? Character->GetConditionComponent() : nullptr;
	UCatEquipmentComponent* LegacyProjectionEquipment = Character ? Character->GetEquipmentComponent() : nullptr;
	UCatInventoryComponent* OwnerInventory = Character ? Character->GetInventoryComponent() : nullptr;
	if (!OwnerInventory)
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, ECatChumFieldError::DependencyUnavailable));
	}
	FName ChumDefinitionId = NAME_None;
	UCatEquipmentDefinition* Definition = nullptr;
	int32 FormalChumSlotIndex = INDEX_NONE;
	// 正式库存复核：
	// 1. 服务层不信任命令里的 DefinitionId，而是按实例 ID 回到当前正式库存槽位。
	// 2. 再直接读取库存实例和定义资产，确认它仍是一份运行就绪、可由库存组件扣量的 Chum。
	// 3. 这里不再构造 FCatRunInventorySlot，也不再调用 UCatEquipmentDefinition::Use 做旧定义裁决；窝点由环境服务裁决，库存变化由 InventoryComponent 执行。
	FormalChumSlotIndex = OwnerInventory->FindInventorySlotIndexFromInstanceId(Command.ChumItemInstanceId);
	const FCatInventoryEntry* FormalChumEntry =
		OwnerInventory->GetInventoryEntryAtSlot(FormalChumSlotIndex);
	const UCatInventoryItemInstance* FormalChumInstance =
		FormalChumEntry != nullptr ? FormalChumEntry->Instance.Get() : nullptr;
	Definition = FormalChumInstance != nullptr
		? Cast<UCatEquipmentDefinition>(FormalChumInstance->GetItemDefinition()) : nullptr;
	if (FormalChumInstance != nullptr
		&& FormalChumEntry->StackCount >= Command.Quantity
		&& FormalChumInstance->GetItemInstanceId() == Command.ChumItemInstanceId
		&& Definition != nullptr
		&& Definition->IsRuntimeDefinitionReady()
		&& Definition->Kind == ECatEquipmentKind::Chum
		&& Definition->ConsumesInventoryQuantityOnUse())
	{
		ChumDefinitionId = FormalChumInstance->GetItemDefinitionId();
	}
	if (ChumDefinitionId.IsNone())
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, ECatChumFieldError::EquipmentUnavailable));
	}
	if (!Character || !Conditions || Conditions->GetSnapshot().bDowned || !Definition
		|| Definition->Kind != ECatEquipmentKind::Chum
		|| !Definition->IsRuntimeDefinitionReady()
		|| !Definition->ConsumesInventoryQuantityOnUse()
		|| Command.Quantity > Definition->ChumInfluence.MaximumQuantityPerPlacement)
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, ECatChumFieldError::DefinitionUnavailable));
	}
	if (!Command.ChumDefinitionId.IsNone() && Command.ChumDefinitionId != ChumDefinitionId)
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, ECatChumFieldError::InvalidPayload));
	}
	// 迁移期允许新旧版本字段同时到达，但两者必须指向同一份正式库存快照；冲突时拒绝请求，避免服务端猜测该相信哪个 UI 状态。
	if (Command.ExpectedInventoryRevision != 0 && Command.ExpectedEquipmentRevision != 0
		&& Command.ExpectedInventoryRevision != Command.ExpectedEquipmentRevision)
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, ECatChumFieldError::InvalidPayload));
	}
	// 正式库存版本裁决：新命令读 ExpectedInventoryRevision，旧蓝图只填历史字段时才兼容回退，Equipment Snapshot 不再参与数量并发判断。
	const int64 ExpectedInventoryRevision = Command.GetExpectedInventoryRevision();
	if (OwnerInventory->GetInventoryRevision() != ExpectedInventoryRevision)
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, ECatChumFieldError::EquipmentRevisionConflict));
	}
	UCatWaterQuerySubsystem* WaterQuery = World->GetSubsystem<UCatWaterQuerySubsystem>();
	if (!WaterQuery)
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, ECatChumFieldError::DependencyUnavailable));
	}
	const FCatWaterSpatialResult Water = WaterQuery->ResolveCandidatePointToWater(
		Command.ClientCandidateWorldPoint, Command.ExpectedWaterRegionHandle);
	if (!Water.bSucceeded || Water.Containment == ECatWaterContainment::Outside)
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, MapWaterError(Water.Error)));
	}
	const FVector ViewOrigin = Character->GetPawnViewLocation();
	const FVector ToTarget = Water.WaterSurfaceWorldPoint - ViewOrigin;
	const double Distance = ToTarget.Length();
	const FVector ViewDirection = RequestingController->GetControlRotation().Vector();
	// 夹角限制用策划配置的最大偏离角转成点积阈值，服务器据此拒绝视线明显偏离落点的投放。
	const double MinimumAimDot = FMath::Cos(FMath::DegreesToRadians(Settings->MaxAimDeviationDegrees));
	if (!FMath::IsFinite(Distance) || Distance > Settings->MaxPlacementRangeCentimeters
		|| ToTarget.IsNearlyZero() || FVector::DotProduct(ViewDirection, ToTarget.GetSafeNormal()) < MinimumAimDot)
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, ECatChumFieldError::PlacementOutOfRange));
	}
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(CatPlaceChumLineOfSight), true);
	TraceParams.AddIgnoredActor(Character);
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, ViewOrigin, Water.WaterSurfaceWorldPoint,
		Settings->PlacementLineOfSightChannel, TraceParams))
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, ECatChumFieldError::PlacementOccluded));
	}
	FCatPlaceChumCommand AuthoritativeCommand = Command;
	AuthoritativeCommand.ChumDefinitionId = ChumDefinitionId;
	FCatPrepareChumFieldRequest PrepareRequest;
	PrepareRequest.StableNetId = StableNetId;
	PrepareRequest.Command = AuthoritativeCommand;
	PrepareRequest.ServerCorrectedCenter = Water.WaterSurfaceWorldPoint;
	PrepareRequest.Influence = Definition->ChumInfluence;
	PrepareRequest.ServerTime = World->GetTimeSeconds();
	const FCatPrepareChumFieldResult Prepared = Fields->PrepareField(PrepareRequest);
	if (!Prepared.bPrepared)
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, Prepared.Error));
	}
	// 正式库存提交流程：先保存可恢复的库存快照，再扣除本次窝料数量；正式库存是事实写口，旧 Equipment 投影只是兼容读模型。
	// 扣量失败只撤销窝点；扣量后若旧投影刷新失败，还要恢复库存并撤销窝点准备，避免世界影响、正式库存和旧读模型看到三种事务结果。
	const TArray<FCatInventoryEntry> SavedEntries = OwnerInventory->GetInventoryEntries();
	if (!OwnerInventory->ConsumeItemAtSlot(FormalChumSlotIndex, Command.Quantity))
	{
		Fields->AbortPreparedField(Prepared.CommitToken);
		return FinalizeFirstResult(MakeError(Command.RequestId, ECatChumFieldError::DependencyUnavailable));
	}
	if (LegacyProjectionEquipment != nullptr
		&& !LegacyProjectionEquipment->RefreshInventoryProjectionFromInventoryComponentFromAuthority())
	{
		OwnerInventory->ReplaceInventoryEntriesFromAuthority(SavedEntries, SavedEntries.Num());
		Fields->AbortPreparedField(Prepared.CommitToken);
		return FinalizeFirstResult(MakeError(Command.RequestId, ECatChumFieldError::DependencyUnavailable));
	}
	UE_LOG(LogCatEnvironment, Log,
		TEXT("Event=chum_inventory_consumed RequestId=%s Definition=%s ItemInstance=%s Quantity=%d InventoryRevision=%lld"),
		*Command.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower), *ChumDefinitionId.ToString(),
		*Command.ChumItemInstanceId.ToString(EGuidFormats::DigitsWithHyphensLower), Command.Quantity,
		OwnerInventory->GetInventoryRevision());
	const FCatPlaceChumResult Activated = Fields->ActivatePreparedFieldDeferred(
		Prepared.CommitToken, OwnerInventory->GetInventoryRevision());
	if (!Activated.bCommitted)
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, Activated.Error));
	}
	const FCatPlaceChumResult Frozen = FinalizeFirstResult(Activated);
	Fields->PublishActivatedField(Frozen.FieldId);
	return Frozen;
}
