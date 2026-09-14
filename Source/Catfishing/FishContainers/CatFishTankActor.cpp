#include "FishContainers/CatFishTankActor.h"

#include "Components/SceneComponent.h"
#include "Character/CatCharacter.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Inventory/CatInventoryAccessRules.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "Components/SphereComponent.h"
#include "FishContainers/CatFishContainerSettings.h"
#include "FishContainers/CatFishTankInteractionComponent.h"
#include "Logging/CatLog.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/CatInteractionSettings.h"
#include "Net/UnrealNetwork.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Inventory/CatFishOnlyInventoryComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "UI/WorldInfo/CatFishTankWorldInfoComponent.h"

// 构造流程：创建固定摆放根、正式鱼库存和交互入口，再挂接只读信息锚点；开启复制并关闭 Tick，鱼数组仍只归库存持有。
ACatFishTankActor::ACatFishTankActor()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = false;
	TankRoot = CreateDefaultSubobject<USceneComponent>(TEXT("TankRoot"));
	SetRootComponent(TankRoot);
	InteractionCollision = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionCollision"));
	InteractionCollision->SetupAttachment(TankRoot);
	InteractionCollision->SetSphereRadius(100.0f);
	InteractionCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionCollision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractionCollision->SetGenerateOverlapEvents(false);
	FishInventory = CreateDefaultSubobject<UCatFishOnlyInventoryComponent>(TEXT("FishInventory"));
	TankInteraction = CreateDefaultSubobject<UCatFishTankInteractionComponent>(TEXT("TankInteraction"));
	WorldInfo = CreateDefaultSubobject<UCatFishTankWorldInfoComponent>(TEXT("WorldInfo"));
	WorldInfo->SetupAttachment(TankRoot);
	InteractionPrompt = NSLOCTEXT("Catfishing", "FishTankInteractionPrompt", "打开鱼缸");
}

// BeginPlay 流程：父类先启动组件与订阅，再配置交互碰撞；服务器写入正式库存容量后显式刷新摘要，客户端等待复制。
void ACatFishTankActor::BeginPlay()
{
	Super::BeginPlay();
	if (const UCatInteractionSettings* Settings = GetDefault<UCatInteractionSettings>(); Settings && InteractionCollision)
	{
		InteractionCollision->SetCollisionResponseToChannel(Settings->TargetingTraceChannel, ECR_Block);
	}
	if (HasAuthority())
	{
		// 一局稳定的鱼缸身份；缸随局清空、不跨局延续，所以每次入场新发一个就够。
		TankContainerId = FGuid::NewGuid();
	}
	if (HasAuthority() && FishInventory != nullptr)
	{
		// 入场按初始档开缸（设计：10 条）。升级只在本局有效，所以每次入场都从 0 档重新开始——
		// 这就是「鱼缸容量随局清空」，不需要另写一条清理。
		CapacityTier = 0;
		CommittedUpgradeRequestIds.Reset();
		FishInventory->SetInventorySlotCountFromAuthority(ResolveSlotCapacityForTier(CapacityTier));
	}
	// 库存只在扩容时广播；即使容量没有变化，也必须在组件 BeginPlay 之后发布第一份完整摘要。
	if (HasAuthority() && WorldInfo)
	{
		WorldInfo->RefreshSummary();
	}
	// 缸内游动表现要在鱼进出缸时增删；服务器与客户端都订阅，客户端由复制回调驱动同一条通知。
	if (FishInventory)
	{
		FishInventoryChangedHandle = FishInventory->OnInventoryObservedChanged.AddUObject(
			this, &ACatFishTankActor::HandleFishInventoryChanged);
	}
}

// 复制登记流程：只登记一局鱼缸容器 ID；鱼数组由 FishInventory 组件自己的 FastArray 复制，不在此重复一份。
void ACatFishTankActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, TankContainerId);
	DOREPLIFETIME(ThisClass, CapacityTier);
}

// 档位读取流程：直接返回复制字段；服务器是写入方，客户端读到的是最近一次复制值，两端用同一个数展示容量。
int32 ACatFishTankActor::GetCapacityTier() const
{
	return CapacityTier;
}

