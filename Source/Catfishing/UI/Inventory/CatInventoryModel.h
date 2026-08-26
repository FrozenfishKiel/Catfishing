#pragma once

#include "CoreMinimal.h"
#include "UI/Inventory/CatInventoryTypes.h"
#include "UObject/Object.h"
#include "CatInventoryModel.generated.h"

class APlayerController;
class ACatCharacter;
class UCatContainerReplicationComponent;
class UCatEquipmentComponent;
class ULocalPlayer;

/** 背包 Model 完整投影变化通知；PageController 收到后只把最新 ViewState 交给背包 WBP。 */
DECLARE_MULTICAST_DELEGATE(FCatInventoryModelChanged);

/** 背包 MVC Model；它订阅鱼护、外部容器和本人装备的只读快照，不创建 Widget、不提交命令。 */
UCLASS()
class CATFISHING_API UCatInventoryModel : public UObject
{
	GENERATED_BODY()

public:
	/** 绑定当前 LocalPlayer、Controller、Character 鱼护和 Equipment 复制源；成功后立即发布完整背包投影。 */
	bool Bind(ULocalPlayer* InLocalPlayer, APlayerController* InController, ACatCharacter* InCharacter);

	/** 成对解除鱼护、外部容器、装备和 PlayerController 结果订阅，并清空当前选择、pending 和 ViewState。 */
	void Unbind();

	/** 返回 Model 是否仍绑定有效玩家背包读源；PageController 用它过滤旧 Widget 意图。 */
	bool IsBound() const;

	/** 写入背包打开状态并刷新投影；打开状态由 PageController 持有，Model 只把它合入 ViewState。 */
	void SetOpen(bool bOpen);

	/** 设置当前背包外部容器上下文；交互对象可传入任意数量的只读容器复制源，普通按键打开时应清空它们。 */
	void SetExternalContainerContexts(const TArray<UCatContainerReplicationComponent*>& InExternalContainers);

	/** 清除当前背包的外部容器上下文；普通背包打开和离开交互对象时调用，避免旧容器留在新页面。 */
	void ClearExternalContainerContexts();

	/** 按格子下标选择当前背包条目；Model 会基于最新 Slots 裁剪空格和越界，并派生鱼动作或装备取用 gate。 */
	bool SelectSlot(int32 SlotIndex);

	/** 标记 PageController 已提交服务器命令；View 会进入 pending 并禁用重复点击。 */
	void MarkActionSubmitted(ECatInventoryAction Action, FGuid RequestId);

	/** 标记 PageController 在本地适配阶段拒绝命令；用于找不到营地或载荷无效这类前置失败。 */
	void MarkActionRejected(ECatInventoryAction Action, FGuid RequestId, ECatDomainCommandError Error,
		int64 Revision);

	/** 主动从当前背包读源重读完整快照并广播；外部只读事实变化都收敛到这里。 */
	void Refresh();

	/** 背包只读投影的查询入口；调用方拿到最近缓存副本，避免 View 或 PageController 接触后端写口。 */
	const FCatInventoryViewState& GetViewState() const;

	/** 背包 ViewState 已变化通知；只有 Bind、Refresh、选择、pending 和结果变化会触发。 */
	FCatInventoryModelChanged OnViewStateChanged;

private:
	/** 个人鱼护复制快照变化入口；事件只表示需要重读，不携带写权限。 */
	void HandleFishGuardSnapshotChanged();

	/** Equipment 快照变化入口；当前装备、随身耗材或耐久变化都会让背包重读完整投影。 */
	void HandleEquipmentSnapshotChanged();

	/** 外部容器复制变化入口；任意已绑定外部容器内容变化后背包会重读完整投影。 */
	void HandleExternalContainerSnapshotChanged();

	/** owning Controller 收到跨容器物体移动结果时匹配当前 pending 并刷新背包反馈。 */
	void HandleCampCommandResult(const FCatDomainCommandResult& Result);

	/** owning Controller 收到献祭结果时匹配当前 pending 并刷新背包反馈。 */
	void HandleSacrificeResult(const FCatSacrificeResult& Result);

	/** owning Controller 收到吃鱼结果时匹配当前 pending 并刷新背包反馈。 */
	void HandleFishConsumeResult(const FCatFishConsumeResult& Result);

