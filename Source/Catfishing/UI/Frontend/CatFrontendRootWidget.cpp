#include "UI/Frontend/CatFrontendRootWidget.h"

#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/PanelWidget.h"
#include "Components/ProgressBar.h"
#include "Components/EditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "HAL/PlatformApplicationMisc.h"
#include "InputCoreTypes.h"
#include "Logging/CatLog.h"
#include "UI/Frontend/CatFrontendPageController.h"
#include "UI/Frontend/CatFrontendRoomModel.h"
#include "UI/Frontend/CatFrontendSaveModel.h"
#include "UI/Frontend/CatFrontendSettingsModel.h"

// 存档行配置流程：保存摘要的稳定 SlotId，写入真实显示名和最近保存时间；未知扩展元数据不会被 UI 补成虚构进度。
void UCatFrontendSaveSlotRowWidget::ConfigureRow(UCatFrontendRootWidget* InRootWidget, const FCatSaveSlotSummary& Summary)
{
	RootWidget = InRootWidget;
	SlotId = Summary.SlotId;
	if (SaveSlotNameText) { SaveSlotNameText->SetText(FText::FromString(Summary.DisplayName)); }
	if (SaveSlotMetaText)
	{
		SaveSlotMetaText->SetText(Summary.LastSavedAt.GetTicks() > 0
			? FText::FromString(FString::Printf(TEXT("最近保存：%s"), *Summary.LastSavedAt.ToString()))
			: FText::FromString(TEXT("最近保存时间不可用")));
	}
}

// 存档行初始化流程：WidgetTree 建立后只绑定自身按钮；缺少强制控件时保留不可操作行并记录资产合同错误。
void UCatFrontendSaveSlotRowWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!SelectSaveSlotButton)
	{
		UE_LOG(LogCatUI, Error, TEXT("Event=frontend_widget_contract_missing Page=SaveSlotRow Control=SelectSaveSlotButton"));
		return;
	}
	SelectSaveSlotButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleSelectClicked);
}

// 存档选择流程：只把 ConfigureRow 保存的稳定标识交给 Root；空 Root 或空标识安全跳过，不按文本或列表位置猜测身份。
void UCatFrontendSaveSlotRowWidget::HandleSelectClicked()
{
	if (UCatFrontendRootWidget* Root = RootWidget.Get(); Root && !SlotId.IsNone())
	{
		Root->RequestSelectSaveSlot(SlotId);
	}
}

// 好友行配置流程：保存 opaque FriendHandle，写入平台公开名称和在线观察状态；不将显示名作为邀请身份或缓存平台对象。
void UCatFrontendRoomFriendRowWidget::ConfigureRow(UCatFrontendRootWidget* InRootWidget, const FCatOnlineFriendSummary& Summary)
{
	RootWidget = InRootWidget;
	FriendHandle = Summary.Handle;
	if (FriendNameText) { FriendNameText->SetText(FText::FromString(Summary.DisplayName)); }
	if (FriendStatusText)
	{
		FriendStatusText->SetText(FText::FromString(Summary.bIsOnline
			? (Summary.bIsPlayingThisGame ? TEXT("在线，正在游玩") : TEXT("在线")) : TEXT("离线")));
	}
	if (InviteFriendButton) { InviteFriendButton->SetIsEnabled(Summary.bIsOnline && FriendHandle.IsValid()); }
}

// 好友行初始化流程：WidgetTree 建立后只绑定自身邀请按钮；缺失时记录资产合同错误，不降级为页面级默认好友邀请。
void UCatFrontendRoomFriendRowWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!InviteFriendButton)
	{
		UE_LOG(LogCatUI, Error, TEXT("Event=frontend_widget_contract_missing Page=RoomFriendRow Control=InviteFriendButton"));
		return;
	}
	InviteFriendButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleInviteClicked);
}

// 好友邀请流程：只把当前行 opaque 句柄交给 Root；句柄过期或平台拒绝由 RoomModel/Online 产生正式反馈。
void UCatFrontendRoomFriendRowWidget::HandleInviteClicked()
{
	if (UCatFrontendRootWidget* Root = RootWidget.Get(); Root && FriendHandle.IsValid())
	{
		Root->RequestInviteFriend(FriendHandle);
	}
}

// 成员行配置流程：写入 Snapshot 已确认的成员名称和 Lobby owner 标记；不从本地角色或静态列表推导房主。
void UCatFrontendRoomPlayerSlotWidget::ConfigureRow(const FCatOnlineRoomMember& Member)
{
	if (PlayerNameText) { PlayerNameText->SetText(FText::FromString(Member.DisplayName)); }
	if (PlayerRoleText) { PlayerRoleText->SetText(FText::FromString(Member.bIsLobbyOwner ? TEXT("房主") : TEXT("成员"))); }
	if (PlayerSlotStateText) { PlayerSlotStateText->SetText(FText::FromString(TEXT("已加入房间"))); }
}

// 空槽配置流程：只在 Root 已从真实容量确认剩余名额时调用，明确展示空位而不生成虚构成员或 ready 状态。
void UCatFrontendRoomPlayerSlotWidget::ConfigureEmptySlot()
{
	if (PlayerNameText) { PlayerNameText->SetText(FText::FromString(TEXT("空位"))); }
	if (PlayerRoleText) { PlayerRoleText->SetText(FText::GetEmpty()); }
	if (PlayerSlotStateText) { PlayerSlotStateText->SetText(FText::FromString(TEXT("等待加入"))); }
}

// 协作者装配流程：
// 1. 先解除旧 Model 通知，避免 LocalPlayer 切换时旧 World 的刷新落入当前 Root。
// 2. 保存新 Controller 与三个专属 Model，解析子 WBP 控件树并绑定实际按钮。
// 3. 最后订阅 Model 变化并立即刷新原生控件；可选蓝图事件只用于纯表现扩展，Root 不复制业务数据或发起底层请求。
void UCatFrontendRootWidget::InitializeFrontend(UCatFrontendPageController* InController, UCatFrontendSaveModel* InSaveModel,
	UCatFrontendRoomModel* InRoomModel, UCatFrontendSettingsModel* InSettingsModel)
{
	UnbindModelChanges();
	UnbindPageControls();
	PageController = InController;
	SaveModel = InSaveModel;
	RoomModel = InRoomModel;
	SettingsModel = InSettingsModel;
	ResolvePageControls();
	BindPageControls();
	BindModelChanges();
	HandleSaveModelChanged();
	HandleRoomModelChanged();
	HandleSettingsModelChanged();
	RefreshLoadingPresentation();
}

// 协作者拆除流程：先解除 Model 与按钮委托，再清空协作者引用；Root 不主动取消 Session、存档或设置操作，避免 View 生命周期反向改写业务。
void UCatFrontendRootWidget::ResetFrontend()
{
	UnbindModelChanges();
	UnbindPageControls();
	AudioOutputDeviceIdsByOption.Reset();
	PageController = nullptr;
	SaveModel = nullptr;
	RoomModel = nullptr;
	SettingsModel = nullptr;
}