bool ACatFishTankActor::RestoreCapacityTierFromAuthority(const int32 Tier)
{
	const int32 Capacity = ResolveSlotCapacityForTier(Tier);
	if (!HasAuthority() || !FishInventory || Tier < 0 || Capacity <= 0) return false;
	for (int32 Index = Capacity; Index < FishInventory->GetInventoryEntries().Num(); ++Index)
		if (FishInventory->GetInventoryEntries()[Index].Instance) return false;
	CapacityTier = Tier;
	FishInventory->SetInventorySlotCountFromAuthority(Capacity);
	CommittedUpgradeRequestIds.Reset();
	if (WorldInfo) WorldInfo->RefreshSummary();
	ForceNetUpdate();
	UE_LOG(LogCatFishContainers, Log, TEXT("Event=fish_tank_capacity_restored World=%s NetMode=%d Authority=1 LocalRole=%d Tank=%s Tier=%d Capacity=%d"),
		*GetNameSafe(GetWorld()), GetNetMode(), GetLocalRole(), *GetName(), Tier, Capacity);
	return true;
}

// 容量解析流程：优先按「设置里的初始档 + 已购档」取容量；设置没给出正容量时回退到编辑器容量并记一条 Warning。
// 这条回退是刻意的：鱼缸在容量档位落地之前就在跑，没配置就把缸判成 0 格等于让整局收不了鱼。
int32 ACatFishTankActor::ResolveSlotCapacityForTier(const int32 Tier) const
{
	const UCatFishContainerSettings* Settings = GetDefault<UCatFishContainerSettings>();
	const int32 ConfiguredCapacity = Settings ? Settings->GetSharedFishTankCapacityForTier(Tier) : 0;
	if (ConfiguredCapacity > 0)
	{
		return ConfiguredCapacity;
	}
	UE_LOG(LogCatFishContainers, Warning,
		TEXT("Event=fish_tank_capacity_config_missing Tank=%s Tier=%d FallbackCapacity=%d Reason=SettingsTierUnresolved"),
		*GetNameSafe(this), Tier, FMath::Max(0, FishInventorySlotCapacity));
	return FMath::Max(0, FishInventorySlotCapacity);
}

// 升级可行性流程：只允许严格下一档，且该档能从配置解析出正容量；查询本身不写状态，供商店在扣钱之前问。
// 同号重放先放行：钱在首次那一趟已经扣了，再用「现在买不了」挡住重试只会造成「钱扣了、货没到」。
bool ACatFishTankActor::CanApplyCapacityUpgradeFromAuthority(const int32 TargetTier, const FGuid& RequestId) const
{
	if (!HasAuthority() || FishInventory == nullptr)
	{
		return false;
	}
	if (RequestId.IsValid() && CommittedUpgradeRequestIds.Contains(RequestId))
	{
		return true;
	}
	if (TargetTier != CapacityTier + 1)
	{
		return false;
	}
	const UCatFishContainerSettings* Settings = GetDefault<UCatFishContainerSettings>();
	const int32 TargetCapacity = Settings ? Settings->GetSharedFishTankCapacityForTier(TargetTier) : 0;
	return TargetCapacity > 0 && TargetCapacity > Settings->GetSharedFishTankCapacityForTier(CapacityTier);
}

// 连档可行性流程：把一车里的设施行排序后按「当前档往上逐档」核一遍；任一档断链就整串判否。
// 它不改状态，只是把单档判据放大到一整车，好让「同一车里买两档」也能在扣钱之前问清楚。
bool ACatFishTankActor::CanApplyCapacityUpgradeSequenceFromAuthority(
	const TArray<int32>& TargetTiers, const FGuid& RequestId) const
{
	if (!HasAuthority() || FishInventory == nullptr)
	{
		return false;
	}
	if (RequestId.IsValid() && CommittedUpgradeRequestIds.Contains(RequestId))
	{
		return true;
	}
	TArray<int32> SortedTiers = TargetTiers;
	SortedTiers.Sort();
	const UCatFishContainerSettings* Settings = GetDefault<UCatFishContainerSettings>();
	if (Settings == nullptr)
	{
		return false;
	}
	int32 PreviousCapacity = Settings->GetSharedFishTankCapacityForTier(CapacityTier);
	for (int32 Index = 0; Index < SortedTiers.Num(); ++Index)
	{
		const int32 ExpectedTier = CapacityTier + 1 + Index;
		const int32 TargetCapacity = Settings->GetSharedFishTankCapacityForTier(ExpectedTier);
		if (SortedTiers[Index] != ExpectedTier || TargetCapacity <= 0 || TargetCapacity <= PreviousCapacity)
		{
			return false;
		}
		PreviousCapacity = TargetCapacity;
	}
	return true;
}

