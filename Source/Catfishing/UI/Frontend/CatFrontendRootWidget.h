#pragma once

#include "CoreMinimal.h"
#include "Online/CatOnlineTypes.h"
#include "Save/CatRunSaveGame.h"
#include "Types/SlateEnums.h"
#include "Blueprint/UserWidget.h"
#include "CatFrontendRootWidget.generated.h"

class UButton;
class UImage;
class UPanelWidget;
class UScrollBox;
class UEditableTextBox;
class UTextBlock;
class UUserWidget;
class UWidget;
class UWidgetSwitcher;
class UCatFrontendPageController;
class UCatFrontendRoomModel;
class UCatFrontendSaveModel;
class UCatFrontendSettingsModel;

/** 存档列表的一行原生 View；它只保存当前渲染行的稳定 SlotId，并把选择点击原样交给 Root，不保存或修改世界存档。 */
UCLASS(Abstract)
class CATFISHING_API UCatFrontendSaveSlotRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 用 SaveModel 的真实摘要配置本行；写入展示文本和稳定 SlotId，点击以后不通过行索引或文本猜测存档身份。 */
	void ConfigureRow(UCatFrontendRootWidget* InRootWidget, const FCatSaveSlotSummary& Summary);

protected:
	/** WidgetTree 建立后绑定本行选择按钮；按钮缺失时保持不可操作并记录资产合同错误，不生成替身。 */
	virtual void NativeOnInitialized() override;

private:
	/** 将本行保存的稳定 SlotId 交给 Root；Root 再转交 Controller，避免焦点、文本或数组下标成为身份键。 */
	UFUNCTION()
	void HandleSelectClicked();

	/** 当前列表行所属的 Frontend Root；ConfigureRow 写入，Root 销毁后弱引用自然失效。 */
	TWeakObjectPtr<UCatFrontendRootWidget> RootWidget;

	/** 本行代表的真实世界存档稳定标识；SaveModel 摘要写入，选择点击读取，绝不由展示名称反推。 */
	FName SlotId;

	/** 行内的存档名称文本；WBP_CatSaveSlotRow 必须提供，ConfigureRow 写入真实摘要显示名。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> SaveSlotNameText;

	/** 行内的存档元数据文本；WBP_CatSaveSlotRow 必须提供，未知字段保留明确不可用而不是伪造日期或进度。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> SaveSlotMetaText;

	/** 行内的选择按钮；WBP_CatSaveSlotRow 必须提供，NativeOnInitialized 绑定到稳定 SlotId 的选择回调。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> SelectSaveSlotButton;
};

/** 房间好友的一行原生 View；它只保存当前行的 opaque FriendHandle，并把邀请点击交给 Root，不接触平台身份。 */
UCLASS(Abstract)
class CATFISHING_API UCatFrontendRoomFriendRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 用 RoomModel 的真实好友摘要配置本行；写入名称、在线状态和 opaque 句柄，邀请按钮不解析 Steam 身份。 */
	void ConfigureRow(UCatFrontendRootWidget* InRootWidget, const FCatOnlineFriendSummary& Summary);

protected:
	/** WidgetTree 建立后绑定本行邀请按钮；缺失时记录资产合同错误，本行不会用页面级默认好友替代。 */
	virtual void NativeOnInitialized() override;

