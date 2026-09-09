#include "UI/Frontend/CatFrontendPageController.h"

#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Logging/CatLog.h"
#include "UI/Frontend/CatFrontendRootWidget.h"
#include "UI/Frontend/CatFrontendRoomModel.h"
#include "UI/Frontend/CatFrontendSaveModel.h"
#include "UI/Frontend/CatFrontendSettingsModel.h"

// Controller 绑定流程把本地玩家、Root 和三个 Model 收束到同一个流程真相，避免页面自己缓存跨页状态：
// 1. 先解除旧协作者，避免 LocalPlayer 或 World 更换后保留失效 Model 的通知。
// 2. 保存本轮 LocalPlayer、Root 与三个专属 Model，并成对订阅各自的无参变化事件。
// 3. 菜单作为无房间初态，随后立即消费已有快照；初始化前已加入的 Host/Client 会在首次呈现前进入 Room，不重新发起 Join。
void UCatFrontendPageController::Initialize(ULocalPlayer* InLocalPlayer, UCatFrontendRootWidget* InRootWidget,
	UCatFrontendSaveModel* InSaveModel, UCatFrontendRoomModel* InRoomModel, UCatFrontendSettingsModel* InSettingsModel)
{
	Shutdown();
	LocalPlayer = InLocalPlayer;
	RootWidget = InRootWidget;
	SaveModel = InSaveModel;
	RoomModel = InRoomModel;
	SettingsModel = InSettingsModel;
	if (UCatFrontendSaveModel* Save = SaveModel.Get())
	{
		SaveModelChangedHandle = Save->OnChanged.AddUObject(this, &ThisClass::HandleSaveModelChanged);
	}
	if (UCatFrontendRoomModel* Room = RoomModel.Get())
	{
		RoomModelChangedHandle = Room->OnChanged.AddUObject(this, &ThisClass::HandleRoomModelChanged);
	}
	if (UCatFrontendSettingsModel* Settings = SettingsModel.Get())
	{
		SettingsModelChangedHandle = Settings->OnChanged.AddUObject(this, &ThisClass::HandleSettingsModelChanged);
	}
	if (UCatFrontendRootWidget* Root = RootWidget.Get())
	{
		Root->ShowMenu();
	}
	HandleRoomModelChanged();
}

// 销毁流程：
// 1. 先按原 Model 和句柄解除三类通知，阻止异步存档或 Online 更新进入已失效 Controller。
// 2. 再清空流程、槽位和确认状态，确保下一个 LocalPlayer 不继承本轮选择或等待命令。
// 3. 不取消已受理的底层请求；Save、Room、Settings 各自在自己的正式生命周期内收口。
void UCatFrontendPageController::Shutdown()
{
	if (UCatFrontendSaveModel* Save = SaveModel.Get()) { Save->OnChanged.Remove(SaveModelChangedHandle); }
	if (UCatFrontendRoomModel* Room = RoomModel.Get()) { Room->OnChanged.Remove(RoomModelChangedHandle); }
	if (UCatFrontendSettingsModel* Settings = SettingsModel.Get()) { Settings->OnChanged.Remove(SettingsModelChangedHandle); }
	SaveModelChangedHandle.Reset();
	RoomModelChangedHandle.Reset();
	SettingsModelChangedHandle.Reset();
	LocalPlayer.Reset();
	RootWidget.Reset();
	SaveModel.Reset();
	RoomModel.Reset();
	SettingsModel.Reset();
	bStartGameFlowActive = false;
	SelectedSlotId = NAME_None;
	PendingDeleteSlotId = NAME_None;
	bExitConfirmationVisible = false;
	bWaitingForSaveLoad = false;
	bWaitingForRoomCreation = false;
	PendingRoomCreationRequestId.Invalidate();
	bPresentedFrontendRoom = false;
	PresentedInviteFeedback = FText::GetEmpty();
	PresentedInviteFeedbackRequestId.Invalidate();
	LastResultText = FText::GetEmpty();
	LastResultSource.Reset();
}

// 开始游戏流程：先挡住正在读档、创建或已有 Online 操作/房间的重复入口，再清旧选择、确认与提示；显示存档页并刷新真实目录，不创建虚拟槽或房间。
void UCatFrontendPageController::RequestStartGameFlow()
{
	if (bWaitingForSaveLoad || bWaitingForRoomCreation) { return; }
	if (UCatFrontendRoomModel* Room = RoomModel.Get())
	{
		const FCatOnlineSnapshot Snapshot = Room->GetSnapshot();
		if (Snapshot.SessionState != ECatOnlineSessionState::NoSession || Snapshot.ActiveOperation != ECatOnlineOperation::None
			|| Snapshot.bIsAcceptedInvitePending) { HandleRoomModelChanged(); return; }
	}
	SetLocalResultText(FText::GetEmpty());
	bStartGameFlowActive = true;
	SelectedSlotId = NAME_None;
	PendingDeleteSlotId = NAME_None;
	bExitConfirmationVisible = false;
	if (UCatFrontendRootWidget* Root = RootWidget.Get()) { Root->ShowSaveList(); }
	if (UCatFrontendSaveModel* Save = SaveModel.Get()) { Save->RefreshSlotSummaries(); }
}

