#pragma once

#include "CoreMinimal.h"
#include "Online/CatOnlineTypes.h"
#include "UObject/Object.h"
#include "CatFrontendPageController.generated.h"

class ULocalPlayer;
class UCatFrontendRootWidget;
class UCatFrontendRoomModel;
class UCatFrontendSaveModel;
class UCatFrontendSettingsModel;

/**
 * Frontend 页面流程协调器；只保存入口、确认槽位和命令等待事实，并把业务请求交给三个专属 Model。
 * LocalPlayer UI 子系统拥有它；Root Widget 只提交玩家意图，Controller 不复制好友、槽位摘要或设置草稿。
 */
UCLASS()
class CATFISHING_API UCatFrontendPageController : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * 装配一个 LocalPlayer 的前端流程协作者；子系统在 Root 和三个 Model 都创建完成后调用，完成后首页可接受玩家意图。
	 * 本方法保存弱生命周期边界、订阅 Model 变化并消费已有房间快照；没有已成立房间时显示菜单，不读取存档、不创建房间也不应用设置。
	 */
	void Initialize(ULocalPlayer* InLocalPlayer, UCatFrontendRootWidget* InRootWidget, UCatFrontendSaveModel* InSaveModel,
		UCatFrontendRoomModel* InRoomModel, UCatFrontendSettingsModel* InSettingsModel);

	/**
	 * 解除前端流程协作者；LocalPlayer UI 子系统在 Controller 或 World 改变前调用，完成后任何迟到 Model 通知都会被忽略。
	 * 本方法先解绑所有委托，再清除已确认槽位和入口来源，不主动取消底层异步业务请求。
	 */
	void Shutdown();

	/** 开始游戏流程；把入口标记为 StartGame、要求 SaveModel 刷新摘要并显示存档列表，不创建替身房间。 */
	void RequestStartGameFlow();

	/** 加入队伍占位请求；产品尚未定义流程，因此只保存可读反馈，不调用 RoomModel 的创建、搜索或加入接口。 */
	void RequestJoinParty();

	/** 打开设置请求；显示设置页面，具体草稿初始化和数据读取仍属于 SettingsModel。 */
	void RequestOpenFrontendSettings();

	/** 请求显示退出确认；确认层属于 MenuPage，Controller 保存确认状态供 Root 的菜单表现读取。 */
	void RequestShowExitConfirmation();

	/** 确认退出请求；只在 Menu 的确认状态有效时执行本地玩家退出入口，重复输入保持幂等并记录反馈。 */
	void RequestConfirmExit();

	/** 取消退出确认请求；清除确认状态并保持 MenuPage 可见，不从 WidgetSwitcher 的可见页推导流程。 */
	void RequestCancelExitConfirmation();

	/** 记录玩家从存档列表选中的稳定槽位；只接受 SaveModel 当前摘要中的槽位，避免刷新后保留失效行。 */
	void RequestSelectSaveSlot(FName SlotId);

	/** 请求新建存档；DisplayName 直接交给 SaveModel，Controller 只避免重复提交并等待 OnChanged 的终态。 */
	void RequestCreateSaveSlot(const FString& DisplayName);

	/** 请求读取当前已选槽位；只有 SaveModel 成功报告加载完成后才调用 RoomModel 创建房间。 */
	void RequestLoadSelectedSaveSlot();

	/** 请求删除当前已选槽位；本方法只记录删除确认目标并刷新菜单表现，真实删除必须走确认方法。 */
	void RequestDeleteSelectedSaveSlot();

	/** 确认删除请求；把预确认的稳定槽位交给 SaveModel，成功或失败由 SaveModel 的变化通知刷新 UI。 */
	void RequestConfirmDeleteSaveSlot();

	/** 通用返回或取消请求；优先取消菜单确认或存档删除确认，否则按当前正式流程回到菜单。 */
	void RequestCancel();

	/** 请求刷新 Steam 好友；Controller 只转交 RoomModel，不保存好友库存或平台句柄。 */
	void RequestRefreshFriends();

	/** 请求邀请一个房间好友；FriendHandle 由 RoomModel 提供，Controller 不解析平台身份。 */
	void RequestInviteFriend(FCatOnlineFriendHandle FriendHandle);

	/** 请求离开当前房间；RoomModel 收口 Host 与 Client 的退出差异，完成后 Controller 回到存档列表。 */
	void RequestLeaveRoom();

	/** 请求由房主开始游戏并消费可能同步结案的快照；正式 Start 预载或旅行已成立时交给全局遮罩表现，失败保留 RoomModel 的正式反馈。 */
	void RequestStartRoomGame();

	/** 请求应用设置草稿；具体字段和提交结果由 SettingsModel 定义，Controller 只维持页面流程。 */
	void RequestApplyFrontendSettings();

	/** 请求取消设置草稿；SettingsModel 回滚草稿后 Controller 回到菜单。 */
	void RequestCancelFrontendSettings();

	/** 请求把设置草稿恢复为默认；默认值的定义和草稿变化都由 SettingsModel 维护。 */
	void RequestRestoreFrontendSettingsDefaults();

	/** 请求异步刷新当前平台的音频输出设备；Controller 只转交 SettingsModel 的正式 AudioMixer 请求并呈现同步拒绝结果。 */
	void RequestRefreshAudioOutputDevices();

	/** 请求选择游戏设置分类；Controller 直接调用 SettingsModel 的明确接口，不引入分类字符串或枚举分发表。 */
	void RequestSelectGameSettings();

	/** 请求选择画面设置分类；Controller 直接调用 SettingsModel 的明确接口，不引入分类字符串或枚举分发表。 */
	void RequestSelectGraphicsSettings();

	/** 请求选择声音设置分类；Controller 直接调用 SettingsModel 的明确接口，不引入分类字符串或枚举分发表。 */
	void RequestSelectAudioSettings();

	/** 请求选择控制设置分类；当前只进入正式空分类说明，不发明控制字段或输入映射。 */
	void RequestSelectControlsSettings();

	/** 返回当前已被玩家选中且仍在 SaveModel 中有效的槽位标识；None 表示尚未选择或选择已失效。 */
	FName GetSelectedSlotId() const;

	/** 删除确认目标是玩家已经点过“删除”但尚未确认的槽位；Root 只能拿它做提示，None 表示没有危险操作待确认。 */
	FName GetPendingDeleteSlotId() const;

	/** 退出确认可见性属于菜单内部模态状态；Controller 用它拦截菜单输入，不能把它当成已经退出或页面跳转。 */
	bool IsExitConfirmationVisible() const;

	/** 返回属于指定 Model 的局部流程反馈，来源不匹配时为空；空来源专指菜单提示，业务错误仍保存在各自 Model。 */
	FText GetLastResultText(const UObject* ResultSource = nullptr) const;

