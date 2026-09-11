#include "Inventory/CatInventoryStatics.h"

#include "Camp/CatCampSettings.h"
#include "Character/CatCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Inventory/CatInventoryAccessRules.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/CatInventorySettings.h"
#include "Logging/CatLog.h"

// 落点求解流程：只用物理根的局部包围盒检查占用，不把准星交互球算作实体；放置依次搜索正前方与左右各30度内的地面。
// 丢弃从视点、角色半径和物理盒尺寸求前方释放中心，沿途扫盒并检查终点占用，阻挡即拒绝；成功把盒中心换算为Actor变换。
// 放置同时检查坡度、相对脚底高差、视线、物体占用和四角支撑，全部通过才返回最终 Actor 变换；全过程不移动 Actor。
bool UCatInventoryStatics::FindWorldReleaseTransform(ACatCharacter* Character, AActor* ItemActor, const ECatInventoryWorldAction Action,
	const UCatInventorySettings& Settings, FTransform& OutTransform)
{
	if (!IsValid(Character) || !IsValid(ItemActor) || Character->GetWorld() != ItemActor->GetWorld()
		|| (Action != ECatInventoryWorldAction::Drop && Action != ECatInventoryWorldAction::Place)) return false;
	UWorld* World = Character->GetWorld();
	const UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(ItemActor->GetRootComponent());
	if (!Body) return false;
	const FBox Bounds = Body->CalcBounds(FTransform::Identity).GetBox();
	if (!Bounds.IsValid) return false;
	const FVector Scale = ItemActor->GetActorScale3D();
	const FVector Extent = Bounds.GetExtent() * Scale.GetAbs();
	const FVector CenterOffset = Bounds.GetCenter() * Scale;
	const FVector Forward = Character->GetActorForwardVector().GetSafeNormal2D();
	const FVector Eye = Character->GetPawnViewLocation();
	if (!World || Extent.ContainsNaN() || Extent.GetMin() <= 0.0 || Forward.IsNearlyZero()) return false;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(CatInventoryWorldRelease), false, Character);
	Query.AddIgnoredActor(ItemActor);
	const FCollisionShape Shape = FCollisionShape::MakeBox(Extent);
	const double FeetZ = Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	if (Action == ECatInventoryWorldAction::Drop)
	{
		const FQuat Rotation = Forward.Rotation().Quaternion();
		const FVector Center = Eye + Forward * (Character->GetCapsuleComponent()->GetScaledCapsuleRadius() + Extent.GetMax() + 10.0);
		FHitResult Hit;
		if (World->SweepSingleByChannel(Hit, Eye, Center, Rotation, ECC_WorldDynamic, Shape, Query)
			|| World->OverlapBlockingTestByChannel(Center, Rotation, ECC_WorldDynamic, Shape, Query)) return false;
		OutTransform = FTransform(Rotation, Center - Rotation.RotateVector(CenterOffset), Scale);
		return true;
	}
	const double Height = Settings.PlacementHeightDifferenceCentimeters;
	const double MinimumNormalZ = FMath::Cos(FMath::DegreesToRadians(Settings.PlacementSlopeDegrees));
	for (const double Angle : {0.0, -15.0, 15.0, -30.0, 30.0})
	{
		const FVector Direction = Forward.RotateAngleAxis(Angle, FVector::UpVector);
		for (const double Fraction : {2.0 / 3.0, 0.5, 5.0 / 6.0, 1.0, 1.0 / 3.0})
		{
			FVector Candidate = Eye + Direction * Settings.PlacementRangeCentimeters * Fraction;
			Candidate.Z = FeetZ;
			FHitResult Ground;
			if (!World->LineTraceSingleByChannel(Ground, Candidate + FVector(0, 0, Height + 2.0),
				Candidate - FVector(0, 0, Height + 2.0), ECC_WorldDynamic, Query)
				|| FMath::Abs(Ground.ImpactPoint.Z - FeetZ) > Height || Ground.ImpactNormal.Z < MinimumNormalZ) continue;
			FHitResult Sight;
			if (World->LineTraceSingleByChannel(Sight, Eye, Ground.ImpactPoint, ECC_Visibility, Query)
				&& FVector::DistSquared(Sight.ImpactPoint, Ground.ImpactPoint) > FMath::Square(3.0)) continue;
			const FQuat Rotation = FRotationMatrix::MakeFromZX(Ground.ImpactNormal, Forward).ToQuat();
			const FVector Center = Ground.ImpactPoint + Ground.ImpactNormal * (Extent.Z + 1.0);
			if (World->OverlapBlockingTestByChannel(Center, Rotation, ECC_WorldDynamic, Shape, Query)) continue;
			bool bSupported = true;
			for (const FVector2D Corner : {FVector2D(-1, -1), FVector2D(-1, 1), FVector2D(1, -1), FVector2D(1, 1)})
			{
				const FVector Support = Ground.ImpactPoint + Rotation.RotateVector(FVector(Corner.X * Extent.X * 0.9, Corner.Y * Extent.Y * 0.9, 0));
				FHitResult Foot;
				if (!World->LineTraceSingleByChannel(Foot, Support + FVector(0, 0, Height + 2.0),
					Support - FVector(0, 0, Height + 2.0), ECC_WorldDynamic, Query)
					|| Foot.ImpactNormal.Z < MinimumNormalZ
					|| FMath::Abs(FVector::DotProduct(Foot.ImpactPoint - Ground.ImpactPoint, Ground.ImpactNormal)) > 2.0)
				{
					bSupported = false;
					break;
				}
			}
			if (bSupported)
			{
				OutTransform = FTransform(Rotation, Center - Rotation.RotateVector(CenterOffset), Scale);
				return true;
			}
		}
	}
	return false;
}

