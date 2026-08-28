#pragma once

#include "CoreMinimal.h"
#include "UI/Shop/CatShopTypes.h"
#include "UObject/Object.h"
#include "CatShopModel.generated.h"

class APlayerController;
class ACatfishingGameState;
class UCatShopInventoryComponent;

/** 商店 Model 完整投影变化通知；PageController 收到后只把 ViewState 交给 Shop WBP。 */
DECLARE_MULTICAST_DELEGATE(FCatShopModelChanged);

/** 商店 MVC Model；它只读 GameState 经济快照和来源摊位库存组件，不创建 Widget、不提交订单。 */
UCLASS()
class CATFISHING_API UCatShopModel : public UObject
{
	GENERATED_BODY()

public:
	/** 绑定当前要打开商店的 Controller 和摊位库存；成功后订阅 GameState 商店快照并发布首份投影。 */
	bool Bind(APlayerController* InController, UCatShopInventoryComponent* InShopInventory);

	/** 解除 GameState 订阅并清空当前商品、公款和 pending 投影。 */
	void Unbind();

	/** 写入商店打开状态并刷新投影；打开来源仍归交互对象组件所有。 */
	void SetOpen(bool bOpen);

	/** 标记 PageController 已经提交购买或领取 RPC；等待经济/货架快照刷新时禁用重复动作。 */
	void MarkActionSubmitted(ECatShopUIAction Action, FName EntryId);

	/** 标记一次商店动作被拒绝；本地校验和服务器回包都会用它恢复按钮状态并展示原因。 */
	void MarkActionRejected(ECatShopUIAction Action, FName EntryId, FText Reason);

	/** 主动重读当前公开经济/货架快照和配置目录；外部只读事实变化都收敛到这里。 */
	void Refresh();

	/** 商店只读投影的查询入口；调用方拿到最近缓存副本，避免写回团队公款或商品目录。 */
	const FCatShopViewState& GetViewState() const;

	/** 按 EntryId 查找最近投影中的商品行；PageController 用它确认点击来自当前目录。 */
	bool TryFindEntryView(FName EntryId, FCatShopEntryView& OutEntry) const;

	/** 商店 ViewState 变化通知；Bind、Refresh、pending 和本地拒绝都会触发。 */
	FCatShopModelChanged OnViewStateChanged;

private:
	/** GameState 商店快照变化入口；刷新公款、商品可用性和 pending 提示。 */
	void HandleShopEconomySnapshotChanged();

	/** 摊位库存身份复制入口；刷新当前候选商品和公开货架快照的匹配关系。 */
	void HandleShopInventoryIdentityChanged();

	/** 按摊位目录和公开经济/货架快照生成一条商品展示投影；只推导玩家可见余额/库存状态，不读取商店写口。 */
	FCatShopEntryView MakeEntryView(const FCatShopCatalogEntry& Entry,
		const FCatShopPublicEconomySnapshot& Economy, bool bEconomyAvailable) const;

	/** 当前打开商店的 Controller 弱引用；只用于定位 World/GameState，不拥有 UI 生命周期。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> BoundPlayerController;

	/** 当前公开经济快照来源；客户端从 GameState 读，服务器本机也走同一投影。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ACatfishingGameState> BoundGameState;

	/** 当前页面对应的摊位库存组件；商品候选和免费白名单都从它读取，不再从全局 Settings 猜。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatShopInventoryComponent> BoundShopInventory;

	/** GameState 商店快照订阅句柄；Unbind 必须从同一个 GameState 移除。 */
	FDelegateHandle ShopEconomyChangedHandle;

	/** 摊位库存身份订阅句柄；Unbind 必须从同一个库存组件移除，避免页面关闭后继续刷新。 */
	FDelegateHandle ShopInventoryIdentityChangedHandle;

	/** 当前商店窗口是否打开的交互组件投影；Model 不从 Widget 可见性反推。 */
	bool bOpen = false;

	/** 当前是否已有购买或领取请求提交后等待商店快照同步。 */
	bool bActionPending = false;

	/** 最近一次提交或本地拒绝的动作类型。 */
	ECatShopUIAction LastAction = ECatShopUIAction::None;

	/** 最近一次提交或本地拒绝的商品目录 ID。 */
	FName LastEntryId = NAME_None;

	/** 最近一次拒绝原因；本地校验失败和服务器未扣款失败都会写入它，下一次成功提交会清空。 */
	FText LastRejectedReason;

	/** 最近发布给 View 的完整商店投影；所有刷新都先写这里再广播。 */
	FCatShopViewState ViewState;
};
