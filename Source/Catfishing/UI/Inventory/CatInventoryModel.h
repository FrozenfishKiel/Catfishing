#pragma once

#include "CoreMinimal.h"
#include "UI/Inventory/CatInventoryTypes.h"
#include "UObject/Object.h"
#include "CatInventoryModel.generated.h"

class APlayerController;
class ACatCampInventoryActor;
class ACatCharacter;
class UCatContainerReplicationComponent;
class UCatEquipmentComponent;
class ULocalPlayer;

/** 库存 Model 完整投影变化通知；每个库存 WBP 自己监听后重读 ViewState，并只刷新自己对应的数据源格子。 */
DECLARE_MULTICAST_DELEGATE(FCatInventoryModelChanged);

/** 库存 MVC Model；它订阅随身库存、外部容器和营地公共仓库的只读快照，不创建 Widget、不提交命令。 */
UCLASS()
class CATFISHING_API UCatInventoryModel : public UObject
{
	GENERATED_BODY()

public:
	/** 绑定当前 LocalPlayer、Controller 和随身库存读源；成功后立即发布完整库存投影。 */
	bool Bind(ULocalPlayer* InLocalPlayer, APlayerController* InController, ACatCharacter* InCharacter);

	/** 成对解除外部容器、随身库存和 PlayerController 结果订阅，并清空 pending、结果和 ViewState。 */
	void Unbind();

	/** 返回 Model 是否仍绑定有效玩家库存读源；PageController 用它拒绝已失效 Widget 意图。 */
	bool IsBound() const;

	/** 写入库存打开状态并刷新投影；打开状态由 PageController 持有，Model 只把它合入 ViewState。 */
	void SetOpen(bool bOpen);

	/** 设置当前库存外部容器上下文；交互对象可传入任意数量的只读容器复制源，普通按键打开时应清空它们。 */
	void SetExternalContainerContexts(const TArray<UCatContainerReplicationComponent*>& InExternalContainers);

	/** 清除当前库存的外部容器上下文；普通库存打开和离开交互对象时调用，避免上一容器留在新页面。 */
	void ClearExternalContainerContexts();

	/** 设置当前库存的营地公共仓库上下文；交互打开公共仓库时调用，Model 只订阅它的只读快照变化。 */
	void SetCampInventoryContext(ACatCampInventoryActor* InCampInventory);

	/** 清除当前库存的营地公共仓库上下文；默认库存页或其他箱子打开前调用，避免上一公共仓库格留在新页面。 */
	void ClearCampInventoryContext();

	/** 标记 PageController 已提交服务器命令；只记录等待回包和结果匹配，不把提交中状态广播成库存刷新。 */
	void MarkActionSubmitted(ECatInventoryAction Action, FGuid RequestId);

	/** 标记 PageController 在本地适配阶段拒绝命令；本地失败不代表库存数据变化，因此不会触发 ViewState 广播。 */
	void MarkActionRejected(ECatInventoryAction Action, FGuid RequestId, ECatDomainCommandError Error,
		int64 Revision);

	/** 当前是否存在等待服务器终态的库存命令；PageController 用它防重复，不把 pending 当成库存数据广播给所有 WBP。 */
	bool IsActionPending() const;

	/** 主动从当前库存读源重读完整快照并广播；外部只读事实变化都收敛到这里。 */
	void Refresh();

	/** 库存只读投影的查询入口；调用方拿到最近缓存副本，避免 View 或 PageController 接触后端写口。 */
	const FCatInventoryViewState& GetViewState() const;

	/** 库存 ViewState 已变化通知；真实读源变化或上下文切换会触发，格子选择和服务器回包确认不主动重画所有 WBP。 */
	FCatInventoryModelChanged OnViewStateChanged;

private:
	/** 随身库存快照变化入口；物品数量或耐久变化会关闭本地等待并让库存重读完整投影。 */
	void HandleEquipmentSnapshotChanged();

	/** 外部容器复制变化入口；任意已绑定外部容器内容变化后会关闭本地等待并重读完整投影。 */
	void HandleExternalContainerSnapshotChanged();

	/** 营地公共仓库快照变化入口；商店发货或玩家取用后会关闭本地等待并重读公共仓库格和玩家随身库存。 */
	void HandleCampInventorySnapshotChanged();

	/** owning Controller 收到公共领域结果时只消费非成功终态；成功终态等真实库存读源变化来关闭 pending。 */
	void HandleCampCommandResult(const FCatDomainCommandResult& Result);

	/** owning Controller 收到献祭结果时只消费 Items 未提交前的非成功终态；鱼已提交后等容器快照关闭 pending。 */
	void HandleSacrificeResult(const FCatSacrificeResult& Result);

	/** owning Controller 收到吃鱼结果时只消费非成功终态；成功移除鱼后等容器快照关闭 pending。 */
	void HandleFishConsumeResult(const FCatFishConsumeResult& Result);

	/** 判断服务器回包是否属于当前等待的库存动作；动作类型和 RequestId 必须同时匹配。 */
	bool IsPendingResult(ECatInventoryAction Action, FGuid RequestId) const;

	/** 真实库存读源已经变化时清掉本地等待标记；数据源广播本身会刷新 UI，不需要服务器结果再补一次。 */
	void ClearPendingAfterObservedSourceChange();