namespace
{
	// Actor 间库存移动解析出的单端点；它把客户端提交的宿主 Actor 收敛为正式库存组件、可选装备读模型和槽位候选。
	struct FCatInventoryHostEndpoint
	{
		// 已通过 World 与触达校验的库存宿主 Actor；幂等载荷和日志用它区分来源端与目标端。
		AActor* Host = nullptr;

		// 宿主上被选中的正式库存组件；后续格子写入、变化通知和终态缓存都只进入这一个组件。
		UCatInventoryComponent* Inventory = nullptr;

		// 玩家自身宿主上的装备读模型组件；库存移动成功后用它刷新钓具选择投影，公共仓库端点保持为空。
		UCatEquipmentComponent* Equipment = nullptr;

		// 客户端指定的该端槽位下标；合法性和空满状态由库存组件在正式事务里裁决。
		int32 SlotIndex = INDEX_NONE;
	};

	// Actor 端点解析流程：
	// 1. 当前玩家宿主直接使用 Character 的正式随身库存，避免组件扫描选到展示或临时库存。
	// 2. 其他宿主必须仍在同一 World 且通过当前触达规则，再从 Actor 组件列表收集正式库存。
	// 3. 解析只产出 InventoryComponent 与可选 Equipment；槽位和内容交给库存组件正式裁决。
	bool ResolveInventoryHostEndpoint(UWorld* World, ACatCharacter* ControlledCharacter,
		AActor* SubmittedHost, const int32 SlotIndex,
		FCatInventoryHostEndpoint& OutEndpoint)
	{
		OutEndpoint = FCatInventoryHostEndpoint();
		if (World == nullptr || ControlledCharacter == nullptr || SubmittedHost == nullptr
			|| SubmittedHost->GetWorld() != World)
		{
			return false;
		}

		OutEndpoint.Host = SubmittedHost;

		OutEndpoint.SlotIndex = SlotIndex;
		if (SubmittedHost == ControlledCharacter)
		{
			OutEndpoint.Inventory = ControlledCharacter->GetInventoryComponent();
			OutEndpoint.Equipment = ControlledCharacter->GetEquipmentComponent();
			return OutEndpoint.Inventory != nullptr;
		}

		const UCatCampSettings* CampSettings = GetDefault<UCatCampSettings>();
		if (!CatInventoryAccessRules::IsHostReachable(SubmittedHost, ControlledCharacter, CampSettings))
		{
			return false;
		}

		TArray<UCatInventoryComponent*> InventoryComponents;
		UCatInventoryStatics::AppendInventoryComponentsFromActor(SubmittedHost, InventoryComponents);
		OutEndpoint.Inventory = InventoryComponents.Num() > 0 ? InventoryComponents[0] : nullptr;
		return OutEndpoint.Inventory != nullptr;
	}
}

