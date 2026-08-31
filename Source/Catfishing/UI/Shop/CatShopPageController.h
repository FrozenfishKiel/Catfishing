#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "UObject/Object.h"
#include "UI/CatUIModalInputMode.h"
#include "UI/Shop/CatShopTypes.h"
#include "CatShopPageController.generated.h"

class APlayerController;
class ACatShopKioskActor;
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
	/** 绑定本次打开商店的 Controller、Model、View 和来源摊位；成功后订阅 Model/View 并渲染首帧。 */
	bool Bind(APlayerController* InController, UCatShopModel* InModel, UCatShopWidget* InView,
		ACatShopKioskActor* InSourceShop);

	/** 关闭页面、恢复输入模式、解绑 Model/View 广播并清空弱引用；视口移除只处理仍挂在视口中的 View。 */
	void Unbind();

	/** 打开商店页面到视口并应用模态输入焦点；重复调用保持幂等。 */
	void OpenShop();

	/** 关闭商店页面并释放模态输入锁；只在 View 仍挂视口时移除，是否销毁对象由交互组件决定。 */
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

	/** View 商品加购意图入口；验证当前条目后只修改本地购物车，不提交服务器购买。 */
	void HandleViewAddEntryToCartRequested(FName EntryId);

	/** View 购物车删除意图入口；每次从本地购物车删掉一份指定商品。 */
	void HandleViewRemoveCartLineRequested(FName EntryId);

	/** View 支付意图入口；把当前本地购物车转换成正式服务器 RPC。 */
	void HandleViewPayCartRequested();

	/** 服务器购物车结果入口；只处理本页提交的 RequestId，失败时购物车保留，成功时清空本地队列。 */
	void HandleCampCommandResultReceived(const FCatDomainCommandResult& Result);

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

	/** 当前商店页面来源摊位；订单 RPC 只提交它做距离证明，公共仓库由服务器全图询问营地。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ACatShopKioskActor> BoundSourceShop;

	/** Model 投影变化订阅句柄；Unbind 必须从同一 Model 移除，避免旧页面继续渲染。 */
	FDelegateHandle ModelViewChangedHandle;

	/** View 关闭意图订阅句柄；Unbind 必须从同一 View 移除，避免销毁期按钮重复关闭。 */
	FDelegateHandle ViewCloseHandle;

	/** View 商品加购意图订阅句柄；Unbind 必须从同一 View 移除，避免旧商品按钮迟到改购物车。 */
	FDelegateHandle ViewAddEntryHandle;

	/** View 购物车删除意图订阅句柄；Unbind 必须从同一 View 移除，避免旧垃圾桶迟到改购物车。 */
	FDelegateHandle ViewRemoveCartLineHandle;

	/** View 支付意图订阅句柄；Unbind 必须从同一 View 移除，避免旧支付按钮迟到提交。 */
	FDelegateHandle ViewPayCartHandle;

	/** 服务器领域结果订阅句柄；只在本次商店页生命周期内监听，关闭时必须解除。 */
	FDelegateHandle CampCommandResultHandle;

	/** 本页最近一次已提交但尚未由服务器结果或经济快照收束的商店请求；只用于匹配回包，不参与权威结算。 */
	FGuid PendingShopRequestId;

	/** 本页最近一次 pending 请求的动作类型；服务器失败回包要用它恢复支付反馈。 */
	ECatShopUIAction PendingShopAction = ECatShopUIAction::None;

	/** 本页最近一次 pending 请求关联的商品条目；购物车支付保持 None，兼容本地拒绝提示接口。 */
	FName PendingShopEntryId = NAME_None;

	/** 商店打开期间的模态输入恢复记录；关闭页面时用它撤销本页面的移动/视角锁和鼠标状态。 */
	FCatUIModalInputModeState ModalInputModeState;

	/** 当前商店页面是否由本 PageController 打开；OpenShop/CloseShop 是唯一写者，Widget 可见性不反推它。 */
	bool bShopOpen = false;
};
