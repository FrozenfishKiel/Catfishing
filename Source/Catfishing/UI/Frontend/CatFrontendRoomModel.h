#pragma once

#include "CoreMinimal.h"
#include "Online/CatOnlineTypes.h"
#include "UObject/Object.h"
#include "CatFrontendRoomModel.generated.h"

class UCatOnlineSubsystem;
class ULocalPlayer;

/** 房间页数据变化通知；订阅方在回调后读取 Model 的只读查询，不持有 Online 或 Steam 平台对象。 */
DECLARE_MULTICAST_DELEGATE(FCatFrontendRoomModelChanged);

/** 房间视图的产品语义 Model；它把 LocalPlayer 生命周期限定在一个只读 Online 来源上，不保存会话、好友或邀请码的第二份副本。 */
UCLASS()
class CATFISHING_API UCatFrontendRoomModel : public UObject
{
	GENERATED_BODY()

public:
	/** 以指定 LocalPlayer 的 GameInstance Online 子系统作为唯一来源并订阅，立即读取已有快照，避免漏掉冷启动期间的邀请状态。 */
	void Initialize(ULocalPlayer* InLocalPlayer);

	/** 解除 Online 快照订阅并清空弱来源；可在 Controller、World 或 LocalPlayer 销毁边界重复调用。 */
	void Shutdown();

	/** 在前台提交创建当前房间的产品意图；最终创建结果仍通过 Online 快照和 OnChanged 返回。 */
	FCatOnlineResult CreateRoom();

	/** 提交离开当前房间的产品意图；Host 的玩法内保存收口由 Online 层处理，前台新局不触发保存。 */
	FCatOnlineResult LeaveRoom();

	/** 仅房主提交正式开始游戏意图；Online 会验证已加载的 Save 状态并在预载完成后旅行。 */
	FCatOnlineResult StartGame();

	/** 请求刷新 Steam 好友缓存；好友数据变化通过 OnChanged 通知，失败信息通过 GetLastResultText 读取。 */
	FCatOnlineResult RefreshFriends();

	/** 用好友行的 opaque 句柄请求平台邀请；Model 不解释或保存 Steam 身份。 */
	FCatOnlineResult InviteFriend(FCatOnlineFriendHandle FriendHandle);

	/** 返回 Online 的当前只读房间快照；来源不可用时返回默认空快照，不制造本地状态。 */
	FCatOnlineSnapshot GetSnapshot() const;

	/** 返回最近一次同步结果或 Online 邀请等待、Join 与错误的可展示文本；不返回平台错误、身份或连接信息。 */
	FText GetLastResultText() const;

	/** 当前用户是否可点击开始游戏；必须已经确认是 Host、没有并发操作且尚未预载。 */
	bool CanStartGame() const;

	/** 返回 Online 的真实玩法包异步加载比例；引擎未知或尚未预载时为 -1，房间页和加载页不得自行补百分比。 */
	float GetGameplayLoadProgress() const;

	/** 返回玩法进入过程的可读阶段；加载页用它区分包预载、旅行排队和失败文本，不新增前端枚举或复制 Online 状态。 */
	FText GetGameplayLoadStatusText() const;

	/** 房间页的 native 变化入口；Online 快照变化和同步拒绝都会广播，View 随后重新读取查询。 */
	FCatFrontendRoomModelChanged OnChanged;

private:
	/** 读取 Online 邀请等待、Join 与错误事实生成文本并通知订阅者；不缓存快照，也不代替 Online 接受或加入。 */
	void HandleOnlineChanged();

	/** 从 LocalPlayer 定位有效 Online 来源；GameInstance 或子系统不可用时返回空，不构造临时子系统。 */
	UCatOnlineSubsystem* GetOnline() const;

	/** 同步拒绝直接回显；受理后从当前快照恢复结果，防止同步回调已经失败却被 accepted 返回值清空，异步变化仍由原通知推进。 */
	void CaptureResult(const FCatOnlineResult& Result);

	/** 当前 Model 所服务的本地玩家；Initialize 写入、Shutdown 清空，弱引用不会延长 LocalPlayer 生命周期。 */
	TWeakObjectPtr<ULocalPlayer> LocalPlayer;

	/** 当前订阅的 Online 子系统；Initialize 绑定、Shutdown 成对解绑，Model 只读它的快照与命令入口。 */
	TWeakObjectPtr<UCatOnlineSubsystem> Online;

	/** Online 快照广播的配对解绑句柄；只在 Online 有效期间存在，防止 LocalPlayer 释放后的迟到通知。 */
	FDelegateHandle OnlineChangedHandle;

	/** 最近一次可展示结果或邀请处理状态；同步 API 和 Online 快照通知写入，视图读取文本但不据此推导会话事实。 */
	FText LastResultText;
};