private:
	/** 将本行保存的 opaque FriendHandle 原样交给 Root；失效句柄仍由 RoomModel/Online 返回正式拒绝。 */
	UFUNCTION()
	void HandleInviteClicked();

	/** 当前好友行所属的 Frontend Root；ConfigureRow 写入，Root 拆除后弱引用不延长 UI 生命周期。 */
	TWeakObjectPtr<UCatFrontendRootWidget> RootWidget;

	/** 本行好友的 opaque 平台句柄；RoomModel 摘要写入，邀请点击读取，不使用显示名或行下标识别好友。 */
	FCatOnlineFriendHandle FriendHandle;

	/** 行内的好友名称文本；WBP_CatRoomFriendRow 必须提供，ConfigureRow 写入平台公开显示名。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> FriendNameText;

	/** 行内的好友状态文本；WBP_CatRoomFriendRow 必须提供，ConfigureRow 只显示 Online 已确认的观察事实。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> FriendStatusText;

	/** 行内的邀请按钮；WBP_CatRoomFriendRow 必须提供，NativeOnInitialized 绑定到本行 opaque 句柄。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UButton> InviteFriendButton;
};

/** 房间成员的一行原生 View；它只渲染 RoomModel 的真实成员记录，不新增准备、人数或房主第二份状态。 */
UCLASS(Abstract)
class CATFISHING_API UCatFrontendRoomPlayerSlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 用 RoomModel 的真实成员摘要配置本行；成员名称和房主标记均来自当前 Snapshot，空位由 Root 显式决定是否创建。 */
	void ConfigureRow(const FCatOnlineRoomMember& Member);

	/** 配置一个明确的真实空槽表现；只有 Snapshot 给出可验证容量时 Root 才创建，不用静态假玩家占位。 */
	void ConfigureEmptySlot();

private:
	/** 行内的成员名称文本；WBP_CatRoomPlayerSlot 必须提供，配置成员或空槽时写入。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> PlayerNameText;

	/** 行内的角色文本；WBP_CatRoomPlayerSlot 必须提供，真实 Lobby owner 显示为房主，其余成员不推导额外权限。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> PlayerRoleText;

	/** 行内的槽状态文本；WBP_CatRoomPlayerSlot 必须提供，空槽只显示等待状态，不伪造成员事实。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> PlayerSlotStateText;
};

/**
 * Frontend 根视图；只维护 Root 内业务页和动态行的装配与可见页，不拥有两张全局 Loading 资产，也不保存存档、房间或设置的业务事实。
 * LocalPlayer UI 子系统创建它，页面按钮经由这里转成 Controller 意图，Model 只供 WBP 只读渲染。
 */