// 加入队伍流程：产品尚未定义搜索或加入规则，因此只保存可读反馈；不调用 RoomModel，避免制造离线或本地替身房间。
void UCatFrontendPageController::RequestJoinParty()
{
	SetLocalResultText(FText::FromString(TEXT("加入队伍功能尚未开放。")));
	if (UCatFrontendRootWidget* Root = RootWidget.Get()) { Root->ShowMenu(); }
}

// 设置打开流程：清除上一业务面的局部提示并显示正式设置页；各 Model 的真实结果不改写，草稿仍由 SettingsModel 保有。
void UCatFrontendPageController::RequestOpenFrontendSettings()
{
	bExitConfirmationVisible = false;
	SetLocalResultText(FText::GetEmpty());
	if (UCatFrontendRootWidget* Root = RootWidget.Get()) { Root->ShowFrontendSettings(); }
}

// 退出确认显示流程：只写同菜单业务面的确认标记并要求菜单重绘；不新建页面或立即退出进程。
void UCatFrontendPageController::RequestShowExitConfirmation()
{
	bExitConfirmationVisible = true;
	if (UCatFrontendRootWidget* Root = RootWidget.Get()) { Root->ShowMenu(); }
}

// 退出确认流程：先拒绝未确认的重复点击，再从当前 LocalPlayer 取得本地 Controller 并调用引擎退出入口；缺失 World/Controller 时保留明确反馈。
void UCatFrontendPageController::RequestConfirmExit()
{
	if (!bExitConfirmationVisible)
	{
		SetLocalResultText(FText::FromString(TEXT("请先确认退出。")));
		return;
	}
	ULocalPlayer* Player = LocalPlayer.Get();
	APlayerController* Controller = Player ? Player->GetPlayerController(Player->GetWorld()) : nullptr;
	if (!Player || !Player->GetWorld() || !Controller)
	{
		SetLocalResultText(FText::FromString(TEXT("当前无法退出游戏。")));
		return;
	}
	bExitConfirmationVisible = false;
	UE_LOG(LogCatUI, Log, TEXT("Event=frontend_exit_confirmed World=%s NetMode=%d Controller=%s"),
		*Player->GetWorld()->GetName(), static_cast<int32>(Player->GetWorld()->GetNetMode()), *GetNameSafe(Controller));
	UKismetSystemLibrary::QuitGame(Player->GetWorld(), Controller, EQuitPreference::Quit, false);
}

// 退出确认取消流程：清除确认标记并重绘同一菜单页面；不改变开始游戏、槽位或房间状态。
void UCatFrontendPageController::RequestCancelExitConfirmation()
{
	bExitConfirmationVisible = false;
	if (UCatFrontendRootWidget* Root = RootWidget.Get()) { Root->ShowMenu(); }
}

// 槽位选择流程：只接受当前 SaveModel 真实摘要中的稳定标识；busy、空值或旧行都会拒绝，避免异步刷新后选择串线。
void UCatFrontendPageController::RequestSelectSaveSlot(const FName SlotId)
{
	if (!bStartGameFlowActive || bWaitingForSaveLoad || bWaitingForRoomCreation
		|| (SaveModel.IsValid() && SaveModel->IsBusy()) || !IsCurrentSaveSlot(SlotId))
	{
		SetLocalResultText(FText::FromString(TEXT("该存档当前不可选择。")), SaveModel.Get());
		return;
	}
	SelectedSlotId = SlotId;
	PendingDeleteSlotId = NAME_None;
	SetLocalResultText(FText::GetEmpty());
	if (UCatFrontendRootWidget* Root = RootWidget.Get()) { Root->ShowSaveList(); }
}

// 新建槽流程：先拒绝流程外、busy、等待读档和空显示名，再原样交给 SaveModel；结果只以正式 Save 的异步通知为准。
void UCatFrontendPageController::RequestCreateSaveSlot(const FString& DisplayName)
{
	UCatFrontendSaveModel* Save = SaveModel.Get();
	if (!bStartGameFlowActive || !Save || bWaitingForSaveLoad || bWaitingForRoomCreation || Save->IsBusy())
	{
		SetLocalResultText(FText::FromString(TEXT("存档操作正在进行或当前不可用。")), Save);
		return;
	}
	if (DisplayName.TrimStartAndEnd().IsEmpty())
	{
		SetLocalResultText(FText::FromString(TEXT("请输入存档名称。")), Save);
		return;
	}
	const FCatSaveResult Result = Save->RequestCreateSlot(DisplayName);
	if (!Result.bAccepted)
	{
		SetLocalResultText(Result.Message, Save);
	}
}

