#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CatOnlineTypes.h"
#include "CatTravelWidget.generated.h"

class UButton;
class UTextBlock;

/** View 向协调层上报的纯用户意图；opaque 句柄只原样回传，Widget 不解析平台结果或执行旅行。 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FCatOnlineActionRequested, ECatOnlineUIAction, FGuid);

/** 阶段 B 的原生白盒会话界面；展示只读 Snapshot，并广播 Host/Find/Join/Invite/Leave 意图。 */
UCLASS()
class CATFISHING_API UCatTravelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 用完整 Online Snapshot 刷新文本、opaque 首项句柄和按钮可用性；View 不从错误或 NetMode 推断会话角色。 */
	void Configure(const FCatOnlineSnapshot& Snapshot);

	/** 用户意图广播；LocalPlayer UI 子系统订阅后转交 UCatOnlineSubsystem 公共接口。 */
	FCatOnlineActionRequested OnActionRequested;

protected:
	/** 首次初始化时用 WidgetTree 构造状态文本与五个白盒按钮，不依赖 CommonUI 或阶段外蓝图资产。 */
	virtual void NativeOnInitialized() override;

	/** 每次进入视口时对五个按钮去重绑定，避免 Slate 重建后一次点击触发多次请求。 */
	virtual void NativeConstruct() override;

	/** 离开视口时解除全部点击绑定，再交还父类 Slate 生命周期。 */
	virtual void NativeDestruct() override;

private:
	/** 广播创建会话意图；不携带 opaque 句柄。 */
	UFUNCTION()
	void HandleHostClicked();

	/** 广播搜索会话意图；不读取或修改当前结果数组。 */
	UFUNCTION()
	void HandleFindClicked();

	/** 广播当前 Snapshot 第一项搜索结果的 opaque 句柄；无句柄时按钮保持禁用。 */
	UFUNCTION()
	void HandleJoinClicked();

	/** 广播当前 Snapshot 第一项已接受邀请的 opaque 句柄；邀请结果仍由 Online 子系统持有。 */
	UFUNCTION()
	void HandleInviteClicked();

	/** 广播统一离局意图；Host/Client 分支只由 Online 子系统根据 SessionRole 决定。 */
	UFUNCTION()
	void HandleLeaveClicked();

	/** 当前四类 Online 事实与结果数量的白盒文本；Configure 是唯一写入者。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusText;

	/** 玩家提交创建意图的白盒控件；仅空闲 Frontend 且无会话角色时启用，避免 View 在平台操作 pending 时制造并发请求。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> HostButton;

	/** 搜索 Session 按钮；只在空闲 Frontend 且没有会话角色时启用。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> FindButton;

	/** 加入搜索首项按钮；只有空闲 Frontend 且存在有效 opaque 搜索句柄时启用。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> JoinButton;

	/** 接受邀请按钮；只有空闲 Frontend 且存在有效 opaque 邀请句柄时启用。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> InviteButton;

	/** 离开当前 Session 按钮；只有空闲 Lake 且角色已确认为 Host 或 Client 时启用。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> LeaveButton;

	/** 最近一次 Snapshot 的第一项搜索 opaque 句柄；Join 点击只原样广播它。 */
	FGuid FirstSearchHandle;

	/** 最近一次 Snapshot 的第一项已接受邀请 opaque 句柄；Invite 点击只原样广播它。 */
	FGuid FirstInviteHandle;
};
