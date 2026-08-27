#include "Items/CatFishGuardActor.h"

#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/CatInteractionSettings.h"
#include "Items/CatContainerReplicationComponent.h"
#include "Items/CatItemsService.h"
#include "Items/CatItemsSettings.h"
#include "Logging/CatLog.h"
#include "Net/UnrealNetwork.h"
#include "UI/CatLocalPlayerUISubsystem.h"

// 构造流程：创建独立箱子根、Items 复制出口和交互入口；开启 Actor 复制并关闭 Tick，鱼数组只由 Items 服务发布。
ACatFishGuardActor::ACatFishGuardActor()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = false;
	GuardRoot = CreateDefaultSubobject<USceneComponent>(TEXT("GuardRoot"));
	SetRootComponent(GuardRoot);
	InteractionCollision = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionCollision"));
	InteractionCollision->SetupAttachment(GuardRoot);
	InteractionCollision->SetSphereRadius(75.0f);
	InteractionCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionCollision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractionCollision->SetGenerateOverlapEvents(false);
	ContainerReplication = CreateDefaultSubobject<UCatContainerReplicationComponent>(TEXT("ContainerReplication"));
	InteractionPrompt = NSLOCTEXT("Catfishing", "FishGuardInteractionPrompt", "打开鱼护");
}

// 复制注册流程：Actor 只复制容器 ID；鱼槽数组继续由 ContainerReplication 的 FastArray 负责。
void ACatFishGuardActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, GuardContainerId);
}

// BeginPlay 流程：服务器为这个世界箱子生成稳定容器 ID 并注册 FishGuard；客户端只等待复制组件收到快照。
void ACatFishGuardActor::BeginPlay()
{
	Super::BeginPlay();
	if (const UCatInteractionSettings* Settings = GetDefault<UCatInteractionSettings>(); Settings && InteractionCollision)
	{
		InteractionCollision->SetCollisionResponseToChannel(Settings->TargetingTraceChannel, ECR_Block);
	}
	if (!HasAuthority())
	{
		return;
	}
	if (!GuardContainerId.IsValid())
	{
		GuardContainerId = FGuid::NewGuid();
	}
	if (RegisterContainerFromAuthority())
	{
		ForceNetUpdate();
	}
}

// 结束流程：authority 按本鱼护箱子的复制组件精确注销 Items 容器，再交给父类销毁组件和复制引用。
void ACatFishGuardActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority() && bRegisteredWithItems)
	{
		if (UCatItemsService* Items = GetWorld() ? GetWorld()->GetSubsystem<UCatItemsService>() : nullptr)
		{
			Items->UnregisterContainer(ContainerReplication);
		}
		bRegisteredWithItems = false;
	}
	Super::EndPlay(EndPlayReason);
}

// 注册流程：读取 FishGuard 配置容量并交给 Items 注册；鱼护对象只提供箱子宿主，不直接改鱼数组。
bool ACatFishGuardActor::RegisterContainerFromAuthority()
{
	if (!HasAuthority() || bRegisteredWithItems || !ContainerReplication || !GuardContainerId.IsValid()
		|| !GetWorld())
	{
		return bRegisteredWithItems;
	}
	UCatItemsService* Items = GetWorld()->GetSubsystem<UCatItemsService>();
	if (!Items)
	{
		return false;
	}
	const int32 Capacity = GetDefault<UCatItemsSettings>()->GetContainerCapacity(
		static_cast<uint8>(ECatContainerKind::FishGuard));
	bRegisteredWithItems = Items->RegisterContainer(ContainerReplication, GuardContainerId,
		ECatContainerKind::FishGuard, FString(), Capacity);
	if (bRegisteredWithItems)
	{
		UE_LOG(LogCatItems, Log, TEXT("Event=fish_guard_registered Guard=%s Container=%s Capacity=%d"),
			*GetNameSafe(this),
			*GuardContainerId.ToString(EGuidFormats::DigitsWithHyphens),
			Capacity);
	}
	else
	{
		UE_LOG(LogCatItems, Warning, TEXT("Event=fish_guard_register_rejected Guard=%s Container=%s Capacity=%d"),
			*GetNameSafe(this),
			*GuardContainerId.ToString(EGuidFormats::DigitsWithHyphens),
			Capacity);
	}
	return bRegisteredWithItems;
}

// 容器 ID 读取流程：直接返回服务器 BeginPlay 后的稳定 ID；空值表示这只鱼护箱子尚未完成注册。
FGuid ACatFishGuardActor::GetGuardContainerId() const
{
	return GuardContainerId;
}

// 复制组件读取流程：返回 Items 的只读复制出口；调用者不能通过它提交鱼、移动鱼或修改容量。
UCatContainerReplicationComponent* ACatFishGuardActor::GetContainerReplicationComponent() const
{
	return ContainerReplication;
}

bool ACatFishGuardActor::CanInteract_Implementation(AController* RequestingController) const
{
	const APlayerController* PlayerController = Cast<APlayerController>(RequestingController);
	return bInteractionEnabled && PlayerController && PlayerController->IsLocalController()
		&& GuardContainerId.IsValid() && ContainerReplication;
}

FText ACatFishGuardActor::GetInteractionPrompt_Implementation() const
{
	return bInteractionEnabled && GuardContainerId.IsValid() ? InteractionPrompt : FText::GetEmpty();
}

double ACatFishGuardActor::GetInteractionRadius_Implementation() const
{
	return FMath::IsFinite(InteractionRadiusCentimeters)
		? FMath::Max(0.0, InteractionRadiusCentimeters) : 0.0;
}

// 鱼护交互只在本地打开“随身背包 + 地面鱼护”双容器视图；鱼移动仍通过 Inventory 的服务器 Items 事务完成。
bool ACatFishGuardActor::Interact_Implementation(AController* RequestingController, const FGuid RequestId)
{
	APlayerController* PlayerController = Cast<APlayerController>(RequestingController);
	ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	UCatLocalPlayerUISubsystem* UISubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<UCatLocalPlayerUISubsystem>() : nullptr;
	if (!RequestId.IsValid() || !CanInteract_Implementation(RequestingController) || !UISubsystem)
	{
		UE_LOG(LogCatItems, Warning,
			TEXT("Event=fish_guard_interaction_rejected Reason=DependencyUnavailable Controller=%s Guard=%s"),
			*GetNameSafe(PlayerController), *GetNameSafe(this));
		return false;
	}
	TArray<UCatContainerReplicationComponent*> ExternalContainers;
	ExternalContainers.Add(ContainerReplication);
	UISubsystem->OpenInventoryWithExternalContainerContexts(ExternalContainers);
	const FCatContainerSnapshot& Snapshot = ContainerReplication->GetSnapshot();
	UE_LOG(LogCatItems, Log,
		TEXT("Event=fish_guard_interaction_inventory_opened Guard=%s Container=%s Revision=%lld"),
		*GetNameSafe(this), *Snapshot.ContainerId.ToString(EGuidFormats::DigitsWithHyphens), Snapshot.Revision);
	return true;
}