UCLASS(Abstract, BlueprintType)
class CATFISHING_API UCatFrontendRootWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * 注入当前 LocalPlayer 的前端协作者；子系统在根 WBP 加入视口前调用，之后各子 WBP 可通过只读 Getter 查询 Model。
	 * 本方法只保存协作者引用并立即刷新当前页面表现，不订阅或改写任何底层业务状态。
	 */
	void InitializeFrontend(UCatFrontendPageController* InController, UCatFrontendSaveModel* InSaveModel,
		UCatFrontendRoomModel* InRoomModel, UCatFrontendSettingsModel* InSettingsModel);

	/**
	 * 清除前端协作者引用；LocalPlayer UI 子系统在 Controller 或 World 变化前调用，防止旧 World 的 WBP 继续向失效 Controller 提交意图。
	 * 本方法解除 Model 和控件订阅，清空协作者与输出设备显示映射，不负责关闭 Session、读取存档或应用设置。
	 */
	void ResetFrontend();

	/**
	 * 显示首页与同一业务面内的退出确认弹层；Controller 在流程取消、设置返回或初始进入 Frontend 时调用。
	 * 本方法只显式切换 MenuPage，不从当前显示页推导流程状态。
	 */
	void ShowMenu();

	/**
	 * 显示 Minecraft 风格的单页存档列表；Controller 在开始游戏流程中调用，存档数据仍由 SaveModel 负责刷新。
	 * 本方法显式切换 SaveListPage 并原生回填列表与确认控件，不读取或改变槽位内容。
	 */
	void ShowSaveList();

	/**
	 * 显示 Steam 好友与当前房间页面；Controller 在房间创建成功或已加入房间的玩法加载失败时调用。
	 * 本方法只显式切换 RoomPage，不自行创建 Session 或伪造房间数据。
	 */
	void ShowRoom();

	/**
	 * 显示主界面设置页面；设置草稿、应用与恢复默认全部仍由 SettingsModel 和 Controller 协作处理。
	 * 本方法显式切换 FrontendSettingsPage，并原生回填 SettingsModel 的草稿控件。
	 */
	void ShowFrontendSettings();

	/** 返回当前是否仍显示房间页面；Controller 用它避免迟到的房间终态覆盖菜单与设置，不据此推导 Session 或旅行状态。 */
	bool IsShowingRoom() const;

	/** 返回当前可见业务面的只读 Model 身份；仅供 Controller 定向投递跨页邀请提示，菜单返回空，不据此裁决业务操作。 */
	UObject* GetVisibleFeedbackSource() const;

	/** 刷新各页面所属的流程与 Model 反馈文本，不切页、不提交业务；Controller 写入本地校验或邀请提示后也可立即调用。 */
	void RefreshFlowFeedback();

	/** 返回前端流程 Controller；WBP 用它提交明确玩家意图，空值表示根视图正在拆除或尚未完成装配。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Frontend")
	UCatFrontendPageController* GetPageController() const;

	/** 返回存档列表只读 Model；SaveList 与 SaveSlotRow 读取它渲染槽位，不能通过它直接写 SaveGame。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Frontend")
	UCatFrontendSaveModel* GetSaveModel() const;

	/** 返回房间页只读 Model；Room、RoomFriendRow 与 RoomPlayerSlot 读取它渲染好友和成员，不缓存第二份房间真相。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Frontend")
	UCatFrontendRoomModel* GetRoomModel() const;

	/** 返回设置页只读 Model；Settings WBP 读取草稿与说明，不直接调用 GameUserSettings 或保存配置。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Frontend")
	UCatFrontendSettingsModel* GetSettingsModel() const;

	/** 首页“开始游戏”意图；把流程启动交给 Controller，Root 不自行刷新槽位或创建房间。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Frontend")
	void RequestStartGameFlow();

	/** 首页“加入队伍”占位意图；当前产品未定义加入流程，因此只交给 Controller 记录可见反馈，不触发搜索或本地替身房间。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Frontend")
	void RequestJoinParty();

	/** 首页“设置”意图；Controller 决定进入设置页的时机，Root 不读取或修改设置草稿。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Frontend")
	void RequestOpenFrontendSettings();

	/** 首页“退出游戏”意图；菜单页面负责显示自己的确认层，Root 不把它建成独立流程页面。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Frontend")
	void RequestShowExitConfirmation();

	/** 退出确认意图；Controller 在确认后执行本地退出，Root 不直接调用 ConsoleCommand 或终止进程。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Frontend")
	void RequestConfirmExit();

	/** 退出确认取消意图；Controller 恢复同一菜单业务面，Root 不进行页面编号跳转。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Frontend")
	void RequestCancelExitConfirmation();

	/** 存档行选中意图；SlotId 是 SaveModel 提供的稳定标识，Controller 只记录当前选择，不直接读取槽位内容。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Frontend")
	void RequestSelectSaveSlot(FName SlotId);

	/** 新建存档意图；DisplayName 由 WBP 输入，Controller 会交给 SaveModel 处理空值、重复和 pending 边界。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Frontend")
	void RequestCreateSaveSlot(const FString& DisplayName);

	/** 读取当前选中存档意图；只有 SaveModel 成功完成读取后，Controller 才允许进入创建房间流程。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Frontend")
	void RequestLoadSelectedSaveSlot();

	/** 删除当前选中存档的预确认意图；Controller 只打开可操作确认状态，真正删除必须再由确认按钮提交。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Frontend")
	void RequestDeleteSelectedSaveSlot();

	/** 删除确认意图；Controller 把已确认的稳定槽位标识交给 SaveModel，避免 WBP 刷新后误删另一行。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Frontend")
	void RequestConfirmDeleteSaveSlot();

	/** 取消当前页面操作意图；Controller 按当前正式流程返回上一业务面或取消确认状态，不从 WidgetSwitcher 反推状态。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Frontend")
	void RequestCancel();

	/** 房间页刷新好友意图；Controller 交给 RoomModel 请求平台刷新，Root 不持有好友数组或平台句柄。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Frontend")
	void RequestRefreshFriends();

	/** 房间页邀请好友意图；FriendHandle 是 RoomModel 提供的不透明展示安全句柄，邀请提交和失败反馈都由 RoomModel 收口。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Frontend")
	void RequestInviteFriend(FCatOnlineFriendHandle FriendHandle);

	/** 房间页离开意图；Controller 交给 RoomModel 处理 Host 与 Client 的不同退出语义，Root 不直接修改 Online 状态。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Frontend")
	void RequestLeaveRoom();

	/** 房主开始游戏意图；Controller 只提交正式 Start 请求，全局加载遮罩由 LocalPlayer UI 根据 Online 快照显示。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Frontend")
	void RequestStartRoomGame();

	/** 设置应用意图；具体草稿字段由 SettingsModel 定义，Root 仅把用户确认转交 Controller。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Frontend")
	void RequestApplyFrontendSettings();

	/** 设置取消意图；Controller 要求 SettingsModel 丢弃未应用草稿后返回菜单，Root 不直接恢复配置。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Frontend")
	void RequestCancelFrontendSettings();

	/** 设置恢复默认意图；SettingsModel 决定默认值与草稿影响，Root 不维护第二份设置状态。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Frontend")
	void RequestRestoreFrontendSettingsDefaults();

	/** 刷新音频输出设备意图；Root 把玩家明确点击交给 Controller 发起 AudioMixer 异步枚举，不能把旧下拉选项当正式设备列表。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Frontend")
	void RequestRefreshAudioOutputDevices();

	/** 房间页复制邀请码意图；Root 仅将 Online 已确认的 joinlobby URI 放入系统剪贴板，空邀请码保持不可复制且不生成替代码。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Frontend")
	void RequestCopyRoomInviteCode();

	/** 设置页选择游戏分类的意图；Controller 转交 SettingsModel，不用字符串或页面编号表达分类。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Frontend")
	void RequestSelectGameSettings();

	/** 设置页选择画面分类的意图；Controller 转交 SettingsModel，不用字符串或页面编号表达分类。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Frontend")
	void RequestSelectGraphicsSettings();

	/** 设置页选择声音分类的意图；Controller 转交 SettingsModel，不用字符串或页面编号表达分类。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Frontend")
	void RequestSelectAudioSettings();

	/** 控制分类按钮只把玩家带到正式“控制”分类说明；当前没有冻结输入字段，因此这里明确不生成临时控制配置。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Frontend")
	void RequestSelectControlsSettings();

protected:
	/**
	 * UMG 子控件完成创建后解析并绑定每页的必需控件；只有背景等扩展表现才可以省略，页面交互不得交给空蓝图事件图兜底。
	 * 本实现把实际 View 控件绑定到 Controller 的单向意图入口，并渲染当前页面，不创建原生替身或业务数据。
	 */
	virtual void NativeOnInitialized() override;

	/**
	 * 接收 Root 获得键盘焦点后的 Escape；按下时交给 Controller 执行确认取消或流程返回，其余按键保持父类处理。
	 * 本实现不根据当前 Switcher 索引做页面分发，避免 View 的表现状态反过来成为流程真相。
	 */
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/**
	 * UMG 即将销毁时解除按钮委托并清除协作者引用；避免 Root 被移出视口后仍从旧按钮接收玩家输入。
	 * 本实现不关闭 Session、不取消存档请求，生命周期收口由 Controller 与 LocalPlayer 子系统负责。
	 */
	virtual void NativeDestruct() override;

	/** 菜单页面重绘扩展点；原生已完成首页与退出确认层的实际控件更新，WBP 可选地补充纯表现动画。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Catfishing|Frontend")
	void BP_RenderMenu();

	/** 存档页面重绘扩展点；原生已完成真实列表行与反馈更新，WBP 可选地补充纯表现动画。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Catfishing|Frontend")
	void BP_RenderSaveList();

	/** 房间页面重绘扩展点；原生已完成好友、成员行与快照文本更新，WBP 可选地补充纯表现动画。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Catfishing|Frontend")
	void BP_RenderRoom();

	/** 设置页表现扩展只承接动画和样式刷新；正式控件值已由 C++ 写回，蓝图不能另存草稿。 */
	UFUNCTION(BlueprintImplementableEvent, Category = "Catfishing|Frontend")
	void BP_RenderFrontendSettings();