// 候选库存收集流程：只读取目标 Actor 上的正式库存组件，再按统一收货优先级稳定排序。
void UCatInventoryStatics::CollectInventoryComponentsFromActor(const AActor* TargetActor,
	TArray<UCatInventoryComponent*>& OutInventoryComponents)
{
	OutInventoryComponents.Reset();

	if (TargetActor == nullptr)
	{
		return;
	}

	TargetActor->GetComponents<UCatInventoryComponent>(OutInventoryComponents);

	OutInventoryComponents.StableSort([](const UCatInventoryComponent& Left,
		const UCatInventoryComponent& Right)
	{
		return Left.GetUnifiedInventoryIntakePriority() > Right.GetUnifiedInventoryIntakePriority();
	});
}

// 外部收集流程：向调用方追加当前 Actor 拥有的正式库存组件，不清空调用方已有列表。
void UCatInventoryStatics::AppendInventoryComponentsFromActor(const AActor* TargetActor,
	TArray<UCatInventoryComponent*>& OutInventoryComponents)
{
	TArray<UCatInventoryComponent*> CollectedInventoryComponents;
	CollectInventoryComponentsFromActor(TargetActor, CollectedInventoryComponents);

	for (UCatInventoryComponent* InventoryComponent : CollectedInventoryComponents)
	{
		if (InventoryComponent != nullptr)
		{
			OutInventoryComponents.Add(InventoryComponent);
		}
	}
}

// Actor 预检流程：空批次直接成功；非空批次交给第一个允许统一收货且能完整接收的库存组件。
bool UCatInventoryStatics::CanActorFullyAcceptInventoryBatch(const AActor* TargetActor,
	const FCatInventoryReceiveBatch& ReceiveBatch)
{
	if (ReceiveBatch.IsEmpty())
	{
		return true;
	}

	TArray<UCatInventoryComponent*> InventoryComponents;
	CollectInventoryComponentsFromActor(TargetActor, InventoryComponents);

	for (const UCatInventoryComponent* InventoryComponent : InventoryComponents)
	{
		if (InventoryComponent != nullptr
			&& InventoryComponent->CanReceiveUnifiedInventoryIntake()
			&& InventoryComponent->CanFullyAcceptInventoryBatch(ReceiveBatch))
		{
			return true;
		}
	}

	return false;
}

