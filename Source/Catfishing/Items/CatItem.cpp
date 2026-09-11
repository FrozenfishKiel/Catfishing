#include "Items/CatItem.h"

#include "Character/CatCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "GameFramework/Controller.h"
#include "Logging/CatLog.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/CatInventoryComponent.h"

// 构造流程：创建目标扫描与落地物理根，阻挡场景但忽略 Pawn；网格只负责表现，避免两份刚体争夺运动。
ACatItem::ACatItem()
{
	bReplicates = true;
	SetReplicateMovement(true);
	PrimaryActorTick.bCanEverTick = false;
	PickupCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("PickupCollision"));
	SetRootComponent(PickupCollision);
	PickupCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	PickupCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	PickupCollision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	PickupCollision->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	PickupCollision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	StaticMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMesh"));
	StaticMesh->SetupAttachment(PickupCollision);
	StaticMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkeletalMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("SkeletalMesh"));
	SkeletalMesh->SetupAttachment(PickupCollision);
	SkeletalMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

// 批次读取流程：返回蓝图配置的静态载荷副本；调用方随后仍由统一库存入口做 authority、定义和容量校验。
FCatInventoryReceiveBatch ACatItem::GetPickupInventory() const { return StaticPickupInventory; }

// 落地接收流程：检查实例与数量，刷新载荷和原 Actor 引用，再解除上次拾取占用；公开和物理恢复仍由库存落地入口完成。
bool ACatItem::InitializeFromInventoryFromAuthority(UCatInventoryItemInstance* Item, const int32 Quantity)
{
	if (!HasAuthority() || !Item || !Item->GetItemDefinition() || Quantity <= 0)
	{
		return false;
	}
	StaticPickupInventory = FCatInventoryReceiveBatch();
	FCatInventoryInstanceEntry& Entry = StaticPickupInventory.InstanceEntries.AddDefaulted_GetRef();
	Entry.ItemInstance = Item;
	Entry.Count = Quantity;
	Item->SetRuntimeOwnerActor(this);
	Item->SetWorldActor(this);
	bHasInventoryPayload = true;
	bPickupClaimed = false;
	return true;
}

// 生成配置流程：基础世界物只提供稳定组件与批次契约；没有额外配置时不写碰撞或库存状态。
void ACatItem::InitializeActorSpawnConfig() {}

// 开始运行流程：先完成引擎生命周期，再按已经加载的蓝图默认值调用物品配置扩展点；不在构造期读取尚未反序列化的资产字段。
void ACatItem::BeginPlay()
{
	Super::BeginPlay();
	InitializeActorSpawnConfig();
}

// 交互资格流程：确认请求者和物品同世界且物品可见、未被占用，再按角色距离检查半径；库存隐藏的原物不能重复拾取。
bool ACatItem::CanInteract_Implementation(AController* RequestingController) const
{
	const ACatCharacter* Character = RequestingController ? Cast<ACatCharacter>(RequestingController->GetPawn()) : nullptr;
	return Character && RequestingController->GetWorld() == GetWorld() && Character->GetWorld() == GetWorld()
		&& !bPickupClaimed && !IsHidden() && !IsActorBeingDestroyed()
		&& FMath::IsFinite(InteractionRadiusCentimeters) && InteractionRadiusCentimeters > 0.0
		&& FVector::DistSquared(Character->GetActorLocation(), GetActorLocation()) <= FMath::Square(InteractionRadiusCentimeters);
}

// 提示读取流程：返回基础拾取文案；不读取库存容量，因为容量必须由服务器收货预检裁决。
FText ACatItem::GetInteractionPrompt_Implementation() const { return FText::FromString(TEXT("拾取")); }

// 半径读取流程：返回配置厘米值，让目标扫描与 authority 复核遵守同一边界。
double ACatItem::GetInteractionRadius_Implementation() const { return InteractionRadiusCentimeters; }

