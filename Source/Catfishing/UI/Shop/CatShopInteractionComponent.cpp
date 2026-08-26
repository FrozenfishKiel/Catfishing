#include "UI/Shop/CatShopInteractionComponent.h"

#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "Logging/CatLog.h"
#include "UI/CatUISettings.h"
#include "UI/Shop/CatShopModel.h"
#include "UI/Shop/CatShopPageController.h"
#include "UI/Shop/CatShopWidget.h"

// 构造流程：商店交互组件不需要 Tick；它只在交互系统调用打开/关闭时创建临时 UI 实例。
UCatShopInteractionComponent::UCatShopInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	InteractionTargetText = FText::FromString(TEXT("商店"));
	InteractionRadiusCentimeters = 300.0;
}

// 结束流程：交互对象或 World 销毁时先关闭商店 UI，再交还 ActorComponent 生命周期；重复关闭保持幂等。
void UCatShopInteractionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CloseShop();
	Super::EndPlay(EndPlayReason);
}

// 可交互判断流程：复用通用目标 gate，并避免同一个商店对象在已打开时重复创建第二套 UI。
bool UCatShopInteractionComponent::CanInteract_Implementation(APlayerController* PlayerController) const
{
	return Super::CanInteract_Implementation(PlayerController) && !IsShopOpen();
}

// 通用交互流程：LocalPlayer 扫描器只调用这个能力入口；商店组件自己决定创建并拥有商店 UI。
bool UCatShopInteractionComponent::Interact_Implementation(APlayerController* PlayerController)
{
	return OpenShopForPlayer(PlayerController);
}

// 提示文本流程：商店默认显示“商店”；蓝图如果改了 InteractionTargetText，这里会沿用基类文本。
FText UCatShopInteractionComponent::GetInteractionTargetText_Implementation(APlayerController* PlayerController) const
{
	return Super::GetInteractionTargetText_Implementation(PlayerController);
}

// 打开流程：
// 1. 关闭旧实例，避免同一个商店对象同时拥有两套 UI。
// 2. 从 UI Settings 加载正式商店 WBP；缺失时 fail-closed，不创建原生白盒替身。
// 3. 创建 Model/PageController/View 并绑定，最后由 PageController 入视口和切输入模式。
bool UCatShopInteractionComponent::OpenShopForPlayer(APlayerController* PlayerController)
{
	CloseShop();
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return false;
	}
	const UCatUISettings* Settings = GetDefault<UCatUISettings>();
	const TSubclassOf<UCatShopWidget> ShopWidgetClass = Settings ? Settings->LoadShopWidgetClass() : nullptr;
	if (!Settings || !ShopWidgetClass)
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_shop_widget_class_missing Owner=%s"),
			*GetNameSafe(GetOwner()));
		return false;
	}

	ShopModel = NewObject<UCatShopModel>(this);
	ShopPageController = NewObject<UCatShopPageController>(this);
	ShopWidget = CreateWidget<UCatShopWidget>(PlayerController, ShopWidgetClass);
	if (!ShopModel || !ShopPageController || !ShopWidget)
	{
		CloseShop();
		return false;
	}
	if (!ShopModel->Bind(PlayerController) || !ShopPageController->Bind(PlayerController, ShopModel, ShopWidget))
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

// 关闭流程：先解绑 PageController，再解绑 Model 并移除 View；组件保持可重复打开。
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
		ShopWidget->RemoveFromParent();
		ShopWidget = nullptr;
	}
}

// 状态读取流程：只信任 PageController 的打开状态；空实例固定返回 false。
bool UCatShopInteractionComponent::IsShopOpen() const
{
	return ShopPageController ? ShopPageController->IsShopOpen() : false;
}

// 页面关闭通知流程：玩家点击关闭后由组件统一销毁 Model/PageController/View 三件套。
void UCatShopInteractionComponent::HandleShopPageCloseRequested()
{
	CloseShop();
}