private:
	/**
	 * 从四个已强制装配的子 WBP 中显式解析页面控件；UMG 的 BindWidget 不穿透嵌套 UserWidget，因此页面内部按钮必须在这里按所属 WidgetTree 查询。
	 * 缺少必需控件时记录明确资产接线错误并保持该页面不可操作，避免空蓝图事件被误认为已交付交互。
	 */
	void ResolvePageControls();

	/**
	 * 按页面根与控件名解析指定类型的控件；只读取该子 WidgetTree，不扫描其他页面，避免同名控件被错误接线。
	 * 解析失败会记录所属页面与控件名，调用方据此决定禁用哪一条交互，不生成原生兜底。
	 */
	template <typename WidgetType>
	WidgetType* FindPageControl(UUserWidget* Page, const FName ControlName, const TCHAR* PageName) const;

	/**
	 * 绑定已解析的可用按钮到公开 Request 函数；重复初始化用 AddUnique 保持幂等，缺失按钮由页面资产补齐而不是在 C++ 生成替身。
	 * 本方法不把按钮点击解释成业务规则，每个点击仍只转交 Controller 的明确意图。
	 */
	void BindPageControls();

	/**
	 * 解除页面按钮与本 Root 的动态委托；NativeDestruct 和 ResetFrontend 成对调用，避免复用页面实例时叠加点击回调。
	 * 该方法只清理 View 侧委托，不取消 Save、Room 或 Settings 已经受理的异步请求。
	 */
	void UnbindPageControls();

	/**
	 * 订阅三个 Model 的本地刷新通知；每次注入协作者后建立，ResetFrontend 前成对移除，Root 不保存 Model 数据副本。
	 * 订阅只驱动原生控件刷新和可选纯表现扩展，不把通知转换为存档、房间或设置命令。
	 */
	void BindModelChanges();

	/**
	 * 解除三个 Model 的本地刷新通知；弱协作者失效或 Root 拆除时安全跳过，避免旧 World 的迟到 View 刷新。
	 * 保存的委托句柄只代表 View 订阅，不代表底层异步请求生命周期。
	 */
	void UnbindModelChanges();

	/** SaveModel 变化后原生更新存档反馈文本与实际列表行，并可选调用纯表现扩展。 */
	void HandleSaveModelChanged();

	/** RoomModel 变化后原生更新房间反馈文本、好友行和成员行，并可选调用纯表现扩展。 */
	void HandleRoomModelChanged();

	/** SettingsModel 变化后按四个命名分类查询切换具名 Panel 可见性，回填合法选项与草稿；反馈只读取设置来源。 */
	void HandleSettingsModelChanged();

	/** 依据 SaveModel 当前真实摘要重建紧凑存档行；每行使用稳定 SlotId 的原生 View 类，列表为空时不生成假槽位。 */
	void RebuildSaveRows();

	/** 依据 RoomModel 当前真实快照重建好友与成员行；每行使用 opaque FriendHandle 或已确认成员事实，不按索引猜身份。 */
	void RebuildRoomRows();

	/** 更新当前房间的邀请码、访问方式和页面级按钮可用性；缺少真实 Lobby URI 时保持明确不可用状态。 */
	void RefreshRoomPresentation();

	/** 读取新建存档输入框并转交 Controller；缺少输入控件时保留明确合同错误，不补默认名称。 */
	UFUNCTION()
	void HandleCreateSaveClicked();

	/** 好友筛选输入变化后只重建当前快照的显示行；不会向 Online 提交搜索或改变好友缓存。 */
	UFUNCTION()
	void HandleFriendSearchTextChanged(const FText& NewText);

	/** 语言下拉输入处理；只把 Model 提供的已打包 culture 写入草稿，Apply 前不改变运行时国际化。 */
	UFUNCTION() void HandleLanguageSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	/** 窗口模式下拉输入处理；只接受三种明确显示项写入草稿，未知项保留原值，实际窗口切换仍由 Apply 提交。 */
	UFUNCTION() void HandleFullscreenModeSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	/** 分辨率下拉输入处理；将宽高显示文本转换为尺寸交给 Model 校验并写入草稿，无法拆分时保持原值。 */
	UFUNCTION() void HandleScreenResolutionSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	/** 质量下拉输入处理；仅把已知 UE 质量档或自定义状态写入草稿，未知显示项不得隐式替换当前配置。 */
	UFUNCTION() void HandleQualitySelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	/** 垂直同步勾选输入处理；只写 SettingsModel 草稿，Apply 前不改变渲染同步。 */
	UFUNCTION() void HandleVSyncChanged(bool bIsChecked);
	/** UI 比例滑块输入处理；把归一化 View 值换算为正式 0.75 至 2.0 草稿范围。 */
	UFUNCTION() void HandleUIScaleChanged(float NormalizedValue);
	/** 显示 Gamma 滑块输入处理；把归一化 View 值换算为正式 0.5 至 5.0 草稿范围。 */
	UFUNCTION() void HandleBrightnessChanged(float NormalizedValue);
	/** 震动勾选输入处理；只在 Model 确认本地 Controller 可用时写入 ForceFeedback 草稿。 */
	UFUNCTION() void HandleVibrationChanged(bool bIsChecked);
	/** 网络语音勾选输入处理；只在 OSS Voice 正式可用时写入 Start/Stop 草稿。 */
	UFUNCTION() void HandleVoiceChatChanged(bool bIsChecked);
	/** 后台静音勾选输入处理；只写失焦音量草稿，Apply 前不改变当前窗口音频。 */
	UFUNCTION() void HandleMuteAudioWhenUnfocusedChanged(bool bIsChecked);
	/** 输出设备下拉输入处理；通过 View 的稳定显示项映射提交真实 AudioMixer 设备 ID，不从显示文字猜 ID。 */
	UFUNCTION() void HandleAudioOutputDeviceSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	/** 主音量滑块输入处理；只写 SettingsModel 草稿，Apply 前不改变 AudioDevice。 */
	UFUNCTION() void HandleMasterVolumeChanged(float Value);
	/** 音乐音量滑块输入处理；只写 SettingsModel 草稿，Apply 前不改变 AudioDevice。 */
	UFUNCTION() void HandleMusicVolumeChanged(float Value);
	/** 音效音量滑块输入处理；只写 SettingsModel 草稿，Apply 前不改变 AudioDevice。 */
	UFUNCTION() void HandleSFXVolumeChanged(float Value);
	/** 环境音音量滑块输入处理；只写 SettingsModel 草稿，Apply 前不改变 AudioDevice。 */
	UFUNCTION() void HandleAmbienceVolumeChanged(float Value);
	/** 语音分类音量滑块输入处理；它不替代网络语音开关，只写分类混音草稿。 */
	UFUNCTION() void HandleVoiceVolumeChanged(float Value);

	/**
	 * 切换到指定的已装配页面；所有公开 Show 函数通过该方法显式调用 WidgetSwitcher，避免引入页面枚举或按名称分发。
	 * 页面为空时保持当前可见页并记录诊断，防止资产未接线时把业务流程伪装为成功切页。
	 */
	void ShowPage(UWidget* Page, const TCHAR* PageName);

	/** 当前 Frontend 的流程调度者；LocalPlayer 子系统写入，按钮处理函数读取，ResetFrontend 与销毁阶段清空。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatFrontendPageController> PageController;

	/** 当前 LocalPlayer 的存档展示数据源；子 WBP 只读它，SaveModel 的 OnChanged 会驱动重绘而不是 Root 复制槽位。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatFrontendSaveModel> SaveModel;

	/** 当前 LocalPlayer 的房间展示数据源；Room WBP 只读它，Root 不缓存好友、成员或邀请码。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatFrontendRoomModel> RoomModel;

	/** 当前 LocalPlayer 的设置展示数据源；Settings WBP 只读草稿，Root 不应用或持久化任何设置。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatFrontendSettingsModel> SettingsModel;

	/** 静态背景资产位；WBP_CatFrontendRoot 可绑定 UImage 展示固定背景，也可在未配置时保持空白。 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> StaticBackgroundImage;

	/** 动态背景资产容器位；WBP_CatFrontendRoot 将动态材质、媒体或场景子 WBP 放在此容器，C++ 不假定其具体媒介。 */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> DynamicBackgroundContainer;

	/** 四个正式业务页面的可见性承载器；Root 的 Show 函数显式写入它，不能用当前索引反推流程。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UWidgetSwitcher> FrontendPageSwitcher;

	/** WBP_CatFrontendMenu 的根实例；首页和退出确认共用此业务面，MenuPage 必须在 Root 资产中可被切换。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UUserWidget> MenuPage;

	/** WBP_CatFrontendSaveList 的根实例；它内部创建或复用 WBP_CatSaveSlotRow，不额外需要一套页面 C++ 基类。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UUserWidget> SaveListPage;

	/** WBP_CatFrontendRoom 的根实例；它内部承载 WBP_CatRoomFriendRow 和 WBP_CatRoomPlayerSlot 列表。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UUserWidget> RoomPage;

	/** WBP_CatFrontendSettings 的根实例；它承载游戏、画面、声音、控制四类设置，不把控制分类拆为临时页面。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UUserWidget> FrontendSettingsPage;

	/** MenuPage 子 WidgetTree 中的开始游戏按钮；ResolvePageControls 显式解析并绑定，缺失时该页会记录资产接线错误。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> StartGameButton;

	/** MenuPage 子 WidgetTree 中的加入队伍按钮；当前只转交占位意图，不接搜索、加入或创建房间流程。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> JoinPartyButton;

	/** MenuPage 子 WidgetTree 中的设置按钮；Root 显式解析后只转交 Controller 显示设置页。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> FrontendSettingsButton;

	/** MenuPage 子 WidgetTree 中的退出按钮；点击只打开同菜单页面内的退出确认层。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> ExitGameButton;

	/** MenuPage 子 WidgetTree 中的退出确认按钮；确认后由 Controller 决定实际退出路径。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> ConfirmExitButton;

	/** MenuPage 子 WidgetTree 中的退出取消按钮；取消后仍留在 MenuPage，不触发跨页跳转。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> CancelExitButton;

	/** FrontendSettingsPage 子 WidgetTree 中的游戏分类按钮；Root 显式解析并绑定明确 Controller 意图。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> GameSettingsCategoryButton;

	/** FrontendSettingsPage 子 WidgetTree 中的画面分类按钮；Root 显式解析并绑定明确 Controller 意图。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> GraphicsSettingsCategoryButton;

	/** FrontendSettingsPage 子 WidgetTree 中的声音分类按钮；Root 显式解析并绑定明确 Controller 意图。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> AudioSettingsCategoryButton;

	/** FrontendSettingsPage 子 WidgetTree 中的控制分类按钮；当前只会显示正式不可用状态，不会生成临时键位配置。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> ControlsSettingsCategoryButton;

	/** SaveListPage 子 WidgetTree 中的结果文本；Root 原生写入 SaveModel 的真实反馈，槽位详情仍由列表渲染图读取 Model。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> SaveResultTextBlock;

	/** SaveListPage 子 WidgetTree 中的真实存档行容器；Root 每次刷新先清空再由 SaveModel 摘要创建紧凑行 WBP。 */
	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> SaveRowsScrollBox;

	/** SaveListPage 子 WidgetTree 中的新存档名称输入；CreateSaveButton 读取它的原始用户输入，空或重复由 Controller/SaveModel 裁决。 */
	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> CreateSaveNameTextBox;

	/** SaveListPage 子 WidgetTree 中的新建按钮；Root 读取名称输入并转交 Controller，不能用默认名代替玩家输入。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> CreateSaveButton;

	/** SaveListPage 子 WidgetTree 中的读取按钮；Root 只转交当前稳定选中槽位，不直接读磁盘。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> LoadSelectedSaveButton;

	/** SaveListPage 子 WidgetTree 中的删除预确认按钮；Root 只请求 Controller 显示确认，真实删除必须二次确认。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> DeleteSelectedSaveButton;

	/** SaveListPage 子 WidgetTree 中的删除确认按钮；Root 交给 Controller 使用预确认的稳定槽位提交删除。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> ConfirmDeleteSaveButton;

	/** SaveListPage 子 WidgetTree 中的返回按钮；Root 转交 Controller 的取消规则，不从当前页索引推导上级。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> CancelSaveButton;

	/** RoomPage 子 WidgetTree 中的结果文本；Root 原生写入 RoomModel 的真实反馈，好友/成员仍由列表渲染图读取 Model。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> RoomResultTextBlock;

	/** RoomPage 子 WidgetTree 中的邀请码文本；只显示 Online 已确认的真实 joinlobby URI，不生成自定义或离线邀请码。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> RoomInviteCodeText;

	/** RoomPage 子 WidgetTree 中的访问方式文本；只显示 Snapshot 的实际 SessionAccess，不据房间页按钮猜策略。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> RoomAccessPolicyText;

	/** RoomPage 子 WidgetTree 中的好友筛选输入；它只过滤当前 Snapshot 的显示行，不改变 RoomModel 好友缓存。 */
	UPROPERTY(Transient)
	TObjectPtr<UEditableTextBox> FriendSearchTextBox;

	/** RoomPage 子 WidgetTree 中的好友行容器；Root 根据当前真实好友快照创建具有 opaque handle 的紧凑行。 */
	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> FriendsScrollBox;

	/** RoomPage 子 WidgetTree 中的成员行容器；Root 根据当前真实成员与可验证容量创建成员或空槽行。 */
	UPROPERTY(Transient)
	TObjectPtr<UScrollBox> PlayersScrollBox;

	/** RoomPage 子 WidgetTree 中的刷新好友按钮；Root 只转交 RoomModel 的正式刷新请求。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> RefreshFriendsButton;

	/** RoomPage 子 WidgetTree 中的离开按钮；Root 只转交 RoomModel 的正式 Host/Client 离开请求。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> LeaveRoomButton;

	/** RoomPage 子 WidgetTree 中的开始按钮；仅 RoomModel 确认当前用户可开始时可用，Root 不伪造本地主机权限。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> StartRoomGameButton;

	/** RoomPage 子 WidgetTree 中的邀请码复制按钮；没有真实 joinlobby URI 时禁用，点击不构造备用码。 */
	UPROPERTY(Transient)
	TObjectPtr<UButton> CopyInviteCodeButton;

	/** FrontendSettingsPage 子 WidgetTree 中的结果文本；Root 原生写入 SettingsModel 的真实反馈，草稿控件仍由资产图读取 Model。 */
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FrontendSettingsResultTextBlock;

	/** 输出设备下拉显示项到正式 AudioMixer ID 的瞬态映射；SettingsModel 刷新时重建，选择回调只读它以避免从名称或索引猜设备身份。 */
	TMap<FString, FString> AudioOutputDeviceIdsByOption;

	/** SaveModel 变化通知的解绑句柄；InitializeFrontend 绑定，ResetFrontend 与析构前移除。 */
	FDelegateHandle SaveModelChangedHandle;

	/** RoomModel 变化通知的解绑句柄；InitializeFrontend 绑定，ResetFrontend 与析构前移除。 */
	FDelegateHandle RoomModelChangedHandle;

	/** SettingsModel 变化通知的解绑句柄；InitializeFrontend 绑定，ResetFrontend 与析构前移除。 */
	FDelegateHandle SettingsModelChangedHandle;

	/** 原生回填 SettingsModel 草稿到控件时的重入保护；回填期间输入回调只跳过自身，避免把显示刷新误写成用户修改。 */
	bool bRefreshingSettingsControls = false;
};
