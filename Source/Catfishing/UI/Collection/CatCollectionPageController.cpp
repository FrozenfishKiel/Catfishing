#include "UI/Collection/CatCollectionPageController.h"

#include "EnhancedInputComponent.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Logging/CatLog.h"
#include "UI/CatUISettings.h"
#include "UI/Collection/CatCollectionModel.h"
#include "UI/Collection/CatCollectionWidget.h"

// 绑定流程：先解除旧页面与输入，再校验本地 Controller 与页面；Model 订阅成对建立，最后安装既有图鉴 Action。
bool UCatCollectionPageController::Bind(ULocalPlayer* InLocalPlayer, APlayerController* InController,
	UCatCollectionWidget* InView)
{
	Unbind();
	if (!InLocalPlayer || !InController || !InController->IsLocalController() || !InView)
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_collection_bind_failed LocalPlayer=%s Controller=%s View=%s"),
			*GetNameSafe(InLocalPlayer), *GetNameSafe(InController), *GetNameSafe(InView));
		return false;
	}
	BoundPlayerController = InController;
	BoundView = InView;
	CollectionModel = NewObject<UCatCollectionModel>(this);
	if (!CollectionModel)
	{
		Unbind();
		return false;
	}
	CollectionModelChangedHandle = CollectionModel->OnViewStateChanged.AddUObject(
		this, &ThisClass::HandleCollectionViewStateChanged);
	// Profile 未就绪不是装配失败：补一次 Refresh 让 Model 发布 unavailable 投影，页面照常能开，只显示“本地记录未就绪”。
	if (!CollectionModel->Bind(InLocalPlayer))
	{
		CollectionModel->Refresh();
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_collection_profile_unavailable LocalPlayer=%s"),
			*GetNameSafe(InLocalPlayer));
	}
	InstallCollectionInput();
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_collection_bound Controller=%s View=%s"),
		*GetNameSafe(InController), *GetNameSafe(InView));
	return true;
}

// 解绑流程：先关页面释放模态输入，再移除 Action，最后成对解除 Model 订阅并释放页面引用。
void UCatCollectionPageController::Unbind()
{
	SetCollectionOpen(false);
	RemoveCollectionInput();
	if (CollectionModel)
	{
		CollectionModel->OnViewStateChanged.Remove(CollectionModelChangedHandle);
		CollectionModel->Unbind();
		CollectionModel = nullptr;
	}
	CollectionModelChangedHandle.Reset();
	BoundView.Reset();
	BoundPlayerController.Reset();
}

// 切换流程：三个入口共用同一个开关，页面本身不缓存第二份打开态。
void UCatCollectionPageController::ToggleCollection()
{
	SetCollectionOpen(!bCollectionOpen);
}

// 只返回本页面状态，不从 Model、Widget 可见性或鼠标状态拼接第二份状态。
bool UCatCollectionPageController::IsCollectionOpen() const
{
	return bCollectionOpen;
}

// 输入链完成装配后重装同一 Action，安装函数先移除旧绑定。
void UCatCollectionPageController::RefreshInputBinding()
{
	InstallCollectionInput();
}

// 迟到的关闭请求不会重新打开页面；关闭路径与图鉴键、Escape 共用。
void UCatCollectionPageController::RequestCloseCollectionFromWidget()
{
	SetCollectionOpen(false);
}

// 印记隐藏流程：只转交给 Model（它持有本页面唯一的 Profile 引用）；Model 写盘成功会自己重发投影，
// 页面不缓存第二份隐藏状态。Model 不在时返回 false，调用方据此不显示成功反馈。
bool UCatCollectionPageController::RequestSetImprintHiddenFromWidget(const FGuid ImprintId, const bool bHidden)
{
	return CollectionModel && CollectionModel->SetImprintHidden(ImprintId, bHidden);
}

