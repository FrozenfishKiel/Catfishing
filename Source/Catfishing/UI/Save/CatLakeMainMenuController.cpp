#include "UI/Save/CatLakeMainMenuController.h"

#include "EnhancedInputComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputCoreTypes.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Logging/CatLog.h"
#include "Save/CatSaveSubsystem.h"
#include "UI/CatUISettings.h"
#include "UI/Frontend/CatFrontendSettingsModel.h"
#include "UI/Save/CatLakeMainMenuWidget.h"

// 绑定流程：
// 1. 先解除可能残留的旧 Controller、输入绑定和系统订阅，确保复用对象不会向旧 World 回写 UI。
// 2. 只接受本地 Controller 和有效菜单 View；服务缺失不阻止菜单创建，只会让对应按钮禁用或显示明确反馈。
// 3. 创建局内设置 Model 并注入 View，复用主界面设置来源与草稿规则。
// 4. 订阅 Widget 意图和 Save 变化，再安装 Enhanced Input Action。
// 5. 最后渲染一份初始状态，让正式 WBP 拿到按钮可用性。
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
	PendingManualSaveRequestId.Invalidate();
	SettingsModel = NewObject<UCatFrontendSettingsModel>(this);
	if (SettingsModel)
	{
		SettingsModel->Initialize(InLocalPlayer);
		InView->InitializeLakeMenuSettings(SettingsModel);
	}
	InView->OnActionRequested.AddUObject(this, &ThisClass::HandleMenuActionRequested);
	if (UCatSaveSubsystem* Save = GetSaveSubsystem())
	{
		SaveChangedHandle = Save->OnChanged.AddUObject(this, &ThisClass::HandleSaveChanged);
		SaveCompletedHandle = Save->OnSaveCompleted.AddUObject(this, &ThisClass::HandleSaveCompleted);
	}
	InstallMenuInput();
	UpdateView();
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_lake_menu_bound Controller=%s View=%s"),
		*GetNameSafe(InController), *GetNameSafe(InView));
	return true;
}

// 解绑流程：
// 1. 若菜单打开，先关闭菜单并释放本页申请的输入锁。
// 2. 再移除 Enhanced Input 绑定、Widget 意图订阅、设置模型连接和 Save 订阅。
// 3. 最后清空弱引用、等待标记和结果文本，避免下一次 Pawn 装配继承旧反馈。
void UCatLakeMainMenuController::Unbind()
{
	SetMenuOpen(false);
	RemoveMenuInput();
	if (UCatLakeMainMenuWidget* View = BoundView.Get())
	{
		View->ResetLakeMenuSettings();
		View->OnActionRequested.RemoveAll(this);
		View->RemoveFromParent();
	}
	if (UCatSaveSubsystem* Save = GetSaveSubsystem(); Save && SaveChangedHandle.IsValid())
	{
		Save->OnChanged.Remove(SaveChangedHandle);
	}
	if (UCatSaveSubsystem* Save = GetSaveSubsystem(); Save && SaveCompletedHandle.IsValid())
	{
		Save->OnSaveCompleted.Remove(SaveCompletedHandle);
	}
	if (SettingsModel)
	{
		SettingsModel->Shutdown();
		SettingsModel = nullptr;
	}
	SaveChangedHandle.Reset();
	SaveCompletedHandle.Reset();
	BoundLocalPlayer.Reset();
	BoundPlayerController.Reset();
	BoundView.Reset();
	bMenuOpen = false;
	PendingManualSaveRequestId.Invalidate();
	ModalInputModeState = FCatUIModalInputModeState();
	LastStatusText = FText::GetEmpty();
}

// 菜单切换流程：PIE 约定 Shift+Escape 交给编辑器停止运行；普通 Escape 只反转本 Controller 的打开态，实际视口、输入模式和结果刷新交给 SetMenuOpen。
void UCatLakeMainMenuController::ToggleMenu()
{
#if WITH_EDITOR
	if (const APlayerController* Controller = BoundPlayerController.Get();
		Controller && (Controller->IsInputKeyDown(EKeys::LeftShift) || Controller->IsInputKeyDown(EKeys::RightShift)))
	{
		UE_LOG(LogCatUI, Log, TEXT("Event=ui_lake_menu_toggle_ignored Reason=EditorStopChord Controller=%s"),
			*GetNameSafe(Controller));
		return;
	}
#endif
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

// 关闭请求流程：只关闭菜单，不清 Save 已经受理的异步工作。
void UCatLakeMainMenuController::RequestCloseFromWidget()
{
	SetMenuOpen(false);
}

// 设置请求流程：切到局内设置页并清理暂停菜单底部反馈；设置内容复用 UCatFrontendSettingsModel，不再显示“未接入”的旧缺口。
void UCatLakeMainMenuController::RequestSettingsFromWidget()
{
	UCatLakeMainMenuWidget* View = BoundView.Get();
	if (!View || !SettingsModel)
	{
		LastStatusText = FText::FromString(TEXT("设置服务当前不可用。"));
		UpdateView();
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_lake_menu_settings_unavailable View=%s SettingsModel=%s"),
			*GetNameSafe(View), *GetNameSafe(SettingsModel));
		return;
	}
	LastStatusText = FText::GetEmpty();
	UpdateView();
	View->ShowSettingsPanel();
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_lake_menu_settings_opened View=%s SettingsModel=%s"),
		*GetNameSafe(View), *GetNameSafe(SettingsModel));
}