// Actor 间库存移动流程：
// 1. 先重读 Character、World、RequestId 和两个宿主，客户端提交的 Actor 只作为候选。
// 2. 再把宿主解析成正式 InventoryComponent，普通仓库通过触达规则限制，玩家自己直接走随身库存。
// 3. 正式移动只调用 Source InventoryComponent；Source 持有幂等缓存和格子交换事实。
// 4. 当前玩家随身库存参与时刷新 Equipment 读模型，失败只记录诊断，不回滚已经提交的库存事实。
FCatDomainCommandResult UCatInventoryStatics::MoveItemBetweenInventoryHostsFromAuthority(
	ACatCharacter* ControlledCharacter, const FGuid RequestId, AActor* SourceInventoryHost,
	const int32 SourceSlotIndex, AActor* TargetInventoryHost,
	const int32 TargetSlotIndex)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	UWorld* World = ControlledCharacter != nullptr ? ControlledCharacter->GetWorld() : nullptr;
	if (!RequestId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (ControlledCharacter == nullptr || World == nullptr)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else
	{
		FCatInventoryHostEndpoint SourceEndpoint;
		FCatInventoryHostEndpoint TargetEndpoint;
		if (!ResolveInventoryHostEndpoint(World, ControlledCharacter, SourceInventoryHost,
				SourceSlotIndex, SourceEndpoint)
			|| !ResolveInventoryHostEndpoint(World, ControlledCharacter, TargetInventoryHost,
				TargetSlotIndex, TargetEndpoint))
		{
			Result.Error = ECatDomainCommandError::DependencyUnavailable;
			UE_LOG(LogCatfishing, Warning,
				TEXT("Event=move_inventory_item_between_hosts_rejected Reason=EndpointUnavailable Request=%s SourceHost=%s TargetHost=%s"),
				*RequestId.ToString(EGuidFormats::DigitsWithHyphens),
				*GetNameSafe(SourceInventoryHost), *GetNameSafe(TargetInventoryHost));
		}
		else
		{
			const FString PayloadContext = FString::Printf(
				TEXT("SourceHost=%s|TargetHost=%s"),
				*GetPathNameSafe(SourceEndpoint.Host),
				*GetPathNameSafe(TargetEndpoint.Host));
			Result = SourceEndpoint.Inventory->MoveItemToInventoryFromAuthority(RequestId,
				SourceEndpoint.SlotIndex,
				TargetEndpoint.Inventory, TargetEndpoint.SlotIndex,
				PayloadContext);

			TSet<UCatEquipmentComponent*> EquipmentComponents;
			if (SourceEndpoint.Equipment)
			{
				EquipmentComponents.Add(SourceEndpoint.Equipment);
			}
			if (TargetEndpoint.Equipment)
			{
				EquipmentComponents.Add(TargetEndpoint.Equipment);
			}
			for (UCatEquipmentComponent* Equipment : EquipmentComponents)
			{
				if (Equipment != nullptr && !Equipment->RefreshLoadoutFromInventoryComponentFromAuthority())
				{
					UE_LOG(LogCatfishing, Warning,
						TEXT("Event=move_inventory_item_loadout_refresh_failed Request=%s Character=%s"),
						*RequestId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(ControlledCharacter));
				}
			}
		}
	}

	UE_LOG(LogCatfishing, Log,
		TEXT("Event=move_inventory_item_between_hosts Committed=%s Error=%s SourceHost=%s SourceSlot=%d TargetHost=%s TargetSlot=%d"),
		Result.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Error),
		*GetNameSafe(SourceInventoryHost), SourceSlotIndex,
		*GetNameSafe(TargetInventoryHost), TargetSlotIndex);
	return Result;
}