// 打开先重绘再入视口并申请输入锁；关闭先恢复输入再移出页面，两侧都只处理本页面自己申请的那一层。
void UCatCollectionPageController::SetCollectionOpen(const bool bOpen)
{
	if (bCollectionOpen == bOpen)
	{
		return;
	}
	APlayerController* Controller = BoundPlayerController.Get();
	UCatCollectionWidget* View = BoundView.Get();
	if (bOpen)
	{
		if (!Controller || !View)
		{
			UE_LOG(LogCatUI, Warning,
				TEXT("Event=ui_collection_open_rejected Reason=DependencyUnavailable Controller=%s View=%s"),
				*GetNameSafe(Controller), *GetNameSafe(View));
			return;
		}
		if (CollectionModel)
		{
			View->RenderCollection(CollectionModel->GetViewState());
		}
		View->AddToViewport(10);
		bCollectionOpen = true;
		CatUIModalInputMode::Open(Controller, View, ModalInputModeState);
		UE_LOG(LogCatUI, Log, TEXT("Event=ui_collection_opened Controller=%s View=%s"),
			*GetNameSafe(Controller), *GetNameSafe(View));
		return;
	}
	CatUIModalInputMode::Close(Controller, ModalInputModeState);
	bCollectionOpen = false;
	if (View)
	{
		View->RemoveFromParent();
	}
}

// 从已有输入配置加载图鉴 Action；与主菜单或背包共用同一 Action 时避让，只安装 Action 回调，不另建 IMC 或硬编码按键。
void UCatCollectionPageController::InstallCollectionInput()
{
	RemoveCollectionInput();
	APlayerController* Controller = BoundPlayerController.Get();
	UEnhancedInputComponent* Input = Controller ? Cast<UEnhancedInputComponent>(Controller->InputComponent) : nullptr;
	const UCatUISettings* Settings = GetDefault<UCatUISettings>();
	UInputAction* ToggleAction = Settings->LoadCollectionToggleAction();
	UInputAction* MainMenuAction = Settings->LoadMainMenuToggleAction();
	UInputAction* InventoryAction = Settings->LoadInventoryToggleAction();
	const UInputMappingContext* MappingContext = Settings->LoadGameplayInputMappingContext();
	if (!Input || !ToggleAction || !MappingContext)
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_collection_input_unavailable Controller=%s Action=%s Context=%s"),
			*GetNameSafe(Controller), *Settings->CollectionToggleAction.ToSoftObjectPath().ToString(),
			*Settings->GameplayInputMappingContext.ToSoftObjectPath().ToString());
		return;
	}
	if ((MainMenuAction && ToggleAction == MainMenuAction) || (InventoryAction && ToggleAction == InventoryAction))
	{
		return;
	}
	AppliedCollectionToggleAction = ToggleAction;
	CollectionInputBindingHandle = Input->BindAction(AppliedCollectionToggleAction, ETriggerEvent::Started,
		this, &ThisClass::ToggleCollection).GetHandle();
	BoundCollectionInputComponent = Input;
}

// 从当初绑定的输入组件精确移除句柄；不修改基础 IMC，重复调用保持无操作。
void UCatCollectionPageController::RemoveCollectionInput()
{
	if (UEnhancedInputComponent* Input = BoundCollectionInputComponent.Get(); Input && CollectionInputBindingHandle != 0)
	{
		Input->RemoveBindingByHandle(CollectionInputBindingHandle);
	}
	BoundCollectionInputComponent.Reset();
	CollectionInputBindingHandle = 0;
	AppliedCollectionToggleAction = nullptr;
}

// Model 变化流程：只有页面打开时才重绘；关闭期间的 Profile 变化留给下一次打开时的整份重绘。
void UCatCollectionPageController::HandleCollectionViewStateChanged()
{
	UCatCollectionWidget* View = BoundView.Get();
	if (bCollectionOpen && View && CollectionModel)
	{
		View->RenderCollection(CollectionModel->GetViewState());
	}
}