// 菜单显示流程：显式选择 MenuPage，按确认事实展开全屏遮罩并禁用四个背景入口；确认时焦点给取消按钮，关闭后恢复开始按钮，最后刷新本页反馈，不改任何业务状态。
void UCatFrontendRootWidget::ShowMenu()
{
	ShowPage(MenuPage, TEXT("MenuPage"));
	const bool bShowExitConfirmation = PageController && PageController->IsExitConfirmationVisible();
	if (UPanelWidget* Commands = FindPageControl<UPanelWidget>(MenuPage, TEXT("MenuCommands"), TEXT("MenuPage"))) { Commands->SetIsEnabled(!bShowExitConfirmation); }
	if (UPanelWidget* Overlay = FindPageControl<UPanelWidget>(MenuPage, TEXT("ExitConfirmationOverlay"), TEXT("MenuPage"))) { Overlay->SetVisibility(bShowExitConfirmation ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	if (UTextBlock* ConfirmationText = FindPageControl<UTextBlock>(MenuPage, TEXT("ExitConfirmationText"), TEXT("MenuPage"))) { ConfirmationText->SetVisibility(bShowExitConfirmation ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	if (ConfirmExitButton) { ConfirmExitButton->SetVisibility(bShowExitConfirmation ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	if (CancelExitButton) { CancelExitButton->SetVisibility(bShowExitConfirmation ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	if (UButton* FocusButton = bShowExitConfirmation ? CancelExitButton.Get() : StartGameButton.Get(); FocusButton && GetOwningPlayer()) { FocusButton->SetUserFocus(GetOwningPlayer()); }
	RefreshFlowFeedback();
	BP_RenderMenu();
}

// 存档显示流程：显式选择 SaveListPage，原生写真实结果、可操作状态和列表行；可选蓝图事件不承担任何交互或数据呈现。
void UCatFrontendRootWidget::ShowSaveList()
{
	ShowPage(SaveListPage, TEXT("SaveListPage"));
	const bool bShowDeleteConfirmation = PageController && !PageController->GetPendingDeleteSlotId().IsNone();
	if (UTextBlock* ConfirmationText = FindPageControl<UTextBlock>(SaveListPage, TEXT("DeleteConfirmationText"), TEXT("SaveListPage"))) { ConfirmationText->SetVisibility(bShowDeleteConfirmation ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	if (ConfirmDeleteSaveButton) { ConfirmDeleteSaveButton->SetVisibility(bShowDeleteConfirmation ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	HandleSaveModelChanged();
}

// 房间显示流程：显式选择 RoomPage，原生写真实结果、好友和成员行；可选蓝图事件不承担任何交互或数据呈现。
void UCatFrontendRootWidget::ShowRoom()
{
	ShowPage(RoomPage, TEXT("RoomPage"));
	HandleRoomModelChanged();
}

// 设置显示流程：显式选择 FrontendSettingsPage，原生写真实结果和正式草稿控件；可选蓝图事件不承担任何交互或数据呈现。
void UCatFrontendRootWidget::ShowFrontendSettings()
{
	ShowPage(FrontendSettingsPage, TEXT("FrontendSettingsPage"));
	HandleSettingsModelChanged();
}

// 加载显示流程：显式选择 LoadingPage，并只写 Controller 已确认的真实加载阶段文字；百分比必须由正式进度接口提供后再显示。
void UCatFrontendRootWidget::ShowLoading()
{
	ShowPage(LoadingPage, TEXT("LoadingPage"));
	RefreshLoadingPresentation();
}

// 加载页显示查询流程：确认必需控件仍有效后比较 Switcher 实际显示的 Widget；缺失时返回 false，使迟到失败不能切换其他页面。
bool UCatFrontendRootWidget::IsShowingLoading() const
{
	return FrontendPageSwitcher && LoadingPage && FrontendPageSwitcher->GetActiveWidget() == LoadingPage;
}

// 房间显示范围查询流程：只比较当前显示的房间和加载 Widget；菜单、设置、存档页或未装配状态均返回 false，作为异步呈现保护。
bool UCatFrontendRootWidget::IsShowingRoomOrLoading() const
{
	return (FrontendPageSwitcher && RoomPage && FrontendPageSwitcher->GetActiveWidget() == RoomPage) || IsShowingLoading();
}

// 当前反馈来源查询流程：只用 Switcher 实际激活控件定位文本所属 Model；未装配或菜单返回空，不从可见页推断存档许可、Session 角色或设置分类。
UObject* UCatFrontendRootWidget::GetVisibleFeedbackSource() const
{
	const UWidget* ActiveWidget = FrontendPageSwitcher ? FrontendPageSwitcher->GetActiveWidget() : nullptr;
	if (ActiveWidget && ActiveWidget == SaveListPage) { return SaveModel; }
	if (ActiveWidget && ActiveWidget == FrontendSettingsPage) { return SettingsModel; }
	if (ActiveWidget && (ActiveWidget == RoomPage || ActiveWidget == LoadingPage)) { return RoomModel; }
	return nullptr;
}

// 反馈刷新流程：各文本先取属于自身 Model 的局部提示，为空再读该 Model 正式结果；菜单仅取空来源提示，加载阶段另按 Room 事实更新。不会因一个来源通知而把其错误复制到其他页面。
void UCatFrontendRootWidget::RefreshFlowFeedback()
{
	const FText SaveFeedback = PageController ? PageController->GetLastResultText(SaveModel) : FText::GetEmpty();
	const FText RoomFeedback = PageController ? PageController->GetLastResultText(RoomModel) : FText::GetEmpty();
	const FText SettingsFeedback = PageController ? PageController->GetLastResultText(SettingsModel) : FText::GetEmpty();
	if (SaveResultTextBlock) { SaveResultTextBlock->SetText(!SaveFeedback.IsEmpty() ? SaveFeedback : SaveModel ? SaveModel->GetLastResultText() : FText::GetEmpty()); }
	if (RoomResultTextBlock) { RoomResultTextBlock->SetText(!RoomFeedback.IsEmpty() ? RoomFeedback : RoomModel ? RoomModel->GetLastResultText() : FText::GetEmpty()); }
	if (FrontendSettingsResultTextBlock) { FrontendSettingsResultTextBlock->SetText(!SettingsFeedback.IsEmpty() ? SettingsFeedback : SettingsModel ? SettingsModel->GetLastResultText() : FText::GetEmpty()); }
	if (UTextBlock* MenuFeedback = FindPageControl<UTextBlock>(MenuPage, TEXT("MenuSubtitleText"), TEXT("MenuPage")))
	{
		const FText MenuText = PageController ? PageController->GetLastResultText() : FText::GetEmpty();
		MenuFeedback->SetAutoWrapText(true);
		MenuFeedback->SetText(MenuText.IsEmpty() ? FText::FromString(TEXT("与朋友一同启程")) : MenuText);
	}
	RefreshLoadingPresentation();
}

// Controller 查询流程：只返回已注入协作者；空值表示 Root 处于未装配或拆除阶段，WBP 应禁用提交而非绕过 Controller。
UCatFrontendPageController* UCatFrontendRootWidget::GetPageController() const
{
	return PageController;
}

// SaveModel 查询流程：只返回已注入只读 Model；Root 不保存槽位列表副本，空值表示存档服务尚未装配。
UCatFrontendSaveModel* UCatFrontendRootWidget::GetSaveModel() const
{
	return SaveModel;
}

// RoomModel 查询流程：只返回已注入只读 Model；Root 不缓存 Steam 好友或房间成员，空值表示房间来源尚未装配。
UCatFrontendRoomModel* UCatFrontendRootWidget::GetRoomModel() const
{
	return RoomModel;
}

// SettingsModel 查询流程：只返回已注入只读 Model；Root 不复制设置草稿，空值表示设置来源尚未装配。
UCatFrontendSettingsModel* UCatFrontendRootWidget::GetSettingsModel() const
{
	return SettingsModel;
}

// 开始游戏点击流程：Controller 有效时交给它建立存档流程并显式切页；槽位变化由 SaveModel 通知刷新，本入口不额外重绘。
void UCatFrontendRootWidget::RequestStartGameFlow() { if (PageController) { PageController->RequestStartGameFlow(); } }

// 加入队伍点击流程：仅把占位意图交给有效 Controller 保持菜单；该入口尚无加入流程，也无需刷新隐藏的房间列表。
void UCatFrontendRootWidget::RequestJoinParty() { if (PageController) { PageController->RequestJoinParty(); } }

// 设置入口点击流程：交给有效 Controller 显示设置页；ShowFrontendSettings 会完成草稿控件回填，本入口不重复回填。
void UCatFrontendRootWidget::RequestOpenFrontendSettings() { if (PageController) { PageController->RequestOpenFrontendSettings(); } }

// 退出入口点击流程：交给有效 Controller 打开菜单内确认状态；Controller 随后的 ShowMenu 负责更新确认控件。
void UCatFrontendRootWidget::RequestShowExitConfirmation() { if (PageController) { PageController->RequestShowExitConfirmation(); } }

// 退出确认点击流程：交给有效 Controller 检查确认状态和本地玩家，再执行引擎退出；Root 不发出第二次退出或页面刷新。
void UCatFrontendRootWidget::RequestConfirmExit() { if (PageController) { PageController->RequestConfirmExit(); } }

// 退出取消点击流程：交给有效 Controller 清除确认状态；它重新显示同一菜单以收起确认控件。
void UCatFrontendRootWidget::RequestCancelExitConfirmation() { if (PageController) { PageController->RequestCancelExitConfirmation(); } }

// 存档行点击流程：把行保存的稳定 SlotId 交给有效 Controller 校验，再刷新存档行与反馈；Controller 已拆除时只做安全的只读刷新。
void UCatFrontendRootWidget::RequestSelectSaveSlot(const FName SlotId) { if (PageController) { PageController->RequestSelectSaveSlot(SlotId); } HandleSaveModelChanged(); }

// 新建存档点击流程：把原始输入名交给有效 Controller 校验与提交，再刷新存档反馈以显示同步拒绝；异步写盘结果继续来自 SaveModel。
void UCatFrontendRootWidget::RequestCreateSaveSlot(const FString& DisplayName) { if (PageController) { PageController->RequestCreateSaveSlot(DisplayName); } HandleSaveModelChanged(); }

// 读档点击流程：交给有效 Controller 使用已确认槽位请求读取，再刷新当前 busy 或拒绝文本；本入口不会因请求受理而切到房间。
void UCatFrontendRootWidget::RequestLoadSelectedSaveSlot() { if (PageController) { PageController->RequestLoadSelectedSaveSlot(); } HandleSaveModelChanged(); }

// 删除预确认点击流程：交给有效 Controller 保存待确认槽位并显示确认控件，再刷新存档文本；此处不提交实际删除。
void UCatFrontendRootWidget::RequestDeleteSelectedSaveSlot() { if (PageController) { PageController->RequestDeleteSelectedSaveSlot(); } HandleSaveModelChanged(); }

// 删除确认点击流程：交给有效 Controller 消费预确认槽位并请求删除，再刷新存档反馈；目录终态仍由 SaveModel 发布。
void UCatFrontendRootWidget::RequestConfirmDeleteSaveSlot() { if (PageController) { PageController->RequestConfirmDeleteSaveSlot(); } HandleSaveModelChanged(); }

// 返回点击流程：交给有效 Controller 依次处理确认、读档等待或离房；具体 Show 调用和终态通知负责刷新，本入口不猜目标页。
void UCatFrontendRootWidget::RequestCancel() { if (PageController) { PageController->RequestCancel(); } }

// 好友刷新点击流程：交给有效 Controller 请求平台刷新，再重读房间反馈与现有快照；请求完成前不会把旧缓存标成新结果。
void UCatFrontendRootWidget::RequestRefreshFriends() { if (PageController) { PageController->RequestRefreshFriends(); } HandleRoomModelChanged(); }

// 邀请点击流程：把当前行的 opaque 句柄交给有效 Controller，再刷新房间反馈；Root 不解析好友身份或把受理解释为对方已加入。
void UCatFrontendRootWidget::RequestInviteFriend(const FCatOnlineFriendHandle FriendHandle) { if (PageController) { PageController->RequestInviteFriend(FriendHandle); } HandleRoomModelChanged(); }

// 离房点击流程：交给有效 Controller 提交正式离开意图，再刷新同步反馈；最终返回页由后续 Session 事实决定。
void UCatFrontendRootWidget::RequestLeaveRoom() { if (PageController) { PageController->RequestLeaveRoom(); } HandleRoomModelChanged(); }

// 房主开始点击流程：交给有效 Controller 提交 Start 并消费真实预载事实，再刷新房间反馈和加载文本；切到 Loading 的权限仍属于 Controller。
void UCatFrontendRootWidget::RequestStartRoomGame() { if (PageController) { PageController->RequestStartRoomGame(); } HandleRoomModelChanged(); }

// 设置应用点击流程：交给有效 Controller 请求提交草稿并决定留页或返回，再回填设置控件和反馈；设备切换终态仍由 SettingsModel 通知。
void UCatFrontendRootWidget::RequestApplyFrontendSettings() { if (PageController) { PageController->RequestApplyFrontendSettings(); } HandleSettingsModelChanged(); }

// 设置取消点击流程：交给有效 Controller 丢弃草稿并返回菜单，再回填隐藏设置页；重新打开时不会保留已取消的控件值。
void UCatFrontendRootWidget::RequestCancelFrontendSettings() { if (PageController) { PageController->RequestCancelFrontendSettings(); } HandleSettingsModelChanged(); }

// 恢复默认点击流程：交给有效 Controller 请求默认草稿，再回填全部设置控件；实际应用仍需玩家后续确认。
void UCatFrontendRootWidget::RequestRestoreFrontendSettingsDefaults() { if (PageController) { PageController->RequestRestoreFrontendSettingsDefaults(); } HandleSettingsModelChanged(); }

// 输出设备刷新请求流程：将按钮意图交给 Controller 发起正式异步枚举，再立即按 pending 状态回填控件；不复用旧设备列表或直接访问 AudioMixer。
void UCatFrontendRootWidget::RequestRefreshAudioOutputDevices() { if (PageController) { PageController->RequestRefreshAudioOutputDevices(); } HandleSettingsModelChanged(); }

// 邀请码复制流程：直接读取 RoomModel 已确认的 joinlobby URI，非空时写入系统剪贴板并更新房间提示；Model 缺失或 URI 为空时不改变剪贴板。
void UCatFrontendRootWidget::RequestCopyRoomInviteCode()
{
	const FCatOnlineSnapshot Snapshot = RoomModel ? RoomModel->GetSnapshot() : FCatOnlineSnapshot();
	if (!Snapshot.JoinLobbyUri.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardCopy(*Snapshot.JoinLobbyUri);
		if (RoomResultTextBlock) { RoomResultTextBlock->SetText(FText::FromString(TEXT("邀请码已复制。"))); }
	}
}

// 游戏分类点击流程：交给有效 Controller 调用 SettingsModel 的游戏分类入口；界面回填由 Model 的 OnChanged 触发。
void UCatFrontendRootWidget::RequestSelectGameSettings() { if (PageController) { PageController->RequestSelectGameSettings(); } }

// 画面分类点击流程：交给有效 Controller 调用 SettingsModel 的画面分类入口；界面回填由 Model 的 OnChanged 触发。
void UCatFrontendRootWidget::RequestSelectGraphicsSettings() { if (PageController) { PageController->RequestSelectGraphicsSettings(); } }

// 声音分类点击流程：交给有效 Controller 调用 SettingsModel 的声音分类入口；界面回填由 Model 的 OnChanged 触发。
void UCatFrontendRootWidget::RequestSelectAudioSettings() { if (PageController) { PageController->RequestSelectAudioSettings(); } }

// 控制分类点击流程：交给有效 Controller 选择当前受限分类；其 Model 通知驱动只读回填，不创建暂缓实现的控制设置。
void UCatFrontendRootWidget::RequestSelectControlsSettings() { if (PageController) { PageController->RequestSelectControlsSettings(); } }

// UMG 初始化流程：父类完成 WidgetTree 创建后使 Root 可获得键盘焦点，并解析实际页面子树；协作者稍后注入时会再次幂等绑定。
void UCatFrontendRootWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	ResolvePageControls();
	BindPageControls();
}

// Escape 输入流程：Root 自身或子控件未消费按键而向上冒泡时，将 Escape 交给 Controller 的取消规则；按钮的 Enter/Space 等其他按键仍由 UMG 原路径处理。
FReply UCatFrontendRootWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		RequestCancel();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

// UMG 销毁流程：先解除 View 侧委托和协作者引用，再交还父类；不在销毁路径执行 Save、Online 或 Settings 的业务补偿。
void UCatFrontendRootWidget::NativeDestruct()
{
	ResetFrontend();
	Super::NativeDestruct();
}

// 子树解析流程：UMG 的 BindWidget 不会穿透嵌套 UserWidget，因此只对每个所属页面树查找自己的必需控件并记录缺口。
void UCatFrontendRootWidget::ResolvePageControls()
{
	FindPageControl<UPanelWidget>(MenuPage, TEXT("MenuCommands"), TEXT("MenuPage"));
	FindPageControl<UPanelWidget>(MenuPage, TEXT("ExitConfirmationOverlay"), TEXT("MenuPage"));
	FindPageControl<UTextBlock>(MenuPage, TEXT("MenuSubtitleText"), TEXT("MenuPage"));
	StartGameButton = FindPageControl<UButton>(MenuPage, TEXT("StartGameButton"), TEXT("MenuPage"));
	JoinPartyButton = FindPageControl<UButton>(MenuPage, TEXT("JoinPartyButton"), TEXT("MenuPage"));
	FrontendSettingsButton = FindPageControl<UButton>(MenuPage, TEXT("FrontendSettingsButton"), TEXT("MenuPage"));
	ExitGameButton = FindPageControl<UButton>(MenuPage, TEXT("ExitGameButton"), TEXT("MenuPage"));
	ConfirmExitButton = FindPageControl<UButton>(MenuPage, TEXT("ConfirmExitButton"), TEXT("MenuPage"));
	CancelExitButton = FindPageControl<UButton>(MenuPage, TEXT("CancelExitButton"), TEXT("MenuPage"));
	GameSettingsCategoryButton = FindPageControl<UButton>(FrontendSettingsPage, TEXT("GameSettingsCategoryButton"), TEXT("FrontendSettingsPage"));
	GraphicsSettingsCategoryButton = FindPageControl<UButton>(FrontendSettingsPage, TEXT("GraphicsSettingsCategoryButton"), TEXT("FrontendSettingsPage"));
	AudioSettingsCategoryButton = FindPageControl<UButton>(FrontendSettingsPage, TEXT("AudioSettingsCategoryButton"), TEXT("FrontendSettingsPage"));
	ControlsSettingsCategoryButton = FindPageControl<UButton>(FrontendSettingsPage, TEXT("ControlsSettingsCategoryButton"), TEXT("FrontendSettingsPage"));
	SaveResultTextBlock = FindPageControl<UTextBlock>(SaveListPage, TEXT("SaveResultTextBlock"), TEXT("SaveListPage"));
	SaveRowsScrollBox = FindPageControl<UScrollBox>(SaveListPage, TEXT("SaveRowsScrollBox"), TEXT("SaveListPage"));
	CreateSaveNameTextBox = FindPageControl<UEditableTextBox>(SaveListPage, TEXT("CreateSaveNameTextBox"), TEXT("SaveListPage"));
	CreateSaveButton = FindPageControl<UButton>(SaveListPage, TEXT("CreateSaveButton"), TEXT("SaveListPage"));
	LoadSelectedSaveButton = FindPageControl<UButton>(SaveListPage, TEXT("LoadSelectedSaveButton"), TEXT("SaveListPage"));
	DeleteSelectedSaveButton = FindPageControl<UButton>(SaveListPage, TEXT("DeleteSelectedSaveButton"), TEXT("SaveListPage"));
	ConfirmDeleteSaveButton = FindPageControl<UButton>(SaveListPage, TEXT("ConfirmDeleteSaveButton"), TEXT("SaveListPage"));
	CancelSaveButton = FindPageControl<UButton>(SaveListPage, TEXT("CancelSaveButton"), TEXT("SaveListPage"));
	RoomResultTextBlock = FindPageControl<UTextBlock>(RoomPage, TEXT("RoomResultTextBlock"), TEXT("RoomPage"));
	RoomInviteCodeText = FindPageControl<UTextBlock>(RoomPage, TEXT("RoomInviteCodeText"), TEXT("RoomPage"));
	RoomAccessPolicyText = FindPageControl<UTextBlock>(RoomPage, TEXT("RoomAccessPolicyText"), TEXT("RoomPage"));
	FriendSearchTextBox = FindPageControl<UEditableTextBox>(RoomPage, TEXT("FriendSearchTextBox"), TEXT("RoomPage"));
	FriendsScrollBox = FindPageControl<UScrollBox>(RoomPage, TEXT("FriendsScrollBox"), TEXT("RoomPage"));
	PlayersScrollBox = FindPageControl<UScrollBox>(RoomPage, TEXT("PlayersScrollBox"), TEXT("RoomPage"));
	RefreshFriendsButton = FindPageControl<UButton>(RoomPage, TEXT("RefreshFriendsButton"), TEXT("RoomPage"));
	LeaveRoomButton = FindPageControl<UButton>(RoomPage, TEXT("LeaveRoomButton"), TEXT("RoomPage"));
	StartRoomGameButton = FindPageControl<UButton>(RoomPage, TEXT("StartRoomGameButton"), TEXT("RoomPage"));
	CopyInviteCodeButton = FindPageControl<UButton>(RoomPage, TEXT("CopyInviteCodeButton"), TEXT("RoomPage"));
	FrontendSettingsResultTextBlock = FindPageControl<UTextBlock>(FrontendSettingsPage, TEXT("FrontendSettingsResultTextBlock"), TEXT("FrontendSettingsPage"));
	FindPageControl<UPanelWidget>(FrontendSettingsPage, TEXT("GameSettingsPanel"), TEXT("FrontendSettingsPage"));
	FindPageControl<UPanelWidget>(FrontendSettingsPage, TEXT("GraphicsSettingsPanel"), TEXT("FrontendSettingsPage"));
	FindPageControl<UPanelWidget>(FrontendSettingsPage, TEXT("AudioSettingsPanel"), TEXT("FrontendSettingsPage"));
	FindPageControl<UPanelWidget>(FrontendSettingsPage, TEXT("ControlsSettingsPanel"), TEXT("FrontendSettingsPage"));
	FindPageControl<UTextBlock>(FrontendSettingsPage, TEXT("SettingsDescriptionTextBlock"), TEXT("FrontendSettingsPage"));
	FindPageControl<USlider>(FrontendSettingsPage, TEXT("BrightnessSlider"), TEXT("FrontendSettingsPage"));
	FindPageControl<UCheckBox>(FrontendSettingsPage, TEXT("VibrationCheckBox"), TEXT("FrontendSettingsPage"));
	FindPageControl<UCheckBox>(FrontendSettingsPage, TEXT("VoiceChatCheckBox"), TEXT("FrontendSettingsPage"));
	FindPageControl<UCheckBox>(FrontendSettingsPage, TEXT("MuteAudioWhenUnfocusedCheckBox"), TEXT("FrontendSettingsPage"));
	FindPageControl<UComboBoxString>(FrontendSettingsPage, TEXT("AudioOutputDeviceComboBox"), TEXT("FrontendSettingsPage"));
	FindPageControl<UButton>(FrontendSettingsPage, TEXT("RefreshAudioOutputDevicesButton"), TEXT("FrontendSettingsPage"));
	FindPageControl<UComboBoxString>(FrontendSettingsPage, TEXT("VoiceInputModeComboBox"), TEXT("FrontendSettingsPage"));
	FindPageControl<UComboBoxString>(FrontendSettingsPage, TEXT("MicrophoneComboBox"), TEXT("FrontendSettingsPage"));
	FindPageControl<UTextBlock>(FrontendSettingsPage, TEXT("VoiceInputModeUnavailableText"), TEXT("FrontendSettingsPage"));
	FindPageControl<UTextBlock>(FrontendSettingsPage, TEXT("MicrophoneUnavailableText"), TEXT("FrontendSettingsPage"));
	LoadingProgressTextBlock = FindPageControl<UTextBlock>(LoadingPage, TEXT("LoadingProgressTextBlock"), TEXT("LoadingPage"));
	LoadingProgressBar = FindPageControl<UProgressBar>(LoadingPage, TEXT("LoadingProgressBar"), TEXT("LoadingPage"));
	FindPageControl<UTextBlock>(LoadingPage, TEXT("LoadingDayTextBlock"), TEXT("LoadingPage"));
	FindPageControl<UTextBlock>(LoadingPage, TEXT("LoadingSacrificeProgressTextBlock"), TEXT("LoadingPage"));
}

// 所属页面控件查找流程：页面缺失时记录页面与控件名并返回空；否则仅在该页面树按名查找和 Cast，缺失或类型不符记录合同错误并返回空，不跨页搜索或构造替身。
template <typename WidgetType>
WidgetType* UCatFrontendRootWidget::FindPageControl(UUserWidget* Page, const FName ControlName, const TCHAR* PageName) const
{
	if (!Page)
	{
		UE_LOG(LogCatUI, Error, TEXT("Event=frontend_widget_contract_missing Page=%s Control=%s Reason=page_missing"), PageName, *ControlName.ToString());
		return nullptr;
	}
	WidgetType* Result = Cast<WidgetType>(Page->GetWidgetFromName(ControlName));
	if (!Result)
	{
		UE_LOG(LogCatUI, Error, TEXT("Event=frontend_widget_contract_missing Page=%s Control=%s Reason=missing_or_wrong_type"), PageName, *ControlName.ToString());
	}
	return Result;
}

// 按钮绑定流程：逐个给已解析的实际页面按钮添加唯一动态委托；缺失控件已经由解析阶段记录，绝不创建原生替身。
void UCatFrontendRootWidget::BindPageControls()
{
	if (StartGameButton) { StartGameButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RequestStartGameFlow); }
	if (JoinPartyButton) { JoinPartyButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RequestJoinParty); }
	if (FrontendSettingsButton) { FrontendSettingsButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RequestOpenFrontendSettings); }
	if (ExitGameButton) { ExitGameButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RequestShowExitConfirmation); }
	if (ConfirmExitButton) { ConfirmExitButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RequestConfirmExit); }
	if (CancelExitButton) { CancelExitButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RequestCancelExitConfirmation); }
	if (GameSettingsCategoryButton) { GameSettingsCategoryButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RequestSelectGameSettings); }
	if (GraphicsSettingsCategoryButton) { GraphicsSettingsCategoryButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RequestSelectGraphicsSettings); }
	if (AudioSettingsCategoryButton) { AudioSettingsCategoryButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RequestSelectAudioSettings); }
	if (ControlsSettingsCategoryButton) { ControlsSettingsCategoryButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RequestSelectControlsSettings); }
	if (UButton* ApplyButton = FindPageControl<UButton>(FrontendSettingsPage, TEXT("ApplySettingsButton"), TEXT("FrontendSettingsPage"))) { ApplyButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RequestApplyFrontendSettings); }
	if (UButton* RestoreButton = FindPageControl<UButton>(FrontendSettingsPage, TEXT("RestoreSettingsDefaultsButton"), TEXT("FrontendSettingsPage"))) { RestoreButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RequestRestoreFrontendSettingsDefaults); }
	if (UButton* CancelButton = FindPageControl<UButton>(FrontendSettingsPage, TEXT("CancelSettingsButton"), TEXT("FrontendSettingsPage"))) { CancelButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RequestCancelFrontendSettings); }
	if (UComboBoxString* Control = FindPageControl<UComboBoxString>(FrontendSettingsPage, TEXT("LanguageComboBox"), TEXT("FrontendSettingsPage"))) { Control->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::HandleLanguageSelectionChanged); }
	if (UComboBoxString* Control = FindPageControl<UComboBoxString>(FrontendSettingsPage, TEXT("FullscreenModeComboBox"), TEXT("FrontendSettingsPage"))) { Control->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::HandleFullscreenModeSelectionChanged); }
	if (UComboBoxString* Control = FindPageControl<UComboBoxString>(FrontendSettingsPage, TEXT("ScreenResolutionComboBox"), TEXT("FrontendSettingsPage"))) { Control->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::HandleScreenResolutionSelectionChanged); }
	if (UComboBoxString* Control = FindPageControl<UComboBoxString>(FrontendSettingsPage, TEXT("OverallQualityComboBox"), TEXT("FrontendSettingsPage"))) { Control->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::HandleQualitySelectionChanged); }
	if (UCheckBox* Control = FindPageControl<UCheckBox>(FrontendSettingsPage, TEXT("VSyncCheckBox"), TEXT("FrontendSettingsPage"))) { Control->OnCheckStateChanged.AddUniqueDynamic(this, &ThisClass::HandleVSyncChanged); }
	if (USlider* Control = FindPageControl<USlider>(FrontendSettingsPage, TEXT("UIScaleSlider"), TEXT("FrontendSettingsPage"))) { Control->OnValueChanged.AddUniqueDynamic(this, &ThisClass::HandleUIScaleChanged); }
	if (USlider* Control = FindPageControl<USlider>(FrontendSettingsPage, TEXT("BrightnessSlider"), TEXT("FrontendSettingsPage"))) { Control->OnValueChanged.AddUniqueDynamic(this, &ThisClass::HandleBrightnessChanged); }
	if (UCheckBox* Control = FindPageControl<UCheckBox>(FrontendSettingsPage, TEXT("VibrationCheckBox"), TEXT("FrontendSettingsPage"))) { Control->OnCheckStateChanged.AddUniqueDynamic(this, &ThisClass::HandleVibrationChanged); }
	if (UCheckBox* Control = FindPageControl<UCheckBox>(FrontendSettingsPage, TEXT("VoiceChatCheckBox"), TEXT("FrontendSettingsPage"))) { Control->OnCheckStateChanged.AddUniqueDynamic(this, &ThisClass::HandleVoiceChatChanged); }
	if (UCheckBox* Control = FindPageControl<UCheckBox>(FrontendSettingsPage, TEXT("MuteAudioWhenUnfocusedCheckBox"), TEXT("FrontendSettingsPage"))) { Control->OnCheckStateChanged.AddUniqueDynamic(this, &ThisClass::HandleMuteAudioWhenUnfocusedChanged); }
	if (UComboBoxString* Control = FindPageControl<UComboBoxString>(FrontendSettingsPage, TEXT("AudioOutputDeviceComboBox"), TEXT("FrontendSettingsPage"))) { Control->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::HandleAudioOutputDeviceSelectionChanged); }
	if (UButton* Control = FindPageControl<UButton>(FrontendSettingsPage, TEXT("RefreshAudioOutputDevicesButton"), TEXT("FrontendSettingsPage"))) { Control->OnClicked.AddUniqueDynamic(this, &ThisClass::RequestRefreshAudioOutputDevices); }
	if (USlider* Control = FindPageControl<USlider>(FrontendSettingsPage, TEXT("MasterVolumeSlider"), TEXT("FrontendSettingsPage"))) { Control->OnValueChanged.AddUniqueDynamic(this, &ThisClass::HandleMasterVolumeChanged); }
	if (USlider* Control = FindPageControl<USlider>(FrontendSettingsPage, TEXT("MusicVolumeSlider"), TEXT("FrontendSettingsPage"))) { Control->OnValueChanged.AddUniqueDynamic(this, &ThisClass::HandleMusicVolumeChanged); }
	if (USlider* Control = FindPageControl<USlider>(FrontendSettingsPage, TEXT("SFXVolumeSlider"), TEXT("FrontendSettingsPage"))) { Control->OnValueChanged.AddUniqueDynamic(this, &ThisClass::HandleSFXVolumeChanged); }
	if (USlider* Control = FindPageControl<USlider>(FrontendSettingsPage, TEXT("AmbienceVolumeSlider"), TEXT("FrontendSettingsPage"))) { Control->OnValueChanged.AddUniqueDynamic(this, &ThisClass::HandleAmbienceVolumeChanged); }
	if (USlider* Control = FindPageControl<USlider>(FrontendSettingsPage, TEXT("VoiceVolumeSlider"), TEXT("FrontendSettingsPage"))) { Control->OnValueChanged.AddUniqueDynamic(this, &ThisClass::HandleVoiceVolumeChanged); }
	if (CreateSaveButton) { CreateSaveButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleCreateSaveClicked); }
	if (LoadSelectedSaveButton) { LoadSelectedSaveButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RequestLoadSelectedSaveSlot); }
	if (DeleteSelectedSaveButton) { DeleteSelectedSaveButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RequestDeleteSelectedSaveSlot); }
	if (ConfirmDeleteSaveButton) { ConfirmDeleteSaveButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RequestConfirmDeleteSaveSlot); }
	if (CancelSaveButton) { CancelSaveButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RequestCancel); }
	if (FriendSearchTextBox) { FriendSearchTextBox->OnTextChanged.AddUniqueDynamic(this, &ThisClass::HandleFriendSearchTextChanged); }
	if (RefreshFriendsButton) { RefreshFriendsButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RequestRefreshFriends); }
	if (LeaveRoomButton) { LeaveRoomButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RequestLeaveRoom); }
	if (StartRoomGameButton) { StartRoomGameButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RequestStartRoomGame); }
	if (CopyInviteCodeButton) { CopyInviteCodeButton->OnClicked.AddUniqueDynamic(this, &ThisClass::RequestCopyRoomInviteCode); }
}

