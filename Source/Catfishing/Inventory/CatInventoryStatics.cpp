#include "Inventory/CatInventoryStatics.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Tags/CatStateTags.h"
#include "PhysicsEngine/BodySetup.h"
#include "ShopEconomy/Trading/CatShopTradeController.h"
#include "ShopEconomy/CatFishBuyerActor.h"
#include "FishContainers/CatFishPickupSettings.h"
#include "FishContainers/CatFishGuardActor.h"
#include "Inventory/CatWorldDropProtectionComponent.h"
#include "Inventory/CatInventoryWorldItem.h"
#include "Components/SkeletalMeshComponent.h"

#include "Camp/CatCampSettings.h"
#include "Character/CatCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Environment/CatWaterTypes.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Inventory/CatInventoryAccessRules.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Condition/CatConditionComponent.h"
#include "Inventory/CatInventorySettings.h"
#include "Interaction/CatInteractable.h"
#include "Items/CatItem.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "Logging/CatLog.h"

// 落点求解流程：只用物理根的局部包围盒检查占用，不把准星交互球算作实体；放置依次搜索正前方与左右各30度内的地面。
// 丢弃从视点、角色半径和物理盒尺寸求前方释放中心；沿途扫盒并检查终点占用，阻挡即拒绝。
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
			|| World->OverlapBlockingTestByChannel(Center, Rotation, ECC_WorldDynamic, Shape, Query))
		{
			UE_LOG(LogCatfishing, Warning, TEXT("Event=inventory_drop_space_blocked Character=%s Blocker=%s Origin=%s Center=%s World=%s NetMode=%d Authority=%d"),
				*GetNameSafe(Character), *GetNameSafe(Hit.GetActor()), *Eye.ToString(), *Center.ToString(), *GetNameSafe(World), World->GetNetMode(), Character->HasAuthority());
			return false;
		}
		if (const UCatWaterQuerySubsystem* Water = World->GetSubsystem<UCatWaterQuerySubsystem>();
			Water && Water->DoesWorldDropSweepTouchWater(Center, Center, Extent.Size())) return false;
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
			const UCatWaterQuerySubsystem* Water = World->GetSubsystem<UCatWaterQuerySubsystem>();
			if (bSupported && (!Water || !Water->DoesWorldDropSweepTouchWater(Center, Center, Extent.Size())))
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
		if (World == nullptr || !IsValid(ControlledCharacter) || !IsValid(SubmittedHost)
			|| SubmittedHost->IsActorBeingDestroyed()
			|| (SubmittedHost != ControlledCharacter && Cast<ACatCharacter>(SubmittedHost))
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

// 翻天清理流程：
// 1. 只在服务器跑；先收齐要销毁的对象再统一 Destroy，避免在 TActorIterator 迭代期间改动世界 Actor 列表。
// 2. 落地物只清「还在等人捡」的那些（ACatItem::IsAwaitingPickup）——被捡走的载体是隐藏保管态，清了等于删背包。
// 3. 地上的鱼只清 Available 且没被隐藏的那些；嘴里叼着的是 Carried，进了鱼护/鱼缸的是隐藏保管态，两者都不清。
int32 UCatInventoryStatics::PurgeUnclaimedWorldDropsFromAuthority(UWorld* World)
{
	if (World == nullptr || World->GetNetMode() == NM_Client)
	{
		return 0;
	}
	TArray<AActor*> PendingDestroy;
	for (TActorIterator<ACatItem> It(World); It; ++It)
	{
		ACatItem* WorldItem = *It;
		if (IsValid(WorldItem) && WorldItem->HasAuthority() && WorldItem->IsAwaitingPickup())
		{
			PendingDestroy.Add(WorldItem);
		}
	}
	for (TActorIterator<ACatFishPickupActor> It(World); It; ++It)
	{
		ACatFishPickupActor* WorldFish = *It;
		if (IsValid(WorldFish) && WorldFish->HasAuthority() && !WorldFish->IsHidden()
			&& WorldFish->GetPresentationState().State == ECatFishPickupState::Available)
		{
			PendingDestroy.Add(WorldFish);
		}
	}
	int32 DestroyedCount = 0;
	for (AActor* Doomed : PendingDestroy)
	{
		if (IsValid(Doomed) && Doomed->Destroy())
		{
			++DestroyedCount;
			UE_LOG(LogCatfishing, Log, TEXT("Event=world_drop_purged Actor=%s World=%s NetMode=%d Authority=1 LocalRole=%d"),
				*GetNameSafe(Doomed), *GetNameSafe(World), World->GetNetMode(), Doomed->GetLocalRole());
		}
	}
	UE_LOG(LogCatfishing, Log,
		TEXT("Event=world_drops_purged_for_day_transition World=%s NetMode=%d Authority=1 Candidates=%d Destroyed=%d"),
		*GetNameSafe(World), World->GetNetMode(), PendingDestroy.Num(), DestroyedCount);
	return DestroyedCount;
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
			const auto InvalidFishEndpoint = [&](const FCatInventoryHostEndpoint& Endpoint)
			{
				const FCatInventoryEntry* Entry = Endpoint.Inventory->GetInventoryEntryAtSlot(Endpoint.SlotIndex);
				return Entry && Entry->Instance
					&& (Cast<UCatFishInventoryItemInstance>(Entry->Instance) || Cast<UCatFishDefinition>(Entry->Instance->GetItemDefinition()))
					&& CatInventoryAccessRules::ResolveReachableFishContainer(Endpoint.Host, ControlledCharacter) != Endpoint.Inventory;
			};
			if (!ControlledCharacter->HasAuthority() || !ControlledCharacter->GetConditionComponent()
				|| (ControlledCharacter->GetCatAbilitySystemComponent()->HasMatchingGameplayTag(CatStateTags::Downed)
					&& (SourceInventoryHost != ControlledCharacter || TargetInventoryHost != ControlledCharacter))
				|| InvalidFishEndpoint(SourceEndpoint) || InvalidFishEndpoint(TargetEndpoint))
			{
				Result.Error = ECatDomainCommandError::PermissionDenied;
				UE_LOG(LogCatfishing, Warning, TEXT("Event=inventory_host_move_rejected RequestId=%s Player=%s Source=%s Target=%s Reason=InvalidFishHostOrCharacter World=%s NetMode=%d Authority=%d LocalRole=%d"),
					*RequestId.ToString(), *GetNameSafe(ControlledCharacter), *GetNameSafe(SourceInventoryHost), *GetNameSafe(TargetInventoryHost),
					*GetNameSafe(World), ControlledCharacter->GetNetMode(), ControlledCharacter->HasAuthority(), ControlledCharacter->GetLocalRole());
				return Result;
			}
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

// 菜单路由只解析当前可访问的来源库存；操作内容与实例身份交给该库存唯一入口，成功后刷新既有装备投影。
FCatDomainCommandResult UCatInventoryStatics::ExecuteInventoryActionFromAuthority(ACatCharacter* Character,
	const FGuid RequestId, AActor* SourceHost, const int32 SourceSlot, const FGuid ItemInstanceId,
	const FGameplayTag Action, const int32 Quantity, const FCatInventoryUseTarget& Target)
{
	FCatDomainCommandResult Result; Result.RequestId = RequestId;
	FCatInventoryHostEndpoint Endpoint;
	if (!Character || !Character->HasAuthority() || !RequestId.IsValid()
		|| !ResolveInventoryHostEndpoint(Character->GetWorld(), Character, SourceHost, SourceSlot, Endpoint))
	{ Result.Error = ECatDomainCommandError::PermissionDenied; return Result; }
	FCatInventoryItemUseContext Context;
	Context.RequestId = RequestId; Context.RequestingController = Character->GetController();
	Context.UserPawn = Character; Context.SourceInventory = Endpoint.Inventory; Context.InventorySlotIndex = Endpoint.SlotIndex;
	Context.Target = Target;
	Result = Endpoint.Inventory->ExecuteItemActionFromAuthority(Context, ItemInstanceId, Action, Quantity);
	if (Result.bCommitted && Endpoint.Equipment) Endpoint.Equipment->RefreshLoadoutFromInventoryComponentFromAuthority();
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
	// Carry 访问收敛流程：通用端点只保证同世界和可达；只有从鱼护/鱼缸叼鱼时还必须复核容器当前真的允许这个 Controller 交互，避免关闭入口后仍可通过旧 UI 请求取鱼。
	if (Action == ECatInventoryWorldAction::Carry
		&& (!SourceInventoryHost->Implements<UCatInteractable>()
			|| !ICatInteractable::Execute_CanInteract(SourceInventoryHost, ControlledCharacter->GetController())))
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

// 收货流程：先拒绝重复或非法实例，容量足够时直接原子入库；容量不足再为每个合法堆叠准备载体，非堆叠原物可借用。
// 任何准备失败只清掉本次新建候选，不销毁借用原物，也不改库存。
// 然后静默分配到宿主库存，余量使用预备实例落地；部分入库时为分离出的余量重建 GUID，资源与其他实例状态保持。
// 所有数量落定后才广播库存变化，避免回调重入期间重复收货。没有容量不算失败，无法准备实体才拒绝。
bool UCatInventoryStatics::ReceiveInventoryWithOverflowFromAuthority(AActor* TargetActor,
	const FCatInventoryReceiveBatch& Batch, UCatInventoryComponent** OutReceivingInventory)
{
	if (OutReceivingInventory) *OutReceivingInventory = nullptr;
	if (!IsValid(TargetActor) || !TargetActor->HasAuthority() || !TargetActor->GetWorld()) return false;
	if (Batch.IsEmpty()) return true;
	TArray<UCatInventoryComponent*> Inventories;
	CollectInventoryComponentsFromActor(TargetActor, Inventories);
	Inventories.RemoveAll([](const UCatInventoryComponent* Inventory) { return !Inventory || !Inventory->CanReceiveUnifiedInventoryIntake(); });
	if (Inventories.IsEmpty()) return false;
	// 容量不足才允许溢出；重复身份、错误实例类型及本宿主已有的实例不能借溢出路径复制。
	TSet<FGuid> IncomingIds;
	for (const auto& Entry : Batch.InstanceEntries)
	{
		const auto* Instance = Entry.ItemInstance.Get();
		const auto* Definition = Instance ? Instance->GetItemDefinition() : nullptr;
		if (!Definition || Entry.Count <= 0 || !Instance->GetItemInstanceId().IsValid()
			|| !UCatInventoryItemDefinition::ResolveItemInstanceClass(Definition, Instance->GetClass())
			|| IncomingIds.Contains(Instance->GetItemInstanceId())) return false;
		IncomingIds.Add(Instance->GetItemInstanceId());
		for (const auto* Inventory : Inventories)
			for (const auto& Existing : Inventory->GetInventoryEntries())
				if (Existing.Instance && Existing.Instance->GetItemInstanceId() == Instance->GetItemInstanceId()) return false;
	}
	// 完整接收不需要实体准备，也保留既有载体保管行为。
	if (TryAddInventoryBatchToActor(TargetActor, Batch, OutReceivingInventory)) return true;
	struct FPreparedReceipt
	{
		/** 交给库存分配的实例；首段保留原 GUID，拆分出的其他格另取身份。 */
		UCatInventoryItemInstance* Incoming = nullptr;
		/** 溢出实物持有的状态副本；部分入库时才更换分离身份。 */
		UCatInventoryItemInstance* Overflow = nullptr;
		/** 已验证能保管余量的世界载体；非堆叠原物优先复用。 */
		AActor* Carrier = nullptr;
		/** 当前准备段的件数，不超过该定义的单格上限。 */
		int32 Count = 0;
		/** 是否借用已有载体；准备失败或全部入库时不能销毁它。 */
		bool bReuseCarrier = false;
	};
	TArray<FPreparedReceipt> Prepared;
	const auto Abort = [&]() { for (const auto& Entry : Prepared) if (!Entry.bReuseCarrier && IsValid(Entry.Carrier)) Entry.Carrier->Destroy(); return false; };
	const auto Prepare = [&](UCatInventoryItemInstance* Source, const int32 Quantity) -> bool
	{
		UCatInventoryItemDefinition* Definition = Source ? Source->GetItemDefinition() : nullptr;
		if (!Definition || !Definition->IsInventoryRuntimeDefinitionReady() || Quantity <= 0) return false;
		UClass* CarrierClass = Definition->WorldActorClass.LoadSynchronous();
		if (!CarrierClass || !CarrierClass->ImplementsInterface(UCatInventoryWorldItem::StaticClass())) return false;
		for (int32 Remaining = Quantity; Remaining > 0;)
		{
			FPreparedReceipt& Entry = Prepared.AddDefaulted_GetRef();
			Entry.Count = FMath::Min(Remaining, Definition->GetMaxStackCount());
			Entry.Incoming = Remaining == Quantity ? Source : DuplicateObject<UCatInventoryItemInstance>(Source, TargetActor);
			if (Remaining != Quantity) { Entry.Incoming->SetRuntimeOwnerActor(TargetActor); Entry.Incoming->SetItemInstanceIdFromAuthority(FGuid::NewGuid()); Entry.Incoming->SetWorldActor(nullptr); }
			Entry.Overflow = DuplicateObject<UCatInventoryItemInstance>(Entry.Incoming, TargetActor);
			Entry.bReuseCarrier = Quantity == 1 && Definition->GetMaxStackCount() == 1
				&& IsValid(Source->GetWorldActor()) && Source->GetWorldActor()->Implements<UCatInventoryWorldItem>();
			if (Entry.bReuseCarrier)
			{
				Entry.Carrier = Source->GetWorldActor();
				Remaining -= Entry.Count;
				continue;
			}
			const FVector Offset = TargetActor->GetActorForwardVector() * 90.0 + FVector(0, 0, 30.0 + Prepared.Num() * 5.0);
			FActorSpawnParameters Parameters; Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			Entry.Carrier = TargetActor->GetWorld()->SpawnActor<AActor>(CarrierClass, TargetActor->GetActorLocation() + Offset, FRotator::ZeroRotator, Parameters);
			ICatInventoryWorldItem* Receiver = Cast<ICatInventoryWorldItem>(Entry.Carrier);
			if (!Receiver || !Receiver->InitializeFromInventoryFromAuthority(Entry.Overflow, Entry.Count)) return false;
			Entry.Carrier->SetActorHiddenInGame(true); Entry.Carrier->SetActorEnableCollision(false);
			Remaining -= Entry.Count;
		}
		return true;
	};
	for (const auto& Entry : Batch.DefinitionEntries)
	{
		const auto Class = UCatInventoryItemDefinition::ResolveItemInstanceClass(Entry.ItemDefinition, Entry.ItemInstanceClass);
		if (!Entry.ItemDefinition || !Class) return Abort();
		auto* Instance = NewObject<UCatInventoryItemInstance>(TargetActor, Class);
		Instance->SetItemDefinition(Entry.ItemDefinition);
		if (!Prepare(Instance, Entry.Count)) return Abort();
	}
	for (const auto& Entry : Batch.InstanceEntries) if (!Prepare(Entry.ItemInstance, Entry.Count)) return Abort();
	TSet<UCatInventoryComponent*> Changed;
	for (auto& Entry : Prepared)
	{
		int32 Remaining = Entry.Count;
		for (UCatInventoryComponent* Inventory : Inventories)
		{
			const int32 Before = Remaining; bool bFullyAdded = false;
			Inventory->AddEntry(Entry.Incoming, Remaining, bFullyAdded, false);
			if (Before != Remaining)
			{
				Changed.Add(Inventory);
				if (OutReceivingInventory && !*OutReceivingInventory) *OutReceivingInventory = Inventory;
				// 同一堆叠分到下一个库存时需要另一份格子身份，不能在两个库存登记同一个子对象。
				if (Remaining > 0) { Entry.Incoming = DuplicateObject<UCatInventoryItemInstance>(Entry.Incoming, TargetActor); Entry.Incoming->SetItemInstanceIdFromAuthority(FGuid::NewGuid()); Entry.Incoming->SetWorldActor(nullptr); }
			}
			if (Remaining == 0) break;
		}
		if (Remaining == 0) { if (!Entry.bReuseCarrier) Entry.Carrier->Destroy(); continue; }
		if (Remaining != Entry.Count) Entry.Overflow->SetItemInstanceIdFromAuthority(FGuid::NewGuid());
		Cast<ICatInventoryWorldItem>(Entry.Carrier)->InitializeFromInventoryFromAuthority(Entry.Overflow, Remaining);
		if (Entry.bReuseCarrier)
			Entry.Carrier->SetActorLocation(TargetActor->GetActorLocation() + TargetActor->GetActorForwardVector() * 90.0 + FVector(0,0,30), false, nullptr, ETeleportType::TeleportPhysics);
		Entry.Carrier->SetActorHiddenInGame(false); Entry.Carrier->SetActorEnableCollision(true);
		if (auto* Body = Cast<UPrimitiveComponent>(Entry.Carrier->GetRootComponent()); Body && Body->GetBodySetup()) Body->SetSimulatePhysics(true);
		Entry.Carrier->ForceNetUpdate();
		UE_LOG(LogCatfishing, Log, TEXT("Event=inventory_overflow Host=%s Item=%d Instance=%s Count=%d Carrier=%s World=%s NetMode=%d Authority=1"),
			*GetNameSafe(TargetActor), Entry.Overflow->GetItemId(), *Entry.Overflow->GetItemInstanceId().ToString(), Remaining,
			*GetNameSafe(Entry.Carrier), *GetNameSafe(TargetActor->GetWorld()), TargetActor->GetNetMode());
	}
	for (UCatInventoryComponent* Inventory : Changed) Inventory->BroadcastInventoryChange();
	return true;
}

// 调用方库存已校验并预占请求；先重读来源，Sell 直接提交商店交易并沿用其通知，其他动作进入载体流程。
// Carry 校验容器、鱼和嘴部约束后附着；Drop/Place 先准备载体并检查碰撞与落点，再复核来源并静默扣量。
// 失败清理本次候选，Carry 按失败阶段解除认领或恢复保管表现；不恢复整份库存快照。返回后由库存缓存菜单结果并发布世界动作通知。
FCatDomainCommandResult UCatInventoryStatics::ExecuteResolvedInventoryActionFromAuthority(
	const FCatInventoryItemUseContext& Context, const FGameplayTag ActionTag, const int32 Quantity)
{
	FCatDomainCommandResult Result;
	Result.RequestId = Context.RequestId;
	UCatInventoryComponent* Inventory = Context.SourceInventory;
	ACatCharacter* Character = Cast<ACatCharacter>(Context.UserPawn);
	if (!Inventory || !Character || !Character->HasAuthority())
	{ Result.Error = ECatDomainCommandError::PermissionDenied; return Result; }
	const int32 SlotIndex = Context.InventorySlotIndex;
	const FGuid RequestId = Context.RequestId;
	const FCatInventoryEntry* Source = Inventory->GetInventoryEntryAtSlot(SlotIndex);
	if (!Source || !Source->Instance) { Result.Error = ECatDomainCommandError::InvalidPayload; return Result; }
	const FGuid ItemInstanceId = Source->Instance->GetItemInstanceId();
	if (ActionTag == CatInventoryActionTags::Sell)
	{
		ACatFishGuardActor* Guard = Cast<ACatFishGuardActor>(Inventory->GetOwner());
		ACatFishBuyerActor* Buyer = Guard ? ACatFishBuyerActor::FindAvailableBuyer(Context.RequestingController, Guard) : nullptr;
		UCatShopTradeController* Trading = Character->GetWorld()->GetSubsystem<UCatShopTradeController>();
		if (!Trading || !Buyer) { Result.Error = ECatDomainCommandError::PermissionDenied; return Result; }
		return Trading->SubmitFishSaleFromPlayer(Context.RequestingController, Buyer, Guard, {ItemInstanceId}, RequestId).Delivery;
	}
	const ECatInventoryWorldAction Action = ActionTag == CatInventoryActionTags::Drop ? ECatInventoryWorldAction::Drop
		: ActionTag == CatInventoryActionTags::Place ? ECatInventoryWorldAction::Place : ECatInventoryWorldAction::Carry;
	if (ActionTag != CatInventoryActionTags::Drop && ActionTag != CatInventoryActionTags::Place && ActionTag != CatInventoryActionTags::Carry)
	{ Result.Error = ECatDomainCommandError::InvalidPayload; return Result; }
	AActor* WorldActor = nullptr;
	// 批量请求只能由库存队列拆成单件；这里不再存在同时发布整批物品的备用路径。
	if (Action == ECatInventoryWorldAction::Drop && Quantity != 1)
	{ Result.Error = ECatDomainCommandError::InvalidPayload; return Result; }
	bool bNewActor = false;
	const auto Finish = [&](const ECatDomainCommandError Error)
	{
		Result.Error = Error;
		Result.bCommitted = Error == ECatDomainCommandError::None;
		if (!Result.bCommitted && bNewActor && IsValid(WorldActor)) WorldActor->Destroy();
		const FString Event = FString::Printf(TEXT("Event=inventory_world_release RequestId=%s ItemInstanceId=%s Quantity=%d Action=%d Actor=%s Error=%s World=%s NetMode=%d Authority=%d LocalRole=%d"),
			*RequestId.ToString(), *ItemInstanceId.ToString(), Quantity, static_cast<int32>(Action), *GetNameSafe(WorldActor),
			*UEnum::GetValueAsString(Error), *GetNameSafe(Inventory->GetWorld()), Inventory->GetOwner() ? Inventory->GetOwner()->GetNetMode() : -1,
			Inventory->GetOwner() && Inventory->GetOwner()->HasAuthority(), Inventory->GetOwner() ? static_cast<int32>(Inventory->GetOwner()->GetLocalRole()) : -1);
		if (Result.bCommitted) { UE_LOG(LogCatfishing, Log, TEXT("%s"), *Event); }
		else { UE_LOG(LogCatfishing, Warning, TEXT("%s"), *Event); }
		return Result;
	};
	const FCatInventoryEntry* Entry = Inventory->GetInventoryEntryAtSlot(SlotIndex);
	const UCatInventorySettings* Settings = GetDefault<UCatInventorySettings>();
	// Carry 事务流程：
	// 1. 库存入口已校验身份与动作可用性；此处再限定为可达鱼护/鱼缸内完整的一条鱼，并检查空嘴和真实挂点。
	// 2. 复用或生成载体，认领嘴部后完成附着；每次可能重入的初始化或表现操作后复核来源，失败清理本次认领或恢复保管表现。
	// 3. 静默扣格后更新鱼实例宿主和载体可见性，保留原鱼身份、重量与缩放；返回库存入口后才记录结果并通知。
	if (Action == ECatInventoryWorldAction::Carry)
	{
		UCatFishInventoryItemInstance* FishItem = Cast<UCatFishInventoryItemInstance>(Entry->Instance);
		AActor* OriginalWorldActor = FishItem ? FishItem->GetWorldActor() : nullptr;
		AActor* OriginalRuntimeOwner = FishItem ? FishItem->GetRuntimeOwnerActor() : nullptr;
		// 不只检查格子：回调若转移或重新关联同一实例，本请求已失去原始来源契约，不能继续提交。
		const auto IsSourceStillCurrent = [&]()
		{
			const FCatInventoryEntry* CurrentEntry = Inventory->GetInventoryEntryAtSlot(SlotIndex);
			return CurrentEntry && CurrentEntry->Instance == FishItem && CurrentEntry->StackCount == 1
				&& FishItem && FishItem->GetItemInstanceId() == ItemInstanceId
				&& FishItem->GetWorldActor() == OriginalWorldActor && FishItem->GetRuntimeOwnerActor() == OriginalRuntimeOwner
				&& CatInventoryAccessRules::ResolveReachableFishContainer(Inventory->GetOwner(), Character) == Inventory;
		};
		const bool bFishContainer = CatInventoryAccessRules::ResolveReachableFishContainer(Inventory->GetOwner(), Character) == Inventory;
		if (!bFishContainer || !FishItem || !FishItem->GetFishDefinition()
			|| !FishItem->GetFishDefinition()->IsInventoryRuntimeDefinitionReady()
			|| !FMath::IsFinite(FishItem->GetFishWeightKilograms()) || FishItem->GetFishWeightKilograms() <= 0
			|| Quantity != 1 || Entry->StackCount != 1 || Character->GetMouthCarriedActor() != nullptr)
			return Finish(ECatDomainCommandError::PermissionDenied);
		// 容器取鱼必须有真实嘴部挂点；在生成载体或认领嘴部前拒绝，不能退化成附着到空骨架根。
		const UCatFishPickupSettings* PickupSettings = GetDefault<UCatFishPickupSettings>();
		if (!PickupSettings || !Character->GetMesh() || !Character->GetMesh()->DoesSocketExist(PickupSettings->MouthCarrySocketName))
			return Finish(ECatDomainCommandError::DependencyUnavailable);
		AActor* CarriedActor = FishItem->GetWorldActor();
		const FTransform OriginalRetainedTransform = IsValid(CarriedActor) ? CarriedActor->GetActorTransform() : FTransform::Identity;
		bool bCreatedCarrier = false;
		if (!IsValid(CarriedActor))
		{
			UCatInventoryItemDefinition* FishDefinition = FishItem->GetItemDefinition();
			UClass* CarrierClass = FishDefinition ? FishDefinition->WorldActorClass.LoadSynchronous() : nullptr;
			if (!CarrierClass || !CarrierClass->IsChildOf(ACatFishPickupActor::StaticClass())) return Finish(ECatDomainCommandError::DependencyUnavailable);
			CarriedActor = Inventory->GetWorld()->SpawnActorDeferred<AActor>(CarrierClass, Character->GetActorTransform(), nullptr, Character,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			bCreatedCarrier = true;
			if (!CarriedActor) return Finish(ECatDomainCommandError::DependencyUnavailable);
		}
		ACatFishPickupActor* FishActor = Cast<ACatFishPickupActor>(CarriedActor);
		WorldActor = CarriedActor;
		if (!FishActor || (!bCreatedCarrier && !FishActor->CanCarryInventoryItemFromAuthority(FishItem)))
		{
			if (bCreatedCarrier && IsValid(CarriedActor)) CarriedActor->Destroy();
			return Finish(ECatDomainCommandError::InvalidPayload);
		}
		const bool bClaimedByThisRequest = Character->TryClaimMouthCarriedActorFromAuthority(FishActor);
		if (!bClaimedByThisRequest)
		{
			if (bCreatedCarrier && IsValid(CarriedActor)) CarriedActor->Destroy();
			return Finish(ECatDomainCommandError::PermissionDenied);
		}
		if (bCreatedCarrier)
		{
			CarriedActor->SetActorHiddenInGame(true);
			CarriedActor->SetActorEnableCollision(false);
			CarriedActor->FinishSpawning(Character->GetActorTransform());
		}
		// FinishSpawning 可能运行组件或蓝图回调；提交前必须重新确认原格尚是同一条鱼，不能按旧 SlotIndex 扣掉后来换入的实例。
		if (!IsSourceStillCurrent())
		{
			Character->ReleaseMouthCarriedActorFromAuthority(FishActor);
			if (bCreatedCarrier && IsValid(CarriedActor)) CarriedActor->Destroy();
			return Finish(ECatDomainCommandError::InvalidPayload);
		}
		if (bCreatedCarrier && !FishActor->InitializeFromInventoryForCarryFromAuthority(FishItem, 1))
		{
			Character->ReleaseMouthCarriedActorFromAuthority(FishActor);
			FishActor->Destroy();
			return Finish(ECatDomainCommandError::InvalidPayload);
		}
		if (!IsSourceStillCurrent())
		{
			Character->ReleaseMouthCarriedActorFromAuthority(FishActor);
			if (bCreatedCarrier && IsValid(CarriedActor)) CarriedActor->Destroy();
			return Finish(ECatDomainCommandError::InvalidPayload);
		}
		if (!FishActor->BeginMouthCarryFromAuthority(Character, Character->GetPlayerState(), false))
		{
			Character->ReleaseMouthCarriedActorFromAuthority(FishActor);
			if (!bCreatedCarrier) FishActor->RestoreInventoryRetentionFromAuthority(FishItem, OriginalRetainedTransform);
			if (bCreatedCarrier && IsValid(CarriedActor)) CarriedActor->Destroy();
			return Finish(ECatDomainCommandError::PermissionDenied);
		}
		// 附着路径也可能触发表现回调；再次核对实例身份后才执行不带广播的扣格，确保请求永远只消费自己的原条目。
		if (!IsSourceStillCurrent() || !Inventory->ConsumeItemAtSlot(SlotIndex, 1, false))
		{
			if (!bCreatedCarrier) FishActor->RestoreInventoryRetentionFromAuthority(FishItem, OriginalRetainedTransform);
			else
			{
				Character->ReleaseMouthCarriedActorFromAuthority(FishActor);
				FishActor->Destroy();
			}
			return Finish(ECatDomainCommandError::InvalidPayload);
		}
		FishItem->SetWorldActor(FishActor);
		FishItem->SetRuntimeOwnerActor(FishActor);
		// 退出容器保管态后恢复 Actor 总开关；嘴叼期间仍由组件的 NoCollision 禁止碰撞，后续 Drop 才能正常恢复真实刚体。
		FishActor->SetActorEnableCollision(true);
		FishActor->SetActorHiddenInGame(false);
		// 在返回库存入口前完成网络更新；入口随后记录成功结果再通知，观察者即使重放或销毁鱼，也不会重新执行本段载体操作。
		FishActor->ForceNetUpdate();
		Character->ForceNetUpdate();
		const FCatDomainCommandResult Completed = Finish(ECatDomainCommandError::None);
		return Completed;
	}
	if (!Settings || !FMath::IsFinite(Settings->PlacementRangeCentimeters) || Settings->PlacementRangeCentimeters <= 0
		|| !FMath::IsFinite(Settings->PlacementHeightDifferenceCentimeters) || Settings->PlacementHeightDifferenceCentimeters < 0
		|| !FMath::IsFinite(Settings->PlacementSlopeDegrees) || Settings->PlacementSlopeDegrees < 0 || Settings->PlacementSlopeDegrees >= 90
		|| !FMath::IsFinite(Settings->DropForwardSpeed) || Settings->DropForwardSpeed < 0
		|| !FMath::IsFinite(Settings->DropUpwardSpeed) || Settings->DropUpwardSpeed < 0)
		return Finish(ECatDomainCommandError::InvalidPayload);
	UCatInventoryItemInstance* SourceItem = Entry->Instance;
	const int32 SourceQuantity = Entry->StackCount;
	UCatInventoryItemDefinition* Definition = SourceItem->GetItemDefinition();
	if (!Definition || !Definition->IsInventoryRuntimeDefinitionReady()) return Finish(ECatDomainCommandError::InvalidPayload);
	AActor* SourceWorldActor = SourceItem->GetWorldActor();
	if (SourceWorldActor && (!IsValid(SourceWorldActor) || SourceWorldActor->GetWorld() != Inventory->GetWorld())) return Finish(ECatDomainCommandError::InvalidPayload);
	WorldActor = Quantity == Entry->StackCount ? SourceWorldActor : nullptr;
	UCatInventoryItemInstance* ReleasedItem = SourceItem;
	if (!WorldActor)
	{
		UClass* ActorClass = SourceWorldActor ? SourceWorldActor->GetClass() : Definition->WorldActorClass.LoadSynchronous();
		if (!ActorClass || !ActorClass->ImplementsInterface(UCatInventoryWorldItem::StaticClass())) return Finish(ECatDomainCommandError::DependencyUnavailable);
		FTransform SpawnTransform = Character->GetActorTransform();
		if (SourceWorldActor) SpawnTransform.SetScale3D(SourceWorldActor->GetActorScale3D());
		WorldActor = Inventory->GetWorld()->SpawnActorDeferred<AActor>(ActorClass, SpawnTransform, nullptr, Character,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		bNewActor = true;
		if (!WorldActor) return Finish(ECatDomainCommandError::DependencyUnavailable);
		WorldActor->SetActorHiddenInGame(true);
		WorldActor->SetActorEnableCollision(false);
		ReleasedItem = DuplicateObject<UCatInventoryItemInstance>(SourceItem, WorldActor);
		ReleasedItem->SetWorldActor(nullptr);
		if (Quantity < Entry->StackCount) ReleasedItem->SetItemInstanceIdFromAuthority(FGuid::NewGuid());
		ICatInventoryWorldItem* Receiver = Cast<ICatInventoryWorldItem>(WorldActor);
		if (!Receiver || !Receiver->InitializeFromInventoryFromAuthority(ReleasedItem, Quantity)) return Finish(ECatDomainCommandError::InvalidPayload);
		WorldActor->FinishSpawning(SpawnTransform);
	}
	else if (WorldActor->GetWorld() != Inventory->GetWorld())
	{
		return Finish(ECatDomainCommandError::InvalidPayload);
	}
	UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(WorldActor->GetRootComponent());
	FTransform Transform;
	// 先检查真实刚体形状和可移动性；不能以设置了SimulatePhysics布尔值就假定物体确实能够运动。
	if (!Body || Body->Mobility != EComponentMobility::Movable || !Body->GetBodySetup()
		|| Body->GetBodySetup()->AggGeom.GetElementCount() == 0
		|| Body->GetBodySetup()->CollisionTraceFlag == CTF_UseComplexAsSimple
		|| !UCatInventoryStatics::FindWorldReleaseTransform(Character, WorldActor, Action, *Settings, Transform))
		return Finish(ECatDomainCommandError::PermissionDenied);
	AActor* PreviousRuntimeOwner = ReleasedItem->GetRuntimeOwnerActor();
	// 原物保管期间数量可能已合并或消耗；公开前必须更新拾取载荷，不能再次发放拾取前的旧数量。
	if (!bNewActor)
	{
		ICatInventoryWorldItem* Receiver = Cast<ICatInventoryWorldItem>(WorldActor);
		if (!Receiver || !Receiver->InitializeFromInventoryFromAuthority(ReleasedItem, Quantity))
		{
			ReleasedItem->SetRuntimeOwnerActor(PreviousRuntimeOwner);
			return Finish(ECatDomainCommandError::InvalidPayload);
		}
	}
	const FCatInventoryEntry* Current = Inventory->GetInventoryEntryAtSlot(SlotIndex);
	if (!Current || Current->Instance != SourceItem || Current->StackCount != SourceQuantity || !Inventory->ConsumeItemAtSlot(SlotIndex, Quantity, false))
	{
		if (!bNewActor && Current && Current->Instance == SourceItem) ReleasedItem->SetRuntimeOwnerActor(PreviousRuntimeOwner);
		return Finish(ECatDomainCommandError::InvalidPayload);
	}
	ReleasedItem->SetRuntimeOwnerActor(WorldActor);
	Body->SetSimulatePhysics(false);
	WorldActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	WorldActor->SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
	WorldActor->SetOwner(nullptr);
	WorldActor->SetInstigator(nullptr);
	WorldActor->SetActorHiddenInGame(false);
	WorldActor->SetActorEnableCollision(true);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Body->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	if (Action == ECatInventoryWorldAction::Drop)
	{
		Body->SetSimulatePhysics(true);
		Body->SetPhysicsLinearVelocity(Character->GetActorForwardVector().GetSafeNormal2D() * Settings->DropForwardSpeed + FVector(0, 0, Settings->DropUpwardSpeed));
	}
	UCatWorldDropProtectionComponent::ArmFromAuthority(WorldActor);
	WorldActor->ForceNetUpdate();
	const FCatDomainCommandResult Completed = Finish(ECatDomainCommandError::None);
	return Completed;
}
