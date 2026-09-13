#include "ShopEconomy/CatShopKioskActor.h"

#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/CatInteractionSettings.h"
#include "UI/Shop/CatShopInteractionComponent.h"
#include "ShopEconomy/CatShopInventoryComponent.h"

// 构造流程：摊位不参与逐帧逻辑，先关闭 Tick 并开启复制，再建立空间根、查询碰撞、页面打开组件和货架库存组件；营地绑定仍不保存在摊位上。
ACatShopKioskActor::ACatShopKioskActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	InteractionCollision = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionCollision"));
	InteractionCollision->SetupAttachment(SceneRoot);
	InteractionCollision->SetSphereRadius(75.0f);
	InteractionCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionCollision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractionCollision->SetGenerateOverlapEvents(false);
	ShopInteraction = CreateDefaultSubobject<UCatShopInteractionComponent>(TEXT("ShopInteraction"));
	ShopInventory = CreateDefaultSubobject<UCatShopInventoryComponent>(TEXT("ShopInventory"));
	InteractionPrompt = NSLOCTEXT("Catfishing", "ShopKioskInteractionPrompt", "打开商店");
}

void ACatShopKioskActor::BeginPlay()
{
	Super::BeginPlay();
	// 启动流程：读取项目交互 Trace Channel 并只更新查询碰撞响应；摊位位置和仓库归属仍由关卡 Actor 与服务器订单链路裁决。
	if (const UCatInteractionSettings* Settings = GetDefault<UCatInteractionSettings>(); Settings && InteractionCollision)
	{
		InteractionCollision->SetCollisionResponseToChannel(Settings->TargetingTraceChannel, ECR_Block);
	}
}

bool ACatShopKioskActor::CanInteract_Implementation(AController* RequestingController) const
{
	// 交互 gate 流程：只允许本地玩家在页面未打开时进入 UI；营地和公共仓库留到服务端订单提交时检查。
	const APlayerController* PlayerController = Cast<APlayerController>(RequestingController);
	return bInteractionEnabled && PlayerController && PlayerController->IsLocalController()
		&& ShopInteraction && !ShopInteraction->IsShopOpen();
}

FText ACatShopKioskActor::GetInteractionPrompt_Implementation() const
{
	// 提示读取流程：复用交互开关和页面状态决定是否展示文案；不可交互时返回空文本防止提示残留。
	return bInteractionEnabled && ShopInteraction && !ShopInteraction->IsShopOpen()
		? InteractionPrompt : FText::GetEmpty();
}

double ACatShopKioskActor::GetInteractionRadius_Implementation() const
{
	// 半径读取流程：把编辑器配置裁到非负有限值；本地准星提示与交互检测继续使用同一边界。
	return FMath::IsFinite(InteractionRadiusCentimeters)
		? FMath::Max(0.0, InteractionRadiusCentimeters) : 0.0;
}

bool ACatShopKioskActor::Interact_Implementation(AController* RequestingController, const FGuid RequestId)
{
	// 交互执行流程：先确认请求与本地 gate 有效，再只打开商店 UI；购买、扣款和发货必须走后续服务器 RPC。
	APlayerController* PlayerController = Cast<APlayerController>(RequestingController);
	return RequestId.IsValid() && CanInteract_Implementation(RequestingController)
		&& ShopInteraction->OpenShopForPlayer(PlayerController);
}

// 组件读取流程：返回当前 Actor 自带的交互组件；调用方不能通过 Actor 绕过组件的交互 gate。
UCatShopInteractionComponent* ACatShopKioskActor::GetShopInteraction() const
{
	return ShopInteraction;
}

// 库存组件读取流程：返回摊位自身持有的货架库存组件；订单链路用它把 EntryId 解释到当前摊位，而不是全局商店表。
UCatShopInventoryComponent* ACatShopKioskActor::GetShopInventory() const
{
	return ShopInventory;
}

// 墓碑（2026-09-13，09-09 裁决）：删除下单时的摊位距离二次证明；交互半径与开页入口保留。