// 手动保存流程：只向 Save 子系统提交活动世界保存；受理后记录请求 ID，完成前不让目录刷新文本覆盖“正在保存”。
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
	if (Result.bAccepted)
	{
		PendingManualSaveRequestId = Result.RequestId;
		LastStatusText = FText::FromString(TEXT("正在保存当前游戏。"));
	}
	else
	{
		PendingManualSaveRequestId.Invalidate();
		LastStatusText = Save->GetActiveSlotId().IsNone()
			? FText::FromString(TEXT("当前没有可保存的游戏进度。"))
			: !Result.Message.IsEmpty() ? Result.Message : FText::FromString(TEXT("保存失败，请稍后重试。"));
	}
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

// 退出流程：先确认本地 Player、World 和 Controller 都仍有效；缺上下文时写入失败文本、刷新 View 并记录可定位日志。
// 成功分支按主界面同一语义直接请求本地 Quit，不再走 Online Leave 的保存、拆局、DestroySession 和回前台等待链。
void UCatLakeMainMenuController::RequestExitGameFromWidget()
{
	ULocalPlayer* Player = BoundLocalPlayer.Get();
	UWorld* World = Player ? Player->GetWorld() : nullptr;
	APlayerController* Controller = BoundPlayerController.Get();
	if (!Player || !World || !Controller)
	{
		LastStatusText = FText::FromString(TEXT("当前无法退出游戏。"));
		UpdateView();
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_lake_menu_exit_unavailable Reason=LocalQuitContextMissing Player=%s World=%s Controller=%s"),
			*GetNameSafe(Player), *GetNameSafe(World), *GetNameSafe(Controller));
		return;
	}
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_lake_menu_exit_confirmed World=%s NetMode=%d Controller=%s"),
		*World->GetName(), static_cast<int32>(World->GetNetMode()), *GetNameSafe(Controller));
	UKismetSystemLibrary::QuitGame(World, Controller, EQuitPreference::Quit, false);
}

// 设置应用流程：SettingsModel 负责实际提交；成功时回暂停菜单，失败时留在设置页让玩家看到具体失败原因。
void UCatLakeMainMenuController::RequestApplySettingsFromWidget()
{
	UCatFrontendSettingsModel* Settings = GetSettingsModel();
	UCatLakeMainMenuWidget* View = BoundView.Get();
	if (!Settings || !Settings->Apply())
	{
		LastStatusText = FText::GetEmpty();
		if (View)
		{
			View->ShowSettingsPanel();
		}
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_lake_menu_settings_apply_failed SettingsModel=%s"),
			*GetNameSafe(Settings));
		return;
	}
	LastStatusText = FText::FromString(TEXT("设置已应用。"));
	UpdateView();
	if (View)
	{
		View->ShowCommandMenu();
	}
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_lake_menu_settings_applied SettingsModel=%s"),
		*GetNameSafe(Settings));
}

// 设置取消流程：丢弃未应用草稿并回暂停菜单；它不关闭 ESC 菜单，方便玩家继续保存或退出。
void UCatLakeMainMenuController::RequestCancelSettingsFromWidget()
{
	if (UCatFrontendSettingsModel* Settings = GetSettingsModel())
	{
		Settings->Cancel();
	}
	LastStatusText = FText::GetEmpty();
	UpdateView();
	if (UCatLakeMainMenuWidget* View = BoundView.Get())
	{
		View->ShowCommandMenu();
	}
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_lake_menu_settings_cancelled"));
}

// 设置恢复默认流程：只改 SettingsModel 草稿；View 会通过模型通知刷新，不把默认值直接写进正式配置。
void UCatLakeMainMenuController::RequestRestoreSettingsDefaultsFromWidget()
{
	if (UCatFrontendSettingsModel* Settings = GetSettingsModel())
	{
		Settings->RestoreDefaults();
	}
}

// 输出设备刷新流程：转交 SettingsModel 的正式异步枚举入口；同步拒绝留在设置页自身反馈文本。
void UCatLakeMainMenuController::RequestRefreshAudioOutputDevicesFromWidget()
{
	UCatFrontendSettingsModel* Settings = GetSettingsModel();
	if (!Settings || !Settings->RefreshAudioOutputDevices())
	{
		if (UCatLakeMainMenuWidget* View = BoundView.Get())
		{
			View->ShowSettingsPanel();
		}
	}
}

// 游戏分类流程：通过 SettingsModel 命名接口切分类，避免 View 用字符串或索引保存状态。
void UCatLakeMainMenuController::RequestSelectGameSettingsFromWidget()
{
	if (UCatFrontendSettingsModel* Settings = GetSettingsModel()) { Settings->SelectGame(); }
}

