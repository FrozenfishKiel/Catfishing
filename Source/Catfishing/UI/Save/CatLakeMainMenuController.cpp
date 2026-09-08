#include "UI/Save/CatLakeMainMenuController.h"

#include "EnhancedInputComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Logging/CatLog.h"
#include "Online/CatOnlineSubsystem.h"
#include "Save/CatSaveSubsystem.h"
#include "UI/CatUISettings.h"
#include "UI/Save/CatLakeMainMenuWidget.h"

// 绑定流程：
// 1. 先解除可能残留的旧 Controller、输入绑定和系统订阅，确保复用对象不会向旧 World 回写 UI。
// 2. 只接受本地 Controller 和有效菜单 View；服务缺失不阻止菜单创建，只会让对应按钮禁用或显示明确反馈。
// 3. 订阅 Widget 意图、Save 变化和 Online 快照，再安装 Enhanced Input Action。
// 4. 最后渲染一份初始状态，让原生 fallback 和正式 WBP 都拿到按钮可用性。
bool UCatLakeMainMenuController::Bind(ULocalPlayer* InLocalPlayer, APlayerController* InController,
	UCatLakeMainMenuWidget* InView)
{
	Unbind();
	if (!InLocalPlayer || !InController || !InController->IsLocalController() || !InView)
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_lake_menu_bind_failed LocalPlayer=%s Controller=%s View=%s"),
			*GetNameSafe(InLocalPlayer), *GetNameSafe(InController), *GetNameSafe(InView));
		return false;
	}

	BoundLocalPlayer = InLocalPlayer;
	BoundPlayerController = InController;
	BoundView = InView;
	LastStatusText = FText::GetEmpty();
	InView->OnActionRequested.AddUObject(this, &ThisClass::HandleMenuActionRequested);
	if (UCatSaveSubsystem* Save = GetSaveSubsystem())
	{
		SaveChangedHandle = Save->OnChanged.AddUObject(this, &ThisClass::HandleSaveChanged);
	}
	if (UCatOnlineSubsystem* Online = GetOnlineSubsystem())
	{
		OnlineSnapshotHandle = Online->OnSnapshotChanged.AddUObject(this, &ThisClass::HandleOnlineSnapshotChanged);
	}
	InstallMenuInput();
	UpdateView();
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_lake_menu_bound Controller=%s View=%s"),
		*GetNameSafe(InController), *GetNameSafe(InView));
	return true;
}

// 解绑流程：
// 1. 若菜单打开，先关闭菜单并释放本页申请的输入锁。
// 2. 再移除 Enhanced Input 绑定、Widget 意图订阅和 Save/Online 订阅。
// 3. 最后清空弱引用、等待标记和结果文本，避免下一次 Pawn 装配继承旧反馈。
void UCatLakeMainMenuController::Unbind()
{
	SetMenuOpen(false);
	RemoveMenuInput();
	if (UCatLakeMainMenuWidget* View = BoundView.Get())
	{
		View->OnActionRequested.RemoveAll(this);
		View->RemoveFromParent();
	}
	if (UCatSaveSubsystem* Save = GetSaveSubsystem(); Save && SaveChangedHandle.IsValid())
	{
		Save->OnChanged.Remove(SaveChangedHandle);
	}
	if (UCatOnlineSubsystem* Online = GetOnlineSubsystem(); Online && OnlineSnapshotHandle.IsValid())
	{
		Online->OnSnapshotChanged.Remove(OnlineSnapshotHandle);
	}
	SaveChangedHandle.Reset();
	OnlineSnapshotHandle.Reset();
	BoundLocalPlayer.Reset();
	BoundPlayerController.Reset();
	BoundView.Reset();
	bMenuOpen = false;
	bExitPending = false;
	ModalInputModeState = FCatUIModalInputModeState();
	LastStatusText = FText::GetEmpty();
}

// 菜单切换流程：只反转本 Controller 的打开态；实际视口、输入模式和结果刷新交给 SetMenuOpen。
void UCatLakeMainMenuController::ToggleMenu()
{
	SetMenuOpen(!bMenuOpen);
}

