#include "ShopEconomy/CatShopKioskActor.h"
#include "ShopEconomy/CatShopEconomyService.h"
#include "Framework/Game/CatfishingGameState.h"

#include "Components/SceneComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "UI/Shop/CatShopInteractionComponent.h"
#include "ShopEconomy/CatShopInventoryComponent.h"

// 构造流程：摊位不参与逐帧逻辑，先关闭 Tick 并开启复制，再建立空间根、页面打开组件和货架库存组件。
// 准星命中由正式蓝图的可见模型承担，避免无模型的球体把空白空间误判为可交互目标；营地绑定仍不保存在摊位上。
ACatShopKioskActor::ACatShopKioskActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	ShopInteraction = CreateDefaultSubobject<UCatShopInteractionComponent>(TEXT("ShopInteraction"));
	ShopInventory = CreateDefaultSubobject<UCatShopInventoryComponent>(TEXT("ShopInventory"));
	InteractionPrompt = NSLOCTEXT("Catfishing", "ShopKioskInteractionPrompt", "打开商店");
}

// 营业状态读取：服务器读取交易服务，客户端读取复制快照；世界或依赖尚未就绪时拒绝开店。
bool ACatShopKioskActor::IsShopTradingOpen() const
{
	if (!GetWorld()) return false;
	if (HasAuthority())
	{
		const auto* Shop = GetWorld()->GetSubsystem<UCatShopEconomyService>();
		return Shop && Shop->AreCommandsOpen();
	}
	const auto* State = GetWorld()->GetGameState<ACatfishingGameState>();
	return State && State->GetShopEconomySnapshot().bCommandsOpen;
}

bool ACatShopKioskActor::CanInteract_Implementation(AController* RequestingController) const
{
	// 交互 gate 流程：只允许本地玩家在页面未打开时进入 UI；营地和公共仓库留到服务端订单提交时检查。
	const APlayerController* PlayerController = Cast<APlayerController>(RequestingController);
	return IsShopTradingOpen() && bInteractionEnabled && PlayerController && PlayerController->IsLocalController()
		&& ShopInteraction && !ShopInteraction->IsShopOpen();
}

FText ACatShopKioskActor::GetInteractionPrompt_Implementation() const
{
	// 提示读取流程：复用交互开关和页面状态决定是否展示文案；不可交互时返回空文本防止提示残留。
	return IsShopTradingOpen() && bInteractionEnabled && ShopInteraction && !ShopInteraction->IsShopOpen()
		? InteractionPrompt : FText::GetEmpty();
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

// 下单资格流程：
// 1. 服务器只让启用中的摊位接受订单，客户端不能拿本地 UI 里的 Actor 指针直接绕过校验。
// 2. 再要求服务器侧 Pawn 仍属于当前 World，拒绝旅行后、销毁后或跨世界的过期请求；不再以另一套半径推翻已经打开的页面。
// 3. 本函数不查营地、不查公共仓库；发货目标由 PlayerController 在当前 World 全图寻找营地后再询问营地接口。
bool ACatShopKioskActor::CanServeOrderFromAuthority(AController* RequestingController) const
{
	if (!HasAuthority() || !bInteractionEnabled || !RequestingController)
	{
		return false;
	}
	APawn* RequestingPawn = RequestingController->GetPawn();
	if (!RequestingPawn || RequestingPawn->GetWorld() != GetWorld())
	{
		return false;
	}
	return true;
}