// 画面分类流程：通过 SettingsModel 命名接口切分类，避免 View 用字符串或索引保存状态。
void UCatLakeMainMenuController::RequestSelectGraphicsSettingsFromWidget()
{
	if (UCatFrontendSettingsModel* Settings = GetSettingsModel()) { Settings->SelectGraphics(); }
}

// 声音分类流程：通过 SettingsModel 命名接口切分类，避免 View 用字符串或索引保存状态。
void UCatLakeMainMenuController::RequestSelectAudioSettingsFromWidget()
{
	if (UCatFrontendSettingsModel* Settings = GetSettingsModel()) { Settings->SelectAudio(); }
}

// 控制分类流程：通过 SettingsModel 命名接口进入受限分类；控制设置未开放的事实仍由 Model 和 View 展示。
void UCatLakeMainMenuController::RequestSelectControlsSettingsFromWidget()
{
	if (UCatFrontendSettingsModel* Settings = GetSettingsModel()) { Settings->SelectControls(); }
}

// 打开态写入流程：
// 1. 缺 Controller 或 View 时不改状态，避免留下无法恢复输入的半打开菜单。
// 2. 打开时先入视口、回到命令页、写 ViewState，再切 UIOnly 并锁移动/视角。
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
		View->ShowCommandMenu();
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

// ViewState 刷新流程：状态文本来自最近入口反馈，设置按钮由 SettingsModel 存在性决定，保存按钮要求 Save 服务存在且不忙，返回与退出按钮保持可用。
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
	ViewState.bSettingsEnabled = SettingsModel != nullptr;
	ViewState.bCloseEnabled = true;
	ViewState.bSaveEnabled = Save && !Save->IsBusy();
	ViewState.bExitEnabled = true;
	View->RenderMenu(ViewState);
}

// 菜单意图分发流程：Widget 只广播语义，这里才根据语义调用关闭、设置、保存、退出和设置页命令入口。
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
	case ECatLakeMainMenuAction::ApplySettings:
		RequestApplySettingsFromWidget();
		break;
	case ECatLakeMainMenuAction::CancelSettings:
		RequestCancelSettingsFromWidget();
		break;
	case ECatLakeMainMenuAction::RestoreSettingsDefaults:
		RequestRestoreSettingsDefaultsFromWidget();
		break;
	case ECatLakeMainMenuAction::RefreshAudioOutputDevices:
		RequestRefreshAudioOutputDevicesFromWidget();
		break;
	case ECatLakeMainMenuAction::SelectGameSettings:
		RequestSelectGameSettingsFromWidget();
		break;
	case ECatLakeMainMenuAction::SelectGraphicsSettings:
		RequestSelectGraphicsSettingsFromWidget();
		break;
	case ECatLakeMainMenuAction::SelectAudioSettings:
		RequestSelectAudioSettingsFromWidget();
		break;
	case ECatLakeMainMenuAction::SelectControlsSettings:
		RequestSelectControlsSettingsFromWidget();
		break;
	default:
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_lake_menu_action_unknown Action=%d"), static_cast<int32>(Action));
		break;
	}
}

// Save 变化流程：只刷新 busy 驱动的按钮可用性；手动保存的用户文案等待请求 ID 完成回调，避免被目录读取提示覆盖。
void UCatLakeMainMenuController::HandleSaveChanged()
{
	UpdateView();
}

// 保存完成流程：只匹配本菜单发起的手动保存；成功使用玩家可读完成文案，失败再展示 Save 子系统的具体原因。
void UCatLakeMainMenuController::HandleSaveCompleted(const FGuid RequestId, const bool bSuccess)
{
	if (!PendingManualSaveRequestId.IsValid() || PendingManualSaveRequestId != RequestId)
	{
		return;
	}
	PendingManualSaveRequestId.Invalidate();
	UCatSaveSubsystem* Save = GetSaveSubsystem();
	LastStatusText = bSuccess
		? FText::FromString(TEXT("游戏已保存。"))
		: (Save && !Save->GetLastResultText().IsEmpty()
			? Save->GetLastResultText() : FText::FromString(TEXT("保存失败，请稍后重试。")));
	UpdateView();
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_lake_menu_manual_save_completed RequestId=%s Success=%d"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens), bSuccess);
}

// Save 来源定位流程：LocalPlayer 是本地 UI 与 GameInstance 子系统的生命周期锚点；失效时不退回全局对象。
UCatSaveSubsystem* UCatLakeMainMenuController::GetSaveSubsystem() const
{
	const ULocalPlayer* Player = BoundLocalPlayer.Get();
	UGameInstance* GameInstance = Player ? Player->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UCatSaveSubsystem>() : nullptr;
}

// 设置来源定位流程：返回本 Controller 创建的局内设置 Model；空值表示菜单尚未绑定或已经拆除。
UCatFrontendSettingsModel* UCatLakeMainMenuController::GetSettingsModel() const
{
	return SettingsModel;
}