// 打开态读取流程：返回本 Controller 持有的唯一状态，避免从 Widget 层级或 Visibility 推断业务事实。
bool UCatLakeMainMenuController::IsMenuOpen() const
{
	return bMenuOpen;
}

// 输入刷新流程：Controller InputComponent 可能因 Pawn 或重启重建；重新安装前会移除旧组件绑定。
void UCatLakeMainMenuController::RefreshInputBinding()
{
	InstallMenuInput();
}

// 关闭请求流程：只关闭菜单，不清 Save/Online 已经受理的异步工作。
void UCatLakeMainMenuController::RequestCloseFromWidget()
{
	SetMenuOpen(false);
}

// 设置请求流程：当前项目没有正式局内设置页资产或控制器；保留入口并显示明确缺口，避免按钮点击静默空转。
void UCatLakeMainMenuController::RequestSettingsFromWidget()
{
	LastStatusText = FText::FromString(TEXT("局内设置页尚未接入；当前请在主界面设置中调整。"));
	UpdateView();
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_lake_menu_settings_requested Result=missing_in_game_settings_page"));
}

// 手动保存流程：只向 Save 子系统提交活动世界保存；同步拒绝和异步终态都通过同一结果文本显示，不在 UI 复制槽位状态。
void UCatLakeMainMenuController::RequestSaveFromWidget()
{
	UCatSaveSubsystem* Save = GetSaveSubsystem();
	if (!Save)
	{
		LastStatusText = FText::FromString(TEXT("存档服务当前不可用。"));
		UpdateView();
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_lake_menu_save_unavailable Reason=SaveSubsystemMissing"));
		return;
	}
	const FCatSaveResult Result = Save->RequestSaveActiveRun();
	LastStatusText = Result.Message;
	UpdateView();
	if (Result.bAccepted)
	{
		UE_LOG(LogCatUI, Log,
			TEXT("Event=ui_lake_menu_save_requested RequestId=%s Accepted=true ActiveSlot=%s Busy=%s Message=\"%s\""),
			*Result.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			*Save->GetActiveSlotId().ToString(),
			Save->IsBusy() ? TEXT("true") : TEXT("false"),
			*Result.Message.ToString());
		return;
	}
	UE_LOG(LogCatUI, Warning,
		TEXT("Event=ui_lake_menu_save_requested RequestId=%s Accepted=false ActiveSlot=%s Busy=%s Message=\"%s\""),
		*Result.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		*Save->GetActiveSlotId().ToString(),
		Save->IsBusy() ? TEXT("true") : TEXT("false"),
		*Result.Message.ToString());
}

// 退出流程：只提交 Online Leave；Host 保存、Run teardown、Session 销毁和回前台仍沿用 Online 的唯一生命周期。
void UCatLakeMainMenuController::RequestExitGameFromWidget()
{
	if (bExitPending)
	{
		return;
	}
	UCatOnlineSubsystem* Online = GetOnlineSubsystem();
	if (!Online)
	{
		LastStatusText = FText::FromString(TEXT("房间服务当前不可用。"));
		UpdateView();
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_lake_menu_exit_unavailable Reason=OnlineSubsystemMissing"));
		return;
	}
	const FCatOnlineResult Result = Online->RequestLeave();
	bExitPending = Result.bAccepted;
	LastStatusText = Result.bAccepted
		? FText::FromString(TEXT("正在退出当前游戏。"))
		: FText::FromString(TEXT("当前无法退出游戏。"));
	UpdateView();
	if (Result.bAccepted)
	{
		UE_LOG(LogCatUI, Log,
			TEXT("Event=ui_lake_menu_exit_requested RequestId=%s Accepted=true Error=%s"),
			*Result.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			*UEnum::GetValueAsString(Result.Error));
		return;
	}
	UE_LOG(LogCatUI, Warning,
		TEXT("Event=ui_lake_menu_exit_requested RequestId=%s Accepted=false Error=%s"),
		*Result.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		*UEnum::GetValueAsString(Result.Error));
}