// 按钮解绑流程：逐个移除本 Root 注册的动态委托；空指针和重复拆除安全跳过，防止 Widget 重建叠加点击回调。
void UCatFrontendRootWidget::UnbindPageControls()
{
	if (StartGameButton) { StartGameButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestStartGameFlow); }
	if (JoinPartyButton) { JoinPartyButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestJoinParty); }
	if (FrontendSettingsButton) { FrontendSettingsButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestOpenFrontendSettings); }
	if (ExitGameButton) { ExitGameButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestShowExitConfirmation); }
	if (ConfirmExitButton) { ConfirmExitButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestConfirmExit); }
	if (CancelExitButton) { CancelExitButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestCancelExitConfirmation); }
	if (GameSettingsCategoryButton) { GameSettingsCategoryButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestSelectGameSettings); }
	if (GraphicsSettingsCategoryButton) { GraphicsSettingsCategoryButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestSelectGraphicsSettings); }
	if (AudioSettingsCategoryButton) { AudioSettingsCategoryButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestSelectAudioSettings); }
	if (ControlsSettingsCategoryButton) { ControlsSettingsCategoryButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestSelectControlsSettings); }
	if (UButton* ApplyButton = FindPageControl<UButton>(FrontendSettingsPage, TEXT("ApplySettingsButton"), TEXT("FrontendSettingsPage"))) { ApplyButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestApplyFrontendSettings); }
	if (UButton* RestoreButton = FindPageControl<UButton>(FrontendSettingsPage, TEXT("RestoreSettingsDefaultsButton"), TEXT("FrontendSettingsPage"))) { RestoreButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestRestoreFrontendSettingsDefaults); }
	if (UButton* CancelButton = FindPageControl<UButton>(FrontendSettingsPage, TEXT("CancelSettingsButton"), TEXT("FrontendSettingsPage"))) { CancelButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestCancelFrontendSettings); }
	if (UComboBoxString* Control = FindPageControl<UComboBoxString>(FrontendSettingsPage, TEXT("LanguageComboBox"), TEXT("FrontendSettingsPage"))) { Control->OnSelectionChanged.RemoveDynamic(this, &ThisClass::HandleLanguageSelectionChanged); }
	if (UComboBoxString* Control = FindPageControl<UComboBoxString>(FrontendSettingsPage, TEXT("FullscreenModeComboBox"), TEXT("FrontendSettingsPage"))) { Control->OnSelectionChanged.RemoveDynamic(this, &ThisClass::HandleFullscreenModeSelectionChanged); }
	if (UComboBoxString* Control = FindPageControl<UComboBoxString>(FrontendSettingsPage, TEXT("ScreenResolutionComboBox"), TEXT("FrontendSettingsPage"))) { Control->OnSelectionChanged.RemoveDynamic(this, &ThisClass::HandleScreenResolutionSelectionChanged); }
	if (UComboBoxString* Control = FindPageControl<UComboBoxString>(FrontendSettingsPage, TEXT("OverallQualityComboBox"), TEXT("FrontendSettingsPage"))) { Control->OnSelectionChanged.RemoveDynamic(this, &ThisClass::HandleQualitySelectionChanged); }
	if (UCheckBox* Control = FindPageControl<UCheckBox>(FrontendSettingsPage, TEXT("VSyncCheckBox"), TEXT("FrontendSettingsPage"))) { Control->OnCheckStateChanged.RemoveDynamic(this, &ThisClass::HandleVSyncChanged); }
	if (USlider* Control = FindPageControl<USlider>(FrontendSettingsPage, TEXT("UIScaleSlider"), TEXT("FrontendSettingsPage"))) { Control->OnValueChanged.RemoveDynamic(this, &ThisClass::HandleUIScaleChanged); }
	if (USlider* Control = FindPageControl<USlider>(FrontendSettingsPage, TEXT("BrightnessSlider"), TEXT("FrontendSettingsPage"))) { Control->OnValueChanged.RemoveDynamic(this, &ThisClass::HandleBrightnessChanged); }
	if (UCheckBox* Control = FindPageControl<UCheckBox>(FrontendSettingsPage, TEXT("VibrationCheckBox"), TEXT("FrontendSettingsPage"))) { Control->OnCheckStateChanged.RemoveDynamic(this, &ThisClass::HandleVibrationChanged); }
	if (UCheckBox* Control = FindPageControl<UCheckBox>(FrontendSettingsPage, TEXT("VoiceChatCheckBox"), TEXT("FrontendSettingsPage"))) { Control->OnCheckStateChanged.RemoveDynamic(this, &ThisClass::HandleVoiceChatChanged); }
	if (UCheckBox* Control = FindPageControl<UCheckBox>(FrontendSettingsPage, TEXT("MuteAudioWhenUnfocusedCheckBox"), TEXT("FrontendSettingsPage"))) { Control->OnCheckStateChanged.RemoveDynamic(this, &ThisClass::HandleMuteAudioWhenUnfocusedChanged); }
	if (UComboBoxString* Control = FindPageControl<UComboBoxString>(FrontendSettingsPage, TEXT("AudioOutputDeviceComboBox"), TEXT("FrontendSettingsPage"))) { Control->OnSelectionChanged.RemoveDynamic(this, &ThisClass::HandleAudioOutputDeviceSelectionChanged); }
	if (UButton* Control = FindPageControl<UButton>(FrontendSettingsPage, TEXT("RefreshAudioOutputDevicesButton"), TEXT("FrontendSettingsPage"))) { Control->OnClicked.RemoveDynamic(this, &ThisClass::RequestRefreshAudioOutputDevices); }
	if (USlider* Control = FindPageControl<USlider>(FrontendSettingsPage, TEXT("MasterVolumeSlider"), TEXT("FrontendSettingsPage"))) { Control->OnValueChanged.RemoveDynamic(this, &ThisClass::HandleMasterVolumeChanged); }
	if (USlider* Control = FindPageControl<USlider>(FrontendSettingsPage, TEXT("MusicVolumeSlider"), TEXT("FrontendSettingsPage"))) { Control->OnValueChanged.RemoveDynamic(this, &ThisClass::HandleMusicVolumeChanged); }
	if (USlider* Control = FindPageControl<USlider>(FrontendSettingsPage, TEXT("SFXVolumeSlider"), TEXT("FrontendSettingsPage"))) { Control->OnValueChanged.RemoveDynamic(this, &ThisClass::HandleSFXVolumeChanged); }
	if (USlider* Control = FindPageControl<USlider>(FrontendSettingsPage, TEXT("AmbienceVolumeSlider"), TEXT("FrontendSettingsPage"))) { Control->OnValueChanged.RemoveDynamic(this, &ThisClass::HandleAmbienceVolumeChanged); }
	if (USlider* Control = FindPageControl<USlider>(FrontendSettingsPage, TEXT("VoiceVolumeSlider"), TEXT("FrontendSettingsPage"))) { Control->OnValueChanged.RemoveDynamic(this, &ThisClass::HandleVoiceVolumeChanged); }
	if (CreateSaveButton) { CreateSaveButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleCreateSaveClicked); }
	if (LoadSelectedSaveButton) { LoadSelectedSaveButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestLoadSelectedSaveSlot); }
	if (DeleteSelectedSaveButton) { DeleteSelectedSaveButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestDeleteSelectedSaveSlot); }
	if (ConfirmDeleteSaveButton) { ConfirmDeleteSaveButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestConfirmDeleteSaveSlot); }
	if (CancelSaveButton) { CancelSaveButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestCancel); }
	if (FriendSearchTextBox) { FriendSearchTextBox->OnTextChanged.RemoveDynamic(this, &ThisClass::HandleFriendSearchTextChanged); }
	if (RefreshFriendsButton) { RefreshFriendsButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestRefreshFriends); }
	if (LeaveRoomButton) { LeaveRoomButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestLeaveRoom); }
	if (StartRoomGameButton) { StartRoomGameButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestStartRoomGame); }
	if (CopyInviteCodeButton) { CopyInviteCodeButton->OnClicked.RemoveDynamic(this, &ThisClass::RequestCopyRoomInviteCode); }
}