// 读档请求流程：
// 1. 先拒绝没有正式开始入口、无选择、busy 或已有读档等待的重复输入。
// 2. 先显示存档页再提交；受理返回后才登记等待并重读当前状态，兼容同步完成，也避免同步拒绝时消费旧载荷。
// 3. 当前重读或后续通知必须同时确认旅行许可和活动槽匹配才创建房间，失败留存档页，调用返回后不再次强制切页。
void UCatFrontendPageController::RequestLoadSelectedSaveSlot()
{
	UCatFrontendSaveModel* Save = SaveModel.Get();
	if (!bStartGameFlowActive || !Save || SelectedSlotId.IsNone() || bWaitingForSaveLoad || bWaitingForRoomCreation || Save->IsBusy())
	{
		SetLocalResultText(FText::FromString(TEXT("请先选择可读取的存档。")), Save);
		return;
	}
	if (UCatFrontendRoomModel* Room = RoomModel.Get())
	{
		const FCatOnlineSnapshot Snapshot = Room->GetSnapshot();
		if (Snapshot.SessionState != ECatOnlineSessionState::NoSession || Snapshot.ActiveOperation != ECatOnlineOperation::None
			|| Snapshot.bIsAcceptedInvitePending)
		{
			SetLocalResultText(FText::FromString(TEXT("房间操作尚未结束，暂时不能读取存档。")), Save);
			return;
		}
	}
	SetLocalResultText(FText::GetEmpty());
	if (UCatFrontendRootWidget* Root = RootWidget.Get()) { Root->ShowSaveList(); }
	const FCatSaveResult Result = Save->RequestLoadSlot(SelectedSlotId);
	if (!Result.bAccepted)
	{
		bWaitingForSaveLoad = false;
		SetLocalResultText(Result.Message, Save);
		return;
	}
	UE_LOG(LogCatUI, Log, TEXT("Event=frontend_save_load_requested SlotId=%s RequestId=%s"), *SelectedSlotId.ToString(),
		*Result.RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	bWaitingForSaveLoad = true;
	HandleSaveModelChanged();
}

// 删除预确认流程：只在当前真实选择可删除且 Save 不忙时保存稳定目标；真实删除留给确认按钮，列表刷新不会改写该目标。
void UCatFrontendPageController::RequestDeleteSelectedSaveSlot()
{
	UCatFrontendSaveModel* Save = SaveModel.Get();
	if (!bStartGameFlowActive || !Save || bWaitingForSaveLoad || bWaitingForRoomCreation || Save->IsBusy() || !IsCurrentSaveSlot(SelectedSlotId))
	{
		SetLocalResultText(FText::FromString(TEXT("当前存档不可删除。")), Save);
		return;
	}
	PendingDeleteSlotId = SelectedSlotId;
	if (UCatFrontendRootWidget* Root = RootWidget.Get()) { Root->ShowSaveList(); }
}

// 删除确认流程：先取出并清空预确认槽位以保证重复点击无效，再提交正式 Save 删除；同步拒绝保留其真实消息，异步结果由 OnChanged 刷新。
void UCatFrontendPageController::RequestConfirmDeleteSaveSlot()
{
	UCatFrontendSaveModel* Save = SaveModel.Get();
	const FName SlotToDelete = PendingDeleteSlotId;
	PendingDeleteSlotId = NAME_None;
	if (!Save || SlotToDelete.IsNone() || bWaitingForSaveLoad || bWaitingForRoomCreation || Save->IsBusy() || !IsCurrentSaveSlot(SlotToDelete))
	{
		SetLocalResultText(FText::FromString(TEXT("删除确认已失效。")), Save);
		return;
	}
	const FCatSaveResult Result = Save->RequestDeleteSlot(SlotToDelete);
	if (!Result.bAccepted)
	{
		SetLocalResultText(Result.Message, Save);
	}
	if (UCatFrontendRootWidget* Root = RootWidget.Get()) { Root->ShowSaveList(); }
}

// 取消流程：先处理当前设置页和确认层；真实 I/O、创建或 Online 操作未结束时只提示等待。已成立房间交给 Online Leave；尚未入房的开始流程先释放载荷，再清选择并回菜单。
void UCatFrontendPageController::RequestCancel()
{
	if (bExitConfirmationVisible)
	{
		RequestCancelExitConfirmation();
		return;
	}
	if (UCatFrontendRootWidget* Root = RootWidget.Get(); Root && SettingsModel.IsValid() && Root->GetVisibleFeedbackSource() == SettingsModel.Get())
	{
		RequestCancelFrontendSettings();
		return;
	}
	if (!PendingDeleteSlotId.IsNone())
	{
		PendingDeleteSlotId = NAME_None;
		if (UCatFrontendRootWidget* Root = RootWidget.Get()) { Root->ShowSaveList(); }
		return;
	}
	if (bWaitingForSaveLoad || bWaitingForRoomCreation || (SaveModel.IsValid() && SaveModel->IsBusy()))
	{
		SetLocalResultText(FText::FromString(TEXT("存档或建房操作正在进行，暂时不能取消。")), SaveModel.Get());
		return;
	}
	if (UCatFrontendRoomModel* Room = RoomModel.Get())
	{
		const FCatOnlineSnapshot Snapshot = Room->GetSnapshot();
		if (Snapshot.ActiveOperation != ECatOnlineOperation::None || Snapshot.bIsAcceptedInvitePending)
		{
			SetLocalResultText(FText::FromString(TEXT("房间操作正在进行，暂时不能取消。")), RootWidget.IsValid() ? RootWidget->GetVisibleFeedbackSource() : nullptr);
			return;
		}
		if (Snapshot.SessionState == ECatOnlineSessionState::Host || Snapshot.SessionState == ECatOnlineSessionState::Client)
		{
			RequestLeaveRoom();
			return;
		}
	}
	if (bStartGameFlowActive && !ReleaseUnjoinedSave())
	{
		SetLocalResultText(FText::FromString(TEXT("本局载荷尚未释放，暂时不能返回。")), SaveModel.Get());
		return;
	}
	bStartGameFlowActive = false;
	SelectedSlotId = NAME_None;
	SetLocalResultText(FText::GetEmpty());
	if (UCatFrontendRootWidget* Root = RootWidget.Get()) { Root->ShowMenu(); }
}

// 好友刷新流程：RoomModel 是唯一 Online 适配层；同步拒绝立即显示，异步刷新仍由 RoomModel OnChanged 重绘。
void UCatFrontendPageController::RequestRefreshFriends()
{
	if (UCatFrontendRoomModel* Room = RoomModel.Get())
	{
		const FCatOnlineResult Result = Room->RefreshFriends();
		if (!Result.bAccepted) { SetLocalResultText(Room->GetLastResultText(), Room); }
		return;
	}
	SetLocalResultText(FText::FromString(TEXT("房间服务当前不可用。")), RoomModel.Get());
}

// 邀请流程：只把 opaque 句柄原样交给 RoomModel；Controller 不将平台身份转换成字符串，也不保存邀请回调。
void UCatFrontendPageController::RequestInviteFriend(const FCatOnlineFriendHandle FriendHandle)
{
	if (UCatFrontendRoomModel* Room = RoomModel.Get())
	{
		const FCatOnlineResult Result = Room->InviteFriend(FriendHandle);
		if (!Result.bAccepted) { SetLocalResultText(Room->GetLastResultText(), Room); }
		return;
	}
	SetLocalResultText(FText::FromString(TEXT("房间服务当前不可用。")), RoomModel.Get());
}

// 离开房间流程：先向 RoomModel 提交正式 Host/Client 离开意图；只有同步受理失败才显示即时错误，终态由 RoomModel 通知决定返回页面。
void UCatFrontendPageController::RequestLeaveRoom()
{
	if (UCatFrontendRoomModel* Room = RoomModel.Get())
	{
		const FCatOnlineResult Result = Room->LeaveRoom();
		if (!Result.bAccepted) { SetLocalResultText(Room->GetLastResultText(), Room); }
		return;
	}
	SetLocalResultText(FText::FromString(TEXT("房间服务当前不可用。")), RoomModel.Get());
}

// 开始游戏流程：先拒绝无权启动的请求，再提交 RoomModel Start；同步拒绝保留正式错误，受理后重新消费当前快照，加载遮罩只由真实 Start 预载、TravelQueued 或 TravelingToLake 事实触发。
void UCatFrontendPageController::RequestStartRoomGame()
{
	UCatFrontendRoomModel* Room = RoomModel.Get();
	if (!Room || !Room->CanStartGame())
	{
		SetLocalResultText(FText::FromString(TEXT("当前无法开始游戏。")), Room);
		return;
	}
	const FCatOnlineResult Result = Room->StartGame();
	if (!Result.bAccepted)
	{
		SetLocalResultText(Room->GetLastResultText(), Room);
		return;
	}
	HandleRoomModelChanged();
	UE_LOG(LogCatUI, Log, TEXT("Event=frontend_room_start_requested RequestId=%s"), *Result.RequestId.ToString(EGuidFormats::DigitsWithHyphens));
}

// 设置应用流程：SettingsModel 负责真实字段提交；失败以设置来源显示结果，成功清局部提示后回菜单，不把应用结果带到存档或房间。
void UCatFrontendPageController::RequestApplyFrontendSettings()
{
	UCatFrontendSettingsModel* Settings = SettingsModel.Get();
	if (!Settings || !Settings->Apply())
	{
		SetLocalResultText(Settings ? Settings->GetLastResultText() : FText::FromString(TEXT("设置服务当前不可用。")), Settings);
		if (UCatFrontendRootWidget* Root = RootWidget.Get()) { Root->ShowFrontendSettings(); }
		return;
	}
	SetLocalResultText(FText::GetEmpty());
	if (UCatFrontendRootWidget* Root = RootWidget.Get()) { Root->ShowMenu(); }
}

// 设置取消流程：先要求 SettingsModel 丢弃未应用草稿，再清局部提示回菜单；已生效设置不回滚，已有房间或读档请求不被此返回动作取消。
void UCatFrontendPageController::RequestCancelFrontendSettings()
{
	if (UCatFrontendSettingsModel* Settings = SettingsModel.Get()) { Settings->Cancel(); }
	SetLocalResultText(FText::GetEmpty());
	if (UCatFrontendRootWidget* Root = RootWidget.Get()) { Root->ShowMenu(); }
}

// 设置恢复默认流程：只转交 SettingsModel 修改草稿；玩家仍需显式 Apply，Controller 不直接写引擎配置。
void UCatFrontendPageController::RequestRestoreFrontendSettingsDefaults()
{
	if (UCatFrontendSettingsModel* Settings = SettingsModel.Get()) { Settings->RestoreDefaults(); }
}

// 输出设备刷新流程：只向 SettingsModel 转交当前平台的正式异步枚举请求；同步拒绝留下其明确结果，异步设备列表仍只通过 OnChanged 进入 Root。
void UCatFrontendPageController::RequestRefreshAudioOutputDevices()
{
	UCatFrontendSettingsModel* Settings = SettingsModel.Get();
	if (!Settings || !Settings->RefreshAudioOutputDevices())
	{
		SetLocalResultText(Settings ? Settings->GetLastResultText() : FText::FromString(TEXT("设置服务当前不可用。")), Settings);
	}
}

// 游戏分类流程：取得有效 SettingsModel 后调用其命名分类接口；Model 自行广播，Root 的订阅负责回填。
void UCatFrontendPageController::RequestSelectGameSettings() { if (UCatFrontendSettingsModel* Settings = SettingsModel.Get()) { Settings->SelectGame(); } }

// 画面分类流程：取得有效 SettingsModel 后调用其命名分类接口；Model 自行广播，Root 的订阅负责回填。
void UCatFrontendPageController::RequestSelectGraphicsSettings() { if (UCatFrontendSettingsModel* Settings = SettingsModel.Get()) { Settings->SelectGraphics(); } }

// 声音分类流程：取得有效 SettingsModel 后调用其命名分类接口；Model 自行广播，Root 的订阅负责回填。
void UCatFrontendPageController::RequestSelectAudioSettings() { if (UCatFrontendSettingsModel* Settings = SettingsModel.Get()) { Settings->SelectAudio(); } }

// 控制分类流程：取得有效 SettingsModel 后选择当前受限分类；只由 Model 通知刷新，不生成尚未接线的配置。
void UCatFrontendPageController::RequestSelectControlsSettings() { if (UCatFrontendSettingsModel* Settings = SettingsModel.Get()) { Settings->SelectControls(); } }

// 槽位读取流程：返回当前已验证选择；不再验证时返回 None 的责任由 SaveModel 变化处理承担。
FName UCatFrontendPageController::GetSelectedSlotId() const { return SelectedSlotId; }

// 删除确认目标读取：这里暴露的是等待二次确认的槽位身份，Root 只能据此表现确认层，真正删除仍必须由确认意图回到 Controller。
FName UCatFrontendPageController::GetPendingDeleteSlotId() const { return PendingDeleteSlotId; }

// 退出确认读取流程：返回同菜单业务面的确认状态；它不表示 QuitGame 已调用。
bool UCatFrontendPageController::IsExitConfirmationVisible() const { return bExitConfirmationVisible; }

// 本地反馈读取流程：仅当调用方的 Model 身份与写入来源一致时返回提示；其他页面读到空值后显示自己的正式结果，不复制或清除底层错误。
FText UCatFrontendPageController::GetLastResultText(const UObject* ResultSource) const
{
	return LastResultSource.Get() == ResultSource ? LastResultText : FText::GetEmpty();
}

// 存档变化流程：
// 1. 只清除属于 Save 的局部提示及失效选择，不把异步 Save 文本写到当前设置页。
// 2. 读档终态必须同时匹配入口、活动槽和旅行许可；先清等待再调用 Room，防止同步通知重复创建。
// 3. Room 缺失或同步拒绝时先释放未入房载荷再回存档页；受理后记录 RequestId，并重新消费快照兼容同步结案。
void UCatFrontendPageController::HandleSaveModelChanged()
{
	UCatFrontendSaveModel* Save = SaveModel.Get();
	if (!Save)
	{
		return;
	}
	if (LastResultSource.Get() == Save) { SetLocalResultText(FText::GetEmpty()); }
	if (!SelectedSlotId.IsNone() && !IsCurrentSaveSlot(SelectedSlotId)) { SelectedSlotId = NAME_None; }
	if (!PendingDeleteSlotId.IsNone() && !IsCurrentSaveSlot(PendingDeleteSlotId)) { PendingDeleteSlotId = NAME_None; }
	if (!bWaitingForSaveLoad || Save->IsBusy())
	{
		return;
	}
	bWaitingForSaveLoad = false;
	if (!bStartGameFlowActive || SelectedSlotId.IsNone() || !Save->HasLoadedRunForTravel() || Save->GetActiveSlotId() != SelectedSlotId)
	{
		SetLocalResultText(Save->GetLastResultText(), Save);
		if (UCatFrontendRootWidget* Root = RootWidget.Get()) { Root->ShowSaveList(); }
		return;
	}
	UCatFrontendRoomModel* Room = RoomModel.Get();
	if (!Room)
	{
		ReleaseUnjoinedSave();
		SetLocalResultText(FText::FromString(TEXT("房间服务当前不可用。")), Save);
		if (UCatFrontendRootWidget* Root = RootWidget.Get()) { Root->ShowSaveList(); }
		return;
	}
	const FCatOnlineSnapshot BeforeCreate = Room->GetSnapshot();
	if (BeforeCreate.SessionState != ECatOnlineSessionState::NoSession || BeforeCreate.ActiveOperation != ECatOnlineOperation::None
		|| BeforeCreate.bIsAcceptedInvitePending)
	{
		// 邀请可能在磁盘读取期间先成立；不再创建第二个房间，也不释放已由 Online 接管的会话载荷。
		SetLocalResultText(Room->GetLastResultText(), Save);
		HandleRoomModelChanged();
		return;
	}
	bWaitingForRoomCreation = true;
	PendingRoomCreationRequestId.Invalidate();
	const FCatOnlineResult Result = Room->CreateRoom();
	if (!Result.bAccepted)
	{
		bWaitingForRoomCreation = false;
		ReleaseUnjoinedSave();
		SetLocalResultText(Room->GetLastResultText(), Save);
		if (UCatFrontendRootWidget* Root = RootWidget.Get()) { Root->ShowSaveList(); }
		return;
	}
	PendingRoomCreationRequestId = Result.RequestId;
	UE_LOG(LogCatUI, Log, TEXT("Event=frontend_room_create_requested SlotId=%s RequestId=%s"), *SelectedSlotId.ToString(),
		*Result.RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	HandleRoomModelChanged();
}

// 房间变化流程：
// 1. 自己提交的 Create 先等待调用返回，再只处理同一 RequestId 的终态；失败先清等待再释放未入房载荷，避免 Save 同步广播重入。
// 2. 初始化已有或新加入的正式 Frontend Host/Client 只切 Room 一次；好友轮询及成员刷新不抢走设置。邀请等待、加入和拒绝以文本加 RequestId 去重，定向提示当前页而不导航。
// 3. 真实 Start 预载或旅行由 LocalPlayer 全局遮罩接管，Controller 不在房间页再显示第二套加载反馈。
// 4. Start 链路失败时有房间退回 Room，房间已被补偿清理则回开始流程页或菜单。
// 5. 只有显示 Room 才响应普通离房返回，并等待 ActiveOperation=None，保证 Online 已完成释放；这里不重复清理已成立会话。
void UCatFrontendPageController::HandleRoomModelChanged()
{
	UCatFrontendRoomModel* Room = RoomModel.Get();
	UCatFrontendRootWidget* Root = RootWidget.Get();
	if (!Room || !Root)
	{
		return;
	}
	const FCatOnlineSnapshot Snapshot = Room->GetSnapshot();
	if (bWaitingForRoomCreation)
	{
		if (!PendingRoomCreationRequestId.IsValid()) { return; }
		if (Snapshot.RequestId == PendingRoomCreationRequestId && Snapshot.ActiveOperation == ECatOnlineOperation::None)
		{
			bWaitingForRoomCreation = false;
			PendingRoomCreationRequestId.Invalidate();
			if (Snapshot.SessionState != ECatOnlineSessionState::Host)
			{
				ReleaseUnjoinedSave();
				SetLocalResultText(Room->GetLastResultText(), SaveModel.Get());
				Root->ShowSaveList();
				UE_LOG(LogCatUI, Warning, TEXT("Event=frontend_room_create_failed RequestId=%s Epoch=%lld World=%s Error=%s"),
					*Snapshot.RequestId.ToString(EGuidFormats::DigitsWithHyphens), Snapshot.OperationEpoch,
					*GetNameSafe(Root->GetWorld()), *UEnum::GetValueAsString(Snapshot.LastError));
				return;
			}
		}
	}
	const bool bHasFrontendRoom = Snapshot.WorldState == ECatOnlineWorldState::Frontend
		&& ((Snapshot.SessionState == ECatOnlineSessionState::Host && Snapshot.SessionRole == ECatOnlineSessionRole::Host)
			|| (Snapshot.SessionState == ECatOnlineSessionState::Client && Snapshot.SessionRole == ECatOnlineSessionRole::Client));
	const bool bGameplayStartInProgress = Snapshot.ActiveOperation == ECatOnlineOperation::Start
		&& Snapshot.LastError == ECatOnlineError::None && Snapshot.RequestId.IsValid()
		&& (Snapshot.bIsGameplayLoadPending || Snapshot.TransportState == ECatOnlineTransportState::TravelQueued
			|| Snapshot.WorldState == ECatOnlineWorldState::TravelingToLake);
	const bool bGameplayStartFailed = Snapshot.LastError == ECatOnlineError::GameplayPreloadFailed
		|| Snapshot.LastError == ECatOnlineError::TravelRejected
		|| Snapshot.LastError == ECatOnlineError::TravelFailed
		|| Snapshot.LastError == ECatOnlineError::ConnectStringUnavailable
		|| Snapshot.LastError == ECatOnlineError::NetworkFailure;
	const bool bGameplayStartFailureRecovering = Snapshot.ActiveOperation == ECatOnlineOperation::None && bGameplayStartFailed;
	if (!bHasFrontendRoom && !bGameplayStartInProgress && !bGameplayStartFailureRecovering) { bPresentedFrontendRoom = false; }
	if (bHasFrontendRoom && !bPresentedFrontendRoom && Snapshot.ActiveOperation == ECatOnlineOperation::None)
	{
		bPresentedFrontendRoom = true;
		bExitConfirmationVisible = false;
		PendingDeleteSlotId = NAME_None;
		SetLocalResultText(FText::GetEmpty());
		Root->ShowRoom();
		UE_LOG(LogCatUI, Log, TEXT("Event=frontend_joined_room_shown RequestId=%s Epoch=%lld World=%s Role=%s"),
			*Snapshot.RequestId.ToString(EGuidFormats::DigitsWithHyphens), Snapshot.OperationEpoch,
			*GetNameSafe(Root->GetWorld()), *UEnum::GetValueAsString(Snapshot.SessionRole));
	}
	const bool bInviteFeedback = Snapshot.bIsAcceptedInvitePending || Snapshot.ActiveOperation == ECatOnlineOperation::Join
		|| Snapshot.LastError == ECatOnlineError::InviteAcceptanceBusy || Snapshot.LastError == ECatOnlineError::InviteSessionConflict
		|| Snapshot.LastError == ECatOnlineError::InviteAcceptanceUnavailable || Snapshot.LastError == ECatOnlineError::InviteAcceptanceExpired
		|| Snapshot.LastError == ECatOnlineError::SessionCompatibilityMismatch || Snapshot.LastError == ECatOnlineError::JoinFailed;
	const FText InviteText = bInviteFeedback ? Room->GetLastResultText() : FText::GetEmpty();
	if (bInviteFeedback && !InviteText.IsEmpty()
		&& (Snapshot.RequestId != PresentedInviteFeedbackRequestId || !InviteText.EqualTo(PresentedInviteFeedback)))
	{
		PresentedInviteFeedbackRequestId = Snapshot.RequestId;
		PresentedInviteFeedback = InviteText;
		SetLocalResultText(InviteText, Root->GetVisibleFeedbackSource());
	}
	else if (!bInviteFeedback)
	{
		PresentedInviteFeedbackRequestId.Invalidate();
		PresentedInviteFeedback = FText::GetEmpty();
	}
	const bool bWasShowingRoom = Root->IsShowingRoom();
	if (bGameplayStartFailureRecovering && (bWasShowingRoom || bPresentedFrontendRoom || bStartGameFlowActive))
	{
		const FText FailureText = Room->GetLastResultText();
		if (bHasFrontendRoom)
		{
			SetLocalResultText(FailureText, Room);
			Root->ShowRoom();
			UE_LOG(LogCatUI, Warning, TEXT("Event=frontend_gameplay_loading_failed RequestId=%s Epoch=%lld World=%s NetMode=%d Error=%s Recovery=Room"),
				*Snapshot.RequestId.ToString(EGuidFormats::DigitsWithHyphens), Snapshot.OperationEpoch,
				*GetNameSafe(Root->GetWorld()), Root->GetWorld() ? static_cast<int32>(Root->GetWorld()->GetNetMode()) : -1,
				*UEnum::GetValueAsString(Snapshot.LastError));
			return;
		}
		if (Snapshot.SessionState == ECatOnlineSessionState::NoSession && Snapshot.SessionRole == ECatOnlineSessionRole::None)
		{
			const bool bRecoverToSaveList = bStartGameFlowActive;
			SetLocalResultText(FailureText, bRecoverToSaveList ? SaveModel.Get() : nullptr);
			if (bRecoverToSaveList) { Root->ShowSaveList(); }
			else { Root->ShowMenu(); }
			bPresentedFrontendRoom = false;
			UE_LOG(LogCatUI, Warning, TEXT("Event=frontend_gameplay_loading_failed RequestId=%s Epoch=%lld World=%s NetMode=%d Error=%s Recovery=%s"),
				*Snapshot.RequestId.ToString(EGuidFormats::DigitsWithHyphens), Snapshot.OperationEpoch,
				*GetNameSafe(Root->GetWorld()), Root->GetWorld() ? static_cast<int32>(Root->GetWorld()->GetNetMode()) : -1,
				*UEnum::GetValueAsString(Snapshot.LastError), bRecoverToSaveList ? TEXT("SaveList") : TEXT("Menu"));
			return;
		}
	}
	if (!bWasShowingRoom)
	{
		return;
	}
	if (Snapshot.SessionState == ECatOnlineSessionState::NoSession && Snapshot.ActiveOperation == ECatOnlineOperation::None
		&& Snapshot.LastError == ECatOnlineError::None && !bWaitingForSaveLoad && !bWaitingForRoomCreation)
	{
		SetLocalResultText(FText::GetEmpty());
		if (bStartGameFlowActive) { Root->ShowSaveList(); }
		else { Root->ShowMenu(); }
	}
}

// 设置变化流程：仅清除属于 Settings 的旧局部校验提示，让 View 读取新正式结果；不复制 Model 文本，也不清掉其他来源的异步反馈。
void UCatFrontendPageController::HandleSettingsModelChanged()
{
	if (LastResultSource.Get() == SettingsModel.Get()) { SetLocalResultText(FText::GetEmpty()); }
}

// 槽位有效性流程：遍历 SaveModel 当前正式摘要，只比较稳定 SlotId；显示名和索引都不能作为异步回调匹配键。
bool UCatFrontendPageController::IsCurrentSaveSlot(const FName SlotId) const
{
	if (SlotId.IsNone())
	{
		return false;
	}
	if (const UCatFrontendSaveModel* Save = SaveModel.Get())
	{
		for (const FCatSaveSlotSummary& Summary : Save->GetSlotSummaries())
		{
			if (Summary.SlotId == SlotId) { return true; }
		}
	}
	return false;
}

// 未入房释放流程：只在明确失败/取消调用；Save 无活动载荷时幂等成功，busy 或 Online 仍有操作/角色时拒绝。正式释放会同步广播，所以调用方必须先清自己的等待状态；已成立会话只由 Online 释放。
bool UCatFrontendPageController::ReleaseUnjoinedSave()
{
	UCatFrontendSaveModel* Save = SaveModel.Get();
	if (!Save) { return false; }
	if (Save->IsBusy()) { return false; }
	if (UCatFrontendRoomModel* Room = RoomModel.Get())
	{
		const FCatOnlineSnapshot Snapshot = Room->GetSnapshot();
		if (Snapshot.SessionRole != ECatOnlineSessionRole::None || Snapshot.ActiveOperation != ECatOnlineOperation::None
			|| (Snapshot.SessionState != ECatOnlineSessionState::NoSession && Snapshot.SessionState != ECatOnlineSessionState::Error)) { return false; }
	}
	if (Save->GetActiveSlotId().IsNone() && !Save->HasLoadedRunForTravel()) { return true; }
	const FName ReleasedSlotId = Save->GetActiveSlotId();
	const bool bReleased = Save->ReleaseActiveRun();
	UE_LOG(LogCatUI, Log, TEXT("Event=frontend_unjoined_save_release SlotId=%s Released=%d World=%s"),
		*ReleasedSlotId.ToString(), bReleased, *GetNameSafe(RootWidget.IsValid() ? RootWidget->GetWorld() : nullptr));
	return bReleased;
}

// 本地反馈写入流程：保存提示及所属 Model，空提示同时清来源；直接要求 Root 刷新文本解决无 Model 通知的同步拒绝，但不切页、不广播或改写正式结果。
void UCatFrontendPageController::SetLocalResultText(FText InResultText, UObject* ResultSource)
{
	LastResultText = MoveTemp(InResultText);
	LastResultSource = LastResultText.IsEmpty() ? nullptr : ResultSource;
	if (UCatFrontendRootWidget* Root = RootWidget.Get()) { Root->RefreshFlowFeedback(); }
}