// 打开态写入流程：
// 1. 缺 Controller 或 View 时不改状态，避免留下无法恢复输入的半打开菜单。
// 2. 打开时先入视口、写 ViewState，再切 UIOnly 并锁移动/视角。
// 3. 关闭时先释放输入模式，再从视口移除，让玩家立即回到游戏输入。
void UCatLakeMainMenuController::SetMenuOpen(const bool bOpen)
{
	APlayerController* Controller = BoundPlayerController.Get();
	UCatLakeMainMenuWidget* View = BoundView.Get();
	if (!Controller || !View || bMenuOpen == bOpen)
	{
		return;
	}
	if (bOpen)
	{
		if (!View->IsInViewport())
		{
			View->AddToViewport(30);
		}
		bMenuOpen = true;
		UpdateView();
		ApplyMenuInputMode(true);
		UE_LOG(LogCatUI, Log, TEXT("Event=ui_lake_menu_opened Controller=%s View=%s"),
			*GetNameSafe(Controller), *GetNameSafe(View));
		return;
	}

	bMenuOpen = false;
	ApplyMenuInputMode(false);
	View->RemoveFromParent();
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_lake_menu_closed Controller=%s View=%s"),
		*GetNameSafe(Controller), *GetNameSafe(View));
}

// 输入安装流程：
// 1. 要求 Controller 当前 InputComponent 是 EnhancedInputComponent。
// 2. 从 UI Settings 加载已有 InputContext 中的主菜单 Action，并把 IMC 加载作为资产接线校验。
// 3. 只 BindAction，不 AddMappingContext、不 MapKey，避免运行时代码写死按键或生成第二套 IMC。
void UCatLakeMainMenuController::InstallMenuInput()
{
	RemoveMenuInput();
	APlayerController* Controller = BoundPlayerController.Get();
	UEnhancedInputComponent* Input = Controller ? Cast<UEnhancedInputComponent>(Controller->InputComponent) : nullptr;
	const UCatUISettings* Settings = GetDefault<UCatUISettings>();
	UInputAction* ToggleAction = Settings ? Settings->LoadMainMenuToggleAction() : nullptr;
	const UInputMappingContext* MappingContext = Settings ? Settings->LoadGameplayInputMappingContext() : nullptr;
	if (!Input || !Settings || !ToggleAction || !MappingContext)
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_lake_menu_input_unavailable Controller=%s Action=%s Context=%s"),
			*GetNameSafe(Controller),
			Settings ? *Settings->MainMenuToggleAction.ToSoftObjectPath().ToString() : TEXT("None"),
			Settings ? *Settings->GameplayInputMappingContext.ToSoftObjectPath().ToString() : TEXT("None"));
		return;
	}
	AppliedMainMenuToggleAction = ToggleAction;
	MenuInputBindingHandle = Input->BindAction(
		AppliedMainMenuToggleAction, ETriggerEvent::Started, this, &ThisClass::ToggleMenu).GetHandle();
	BoundMenuInputComponent = Input;
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_lake_menu_input_bound Controller=%s Action=%s"),
		*GetNameSafe(Controller), *GetNameSafe(AppliedMainMenuToggleAction));
}

// 输入移除流程：从安装时记录的 EnhancedInputComponent 删除精确绑定；InputContext 属于基础输入层，本菜单不安装也不移除。
void UCatLakeMainMenuController::RemoveMenuInput()
{
	if (UEnhancedInputComponent* Input = BoundMenuInputComponent.Get();
		Input && MenuInputBindingHandle != 0)
	{
		Input->RemoveBindingByHandle(MenuInputBindingHandle);
	}
	BoundMenuInputComponent.Reset();
	MenuInputBindingHandle = 0;
	AppliedMainMenuToggleAction = nullptr;
}