// Model 订阅流程：每个 Model 各自只有一个刷新入口；订阅后 Root 更新实际原生控件，并可选调用纯表现扩展，不跨模型归纳业务状态。
void UCatFrontendRootWidget::BindModelChanges()
{
	if (SaveModel) { SaveModelChangedHandle = SaveModel->OnChanged.AddUObject(this, &ThisClass::HandleSaveModelChanged); }
	if (RoomModel) { RoomModelChangedHandle = RoomModel->OnChanged.AddUObject(this, &ThisClass::HandleRoomModelChanged); }
	if (SettingsModel) { SettingsModelChangedHandle = SettingsModel->OnChanged.AddUObject(this, &ThisClass::HandleSettingsModelChanged); }
}

// Model 解绑流程：按原 Model 和句柄成对移除订阅；句柄随后复位，避免下一次 LocalPlayer 装配误删新的监听器。
void UCatFrontendRootWidget::UnbindModelChanges()
{
	if (SaveModel) { SaveModel->OnChanged.Remove(SaveModelChangedHandle); }
	if (RoomModel) { RoomModel->OnChanged.Remove(RoomModelChangedHandle); }
	if (SettingsModel) { SettingsModel->OnChanged.Remove(SettingsModelChangedHandle); }
	SaveModelChangedHandle.Reset();
	RoomModelChangedHandle.Reset();
	SettingsModelChangedHandle.Reset();
}

