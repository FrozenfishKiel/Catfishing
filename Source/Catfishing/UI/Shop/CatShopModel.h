#pragma once

#include "CoreMinimal.h"
#include "UI/Shop/CatShopTypes.h"
#include "UObject/Object.h"
#include "CatShopModel.generated.h"

class APlayerController;
class ACatfishingGameState;

/** 商店 Model 完整投影变化通知；PageController 收到后只把 ViewState 交给 Shop WBP。 */
DECLARE_MULTICAST_DELEGATE(FCatShopModelChanged);

/** 商店 MVC Model；它只读 GameState 公款快照和 Settings 商品目录，不创建 Widget、不提交订单。 */
UCLASS()
class CATFISHING_API UCatShopModel : public UObject
{
	GENERATED_BODY()

public:
	/** 绑定当前要打开商店的 Controller；成功后订阅 GameState 商店快照并发布首份投影。 */
	bool Bind(APlayerController* InController);

	/** 解除 GameState 订阅并清空当前商品、公款和 pending 投影。 */
	void Unbind();

	/** 写入商店打开状态并刷新投影；打开来源仍归交互对象组件所有。 */
	void SetOpen(bool bOpen);

	/** 标记 PageController 已经提交购买或领取 RPC；等待公开快照刷新时禁用重复动作。 */
	void MarkActionSubmitted(ECatShopUIAction Action, FName EntryId);

	/** 标记本地适配层拒绝了 UI 意图；例如条目不存在、经济快照缺失或动作类型不匹配。 */
	void MarkActionRejected(ECatShopUIAction Action, FName EntryId, FText Reason);

	/** 主动重读当前公开经济快照和配置目录；外部只读事实变化都收敛到这里。 */
	void Refresh();

	/** 返回最近商店投影；调用方只能读取，不能写回团队钱包或商品目录。 */
	const FCatShopViewState& GetViewState() const;

	/** 按 EntryId 查找最近投影中的商品行；PageController 用它确认点击来自当前目录。 */
	bool TryFindEntryView(FName EntryId, FCatShopEntryView& OutEntry) const;

	/** 商店 ViewState 变化通知；Bind、Refresh、pending 和本地拒绝都会触发。 */
	FCatShopModelChanged OnViewStateChanged;

private:
	/** GameState 商店快照变化入口；刷新公款、流水和 pending 提示。 */
	void HandleShopEconomySnapshotChanged();

	/** 按配置目录生成一条商品展示投影；不读取运行期写口，也不推导价格。 */
	FCatShopEntryView MakeEntryView(const FCatShopCatalogEntry& Entry) const;

	/** 当前打开商店的 Controller 弱引用；只用于定位 World/GameState，不拥有 UI 生命周期。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> BoundPlayerController;

	/** 当前公开经济快照来源；客户端从 GameState 读，服务器本机也走同一投影。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ACatfishingGameState> BoundGameState;

	/** GameState 商店快照订阅句柄；Unbind 必须从同一个 GameState 移除。 */
	FDelegateHandle ShopEconomyChangedHandle;

	/** 当前商店窗口是否打开的交互组件投影；Model 不从 Widget 可见性反推。 */
	bool bOpen = false;

	/** 当前是否已有购买或领取请求提交后等待公开快照刷新。 */
	bool bActionPending = false;

	/** 最近一次提交或本地拒绝的动作类型。 */
	ECatShopUIAction LastAction = ECatShopUIAction::None;

	/** 最近一次提交或本地拒绝的商品目录 ID。 */
	FName LastEntryId = NAME_None;

	/** 最近一次本地拒绝原因；服务器拒绝目前只能通过公开快照缺失或无变化由玩家验收观察。 */
	FText LastRejectedReason;

	/** 最近发布给 View 的完整商店投影；所有刷新都先写这里再广播。 */
	FCatShopViewState ViewState;
};
