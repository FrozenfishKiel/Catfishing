#include "Environment/CatChumPlacementService.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Tags/CatStateTags.h"

#include "Equipment/Fragments/CatEquipmentFragment_Chum.h"

#include "Character/CatCharacter.h"
#include "Condition/CatConditionComponent.h"
#include "Engine/World.h"
#include "Environment/CatChumFieldSettings.h"
#include "Environment/CatChumFieldSubsystem.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentItemDefinition.h"
#include "Fishing/CatFishingService.h"
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

	// 水域错误映射流程：保留几何版本冲突的可区分诊断，其余水域查询失败统一收敛成非法水目标，不产生任何库存或窝点副作用。
	static ECatChumFieldError MapWaterError(const ECatWaterQueryError Error)
	{
		return Error == ECatWaterQueryError::StaleGeometry
			? ECatChumFieldError::StaleGeometry : ECatChumFieldError::InvalidWaterTarget;
	}

}

FCatPlaceChumResult UCatChumPlacementService::PlaceChum(APlayerController* RequestingController,
	const FCatPlaceChumCommand& Command, TFunctionRef<bool()> PayResource)
{
	// 打窝服务流程：
	// 1. 先验证服务器、玩家身份、命令幂等和玩法 gate，再用 ChumFieldSubsystem 重放首次终态。
	// 2. 玩家窝料事实只从正式库存按实例 ID 读取；没有正式库存组件时直接失败，支付由能力提供的同步回调负责。
	// 3. 水域、距离和视线通过后先准备窝点，再调用支付回调；支付失败中止准备，成功后继续激活并刷新钓具读模型。
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
	// 补窝受主动道具总闸门约束：正在搏斗的猫不能掏窝料（钓鱼规则 §2.1，边界见 §3.3）。
	// 闸门只拦这只猫自己；同场其他处于可用道具状态的玩家照常能为同一个窝补料，队友补窝这一半不受影响。
	UCatFishingService* Fishing = World->GetSubsystem<UCatFishingService>();
	if (Fishing && Fishing->IsActiveItemUseBlockedForController(RequestingController))
	{
		UE_LOG(LogCatEnvironment, Warning,
			TEXT("Event=place_chum_rejected Reason=ActiveFishingItemGate RequestId=%s StableNetId=%s"),
			*Command.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower), *StableNetId);
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
	const UCatAbilitySystemComponent* Conditions = Character ? Character->GetCatAbilitySystemComponent() : nullptr;
	UCatEquipmentComponent* Equipment = Character ? Character->GetEquipmentComponent() : nullptr;
	UCatInventoryComponent* OwnerInventory = Character ? Character->GetInventoryComponent() : nullptr;
	if (!OwnerInventory)
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, ECatChumFieldError::DependencyUnavailable));
	}
	int32  ChumItemId = 0;
	UCatEquipmentItemDefinition* Definition = nullptr;
	int32 FormalChumSlotIndex = INDEX_NONE;
	// 正式库存复核：
	// 1. 服务层不信任命令里的 ItemId，而是按实例 ID 回到当前正式库存槽位。
	// 2. 再直接读取库存实例和定义资产，确认它仍是一份运行就绪、由实例声明扣量的 Chum。
	// 3. 窝点由环境服务裁决，库存变化由 InventoryComponent 执行。
	FormalChumSlotIndex = OwnerInventory->FindInventorySlotIndexFromInstanceId(Command.ChumItemInstanceId);
	const FCatInventoryEntry* FormalChumEntry =
		OwnerInventory->GetInventoryEntryAtSlot(FormalChumSlotIndex);
	const UCatInventoryItemInstance* FormalChumInstance =
		FormalChumEntry != nullptr ? FormalChumEntry->Instance.Get() : nullptr;
	Definition = FormalChumInstance != nullptr
		? Cast<UCatEquipmentItemDefinition>(FormalChumInstance->GetItemDefinition()) : nullptr;
	if (FormalChumInstance != nullptr
		&& FormalChumEntry->StackCount >= Command.Quantity
		&& FormalChumInstance->GetItemInstanceId() == Command.ChumItemInstanceId
		&& Definition != nullptr
		&& Definition->IsRuntimeDefinitionReady()
		&& Definition->CanServeChumPlacement())
	{
		ChumItemId = FormalChumInstance->GetItemId();
	}
	if ((ChumItemId == 0))
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, ECatChumFieldError::EquipmentUnavailable));
	}
	if (!Character || !Conditions || Conditions->HasMatchingGameplayTag(CatStateTags::Downed) || !Definition
		|| !Definition->CanServeChumPlacement()
		|| !Definition->IsRuntimeDefinitionReady()
		|| FormalChumInstance == nullptr
		|| Command.Quantity > Definition->FindFragment<UCatEquipmentFragment_Chum>()->ChumInfluence.MaximumQuantityPerPlacement)
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, ECatChumFieldError::DefinitionUnavailable));
	}
	if (!(Command.ChumItemId == 0) && Command.ChumItemId != ChumItemId)
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
	AuthoritativeCommand.ChumItemId = ChumItemId;
	FCatPrepareChumFieldRequest PrepareRequest;
	PrepareRequest.StableNetId = StableNetId;
	PrepareRequest.Command = AuthoritativeCommand;
	PrepareRequest.ServerCorrectedCenter = Water.WaterSurfaceWorldPoint;
	PrepareRequest.Influence = Definition->FindFragment<UCatEquipmentFragment_Chum>()->ChumInfluence;
	PrepareRequest.ServerTime = World->GetTimeSeconds();
	const FCatPrepareChumFieldResult Prepared = Fields->PrepareField(PrepareRequest);
	if (!Prepared.bPrepared)
	{
		return FinalizeFirstResult(MakeError(Command.RequestId, Prepared.Error));
	}
	// 提交流程：所有水域与来源校验完成后同步调用能力支付；物品成本延迟库存通知，支付实现仍须保证不破坏待激活的窝点令牌。
	// 窝点激活并记录终态后再发布库存和环境变化；不恢复整份库存，也不把只读装配刷新当作付款失败。
	if (!PayResource())
	{
		Fields->AbortPreparedField(Prepared.CommitToken);
		return FinalizeFirstResult(MakeError(Command.RequestId, ECatChumFieldError::DependencyUnavailable));
	}
	UE_LOG(LogCatEnvironment, Log,
		TEXT("Event=chum_inventory_consumed RequestId=%s Definition=%s ItemInstance=%s Quantity=%d"),
		*Command.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower), *FString::FromInt(ChumItemId),
		*Command.ChumItemInstanceId.ToString(EGuidFormats::DigitsWithHyphensLower), Command.Quantity);
	const FCatPlaceChumResult Activated = Fields->ActivatePreparedFieldDeferred(
		Prepared.CommitToken);
	if (!Activated.bCommitted)
	{
		// 支付完成后不能用库存快照抹掉其他成本或回调；异常也必须公开真实扣量，让能力撤销和客户端读模型收敛。
		OwnerInventory->BroadcastInventoryChange(FormalChumSlotIndex);
		if (Equipment) Equipment->RefreshLoadoutFromInventoryComponentFromAuthority();
		UE_LOG(LogCatEnvironment, Error, TEXT("Event=chum_post_payment_activation_failed RequestId=%s ItemInstance=%s Result=ResourcePaidFieldUnavailable"),
			*Command.RequestId.ToString(), *Command.ChumItemInstanceId.ToString());
		return FinalizeFirstResult(MakeError(Command.RequestId, Activated.Error));
	}
	const FCatPlaceChumResult Frozen = FinalizeFirstResult(Activated);
	OwnerInventory->BroadcastInventoryChange(FormalChumSlotIndex);
	if (Equipment) Equipment->RefreshLoadoutFromInventoryComponentFromAuthority();
	Fields->PublishActivatedField(Frozen.FieldId);
	return Frozen;
}