	/** 判断服务器回包是否属于当前等待的背包动作；动作类型和 RequestId 必须同时匹配。 */
	bool IsPendingResult(ECatInventoryAction Action, FGuid RequestId) const;

	/** 按某个容器快照生成一个格子的只读投影；空格显示稳定占位文本，源目标身份来自容器公开事实。 */
	FCatInventorySlotView MakeSlotView(const FCatContainerSnapshot& Snapshot, int32 ContainerSlotIndex,
		int32 DisplaySlotIndex, const TCHAR* ContainerDisplayName) const;

	/** 按当前 Equipment 快照生成一个鱼竿槽；它只证明本人后端当前鱼竿，不作为 Items 拖拽源。 */
	FCatInventorySlotView MakeCurrentRodSlotView(const FCatEquipmentLoadoutSnapshot& Equipment,
		int32 DisplaySlotIndex) const;

	/** 按容器种类和顺序生成玩家可读名称；具体容器以后可在上下文层覆盖，Model 默认只做稳定 fallback。 */
	static FText MakeContainerDisplayName(const FCatContainerSnapshot& Snapshot, int32 ExternalContainerIndex);

	/** 解除所有外部容器复制订阅并清空绑定数组；切换上下文和 Unbind 都走同一流程，避免遗漏句柄。 */
	void ClearExternalContainerBindings();

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

	/** 当前 Character 个人鱼护复制出口；背包格子数量和内容都从它的 Snapshot 刷新。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatContainerReplicationComponent> BoundPersonalFishGuard;

	/** 当前 Character 的 Equipment 复制出口；背包从这里读取当前装备和随身耗材，不把它们写入鱼护容器。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatEquipmentComponent> BoundEquipment;

	/** 鱼护快照订阅句柄；Unbind 必须从同一组件移除。 */
	FDelegateHandle FishGuardChangedHandle;

	/** 当前交互上下文贡献的外部容器读源；普通背包打开时为空，不会凭空展示远处容器。 */
	TArray<FExternalContainerBinding> BoundExternalContainers;

	/** Equipment 快照订阅句柄；Unbind 必须从同一组件移除。 */
	FDelegateHandle EquipmentChangedHandle;

	/** PlayerController 公共领域结果订阅句柄；背包用它接收跨容器移动终态。 */
	FDelegateHandle CampCommandResultHandle;

	/** PlayerController 献祭结果订阅句柄；只用于献祭请求终态。 */
	FDelegateHandle SacrificeResultHandle;

	/** PlayerController 吃鱼结果订阅句柄；只用于吃鱼请求终态。 */
	FDelegateHandle FishConsumeResultHandle;

	/** 当前背包窗口是否打开的 PageController 投影；Model 不从 Widget 可见性反推。 */
	bool bOpen = false;

	/** 当前选中格子下标；Refresh 会按最新 Slots 数组裁剪。 */
	int32 SelectedSlotIndex = INDEX_NONE;

	/** 当前 pending 的动作类型；没有待处理服务器请求时为 None。 */
	ECatInventoryAction PendingAction = ECatInventoryAction::None;

	/** 当前 pending 的 RequestId；服务器结果必须匹配它才能关闭 pending。 */
	FGuid PendingRequestId;

	/** 当前是否已有背包动作提交到服务器但尚未收到终态。 */
	bool bActionPending = false;

	/** 最近一次完成或本地拒绝的动作类型；用于反馈文本和调试。 */
	ECatInventoryAction LastAction = ECatInventoryAction::None;

	/** 最近一次背包动作的公共结果头；不作为 Items 终态缓存。 */
	FCatDomainCommandResult LastCommandResult;

	/** 最近是否有可展示的背包动作结果。 */
	bool bHasCommandResult = false;

	/** 最近一次吃鱼详细结果；只在 LastAction 对应吃鱼时有效。 */
	FCatFishConsumeResult LastConsumeResult;

	/** 最近一次献祭详细结果；只在 LastAction 对应献祭时有效。 */
	FCatSacrificeResult LastSacrificeResult;

	/** 最近发布给 View 的完整背包投影；所有刷新都先写这里再广播。 */
	FCatInventoryViewState ViewState;
};