// 输入模式流程：打开时聚焦菜单、锁移动/视角并停止当前移动；关闭时只释放本菜单申请过的那一层锁。
void UCatLakeMainMenuController::ApplyMenuInputMode(const bool bOpen)
{
	APlayerController* Controller = BoundPlayerController.Get();
	if (bOpen)
	{
		CatUIModalInputMode::Open(Controller, BoundView.Get(), ModalInputModeState);
		return;
	}
	CatUIModalInputMode::Close(Controller, ModalInputModeState);
}

// ViewState 刷新流程：Save busy 决定保存按钮，Online 离开 pending 决定所有会产生新流程的按钮；状态文本只显示最近入口反馈。
void UCatLakeMainMenuController::UpdateView()
{
	UCatLakeMainMenuWidget* View = BoundView.Get();
	if (!View)
	{
		return;
	}
	UCatSaveSubsystem* Save = GetSaveSubsystem();
	FCatLakeMainMenuViewState ViewState;
	ViewState.StatusText = LastStatusText;
	ViewState.bSettingsEnabled = !bExitPending;
	ViewState.bSaveEnabled = Save && !Save->IsBusy() && !bExitPending;
	ViewState.bExitEnabled = !bExitPending;
	View->RenderMenu(ViewState);
}

// 菜单意图分发流程：Widget 只广播语义，这里才根据语义调用本 Controller 的明确业务入口。
void UCatLakeMainMenuController::HandleMenuActionRequested(const ECatLakeMainMenuAction Action)
{
	switch (Action)
	{
	case ECatLakeMainMenuAction::Close:
		RequestCloseFromWidget();
		break;
	case ECatLakeMainMenuAction::OpenSettings:
		RequestSettingsFromWidget();
		break;
	case ECatLakeMainMenuAction::Save:
		RequestSaveFromWidget();
		break;
	case ECatLakeMainMenuAction::ExitGame:
		RequestExitGameFromWidget();
		break;
	default:
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_lake_menu_action_unknown Action=%d"), static_cast<int32>(Action));
		break;
	}
}

// Save 变化流程：只在菜单绑定期间读取 Save 的正式结果文本；关闭菜单不取消已排队的保存。
void UCatLakeMainMenuController::HandleSaveChanged()
{
	if (UCatSaveSubsystem* Save = GetSaveSubsystem())
	{
		LastStatusText = Save->GetLastResultText();
	}
	UpdateView();
}

// Online 变化流程：如果离开请求已经回到无活动操作但 UI 仍在 Lake 中，就说明玩家可以重试，按钮恢复可用。
void UCatLakeMainMenuController::HandleOnlineSnapshotChanged()
{
	UCatOnlineSubsystem* Online = GetOnlineSubsystem();
	if (!Online)
	{
		bExitPending = false;
		UpdateView();
		return;
	}
	const FCatOnlineSnapshot Snapshot = Online->GetSnapshot();
	if (bExitPending && Snapshot.ActiveOperation == ECatOnlineOperation::None)
	{
		bExitPending = false;
		if (Snapshot.LastError != ECatOnlineError::None)
		{
			LastStatusText = FText::FromString(TEXT("退出当前游戏失败，请稍后重试。"));
		}
	}
	UpdateView();
}

// Save 来源定位流程：LocalPlayer 是本地 UI 与 GameInstance 子系统的生命周期锚点；失效时不退回全局对象。
UCatSaveSubsystem* UCatLakeMainMenuController::GetSaveSubsystem() const
{
	const ULocalPlayer* Player = BoundLocalPlayer.Get();
	UGameInstance* GameInstance = Player ? Player->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UCatSaveSubsystem>() : nullptr;
}

// Online 来源定位流程：只从当前 LocalPlayer 的 GameInstance 读取正式联机系统，避免 PIE 多 World 串线。
UCatOnlineSubsystem* UCatLakeMainMenuController::GetOnlineSubsystem() const
{
	const ULocalPlayer* Player = BoundLocalPlayer.Get();
	UGameInstance* GameInstance = Player ? Player->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UCatOnlineSubsystem>() : nullptr;
}