// Actor 库存使用流程：
// 1. 先重读 Character、World、RequestId 和来源宿主，客户端提交的 Actor 只作为候选。
// 2. 再把宿主解析成正式 InventoryComponent；玩家自己直接用随身库存，其它世界库存必须通过触达规则。
// 3. 正式 Use 只调用 Source InventoryComponent，具体鱼、草药或装备效果由 ItemInstance 按当前槽位事实裁决。
// 4. 当前玩家随身库存参与时刷新 Equipment 读模型，失败只记录诊断，不回滚已经提交的库存事实。
FCatDomainCommandResult UCatInventoryStatics::UseItemFromInventoryHostFromAuthority(
	ACatCharacter* ControlledCharacter, const FGuid RequestId, AActor* SourceInventoryHost,
	const int32 SourceSlotIndex)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	UWorld* World = ControlledCharacter != nullptr ? ControlledCharacter->GetWorld() : nullptr;
	if (!RequestId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (ControlledCharacter == nullptr || World == nullptr)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else
	{
		FCatInventoryHostEndpoint SourceEndpoint;
		if (!ResolveInventoryHostEndpoint(World, ControlledCharacter, SourceInventoryHost,
				SourceSlotIndex, SourceEndpoint))
		{
			Result.Error = ECatDomainCommandError::DependencyUnavailable;
			UE_LOG(LogCatfishing, Warning,
				TEXT("Event=use_inventory_item_from_host_rejected Reason=EndpointUnavailable Request=%s SourceHost=%s"),
				*RequestId.ToString(EGuidFormats::DigitsWithHyphens),
				*GetNameSafe(SourceInventoryHost));
		}
		else
		{
			FCatInventoryItemUseContext UseContext;
			UseContext.RequestId = RequestId;
			UseContext.RequestingController = ControlledCharacter->GetController();
			UseContext.UserPawn = ControlledCharacter;
			UseContext.SourceInventory = SourceEndpoint.Inventory;

			UseContext.InventorySlotIndex = SourceEndpoint.SlotIndex;
			Result = SourceEndpoint.Inventory->UseItemAtSlotFromAuthority(UseContext);
			if (SourceEndpoint.Equipment
				&& !SourceEndpoint.Equipment->RefreshLoadoutFromInventoryComponentFromAuthority())
			{
				UE_LOG(LogCatfishing, Warning,
					TEXT("Event=use_inventory_item_loadout_refresh_failed Request=%s Character=%s"),
					*RequestId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(ControlledCharacter));
			}
		}
	}

	UE_LOG(LogCatfishing, Log,
		TEXT("Event=use_inventory_item_from_host Committed=%s Error=%s SourceHost=%s SourceSlot=%d"),
		Result.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Error),
		*GetNameSafe(SourceInventoryHost), SourceSlotIndex);
	return Result;
}

// 物品落地路由：复核来源宿主可触达后进入该库存的唯一落地命令，成功时刷新既有装备选择读模型，不调用装备使用。
FCatDomainCommandResult UCatInventoryStatics::ReleaseItemToWorldFromAuthority(ACatCharacter* ControlledCharacter,
	const FGuid RequestId, AActor* SourceInventoryHost, const int32 SourceSlotIndex, const FGuid ItemInstanceId,
	const int32 Quantity, const ECatInventoryWorldAction Action)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	FCatInventoryHostEndpoint Endpoint;
	if (!ControlledCharacter || !ControlledCharacter->HasAuthority()
		|| !ResolveInventoryHostEndpoint(ControlledCharacter->GetWorld(), ControlledCharacter, SourceInventoryHost, SourceSlotIndex, Endpoint))
	{
		Result.Error = ECatDomainCommandError::PermissionDenied;
		return Result;
	}
	Result = Endpoint.Inventory->ReleaseItemToWorldFromAuthority(ControlledCharacter, RequestId, SourceSlotIndex, ItemInstanceId, Quantity, Action);
	if (Result.bCommitted && Endpoint.Equipment) Endpoint.Equipment->RefreshLoadoutFromInventoryComponentFromAuthority();
	return Result;
}

// Actor 收货流程：先清空可选接收者输出，再按优先级找到完整可接收者；仅成功写入后返回该组件，失败不提供接收归属。
bool UCatInventoryStatics::TryAddInventoryBatchToActor(AActor* TargetActor,
	const FCatInventoryReceiveBatch& ReceiveBatch, UCatInventoryComponent** OutReceivingInventory)
{
	if (OutReceivingInventory) *OutReceivingInventory = nullptr;
	if (ReceiveBatch.IsEmpty())
	{
		return true;
	}

	TArray<UCatInventoryComponent*> InventoryComponents;
	CollectInventoryComponentsFromActor(TargetActor, InventoryComponents);

	for (UCatInventoryComponent* InventoryComponent : InventoryComponents)
	{
		if (InventoryComponent == nullptr
			|| !InventoryComponent->CanReceiveUnifiedInventoryIntake()
			|| !InventoryComponent->CanFullyAcceptInventoryBatch(ReceiveBatch))
		{
			continue;
		}

		const bool bAdded = InventoryComponent->TryAddInventoryBatch(ReceiveBatch);
		if (bAdded && OutReceivingInventory) *OutReceivingInventory = InventoryComponent;
		return bAdded;
	}

	return false;
}
