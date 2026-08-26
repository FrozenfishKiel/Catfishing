#pragma once

#include "CoreMinimal.h"
#include "UI/Inventory/CatInventoryTypes.h"
#include "UObject/Object.h"
#include "CatInventoryModel.generated.h"

class APlayerController;
class ACatCharacter;
class UCatContainerReplicationComponent;
class ULocalPlayer;

/** 背包 Model 完整投影变化通知；PageController 收到后只把最新 ViewState 交给背包 WBP。 */
DECLARE_MULTICAST_DELEGATE(FCatInventoryModelChanged);

/** 个人鱼护/背包的 MVC Model；它只订阅 Character 个人鱼护复制快照和动作结果，不创建 Widget、不提交命令。 */
UCLASS()
class CATFISHING_API UCatInventoryModel : public UObject
{
	GENERATED_BODY()

public:
	/** 绑定当前 LocalPlayer、Controller 和 Character 个人鱼护；成功后立即发布一份完整背包投影。 */
	bool Bind(ULocalPlayer* InLocalPlayer, APlayerController* InController, ACatCharacter* InCharacter);

	/** 成对解除鱼护快照和 PlayerController 结果订阅，并清空当前选择、pending 和 ViewState。 */
	void Unbind();

	/** 返回 Model 是否仍绑定有效玩家鱼护；PageController 用它过滤旧 Widget 意图。 */
	bool IsBound() const;

	/** 写入背包打开状态并刷新投影；打开状态由 PageController 持有，Model 只把它合入 ViewState。 */
	void SetOpen(bool bOpen);

	/** 按格子下标选择当前鱼；Model 会基于最新鱼护快照裁剪空格和越界。 */
	bool SelectSlot(int32 SlotIndex);

	/** 标记 PageController 已提交服务器命令；View 会进入 pending 并禁用重复点击。 */
	void MarkActionSubmitted(ECatInventoryAction Action, FGuid RequestId);

	/** 标记 PageController 在本地适配阶段拒绝命令；用于找不到营地或载荷无效这类前置失败。 */
	void MarkActionRejected(ECatInventoryAction Action, FGuid RequestId, ECatDomainCommandError Error,
		int64 Revision);

	/** 主动从当前复制鱼护重读完整快照并广播；外部只读事实变化都收敛到这里。 */
	void Refresh();

	/** 返回最近一次背包投影；调用方只能读取，不获得任何后端写入口。 */
	const FCatInventoryViewState& GetViewState() const;

	/** 背包 ViewState 已变化通知；只有 Bind、Refresh、选择、pending 和结果变化会触发。 */
	FCatInventoryModelChanged OnViewStateChanged;

private:
	/** 个人鱼护复制快照变化入口；事件只表示需要重读，不携带写权限。 */
	void HandleFishGuardSnapshotChanged();

	/** owning Controller 收到转缸结果时匹配当前 pending 并刷新背包反馈。 */
	void HandleCampCommandResult(const FCatDomainCommandResult& Result);

	/** owning Controller 收到献祭结果时匹配当前 pending 并刷新背包反馈。 */
	void HandleSacrificeResult(const FCatSacrificeResult& Result);

	/** owning Controller 收到吃鱼结果时匹配当前 pending 并刷新背包反馈。 */
	void HandleFishConsumeResult(const FCatFishConsumeResult& Result);

	/** 判断服务器回包是否属于当前等待的背包动作；动作类型和 RequestId 必须同时匹配。 */
	bool IsPendingResult(ECatInventoryAction Action, FGuid RequestId) const;

	/** 按当前鱼护快照生成一个格子的只读投影；空格显示稳定占位文本。 */
	FCatInventorySlotView MakeSlotView(const FCatContainerSnapshot& Snapshot, int32 SlotIndex) const;

	/** 当前本地玩家读源；只用于生命周期一致性，不保存任何跨局 Profile 状态。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ULocalPlayer> BoundLocalPlayer;

	/** 当前 owning Controller 读源；Model 用它解绑结果委托并确认 Pawn 仍属于同一玩家。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> BoundPlayerController;

	/** 当前 Character 个人鱼护复制出口；背包格子数量和内容都从它的 Snapshot 刷新。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatContainerReplicationComponent> BoundPersonalFishGuard;

	/** 鱼护快照订阅句柄；Unbind 必须从同一组件移除。 */
	FDelegateHandle FishGuardChangedHandle;

	/** PlayerController 营地结果订阅句柄；只用于转缸请求终态。 */
	FDelegateHandle CampCommandResultHandle;

	/** PlayerController 献祭结果订阅句柄；只用于献祭请求终态。 */
	FDelegateHandle SacrificeResultHandle;

	/** PlayerController 吃鱼结果订阅句柄；只用于吃鱼请求终态。 */
	FDelegateHandle FishConsumeResultHandle;

	/** 当前背包窗口是否打开的 PageController 投影；Model 不从 Widget 可见性反推。 */
	bool bOpen = false;

	/** 当前选中格子下标；Refresh 会按最新鱼护容量和鱼数组裁剪。 */
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
