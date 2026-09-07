#include "Environment/CatChumPlacementService.h"

#include "Character/CatCharacter.h"
#include "Condition/CatConditionComponent.h"
#include "Engine/World.h"
#include "Environment/CatChumFieldSettings.h"
#include "Environment/CatChumFieldSubsystem.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemInstance.h"

namespace CatChumPlacementServicePrivate
{
	static FCatPlaceChumResult MakeError(const FGuid RequestId, const ECatChumFieldError Error)
	{
		FCatPlaceChumResult Result;
		Result.RequestId = RequestId;
		Result.Error = Error;
		return Result;
	}

	static ECatChumFieldError MapWaterError(const ECatWaterQueryError Error)
	{
		return Error == ECatWaterQueryError::StaleGeometry
			? ECatChumFieldError::StaleGeometry : ECatChumFieldError::InvalidWaterTarget;
	}

	static ECatChumFieldError MapEquipmentError(const ECatDomainCommandError Error)
	{
		return Error == ECatDomainCommandError::RevisionConflict
			? ECatChumFieldError::EquipmentRevisionConflict : ECatChumFieldError::EquipmentUnavailable;
	}

	static const FCatRunInventorySlot* FindLegacyChumUseSlot(const UCatEquipmentComponent& Equipment,
		const FGuid ChumItemInstanceId, const int32 Quantity)
	{
		// 旧窝料投影解析流程：没有正式库存组件的宿主才读取 Equipment Snapshot；正式角色必须回到 InventoryComponent。
		for (const FCatRunInventorySlot& Slot : Equipment.GetSnapshot().InventorySlots)
		{
			if (Slot.ItemInstanceId == ChumItemInstanceId && Slot.Quantity >= Quantity)
			{
				return &Slot;
			}
		}
		return nullptr;
	}
}

FCatPlaceChumResult UCatChumPlacementService::PlaceChum(APlayerController* RequestingController,
	const FCatPlaceChumCommand& Command)
{
	// 打窝服务流程：
	// 1. 先验证服务器、玩家身份、命令幂等和玩法 gate，再用 ChumFieldSubsystem 重放首次终态。
	// 2. 玩家窝料事实优先从正式库存按实例 ID 读取；没有正式库存组件的旧宿主才回退 Equipment Snapshot。
	// 3. 水域、距离和视线通过后先准备窝点，再提交库存扣量；扣量失败会撤销待提交窝点。
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
	UCatEquipmentComponent* Equipment = Character ? Character->GetEquipmentComponent() : nullptr;
	FName ChumDefinitionId = NAME_None;
	UCatEquipmentDefinition* Definition = nullptr;
	if (const UCatInventoryComponent* OwnerInventory = Character ? Character->GetInventoryComponent() : nullptr)
	{
		// 正式库存复核：服务层不信任命令里的 DefinitionId，而是用实例当前所在槽位覆盖窝料身份。
		const int32 FormalChumSlotIndex =
			OwnerInventory->FindInventorySlotIndexFromInstanceId(Command.ChumItemInstanceId);
		const FCatInventoryEntry* FormalChumEntry =
			OwnerInventory->GetInventoryEntryAtSlot(FormalChumSlotIndex);
		const UCatInventoryItemInstance* FormalChumInstance =
			FormalChumEntry != nullptr ? FormalChumEntry->Instance : nullptr;
		if (FormalChumInstance != nullptr
			&& FormalChumEntry->StackCount >= Command.Quantity)
		{
			ChumDefinitionId = FormalChumInstance->GetItemDefinitionId();
			Definition = Cast<UCatEquipmentDefinition>(FormalChumInstance->GetItemDefinition());
		}
	}
	else if (Equipment)
	{
		if (const FCatRunInventorySlot* LegacyChumSlot =
			FindLegacyChumUseSlot(*Equipment, Command.ChumItemInstanceId, Command.Quantity))
		{
			ChumDefinitionId = LegacyChumSlot->DefinitionId;
			Definition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(ChumDefinitionId);
		}
	}
	if (ChumDefinitionId.IsNone())
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, ECatChumFieldError::EquipmentUnavailable));
	}
	if (!Character || !Conditions || Conditions->GetSnapshot().bDowned || !Equipment || !Definition
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
	const FCatInventoryItemUseResult UsedChum =
		Equipment->Use(Command.RequestId, Command.ExpectedEquipmentRevision, Command.ChumItemInstanceId, Command.Quantity);
	if (!UsedChum.bCommitted)
	{
		Fields->AbortPreparedField(Prepared.CommitToken);
		return FinalizeFirstResult(MakeError(Command.RequestId, MapEquipmentError(UsedChum.Error)));
	}
	const FCatPlaceChumResult Activated = Fields->ActivatePreparedFieldDeferred(
		Prepared.CommitToken, UsedChum.EquipmentRevision);
	if (!Activated.bCommitted)
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, Activated.Error));
	}
	const FCatPlaceChumResult Frozen = FinalizeFirstResult(Activated);
	Fields->PublishActivatedField(Frozen.FieldId);
	return Frozen;
}