	/** 服务器返回无快照终态时清掉本地等待；AlreadyResolved 和拒绝只解锁 PageController，不广播 ViewState。 */
	void ClearPendingAfterTerminalResultWithoutRefresh(ECatInventoryAction Action, FGuid RequestId,
		ECatDomainCommandError Error);

	/** 按某个容器快照生成一个外部容器格的只读投影；空格显示稳定占位文本，源目标身份来自容器公开事实。 */
	FCatInventorySlotView MakeSlotView(const FCatContainerSnapshot& Snapshot, int32 ContainerSlotIndex,
		int32 ExternalSlotIndex, const TCHAR* ContainerDisplayName) const;

	/** 按当前随身库存数组生成一个只读物品格；它只暴露本随身库存内的格子下标、定义和数量，不提供 Items 容器移动授权。 */
	FCatInventorySlotView MakeInventorySlotView(const FCatRunInventorySlot& InventorySlot,
		int32 InventorySlotIndex) const;

	/** 按营地公共仓库数组生成一个只读物品格；它只暴露本公共仓库内的槽位和版本，取用仍走 PlayerController 服务器入口。 */
	FCatInventorySlotView MakeCampInventorySlotView(const FCatRunInventorySlot& InventorySlot,
		int32 CampSlotIndex, int64 CampRevision) const;

	/** 按容器种类和顺序生成玩家可读名称；具体容器以后可在上下文层覆盖，Model 默认只做稳定 fallback。 */
	static FText MakeContainerDisplayName(const FCatContainerSnapshot& Snapshot, int32 ExternalContainerIndex);

	/** 解除所有外部容器复制订阅并清空绑定数组；切换上下文和 Unbind 都走同一流程，避免遗漏句柄。 */
	void ClearExternalContainerBindings();

	/** 解除营地公共仓库快照订阅并清空弱引用；切换到默认库存页或其他箱子时必须成对执行。 */
	void ClearCampInventoryBinding();

	/** 一份外部容器读源绑定；每个交互对象可以贡献多份容器，Model 统一订阅它们的复制变化。 */
	struct FExternalContainerBinding
	{
		/** 外部容器的复制组件弱引用；它只用于读取公开 Snapshot，不提供服务端写口。 */
		TWeakObjectPtr<UCatContainerReplicationComponent> Component;

		/** 该复制组件变化委托的解绑句柄；上下文切换时必须从同一组件移除。 */
		FDelegateHandle SnapshotChangedHandle;
	};

	/** 当前本地玩家读源；只用于生命周期一致性，不保存任何跨局 Profile 状态。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ULocalPlayer> BoundLocalPlayer;

	/** 当前 owning Controller 读源；Model 用它解绑结果委托并确认 Pawn 仍属于同一玩家。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> BoundPlayerController;

	/** 当前 Character 的 Equipment 复制出口；库存从这里读取随身物品和钓鱼选择，不把它们写入鱼护容器。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatEquipmentComponent> BoundEquipment;

	/** 当前交互上下文贡献的外部容器读源；普通库存打开时为空，不会凭空展示远处容器。 */
	TArray<FExternalContainerBinding> BoundExternalContainers;

	/** 当前交互打开的营地公共仓库读源；它代表团队共享箱子，不参与 Items 鱼容器移动。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ACatCampInventoryActor> BoundCampInventory;

	/** 随身库存快照订阅句柄；Unbind 必须从同一组件移除。 */
	FDelegateHandle EquipmentChangedHandle;

	/** 营地公共仓库快照订阅句柄；公共仓库上下文切换时必须从同一 Actor 移除。 */
	FDelegateHandle CampInventoryChangedHandle;

	/** PlayerController 公共领域结果订阅句柄；库存用它接收跨容器移动终态。 */
	FDelegateHandle CampCommandResultHandle;

	/** PlayerController 献祭结果订阅句柄；只用于献祭请求终态。 */
	FDelegateHandle SacrificeResultHandle;

	/** PlayerController 吃鱼结果订阅句柄；只用于吃鱼请求终态。 */
	FDelegateHandle FishConsumeResultHandle;

	/** 当前库存窗口是否打开的 PageController 投影；Model 不从 Widget 可见性反推。 */
	bool bOpen = false;

	/** 当前 pending 的动作类型；没有待处理服务器请求时为 None。 */
	ECatInventoryAction PendingAction = ECatInventoryAction::None;

	/** 当前 pending 的 RequestId；服务器结果必须匹配它才能关闭 pending。 */
	FGuid PendingRequestId;

	/** 当前是否已有库存动作提交到服务器但尚未收到终态。 */
	bool bActionPending = false;

	/** 最近一次服务器返回终态的动作类型；用于结果反馈文本和调试。 */
	ECatInventoryAction LastAction = ECatInventoryAction::None;

	/** 最近一次库存动作的公共结果头；不作为 Items 终态缓存。 */
	FCatDomainCommandResult LastCommandResult;

	/** 最近是否有可展示的库存动作结果。 */
	bool bHasCommandResult = false;

	/** 最近一次吃鱼详细结果；只在 LastAction 对应吃鱼时有效。 */
	FCatFishConsumeResult LastConsumeResult;

	/** 最近一次献祭详细结果；只在 LastAction 对应献祭时有效。 */
	FCatSacrificeResult LastSacrificeResult;

	/** 最近发布给 View 的完整库存投影；所有刷新都先写这里再广播。 */
	FCatInventoryViewState ViewState;
};
