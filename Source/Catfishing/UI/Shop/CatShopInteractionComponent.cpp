#include "UI/Shop/CatShopInteractionComponent.h"

#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "Logging/CatLog.h"
#include "ShopEconomy/CatShopInventoryComponent.h"
#include "ShopEconomy/CatShopKioskActor.h"
#include "UI/Shop/CatShopModel.h"
#include "UI/Shop/CatShopPageController.h"
#include "UI/Shop/CatShopWidget.h"

// 构造流程：商店交互组件不需要 Tick；它只在交互系统调用打开/关闭时创建临时 UI 实例，并把页面类保存在摊位组件上。
UCatShopInteractionComponent::UCatShopInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	ShopWidgetClass = TSoftClassPtr<UCatShopWidget>(
		FSoftClassPath(TEXT("/Game/UI/Shop/WBP_CatShop.WBP_CatShop_C")));
}

// 结束流程：交互对象或 World 销毁时先关闭商店 UI，再交还 ActorComponent 生命周期；重复关闭保持幂等。
void UCatShopInteractionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseShop();
	Super::EndPlay(EndPlayReason);
}

// 打开流程：
// 1. 关闭旧实例，避免同一个商店对象同时拥有两套 UI。
// 2. 读取拥有本组件的商店摊位和它自己的库存组件；组件挂错对象时不打开。
// 3. 从本组件加载正式商店 WBP；缺失时 fail-closed，不再回退全局 UI Settings 或原生白盒替身。
// 4. 创建 Model/PageController/View 并把摊位库存传给 Model，最后由 PageController 入视口和切输入模式。
bool UCatShopInteractionComponent::OpenShopForPlayer(APlayerController* PlayerController)
{
	CloseShop();
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return false;
	}
	ACatShopKioskActor* SourceShop = Cast<ACatShopKioskActor>(GetOwner());
	if (!SourceShop)
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_shop_owner_invalid Owner=%s"),
			*GetNameSafe(GetOwner()));
		return false;
	}
	UCatShopInventoryComponent* ShopInventory = SourceShop->GetShopInventory();
	if (!ShopInventory)
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_shop_inventory_missing Owner=%s"),
			*GetNameSafe(GetOwner()));
		return false;
	}
	const TSubclassOf<UCatShopWidget> LoadedShopWidgetClass = LoadShopWidgetClass();
	if (!LoadedShopWidgetClass)
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_shop_widget_class_missing Owner=%s"),
			*GetNameSafe(GetOwner()));
		return false;
	}

	ShopModel = NewObject<UCatShopModel>(this);
	ShopPageController = NewObject<UCatShopPageController>(this);
	ShopWidget = CreateWidget<UCatShopWidget>(PlayerController, LoadedShopWidgetClass);
	if (!ShopModel || !ShopPageController || !ShopWidget)
	{
		CloseShop();
		return false;
	}
	if (!ShopModel->Bind(PlayerController, ShopInventory)
		|| !ShopPageController->Bind(PlayerController, ShopModel, ShopWidget, SourceShop))
	{
		CloseShop();
		return false;
	}
	PageCloseHandle = ShopPageController->OnPageCloseRequested.AddUObject(
		this, &ThisClass::HandleShopPageCloseRequested);
	ShopPageController->OpenShop();
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_shop_opened Owner=%s Controller=%s RootClass=%s"),
		*GetNameSafe(GetOwner()),
		*GetNameSafe(PlayerController),
		*GetNameSafe(ShopWidget->GetClass()));
	return true;
}

// 关闭流程：先让 PageController 成对恢复输入并处理视口；组件最后只做带视口检查的兜底清理，避免警告也避免残留。
void UCatShopInteractionComponent::CloseShop()
{
	if (ShopPageController)
	{
		ShopPageController->OnPageCloseRequested.Remove(PageCloseHandle);
		ShopPageController->Unbind();
		ShopPageController = nullptr;
	}
	PageCloseHandle.Reset();
	if (ShopModel)
	{
		ShopModel->Unbind();
		ShopModel = nullptr;
	}
	if (ShopWidget)
	{
		if (ShopWidget->IsInViewport())
		{
			ShopWidget->RemoveFromParent();
		}
		ShopWidget = nullptr;
	}
}

// 状态读取流程：只信任 PageController 的打开状态；空实例固定返回 false。
bool UCatShopInteractionComponent::IsShopOpen() const
{
	return ShopPageController ? ShopPageController->IsShopOpen() : false;
}

// 页面类加载流程：同步解析本组件软类并验证继承商店 View 基类；失败返回空，调用方保持不打开页面。
TSubclassOf<UCatShopWidget> UCatShopInteractionComponent::LoadShopWidgetClass() const
{
	UClass* LoadedClass = ShopWidgetClass.LoadSynchronous();
	if (!LoadedClass || !LoadedClass->IsChildOf(UCatShopWidget::StaticClass()))
	{
		return nullptr;
	}
	return LoadedClass;
}

// 页面关闭通知流程：玩家点击关闭后由组件统一销毁 Model/PageController/View 三件套。
void UCatShopInteractionComponent::HandleShopPageCloseRequested()
{
	CloseShop();
}