// 交互提交流程：
// 1. 客户端只通过现有 PlayerController RPC 重放这次目标交互，不直接修改世界物或库存。
// 2. authority 重新检查请求者距离和 RequestId，再读取完整批次并调用唯一 Actor 收货入口。
// 3. 入库前占用本世界物，阻止回调重入；失败释放占用。成功后将原 Actor 交给接收实例并隐藏。
// 4. 合入已有载体的堆叠只增加数量；多种物品的发货批次没有唯一对应实例，仍按原批次规则清理发货 Actor。
bool ACatItem::Interact_Implementation(AController* RequestingController, const FGuid RequestId)
{
	ACatfishingPlayerController* PlayerController = Cast<ACatfishingPlayerController>(RequestingController);
	if (!PlayerController || !RequestId.IsValid() || !CanInteract_Implementation(RequestingController))
	{
		UE_LOG(LogCatfishing, Warning, TEXT("Event=item_pickup_rejected Item=%s Request=%s Reason=InvalidRequesterOrReach World=%s NetMode=%d Authority=%d LocalRole=%d"),
			*GetNameSafe(this), *RequestId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), HasAuthority(), static_cast<int32>(GetLocalRole()));
		return false;
	}
	if (!HasAuthority())
	{
		UE_LOG(LogCatfishing, Log, TEXT("Event=item_pickup_requested Item=%s Request=%s World=%s NetMode=%d Authority=0 LocalRole=%d"),
			*GetNameSafe(this), *RequestId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()));
		PlayerController->ServerRequestInteraction(this, RequestId);
		return true;
	}
	FCatInventoryReceiveBatch PickupBatch = bHasInventoryPayload ? StaticPickupInventory : GetPickupInventory();
	// 单种定义载荷先生成现有模型的一份实例，让原 Actor 引用随正式收货进入正确的格子。
	if (PickupBatch.DefinitionEntries.Num() == 1 && PickupBatch.InstanceEntries.IsEmpty())
	{
		const FCatInventoryDefinitionEntry& DefinitionEntry = PickupBatch.DefinitionEntries[0];
		const TSubclassOf<UCatInventoryItemInstance> InstanceClass = UCatInventoryItemDefinition::ResolveItemInstanceClass(
			DefinitionEntry.ItemDefinition, DefinitionEntry.ItemInstanceClass);
		if (!DefinitionEntry.ItemDefinition || !InstanceClass) return false;
		UCatInventoryItemInstance* Instance = NewObject<UCatInventoryItemInstance>(PlayerController->GetPawn(), InstanceClass);
		Instance->SetItemDefinition(DefinitionEntry.ItemDefinition);
		FCatInventoryInstanceEntry& InstanceEntry = PickupBatch.InstanceEntries.AddDefaulted_GetRef();
		InstanceEntry.ItemInstance = Instance;
		InstanceEntry.Count = DefinitionEntry.Count;
		PickupBatch.DefinitionEntries.Reset();
	}
	// 延续现有子对象归属规则：保留 GUID 和运行状态，将库存复制宿主放在接收 Pawn 下；原 Actor 另由接收实例保管。
	for (FCatInventoryInstanceEntry& Entry : PickupBatch.InstanceEntries)
	{
		if (Entry.ItemInstance)
		{
			if (Entry.ItemInstance->GetOuter() != PlayerController->GetPawn())
				Entry.ItemInstance = DuplicateObject<UCatInventoryItemInstance>(Entry.ItemInstance, PlayerController->GetPawn());
			Entry.ItemInstance->SetRuntimeOwnerActor(PlayerController->GetPawn());
			if (PickupBatch.DefinitionEntries.IsEmpty() && PickupBatch.InstanceEntries.Num() == 1)
				Entry.ItemInstance->SetWorldActor(this);
		}
	}
	bPickupClaimed = true;
	UCatInventoryComponent* ReceivingInventory = nullptr;
	const bool bCommitted = !PickupBatch.IsEmpty()
		&& UCatInventoryStatics::TryAddInventoryBatchToActor(PlayerController->GetPawn(), PickupBatch, &ReceivingInventory);
	if (!bCommitted)
	{
		bPickupClaimed = false;
		UE_LOG(LogCatfishing, Warning, TEXT("Event=item_pickup_rejected Item=%s Request=%s Reason=EmptyBatchOrInventoryRejected World=%s NetMode=%d Authority=1 LocalRole=%d"),
			*GetNameSafe(this), *RequestId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()));
		return false;
	}
	bool bRetained = false;
	if (ReceivingInventory)
	{
		for (const FCatInventoryEntry& Entry : ReceivingInventory->GetInventoryEntries())
		{
			if (Entry.Instance && Entry.Instance->GetWorldActor() == this) { bRetained = true; break; }
		}
	}
	UE_LOG(LogCatfishing, Log,
		TEXT("Event=item_pickup_committed Item=%s Request=%s Player=%s Definitions=%d Instances=%d RetainedActor=%d World=%s NetMode=%d Authority=1 LocalRole=%d"),
		*GetNameSafe(this), *RequestId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(PlayerController->GetPawn()),
		PickupBatch.DefinitionEntries.Num(), PickupBatch.InstanceEntries.Num(), bRetained, *GetNameSafe(GetWorld()),
		static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()));
	if (bRetained)
	{
		PickupCollision->SetSimulatePhysics(false);
		SetActorEnableCollision(false);
		SetActorHiddenInGame(true);
		ForceNetUpdate();
	}
	else if (!Destroy())
	{
		// 库存已经提交，销毁失败也不能再次发货；关闭交互并记录需要定位的世界物，避免回滚出第二份物品。
		SetActorEnableCollision(false);
		SetActorHiddenInGame(true);
		UE_LOG(LogCatfishing, Warning, TEXT("Event=item_pickup_cleanup_failed Item=%s Request=%s World=%s NetMode=%d Authority=1 LocalRole=%d"), *GetNameSafe(this), *RequestId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()));
	}
	return bCommitted;
}