// 升级提交流程：复用同一套前置，再写档位与正式库存槽位数，最后刷新只读摘要；容量只升不降，失败不部分生效。
bool ACatFishTankActor::ApplyCapacityUpgradeFromAuthority(const int32 TargetTier, const FGuid& RequestId, const bool bPublish)
{
	if (!CanApplyCapacityUpgradeFromAuthority(TargetTier, RequestId))
	{
		return false;
	}
	if (RequestId.IsValid() && CommittedUpgradeRequestIds.Contains(RequestId))
	{
		return true; // 同号重放：首次那趟已经升过档，这里不再动容量。
	}
	const int32 PreviousTier = CapacityTier;
	const int32 PreviousCapacity = FishInventory->GetInventorySlotCount();
	CapacityTier = TargetTier;
	const int32 NewCapacity = ResolveSlotCapacityForTier(CapacityTier);
	if (NewCapacity <= PreviousCapacity)
	{
		CapacityTier = PreviousTier;
		return false;
	}
	if (RequestId.IsValid())
	{
		CommittedUpgradeRequestIds.Add(RequestId);
	}
	if (!bPublish)
	{
		TArray<FCatInventoryEntry> Entries = FishInventory->GetInventoryEntries();
		Entries.SetNum(NewCapacity);
		FishInventory->NumSlots = NewCapacity;
		verify(FishInventory->ReplaceInventoryEntriesFromAuthority(Entries, NewCapacity, false));
		return true;
	}
	FishInventory->SetInventorySlotCountFromAuthority(NewCapacity);
	if (WorldInfo)
	{
		WorldInfo->RefreshSummary();
	}
	ForceNetUpdate();
	UE_LOG(LogCatFishContainers, Log,
		TEXT("Event=fish_tank_capacity_upgraded Tank=%s PreviousTier=%d Tier=%d PreviousCapacity=%d Capacity=%d"),
		*GetNameSafe(this), PreviousTier, CapacityTier, PreviousCapacity, NewCapacity);
	return true;
}

// 鱼缸销毁流程：服务器只回收当前库存条目所保留的一对一隐藏世界鱼，先断开实例引用再销毁 Actor，防止容器拆除后留下不可见可复制的鱼载体。
void ACatFishTankActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (FishInventory && FishInventoryChangedHandle.IsValid())
	{
		FishInventory->OnInventoryObservedChanged.Remove(FishInventoryChangedHandle);
		FishInventoryChangedHandle.Reset();
	}
	if (HasAuthority() && FishInventory)
	{
		for (const FCatInventoryEntry& Entry : FishInventory->GetInventoryEntries())
		{
			if (Entry.StackCount == 1 && Entry.Instance)
			{
				if (AActor* RetainedActor = Entry.Instance->GetWorldActor())
				{
					Entry.Instance->SetWorldActor(nullptr);
					RetainedActor->Destroy();
				}
			}
		}
	}
	Super::EndPlay(EndPlayReason);
}

// 可交互判断流程：读取交互开关、请求 Controller 和正式鱼库存组件；任一条件缺失都拒绝，让提示和执行入口保持同一套可用性边界。
bool ACatFishTankActor::CanInteract_Implementation(AController* RequestingController) const
{
	const APlayerController* PlayerController = Cast<APlayerController>(RequestingController);
	return bInteractionEnabled && PlayerController && FishInventory && TankInteraction;
}

// 提示文本流程：复用交互可用性的核心边界；禁用或库存组件缺失时返回空文本，避免玩家看到当前不可执行的鱼缸提示。
FText ACatFishTankActor::GetInteractionPrompt_Implementation() const
{
	return bInteractionEnabled && FishInventory ? InteractionPrompt : FText::GetEmpty();
}

// 距离读取流程：把编辑器配置的厘米值裁成非负有限数；异常值按 0 处理，让服务器距离复核保守失败。
double ACatFishTankActor::GetInteractionRadius_Implementation() const
{
	return FMath::IsFinite(InteractionRadiusCentimeters)
		? FMath::Max(0.0, InteractionRadiusCentimeters) : 0.0;
}

