#include "FishContainers/CatFishTankActor.h"

#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "FishContainers/CatFishTankInteractionComponent.h"
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
		FishInventory->SetInventorySlotCountFromAuthority(FishInventorySlotCapacity);
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

// 鱼缸交互流程：本地把鱼缸正式库存作为外部库存打开；服务器交互转发只确认本 Actor 当下仍可交互。
bool ACatFishTankActor::Interact_Implementation(AController* RequestingController, const FGuid RequestId)
{
	APlayerController* PlayerController = Cast<APlayerController>(RequestingController);
	return RequestId.IsValid() && CanInteract_Implementation(RequestingController)
		&& TankInteraction->OpenInventoryForPlayer(PlayerController);
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