// 存档刷新流程：按来源刷新反馈并重建真实槽位行；Save 或 Online 操作未结束时禁用写入与读取按钮，Root 不取消在途命令或复制业务状态。
void UCatFrontendRootWidget::HandleSaveModelChanged()
{
	RefreshFlowFeedback();
	const FCatOnlineSnapshot Snapshot = RoomModel ? RoomModel->GetSnapshot() : FCatOnlineSnapshot();
	const bool bCanSubmit = SaveModel && !SaveModel->IsBusy() && Snapshot.ActiveOperation == ECatOnlineOperation::None
		&& Snapshot.SessionState == ECatOnlineSessionState::NoSession && !Snapshot.bIsAcceptedInvitePending;
	const bool bHasSelection = PageController && !PageController->GetSelectedSlotId().IsNone();
	if (CreateSaveButton) { CreateSaveButton->SetIsEnabled(bCanSubmit); }
	if (CreateSaveNameTextBox) { CreateSaveNameTextBox->SetIsEnabled(bCanSubmit); }
	if (LoadSelectedSaveButton) { LoadSelectedSaveButton->SetIsEnabled(bCanSubmit && bHasSelection); }
	if (DeleteSelectedSaveButton) { DeleteSelectedSaveButton->SetIsEnabled(bCanSubmit && bHasSelection); }
	if (ConfirmDeleteSaveButton) { ConfirmDeleteSaveButton->SetIsEnabled(bCanSubmit && PageController && !PageController->GetPendingDeleteSlotId().IsNone()); }
	if (SaveRowsScrollBox) { SaveRowsScrollBox->SetIsEnabled(bCanSubmit); }
	RebuildSaveRows();
	BP_RenderSaveList();
}