// 鱼缸交互流程：本地开现有库存页；客户端转发现有交互RPC，服务器把嘴部原鱼交给本缸的正式库存。
// T24：旧入口仅开UI，依赖通用Move入缸；现沿同一嘴鱼Store事务，不另建鱼数组或库存直转路径。
bool ACatFishTankActor::Interact_Implementation(AController* RequestingController, const FGuid RequestId)
{
	auto* PlayerController = Cast<ACatfishingPlayerController>(RequestingController);
	auto* Character = PlayerController ? Cast<ACatCharacter>(PlayerController->GetPawn()) : nullptr;
	if (!RequestId.IsValid() || !PlayerController || !CanInteract_Implementation(RequestingController)
		|| CatInventoryAccessRules::ResolveReachableFishContainer(this, Character) != FishInventory)
	{
		UE_LOG(LogCatFishContainers, Warning, TEXT("Event=fish_tank_interaction_rejected RequestId=%s Tank=%s Player=%s Reason=InvalidRequestOrReach World=%s NetMode=%d Authority=%d LocalRole=%d"),
			*RequestId.ToString(), *GetName(), *GetNameSafe(Character), *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole());
		return false;
	}
	const bool bOpened = PlayerController->IsLocalController()
		&& TankInteraction->OpenInventoryForPlayer(PlayerController);
	if (!HasAuthority())
	{
		PlayerController->ServerRequestInteraction(this, RequestId);
		return true;
	}
	if (ACatFishPickupActor* Fish = ACatFishPickupActor::FindCarriedFish(Character))
	{
		return Fish->StoreInFishGuardFromAuthority(PlayerController, RequestId, this).Command.bCommitted;
	}
	return bOpened || Character != nullptr;
}

// 鱼库存读取流程：返回本 Actor 持有的正式鱼库存组件；调用者继续通过 InventoryComponent 命令写入。
UCatFishOnlyInventoryComponent* ACatFishTankActor::GetFishInventoryComponent() const
{
	return FishInventory;
}

// 组件读取流程：返回本鱼缸自带的交互组件；调用方不能通过 Actor 绕过组件去直接写库存。
UCatFishTankInteractionComponent* ACatFishTankActor::GetTankInteraction() const
{
	return TankInteraction;
}

// 缸内清单读取流程：按槽位顺序遍历正式库存，只取数量为一的鱼实例并投影三个公开字段。
// 空槽、非鱼实例、鱼种 ID 缺失或重量非法的条目一律跳过——表现层宁可少生成一条鱼，也不要拿伪造值去缩放。
TArray<FCatFishTankOccupant> ACatFishTankActor::GetTankOccupants() const
{
	TArray<FCatFishTankOccupant> Occupants;
	if (!FishInventory)
	{
		return Occupants;
	}
	for (const FCatInventoryEntry& Entry : FishInventory->GetInventoryEntries())
	{
		const UCatFishInventoryItemInstance* Fish = Entry.StackCount == 1
			? Cast<UCatFishInventoryItemInstance>(Entry.Instance) : nullptr;
		if (!IsValid(Fish))
		{
			continue;
		}
		const FName FishDefinitionId = Fish->GetItemDefinitionId();
		const double WeightKilograms = Fish->GetFishWeightKilograms();
		if (FishDefinitionId.IsNone() || !FMath::IsFinite(WeightKilograms) || WeightKilograms <= 0.0)
		{
			continue;
		}
		FCatFishTankOccupant& Occupant = Occupants.AddDefaulted_GetRef();
		Occupant.FishInstanceId = Fish->GetItemInstanceId();
		Occupant.FishDefinitionId = FishDefinitionId;
		Occupant.WeightKilograms = WeightKilograms;
	}
	return Occupants;
}

// 容器身份读取流程：鱼缸目前不经鱼容器服务注册，返回服务器入场时分配并复制下来的一局 ID；
// 服务器与客户端同源、跨帧稳定，足够表现层区分「这条游动的鱼属于哪口缸」。
FGuid ACatFishTankActor::GetTankContainerId() const
{
	return TankContainerId;
}

// 变化转发流程：库存已经把一次完整变化写进自己的快照，这里只把通知转成蓝图形式，不做差异计算。
void ACatFishTankActor::HandleFishInventoryChanged()
{
	OnTankOccupantsChanged.Broadcast();
}
