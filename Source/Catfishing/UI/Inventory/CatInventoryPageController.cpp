#include "UI/Inventory/CatInventoryPageController.h"

#include "Character/CatCharacter.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "Inventory/CatInventoryComponent.h"
#include "Logging/CatLog.h"
#include "UI/CatUISettings.h"
#include "UI/Inventory/CatInventoryWidget.h"
#include "UI/InventorySlot/CatInventorySlotWidget.h"

// 先解除旧页面与输入，再验证当前本地角色；默认背包只绑定该角色库存，随后安装现有库存 Action。
bool UCatInventoryPageController::Bind(APlayerController* InController, UCatInventoryWidget* InView)
{
	Unbind();
	ACatCharacter* Character = InController ? Cast<ACatCharacter>(InController->GetPawn()) : nullptr;
	if (!InController || !InController->IsLocalController() || !InView || !Character || !Character->GetInventoryComponent())
	{
		return false;
	}
	BoundPlayerController = InController;
	DefaultInventoryView = InView;
	BoundView = InView;
	InView->SetInventoryContext(Character->GetInventoryComponent());
	InstallInventoryInput();
	return true;
}

// 先释放视口与模态输入，再移除 Action 和页面引用；各 WBP 在移出时自行解绑所属库存 Model。
void UCatInventoryPageController::Unbind()
{
	SetInventoryOpen(false);
	RemoveInventoryInput();
	BoundView = nullptr;
	DefaultInventoryView.Reset();
	BoundPlayerController.Reset();
}

// 关闭交互页后 BoundView 已回到默认背包；普通切换不改任何库存 Model 或世界上下文。
void UCatInventoryPageController::ToggleInventory()
{
	SetInventoryOpen(!bInventoryOpen);
}

// 所有世界库存采用相同打开流程：关闭旧页，验证资源，创建指定 WBP，注入库存并入视口。
// 嵌套的普通背包 WBP 没有被注入外部库存，构建时会独立绑定 owning Pawn 的背包。
bool UCatInventoryPageController::OpenInventory(UCatInventoryComponent* Inventory,
	const TSubclassOf<UCatInventoryWidget> InventoryViewClass)
{
	SetInventoryOpen(false);
	APlayerController* Controller = BoundPlayerController.Get();
	const TSubclassOf<UCatInventorySlotWidget> SlotClass = GetDefault<UCatUISettings>()->LoadInventorySlotWidgetClass();
	if (!Controller || !Inventory || !InventoryViewClass || !SlotClass)
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_inventory_open_rejected World=%s Reason=DependencyUnavailable Inventory=%s ViewClass=%s"),
			*GetPathNameSafe(GetWorld()), *GetPathNameSafe(Inventory), *GetNameSafe(InventoryViewClass.Get()));
		return false;
	}
	UCatInventoryWidget* View = CreateWidget<UCatInventoryWidget>(Controller, InventoryViewClass);
	if (!View)
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_inventory_open_rejected World=%s Reason=WidgetCreationFailed ViewClass=%s"),
			*GetPathNameSafe(Controller->GetWorld()), *GetNameSafe(InventoryViewClass.Get()));
		return false;
	}
	View->SetInventorySlotWidgetClass(SlotClass);
	View->SetInventoryContext(Inventory);
	BoundView = View;
	SetInventoryOpen(true);
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_inventory_opened World=%s NetMode=%d Inventory=%s View=%s Opened=%s"),
		*GetPathNameSafe(Controller->GetWorld()), static_cast<int32>(Controller->GetNetMode()),
		*GetPathNameSafe(Inventory), *GetNameSafe(View), bInventoryOpen ? TEXT("true") : TEXT("false"));
	return bInventoryOpen;
}

// 只返回本窗口状态，不从 Model、Widget 可见性或鼠标状态拼接第二份状态。
bool UCatInventoryPageController::IsInventoryOpen() const
{
	return bInventoryOpen;
}

// 输入链完成装配后重装同一 Action，安装函数先移除旧绑定。
void UCatInventoryPageController::RefreshInputBinding()
{
	InstallInventoryInput();
}

// 迟到的关闭请求不会重新打开窗口；关闭路径与键盘切换共用。
void UCatInventoryPageController::RequestCloseInventoryFromWidget()
{
	SetInventoryOpen(false);
}

// 打开先加入视口再申请输入锁；关闭先恢复输入再移出页面，交互页关闭后释放，默认背包留给下一次打开。
void UCatInventoryPageController::SetInventoryOpen(const bool bOpen)
{
	if (bInventoryOpen == bOpen)
	{
		return;
	}
	APlayerController* Controller = BoundPlayerController.Get();
	if (bOpen)
	{
		if (!Controller || !BoundView)
		{
			return;
		}
		BoundView->AddToViewport(10);
		bInventoryOpen = true;
		CatUIModalInputMode::Open(Controller, BoundView, ModalInputModeState);
		return;
	}
	CatUIModalInputMode::Close(Controller, ModalInputModeState);
	bInventoryOpen = false;
	if (BoundView)
	{
		BoundView->RemoveFromParent();
	}
	BoundView = DefaultInventoryView.Get();
}

// 从已有输入配置加载库存 Action；与主菜单共用时避让，只安装 Action 回调，不另建 IMC 或硬编码按键。
void UCatInventoryPageController::InstallInventoryInput()
{
	RemoveInventoryInput();
	APlayerController* Controller = BoundPlayerController.Get();
	UEnhancedInputComponent* Input = Controller ? Cast<UEnhancedInputComponent>(Controller->InputComponent) : nullptr;
	const UCatUISettings* Settings = GetDefault<UCatUISettings>();
	UInputAction* ToggleAction = Settings->LoadInventoryToggleAction();
	UInputAction* MainMenuAction = Settings->LoadMainMenuToggleAction();
	const UInputMappingContext* MappingContext = Settings->LoadGameplayInputMappingContext();
	if (!Input || !ToggleAction || !MappingContext)
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_inventory_input_unavailable Controller=%s Action=%s Context=%s"),
			*GetNameSafe(Controller), *Settings->InventoryToggleAction.ToSoftObjectPath().ToString(),
			*Settings->GameplayInputMappingContext.ToSoftObjectPath().ToString());
		return;
	}
	if (MainMenuAction && ToggleAction == MainMenuAction)
	{
		return;
	}
	AppliedInventoryToggleAction = ToggleAction;
	InventoryInputBindingHandle = Input->BindAction(AppliedInventoryToggleAction, ETriggerEvent::Started,
		this, &ThisClass::ToggleInventory).GetHandle();
	BoundInventoryInputComponent = Input;
}

// 从当初绑定的输入组件精确移除句柄；不修改基础 IMC，重复调用保持无操作。
void UCatInventoryPageController::RemoveInventoryInput()
{
	if (UEnhancedInputComponent* Input = BoundInventoryInputComponent.Get(); Input && InventoryInputBindingHandle != 0)
	{
		Input->RemoveBindingByHandle(InventoryInputBindingHandle);
	}
	BoundInventoryInputComponent.Reset();
	InventoryInputBindingHandle = 0;
	AppliedInventoryToggleAction = nullptr;
}