#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UI/CatUIModalInputMode.h"
#include "UI/Shop/CatShopTypes.h"
#include "CatShopPageController.generated.h"

class APlayerController;
class UCatShopModel;
class UCatShopWidget;

/** 商店页面请求关闭后的通知；拥有它的交互对象组件用这个事件清理实例。 */
DECLARE_MULTICAST_DELEGATE(FCatShopPageCloseRequested);

/** 商店 PageController；它把 Shop Widget 意图翻译成 PlayerController RPC，并管理本次打开的模态输入焦点。 */
UCLASS()
class CATFISHING_API UCatShopPageController : public UObject
{
	GENERATED_BODY()

public:
	/** 绑定本次打开商店的 Controller、Model 和 View；成功后订阅 Model/View 并渲染首帧。 */
	bool Bind(APlayerController* InController, UCatShopModel* InModel, UCatShopWidget* InView);

	/** 关闭页面、恢复输入模式、解绑 Model/View 广播并清空弱引用。 */
	void Unbind();

	/** 打开商店页面到视口并应用模态输入焦点；重复调用保持幂等。 */
	void OpenShop();

	/** 关闭商店页面并释放模态输入锁；是否销毁对象由交互组件决定。 */
	void CloseShop();

	/** 本次商店页面打开状态的查询入口；只读取 PageController 状态，避免从 Widget 可见性反推第二份真相。 */
	bool IsShopOpen() const;

	/** PageController 请求关闭通知；交互对象组件订阅它做最终销毁。 */
	FCatShopPageCloseRequested OnPageCloseRequested;

private:
	/** Model 投影变化入口；只把最新 ViewState 交给 Shop WBP。 */
	void HandleModelViewStateChanged();

	/** View 关闭意图入口；关闭页面后通知拥有组件。 */
	void HandleViewCloseRequested();

	/** View 商品动作入口；验证当前条目后提交正式购买或免费领取 RPC。 */
	void HandleViewEntryActionRequested(FName EntryId, ECatShopUIAction Action);

	/** 打开和关闭时应用模态 UI 输入锁；不安装 MappingContext 或硬写按键。 */
	void ApplyShopInputMode(bool bOpen);

	/** 当前打开商店的 Controller；商店 UI 不属于 LocalPlayer 预建根，只属于本次交互。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> BoundPlayerController;

	/** 当前商店 Model；它只读公开经济和商品目录。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatShopModel> BoundModel;

	/** 当前商店 View；它只展示商品、公款和点击意图。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatShopWidget> BoundView;

	/** Model 投影变化订阅句柄；Unbind 必须从同一 Model 移除，避免旧页面继续渲染。 */
	FDelegateHandle ModelViewChangedHandle;

	/** View 关闭意图订阅句柄；Unbind 必须从同一 View 移除，避免销毁期按钮重复关闭。 */
	FDelegateHandle ViewCloseHandle;

	/** View 商品动作意图订阅句柄；Unbind 必须从同一 View 移除，避免旧商品按钮迟到下单。 */
	FDelegateHandle ViewEntryActionHandle;

	/** 商店打开期间的模态输入恢复记录；关闭页面时用它撤销本页面的移动/视角锁和鼠标状态。 */
	FCatUIModalInputModeState ModalInputModeState;

	/** 当前商店页面是否由本 PageController 打开；OpenShop/CloseShop 是唯一写者，Widget 可见性不反推它。 */
	bool bShopOpen = false;
};