// 房间刷新流程：按来源刷新正式反馈、好友与成员行，并同步存档页的 Online busy 可操作性；所有数据只读，蓝图事件不承担必要呈现。
void UCatFrontendRootWidget::HandleRoomModelChanged()
{
	HandleSaveModelChanged();
	RefreshRoomPresentation();
	RebuildRoomRows();
	RefreshLoadingPresentation();
	BP_RenderRoom();
}

// 设置刷新流程：先按 Model 的四个命名查询切换面板与说明，再在回填保护下重建合法下拉并写入真实草稿；未知模式/质量留空不推定值，所有副作用仍需用户经 Model Apply。
void UCatFrontendRootWidget::HandleSettingsModelChanged()
{
	RefreshFlowFeedback();
	if (SettingsModel)
	{
		bRefreshingSettingsControls = true;
		if (UPanelWidget* Panel = FindPageControl<UPanelWidget>(FrontendSettingsPage, TEXT("GameSettingsPanel"), TEXT("FrontendSettingsPage"))) { Panel->SetVisibility(SettingsModel->IsGameSelected() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
		if (UPanelWidget* Panel = FindPageControl<UPanelWidget>(FrontendSettingsPage, TEXT("GraphicsSettingsPanel"), TEXT("FrontendSettingsPage"))) { Panel->SetVisibility(SettingsModel->IsGraphicsSelected() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
		if (UPanelWidget* Panel = FindPageControl<UPanelWidget>(FrontendSettingsPage, TEXT("AudioSettingsPanel"), TEXT("FrontendSettingsPage"))) { Panel->SetVisibility(SettingsModel->IsAudioSelected() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
		if (UPanelWidget* Panel = FindPageControl<UPanelWidget>(FrontendSettingsPage, TEXT("ControlsSettingsPanel"), TEXT("FrontendSettingsPage"))) { Panel->SetVisibility(SettingsModel->IsControlsSelected() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
		if (UTextBlock* Description = FindPageControl<UTextBlock>(FrontendSettingsPage, TEXT("SettingsDescriptionTextBlock"), TEXT("FrontendSettingsPage")))
		{
			Description->SetText(FText::FromString(SettingsModel->IsGameSelected() ? TEXT("语言、语音与手柄震动")
				: SettingsModel->IsGraphicsSelected() ? TEXT("显示、画质、亮度与界面缩放")
				: SettingsModel->IsAudioSelected() ? TEXT("音量、输出设备与后台声音") : TEXT("控制细项暂未开放")));
		}
		if (UComboBoxString* Control = FindPageControl<UComboBoxString>(FrontendSettingsPage, TEXT("LanguageComboBox"), TEXT("FrontendSettingsPage")))
		{
			Control->ClearOptions();
			for (int32 Index = 0; Index < SettingsModel->GetAvailableLanguageCount(); ++Index) { Control->AddOption(SettingsModel->GetAvailableLanguage(Index)); }
			Control->SetSelectedOption(SettingsModel->GetDraftLanguage());
		}
		if (UComboBoxString* Control = FindPageControl<UComboBoxString>(FrontendSettingsPage, TEXT("FullscreenModeComboBox"), TEXT("FrontendSettingsPage")))
		{
			Control->ClearOptions();
			Control->AddOption(TEXT("全屏"));
			Control->AddOption(TEXT("无边框窗口"));
			Control->AddOption(TEXT("窗口"));
			const EWindowMode::Type Mode = SettingsModel->GetDraftFullscreenMode();
			if (Mode == EWindowMode::Fullscreen) { Control->SetSelectedOption(TEXT("全屏")); }
			else if (Mode == EWindowMode::WindowedFullscreen) { Control->SetSelectedOption(TEXT("无边框窗口")); }
			else if (Mode == EWindowMode::Windowed) { Control->SetSelectedOption(TEXT("窗口")); }
			else { Control->ClearSelection(); }
		}
		if (UComboBoxString* Control = FindPageControl<UComboBoxString>(FrontendSettingsPage, TEXT("ScreenResolutionComboBox"), TEXT("FrontendSettingsPage")))
		{
			TArray<FIntPoint> Resolutions;
			Control->ClearOptions();
			if (SettingsModel->GetSupportedScreenResolutions(Resolutions))
			{
				for (const FIntPoint& Resolution : Resolutions) { Control->AddOption(FString::Printf(TEXT("%d x %d"), Resolution.X, Resolution.Y)); }
				const FIntPoint Resolution = SettingsModel->GetDraftScreenResolution();
				Control->SetSelectedOption(FString::Printf(TEXT("%d x %d"), Resolution.X, Resolution.Y));
			}
			Control->SetIsEnabled(Resolutions.Num() > 0);
		}
		if (UComboBoxString* Control = FindPageControl<UComboBoxString>(FrontendSettingsPage, TEXT("OverallQualityComboBox"), TEXT("FrontendSettingsPage")))
		{
			Control->ClearOptions();
			Control->AddOption(TEXT("自定义"));
			Control->AddOption(TEXT("低"));
			Control->AddOption(TEXT("中"));
			Control->AddOption(TEXT("高"));
			Control->AddOption(TEXT("史诗"));
			Control->AddOption(TEXT("电影"));
			const int32 Level = SettingsModel->GetDraftOverallScalabilityLevel();
			if (Level >= -1 && Level <= 4) { Control->SetSelectedOption(Level == -1 ? TEXT("自定义") : Level == 0 ? TEXT("低") : Level == 1 ? TEXT("中") : Level == 2 ? TEXT("高") : Level == 3 ? TEXT("史诗") : TEXT("电影")); }
			else { Control->ClearSelection(); }
		}
		if (UCheckBox* Control = FindPageControl<UCheckBox>(FrontendSettingsPage, TEXT("VSyncCheckBox"), TEXT("FrontendSettingsPage"))) { Control->SetIsChecked(SettingsModel->GetDraftVSyncEnabled()); }
		if (USlider* Control = FindPageControl<USlider>(FrontendSettingsPage, TEXT("UIScaleSlider"), TEXT("FrontendSettingsPage"))) { Control->SetValue((SettingsModel->GetDraftUIScale() - 0.75f) / 1.25f); }
		if (USlider* Control = FindPageControl<USlider>(FrontendSettingsPage, TEXT("BrightnessSlider"), TEXT("FrontendSettingsPage"))) { Control->SetValue((SettingsModel->GetDraftDisplayGamma() - 0.5f) / 4.5f); Control->SetIsEnabled(SettingsModel->IsBrightnessSettingAvailable()); }
		if (UCheckBox* Control = FindPageControl<UCheckBox>(FrontendSettingsPage, TEXT("VibrationCheckBox"), TEXT("FrontendSettingsPage"))) { Control->SetIsChecked(SettingsModel->GetDraftVibrationEnabled()); Control->SetIsEnabled(SettingsModel->IsVibrationSettingAvailable()); }
		if (UCheckBox* Control = FindPageControl<UCheckBox>(FrontendSettingsPage, TEXT("VoiceChatCheckBox"), TEXT("FrontendSettingsPage"))) { Control->SetIsChecked(SettingsModel->GetDraftVoiceChatEnabled()); Control->SetIsEnabled(SettingsModel->IsVoiceChatSettingAvailable()); }
		if (UCheckBox* Control = FindPageControl<UCheckBox>(FrontendSettingsPage, TEXT("MuteAudioWhenUnfocusedCheckBox"), TEXT("FrontendSettingsPage"))) { Control->SetIsChecked(SettingsModel->GetDraftMuteAudioWhenUnfocused()); }
		if (UTextBlock* Control = FindPageControl<UTextBlock>(FrontendSettingsPage, TEXT("VoiceInputModeUnavailableText"), TEXT("FrontendSettingsPage")))
		{
			Control->SetText(FText::FromString(SettingsModel->IsInputModeSettingAvailable()
				? TEXT("当前语音输入方式由平台管理。") : TEXT("当前语音服务不支持切换输入模式。")));
			Control->SetVisibility(ESlateVisibility::Visible);
			Control->SetIsEnabled(false);
		}
		if (UComboBoxString* Control = FindPageControl<UComboBoxString>(FrontendSettingsPage, TEXT("VoiceInputModeComboBox"), TEXT("FrontendSettingsPage")))
		{
			Control->ClearOptions();
			Control->AddOption(TEXT("当前平台不支持此设置"));
			Control->SetSelectedOption(TEXT("当前平台不支持此设置"));
			Control->SetIsEnabled(false);
		}
		if (UTextBlock* Control = FindPageControl<UTextBlock>(FrontendSettingsPage, TEXT("MicrophoneUnavailableText"), TEXT("FrontendSettingsPage")))
		{
			Control->SetText(FText::FromString(SettingsModel->IsMicrophoneSettingAvailable()
				? TEXT("当前麦克风由平台管理。") : TEXT("当前语音服务不支持选择麦克风，请在系统声音设置中更改默认输入设备。")));
			Control->SetVisibility(ESlateVisibility::Visible);
			Control->SetIsEnabled(false);
		}
		if (UComboBoxString* Control = FindPageControl<UComboBoxString>(FrontendSettingsPage, TEXT("MicrophoneComboBox"), TEXT("FrontendSettingsPage")))
		{
			Control->ClearOptions();
			Control->AddOption(TEXT("当前平台不支持此设置"));
			Control->SetSelectedOption(TEXT("当前平台不支持此设置"));
			Control->SetIsEnabled(false);
		}
		if (UComboBoxString* Control = FindPageControl<UComboBoxString>(FrontendSettingsPage, TEXT("AudioOutputDeviceComboBox"), TEXT("FrontendSettingsPage")))
		{
			AudioOutputDeviceIdsByOption.Reset();
			Control->ClearOptions();
			FString SelectedOption;
			for (int32 Index = 0; Index < SettingsModel->GetAudioOutputDeviceCount(); ++Index)
			{
				const FString& DeviceName = SettingsModel->GetAudioOutputDeviceName(Index);
				const FString& DeviceId = SettingsModel->GetAudioOutputDeviceId(Index);
				if (DeviceName.IsEmpty() || DeviceId.IsEmpty()) { continue; }
				FString Option = DeviceName;
				for (int32 DuplicateIndex = 2; AudioOutputDeviceIdsByOption.Contains(Option); ++DuplicateIndex) { Option = FString::Printf(TEXT("%s (%d)"), *DeviceName, DuplicateIndex); }
				AudioOutputDeviceIdsByOption.Add(Option, DeviceId);
				Control->AddOption(Option);
				if (DeviceId == SettingsModel->GetDraftAudioOutputDeviceId()) { SelectedOption = Option; }
			}
			Control->SetSelectedOption(SelectedOption);
			Control->SetIsEnabled(SettingsModel->IsOutputDeviceSettingAvailable() && !SettingsModel->IsAudioOutputDeviceOperationPending());
		}
		if (UButton* Control = FindPageControl<UButton>(FrontendSettingsPage, TEXT("RefreshAudioOutputDevicesButton"), TEXT("FrontendSettingsPage"))) { Control->SetIsEnabled(!SettingsModel->IsAudioOutputDeviceOperationPending()); }
		const bool bAudioAvailable = SettingsModel->IsAudioRoutingAvailable();
		if (USlider* Control = FindPageControl<USlider>(FrontendSettingsPage, TEXT("MasterVolumeSlider"), TEXT("FrontendSettingsPage"))) { Control->SetValue(SettingsModel->GetDraftMasterVolume()); Control->SetIsEnabled(bAudioAvailable); }
		if (USlider* Control = FindPageControl<USlider>(FrontendSettingsPage, TEXT("MusicVolumeSlider"), TEXT("FrontendSettingsPage"))) { Control->SetValue(SettingsModel->GetDraftMusicVolume()); Control->SetIsEnabled(bAudioAvailable); }
		if (USlider* Control = FindPageControl<USlider>(FrontendSettingsPage, TEXT("SFXVolumeSlider"), TEXT("FrontendSettingsPage"))) { Control->SetValue(SettingsModel->GetDraftSFXVolume()); Control->SetIsEnabled(bAudioAvailable); }
		if (USlider* Control = FindPageControl<USlider>(FrontendSettingsPage, TEXT("AmbienceVolumeSlider"), TEXT("FrontendSettingsPage"))) { Control->SetValue(SettingsModel->GetDraftAmbienceVolume()); Control->SetIsEnabled(bAudioAvailable); }
		if (USlider* Control = FindPageControl<USlider>(FrontendSettingsPage, TEXT("VoiceVolumeSlider"), TEXT("FrontendSettingsPage"))) { Control->SetValue(SettingsModel->GetDraftVoiceVolume()); Control->SetIsEnabled(bAudioAvailable); }
		bRefreshingSettingsControls = false;
	}
	BP_RenderFrontendSettings();
}

// 加载表现流程：先按 RoomModel 真实比例切换确定进度或 marquee，未知时优先读取 RoomModel 从 Online 派生的阶段原因；Host 再按正式活动槽匹配 Save 摘要，显示最后保存的天数和献祭记录，Client 无来源和新槽无记录均明示未知，不冒充当前 Run。
void UCatFrontendRootWidget::RefreshLoadingPresentation()
{
	const FCatOnlineSnapshot Snapshot = RoomModel ? RoomModel->GetSnapshot() : FCatOnlineSnapshot();
	const float Progress = RoomModel ? RoomModel->GetGameplayLoadProgress() : -1.0f;
	if (Progress >= 0.0f && Progress <= 100.0f)
	{
		if (LoadingProgressTextBlock) { LoadingProgressTextBlock->SetText(FText::FromString(FString::Printf(TEXT("载入中 %.0f%%"), Progress))); }
		if (LoadingProgressBar) { LoadingProgressBar->SetIsMarquee(false); LoadingProgressBar->SetPercent(Progress / 100.0f); }
	}
	else
	{
		const FText LocalFeedback = PageController ? PageController->GetLastResultText(RoomModel) : FText::GetEmpty();
		const FText GameplayLoadStatus = RoomModel ? RoomModel->GetGameplayLoadStatusText() : FText::GetEmpty();
		if (LoadingProgressTextBlock)
		{
			LoadingProgressTextBlock->SetText(!GameplayLoadStatus.IsEmpty() ? GameplayLoadStatus
				: !LocalFeedback.IsEmpty() ? LocalFeedback
				: RoomModel && !RoomModel->GetLastResultText().IsEmpty() ? RoomModel->GetLastResultText()
				: FText::FromString(TEXT("正在等待正式加载状态。")));
		}
		if (LoadingProgressBar) { LoadingProgressBar->SetIsMarquee(true); }
	}
	const FCatSaveSlotSummary* LoadedSummary = nullptr;
	if (Snapshot.SessionRole == ECatOnlineSessionRole::Host && SaveModel && SaveModel->HasLoadedRunForTravel())
	{
		for (const FCatSaveSlotSummary& Summary : SaveModel->GetSlotSummaries())
		{
			if (Summary.SlotId == SaveModel->GetActiveSlotId()) { LoadedSummary = &Summary; break; }
		}
	}
	if (UTextBlock* DayText = FindPageControl<UTextBlock>(LoadingPage, TEXT("LoadingDayTextBlock"), TEXT("LoadingPage")))
	{
		DayText->SetText(LoadedSummary && LoadedSummary->DayIndex > 0
			? FText::FromString(FString::Printf(TEXT("存档记录：第 %d 天"), LoadedSummary->DayIndex))
			: FText::FromString(Snapshot.SessionRole == ECatOnlineSessionRole::Client ? TEXT("天数：房主尚未提供") : TEXT("天数：存档尚无记录")));
	}
	if (UTextBlock* SacrificeText = FindPageControl<UTextBlock>(LoadingPage, TEXT("LoadingSacrificeProgressTextBlock"), TEXT("LoadingPage")))
	{
		SacrificeText->SetText(LoadedSummary && LoadedSummary->SacrificeTarget > 0
			? FText::FromString(FString::Printf(TEXT("存档献祭记录：%d / %d"), LoadedSummary->SacrificeProgress, LoadedSummary->SacrificeTarget))
			: FText::FromString(Snapshot.SessionRole == ECatOnlineSessionRole::Client ? TEXT("献祭进度：房主尚未提供") : TEXT("献祭进度：存档尚无记录")));
	}
	BP_RenderLoading();
}

// 存档行重建流程：清空旧行后仅从 SaveModel 的当前真实摘要创建紧凑 WBP；每行在 ConfigureRow 中保存稳定 SlotId，列表为空时不补虚构条目。
void UCatFrontendRootWidget::RebuildSaveRows()
{
	if (!SaveRowsScrollBox)
	{
		return;
	}
	SaveRowsScrollBox->ClearChildren();
	if (!SaveModel)
	{
		return;
	}
	const TSubclassOf<UCatFrontendSaveSlotRowWidget> RowClass = LoadClass<UCatFrontendSaveSlotRowWidget>(nullptr,
		TEXT("/Game/UI/Frontend/WBP_CatSaveSlotRow.WBP_CatSaveSlotRow_C"));
	if (!RowClass)
	{
		UE_LOG(LogCatUI, Error, TEXT("Event=frontend_row_class_missing Row=SaveSlot"));
		return;
	}
	for (const FCatSaveSlotSummary& Summary : SaveModel->GetSlotSummaries())
	{
		UCatFrontendSaveSlotRowWidget* Row = CreateWidget<UCatFrontendSaveSlotRowWidget>(GetOwningPlayer(), RowClass);
		if (Row)
		{
			Row->ConfigureRow(this, Summary);
			SaveRowsScrollBox->AddChild(Row);
		}
	}
}

// 房间行重建流程：
// 1. 先读取 RoomModel 的唯一快照并清空旧好友/成员行，输入筛选只影响当前显示，不写回 Online。
// 2. 每个好友行保存其 opaque FriendHandle，成员行只消费已确认 Lobby 成员；不依赖行下标或文本。
// 3. 只有 Snapshot 给出正容量时才补显示空槽，避免把未知成员数量扩展为假玩家。
void UCatFrontendRootWidget::RebuildRoomRows()
{
	if (!RoomModel)
	{
		return;
	}
	const FCatOnlineSnapshot Snapshot = RoomModel->GetSnapshot();
	if (FriendsScrollBox)
	{
		FriendsScrollBox->ClearChildren();
		const FString Filter = FriendSearchTextBox ? FriendSearchTextBox->GetText().ToString().TrimStartAndEnd() : FString();
		const TSubclassOf<UCatFrontendRoomFriendRowWidget> FriendRowClass = LoadClass<UCatFrontendRoomFriendRowWidget>(nullptr,
			TEXT("/Game/UI/Frontend/WBP_CatRoomFriendRow.WBP_CatRoomFriendRow_C"));
		if (!FriendRowClass)
		{
			UE_LOG(LogCatUI, Error, TEXT("Event=frontend_row_class_missing Row=RoomFriend"));
		}
		else for (const FCatOnlineFriendSummary& Friend : Snapshot.Friends)
		{
			if (!Filter.IsEmpty() && !Friend.DisplayName.Contains(Filter, ESearchCase::IgnoreCase)) { continue; }
			UCatFrontendRoomFriendRowWidget* Row = CreateWidget<UCatFrontendRoomFriendRowWidget>(GetOwningPlayer(), FriendRowClass);
			if (Row)
			{
				Row->ConfigureRow(this, Friend);
				FriendsScrollBox->AddChild(Row);
			}
		}
	}
	if (PlayersScrollBox)
	{
		PlayersScrollBox->ClearChildren();
		const TSubclassOf<UCatFrontendRoomPlayerSlotWidget> PlayerRowClass = LoadClass<UCatFrontendRoomPlayerSlotWidget>(nullptr,
			TEXT("/Game/UI/Frontend/WBP_CatRoomPlayerSlot.WBP_CatRoomPlayerSlot_C"));
		if (!PlayerRowClass)
		{
			UE_LOG(LogCatUI, Error, TEXT("Event=frontend_row_class_missing Row=RoomPlayer"));
			return;
		}
		for (const FCatOnlineRoomMember& Member : Snapshot.RoomMembers)
		{
			UCatFrontendRoomPlayerSlotWidget* Row = CreateWidget<UCatFrontendRoomPlayerSlotWidget>(GetOwningPlayer(), PlayerRowClass);
			if (Row)
			{
				Row->ConfigureRow(Member);
				PlayersScrollBox->AddChild(Row);
			}
		}
		for (int32 SlotIndex = Snapshot.RoomMembers.Num(); SlotIndex < Snapshot.MaxPlayers; ++SlotIndex)
		{
			UCatFrontendRoomPlayerSlotWidget* Row = CreateWidget<UCatFrontendRoomPlayerSlotWidget>(GetOwningPlayer(), PlayerRowClass);
			if (Row)
			{
				Row->ConfigureEmptySlot();
				PlayersScrollBox->AddChild(Row);
			}
		}
	}
}

// 房间表现刷新流程：从 Snapshot 写入真实 joinlobby URI、访问策略和开始可用性；无法确认 Lobby 或策略时明确显示不可用而不生成替代值。
void UCatFrontendRootWidget::RefreshRoomPresentation()
{
	const FCatOnlineSnapshot Snapshot = RoomModel ? RoomModel->GetSnapshot() : FCatOnlineSnapshot();
	if (RoomInviteCodeText)
	{
		RoomInviteCodeText->SetText(Snapshot.JoinLobbyUri.IsEmpty()
			? FText::FromString(TEXT("邀请码暂不可用")) : FText::FromString(Snapshot.JoinLobbyUri));
	}
	if (RoomAccessPolicyText)
	{
		const TCHAR* AccessText = Snapshot.SessionAccess == ECatSessionAccessPolicy::Public ? TEXT("公开")
			: Snapshot.SessionAccess == ECatSessionAccessPolicy::FriendsOnly ? TEXT("仅好友")
			: Snapshot.SessionAccess == ECatSessionAccessPolicy::InviteOnly ? TEXT("仅邀请") : TEXT("访问方式未确认");
		RoomAccessPolicyText->SetText(FText::FromString(AccessText));
	}
	if (StartRoomGameButton) { StartRoomGameButton->SetIsEnabled(RoomModel && RoomModel->CanStartGame()); }
	if (CopyInviteCodeButton) { CopyInviteCodeButton->SetIsEnabled(!Snapshot.JoinLobbyUri.IsEmpty()); }
}

// 新建存档点击流程：只读取玩家在强制输入控件中填写的原始名称并转交 Controller；空值由正式校验返回反馈，Root 不生成默认名称。
void UCatFrontendRootWidget::HandleCreateSaveClicked()
{
	if (!CreateSaveNameTextBox)
	{
		UE_LOG(LogCatUI, Error, TEXT("Event=frontend_widget_contract_missing Page=SaveListPage Control=CreateSaveNameTextBox"));
		return;
	}
	RequestCreateSaveSlot(CreateSaveNameTextBox->GetText().ToString());
}

// 好友筛选流程：输入变化时只依据当前 RoomModel Snapshot 重建显示行；不发 Online 请求、不改变好友缓存或邀请状态。
void UCatFrontendRootWidget::HandleFriendSearchTextChanged(const FText& NewText)
{
	RebuildRoomRows();
}

// 语言选择流程：跳过原生回填及 Model 缺失场景，将玩家选择的 culture 交给 SettingsModel 校验已打包语言并写草稿，运行时语言仍等待 Apply。
void UCatFrontendRootWidget::HandleLanguageSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (!bRefreshingSettingsControls && SettingsModel) { SettingsModel->SetDraftLanguage(SelectedItem); }
}

// 窗口模式选择流程：跳过原生回填及 Model 缺失场景，仅将三种明确显示项映射到 UE 窗口模式；未知项记录拒绝并保留原草稿。
void UCatFrontendRootWidget::HandleFullscreenModeSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (bRefreshingSettingsControls || !SettingsModel) { return; }
	EWindowMode::Type WindowMode;
	if (SelectedItem == TEXT("全屏")) { WindowMode = EWindowMode::Fullscreen; }
	else if (SelectedItem == TEXT("窗口")) { WindowMode = EWindowMode::Windowed; }
	else if (SelectedItem == TEXT("无边框窗口")) { WindowMode = EWindowMode::WindowedFullscreen; }
	else
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=frontend_setting_selection_rejected Control=FullscreenModeComboBox Reason=unknown_option"));
		return;
	}
	SettingsModel->SetDraftFullscreenMode(WindowMode);
}

// 分辨率选择流程：跳过原生回填及 Model 缺失场景；把显示项拆成宽高后提交给 Model 验证正尺寸，格式无法拆分时保留原草稿。
void UCatFrontendRootWidget::HandleScreenResolutionSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (bRefreshingSettingsControls || !SettingsModel) { return; }
	FString WidthText;
	FString HeightText;
	if (SelectedItem.Split(TEXT(" x "), &WidthText, &HeightText)) { SettingsModel->SetDraftScreenResolution(FIntPoint(FCString::Atoi(*WidthText), FCString::Atoi(*HeightText))); }
}

// 画质选择流程：跳过原生回填及 Model 缺失场景，仅将已知显示项映射到自定义或 UE 0 至 4 档；未知项记录拒绝，不暗设电影级配置。
void UCatFrontendRootWidget::HandleQualitySelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (bRefreshingSettingsControls || !SettingsModel) { return; }
	int32 QualityLevel;
	if (SelectedItem == TEXT("自定义")) { QualityLevel = -1; }
	else if (SelectedItem == TEXT("低")) { QualityLevel = 0; }
	else if (SelectedItem == TEXT("中")) { QualityLevel = 1; }
	else if (SelectedItem == TEXT("高")) { QualityLevel = 2; }
	else if (SelectedItem == TEXT("史诗")) { QualityLevel = 3; }
	else if (SelectedItem == TEXT("电影")) { QualityLevel = 4; }
	else
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=frontend_setting_selection_rejected Control=OverallQualityComboBox Reason=unknown_option"));
		return;
	}
	SettingsModel->SetDraftOverallScalabilityLevel(QualityLevel);
}
// 垂直同步输入流程：回填保护外把玩家勾选原样写入草稿；真实渲染同步仍等待 Apply。
void UCatFrontendRootWidget::HandleVSyncChanged(bool bIsChecked) { if (!bRefreshingSettingsControls && SettingsModel) { SettingsModel->SetDraftVSyncEnabled(bIsChecked); } }

// UI 比例输入流程：把滑块归一化值换算为正式可读范围，再由 Model 裁剪并发布草稿变化。
void UCatFrontendRootWidget::HandleUIScaleChanged(float NormalizedValue) { if (!bRefreshingSettingsControls && SettingsModel) { SettingsModel->SetDraftUIScale(0.75f + NormalizedValue * 1.25f); } }

// 亮度输入流程：把滑块归一化值换算为正式 Gamma 范围，再由 Model 在 Apply 前保留草稿。
void UCatFrontendRootWidget::HandleBrightnessChanged(float NormalizedValue) { if (!bRefreshingSettingsControls && SettingsModel) { SettingsModel->SetDraftDisplayGamma(0.5f + NormalizedValue * 4.5f); } }

// 震动输入流程：只有当前 LocalPlayer 存在正式 Controller 时才写草稿；失效 UI 不会伪装为可提交。
void UCatFrontendRootWidget::HandleVibrationChanged(bool bIsChecked) { if (!bRefreshingSettingsControls && SettingsModel && SettingsModel->IsVibrationSettingAvailable()) { SettingsModel->SetDraftVibrationEnabled(bIsChecked); } }

// 网络语音输入流程：只有当前 OSS Voice 接口可用时才写 Start/Stop 草稿；平台拒绝保持禁用而不产生本地替代。
void UCatFrontendRootWidget::HandleVoiceChatChanged(bool bIsChecked) { if (!bRefreshingSettingsControls && SettingsModel && SettingsModel->IsVoiceChatSettingAvailable()) { SettingsModel->SetDraftVoiceChatEnabled(bIsChecked); } }

// 失焦静音输入流程：回填保护外写入正式失焦音量草稿；当前 AudioDevice 不会在点击时被立即改变。
void UCatFrontendRootWidget::HandleMuteAudioWhenUnfocusedChanged(bool bIsChecked) { if (!bRefreshingSettingsControls && SettingsModel) { SettingsModel->SetDraftMuteAudioWhenUnfocused(bIsChecked); } }

// 输出设备选择流程：只将当前 View 映射中对应的稳定平台 ID 提交给 Model；过期显示项和空映射安全忽略。
void UCatFrontendRootWidget::HandleAudioOutputDeviceSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	if (!bRefreshingSettingsControls && SettingsModel)
	{
		if (const FString* DeviceId = AudioOutputDeviceIdsByOption.Find(SelectedItem)) { SettingsModel->SetDraftAudioOutputDeviceId(*DeviceId); }
	}
}

// 主音量输入流程：回填保护外写入分类混音草稿；正式 AudioDevice 覆盖仍由 Apply 统一处理。
void UCatFrontendRootWidget::HandleMasterVolumeChanged(float Value) { if (!bRefreshingSettingsControls && SettingsModel) { SettingsModel->SetDraftMasterVolume(Value); } }

// 音乐音量输入流程：回填保护外写入分类混音草稿；缺少正式路由时控件保持禁用。
void UCatFrontendRootWidget::HandleMusicVolumeChanged(float Value) { if (!bRefreshingSettingsControls && SettingsModel) { SettingsModel->SetDraftMusicVolume(Value); } }

// 音效音量输入流程：回填保护外写入分类混音草稿；缺少正式路由时控件保持禁用。
void UCatFrontendRootWidget::HandleSFXVolumeChanged(float Value) { if (!bRefreshingSettingsControls && SettingsModel) { SettingsModel->SetDraftSFXVolume(Value); } }

// 环境音输入流程：回填保护外写入分类混音草稿；缺少正式路由时控件保持禁用。
void UCatFrontendRootWidget::HandleAmbienceVolumeChanged(float Value) { if (!bRefreshingSettingsControls && SettingsModel) { SettingsModel->SetDraftAmbienceVolume(Value); } }

// 语音分类音量输入流程：回填保护外写入 SoundClass 草稿；它不改变网络语音开关。
void UCatFrontendRootWidget::HandleVoiceVolumeChanged(float Value) { if (!bRefreshingSettingsControls && SettingsModel) { SettingsModel->SetDraftVoiceVolume(Value); } }

// 页面切换流程：只对已装配的 Switcher 和目标页执行 SetActiveWidget；缺失任一强制资产时保持原表现并留下诊断，不能伪装为成功切页。
void UCatFrontendRootWidget::ShowPage(UWidget* Page, const TCHAR* PageName)
{
	if (!FrontendPageSwitcher || !Page)
	{
		UE_LOG(LogCatUI, Error, TEXT("Event=frontend_page_show_failed Page=%s Switcher=%s Target=%s"), PageName,
			FrontendPageSwitcher ? TEXT("valid") : TEXT("missing"), Page ? TEXT("valid") : TEXT("missing"));
		return;
	}
	FrontendPageSwitcher->SetActiveWidget(Page);
}
