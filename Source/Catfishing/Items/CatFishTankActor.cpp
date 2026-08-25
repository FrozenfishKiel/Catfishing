#include "Items/CatFishTankActor.h"

#include "Items/CatContainerReplicationComponent.h"
#include "Items/CatItemsService.h"
#include "Items/CatItemsSettings.h"
#include "Components/SceneComponent.h"
#include "Net/UnrealNetwork.h"

// 构造流程：创建固定摆放根，开启 Actor/组件复制并关闭 Tick；鱼数组仅由 Items Service 发布到组件。
ACatFishTankActor::ACatFishTankActor()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = false;
	TankRoot = CreateDefaultSubobject<USceneComponent>(TEXT("TankRoot"));
	SetRootComponent(TankRoot);
	ContainerReplication = CreateDefaultSubobject<UCatContainerReplicationComponent>(TEXT("ContainerReplication"));
}

// 注册流程：服务器为本 World 生成 TankId，并以显式配置容量注册共享容器；客户端只等待 ID 与组件快照复制。
void ACatFishTankActor::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority())
	{
		return;
	}
	if (!TankContainerId.IsValid())
	{
		TankContainerId = FGuid::NewGuid();
	}
	if (UCatItemsService* Items = GetWorld()->GetSubsystem<UCatItemsService>())
	{
		const int32 Capacity = GetDefault<UCatItemsSettings>()->GetContainerCapacity(static_cast<uint8>(ECatContainerKind::SharedFishTank));
		Items->RegisterContainer(ContainerReplication, TankContainerId, ECatContainerKind::SharedFishTank, FString(), Capacity);
	}
}

// 注销流程：authority 先解除复制宿主，随后父类销毁 Actor/组件；服务端已提交记录不会迁移到别的缸。
void ACatFishTankActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		if (UCatItemsService* Items = GetWorld() ? GetWorld()->GetSubsystem<UCatItemsService>() : nullptr)
		{
			Items->UnregisterContainer(ContainerReplication);
		}
	}
	Super::EndPlay(EndPlayReason);
}

// 复制注册流程：保留父类字段并复制稳定容器 ID；鱼内容继续只由组件复制。
void ACatFishTankActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, TankContainerId);
}

// 容器 ID 读取流程：直接返回本 Actor 一局稳定 ID；调用者必须继续让 Items 校验容器存在与 Revision。
FGuid ACatFishTankActor::GetTankContainerId() const
{
	return TankContainerId;
}