private:
	/** Model 变化处理入口；重新检查选中/确认槽位有效性，识别读档终态，并请求 Root 重绘当前业务页面。 */
	void HandleSaveModelChanged();

	/** 消费已有或新成立房间、关联创建终态及邀请反馈；真实 Start 的预载和旅行阶段只刷新房间反馈并交给全局遮罩，同请求失败退回 Room。 */
	void HandleRoomModelChanged();

	/** Model 变化处理入口；只请求设置页重绘并更新结果文本，不从设置变化触发存档或房间操作。 */
	void HandleSettingsModelChanged();

	/** 验证一个槽位仍属于 SaveModel 当前摘要；删除、刷新或失败后用于清理 Controller 的过期选择。 */
	bool IsCurrentSaveSlot(FName SlotId) const;

	/** 仅在未入房流程明确失败或取消时释放本地载荷；Save busy 或 Online 仍持有操作/房间时拒绝，不接管已成立会话的 Leave 清理。 */
	bool ReleaseUnjoinedSave();

	/** 保存带 Model 来源的局部提示并请求 Root 刷新文本；空文本清除旧提示，不广播或覆写任何 Model 的正式结果。 */
	void SetLocalResultText(FText InResultText, UObject* ResultSource = nullptr);

	/** 当前流程绑定的 LocalPlayer；Initialize 写入，Shutdown 清空，只用于退出与 World 生命周期判断。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ULocalPlayer> LocalPlayer;

	/** 当前前端根 View；Controller 只调用明确 Show 函数，Widget 不作为业务事实持有者。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatFrontendRootWidget> RootWidget;

	/** 当前 LocalPlayer 的存档业务 Model；Controller 订阅它的变化并提交槽位命令，但不保留槽位摘要副本。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatFrontendSaveModel> SaveModel;

	/** 当前 LocalPlayer 的房间业务 Model；Controller 只转交产品意图，不复制好友、邀请码或玩家槽。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatFrontendRoomModel> RoomModel;

	/** 当前 LocalPlayer 的设置业务 Model；Controller 管页面确认，不把设置草稿拆进流程状态。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatFrontendSettingsModel> SettingsModel;

	/** 当前是否处于首页开始游戏发起的正式存档流程；开始游戏写入，取消、退出或销毁时清空，成功读档前是创建房间的必要条件。 */
	bool bStartGameFlowActive = false;

	/** 玩家当前选中的稳定存档槽；SaveList 行点击写入，SaveModel 刷新后验证，成功读档前不得创建房间。 */
	FName SelectedSlotId;

	/** 等待二次确认删除的稳定存档槽；第一次删除点击写入，确认或取消后清空，避免列表刷新误删新行。 */
	FName PendingDeleteSlotId;

	/** 同一 MenuPage 内退出确认层是否可见；确认和取消读取并清除它，Root 不额外创建退出页面。 */
	bool bExitConfirmationVisible = false;

	/** Controller 是否正在等待 SaveModel 的读档终态；重复读取被拒绝，只有成功结果才可发起创建房间。 */
	bool bWaitingForSaveLoad = false;

	/** Controller 是否正在等待 RoomModel 的房间创建终态；防止 SaveModel 的重复通知创建多个房间。 */
	bool bWaitingForRoomCreation = false;

	/** 本次读档后创建房间的请求身份；调用返回前为空以推迟同步通知，返回后只消费同一请求终态，结束与 Shutdown 清空。 */
	FGuid PendingRoomCreationRequestId;

	/** 当前 Frontend 房间是否已触发过进入呈现；首次已有房间或真实加入时置位，失去房间时清除，普通好友刷新不能抢走设置页面。 */
	bool bPresentedFrontendRoom = false;

	/** 最近一次已呈现的邀请反馈文本；与请求身份一起抑制相同快照反复覆盖用户后续提示，不参与邀请或 Session 裁决。 */
	FText PresentedInviteFeedback;

	/** 最近一次邀请反馈对应的正式请求身份；同请求下文本变化仍重新呈现，兼容忙碌拒绝不更新 Snapshot.RequestId 的 Online 合同。 */
	FGuid PresentedInviteFeedbackRequestId;

	/** 最近一次已交给全局遮罩表现的 Online 预载请求标识；房间通知写入并用于去重记录，新重试以新 RequestId 重新呈现，Shutdown 清空。 */
	FGuid PresentedGameplayLoadRequestId;

	/** Controller 最近一次本地流程反馈；占位、确认与输入校验写入，WBP 只读显示。 */
	FText LastResultText;

	/** 局部提示所属的 Model；Root 只给对应业务面读取，显式导航清空它，避免 Save 反馈污染 Settings。空来源用于菜单。 */
	TWeakObjectPtr<UObject> LastResultSource;

	/** SaveModel OnChanged 的解绑句柄；Initialize 绑定，Shutdown 必须在清空 Model 前移除。 */
	FDelegateHandle SaveModelChangedHandle;

	/** RoomModel OnChanged 的解绑句柄；Initialize 绑定，Shutdown 必须在清空 Model 前移除。 */
	FDelegateHandle RoomModelChangedHandle;

	/** SettingsModel OnChanged 的解绑句柄；Initialize 绑定，Shutdown 必须在清空 Model 前移除。 */
	FDelegateHandle SettingsModelChangedHandle;
};
