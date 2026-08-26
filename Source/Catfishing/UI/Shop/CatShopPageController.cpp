#include "UI/Shop/CatShopPageController.h"

#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/PlayerController.h"
#include "Logging/CatLog.h"
#include "UI/Shop/CatShopModel.h"
#include "UI/Shop/CatShopWidget.h"

// 绑定流程：先解除旧绑定，再保存 Controller/Model/View，订阅投影、关闭和商品动作，最后渲染首帧。
bool UCatShopPageController::Bind(APlayerController* InController, UCatShopModel* InModel, UCatShopWidget* InView)
{
	Unbind();
	if (!InController || !InModel || !InView)
	{
		return false;
	}
	BoundPlayerController = InController;
	BoundModel = InModel;
	BoundView = InView;
	ModelViewChangedHandle = InModel->OnViewStateChanged.AddUObject(this, &ThisClass::HandleModelViewStateChanged);
	ViewCloseHandle = InView->OnCloseRequested.AddUObject(this, &ThisClass::HandleViewCloseRequested);
	ViewEntryActionHandle = InView->OnEntryActionRequested.AddUObject(
		this, &ThisClass::HandleViewEntryActionRequested);
	HandleModelViewStateChanged();
	return true;
}

// 解绑流程：先关闭页面恢复输入，再移除所有委托和视口实例；最后清弱引用和关闭通知。
void UCatShopPageController::Unbind()
{
	if (bShopOpen)
	{
		bShopOpen = false;
		ApplyShopInputMode(false);
	}
	if (UCatShopModel* Model = BoundModel.Get())
	{
		Model->OnViewStateChanged.Remove(ModelViewChangedHandle);
		Model->SetOpen(false);
	}
	if (UCatShopWidget* View = BoundView.Get())
	{
		View->OnCloseRequested.Remove(ViewCloseHandle);
		View->OnEntryActionRequested.Remove(ViewEntryActionHandle);
		View->RemoveFromParent();
	}
	ModelViewChangedHandle.Reset();
	ViewCloseHandle.Reset();
	ViewEntryActionHandle.Reset();
	BoundPlayerController.Reset();
	BoundModel.Reset();
	BoundView.Reset();
	bPreviousMouseCursorVisible = false;
	OnPageCloseRequested.Clear();
}

// 打开流程：把本次交互创建的商店 View 放入视口，记录鼠标状态并通知 Model 刷新打开投影。
void UCatShopPageController::OpenShop()
{
	APlayerController* Controller = BoundPlayerController.Get();
	UCatShopWidget* View = BoundView.Get();
	UCatShopModel* Model = BoundModel.Get();
	if (!Controller || !View || !Model || bShopOpen)
	{
		return;
	}
	bPreviousMouseCursorVisible = Controller->bShowMouseCursor;
	if (!View->IsInViewport())
	{
		View->AddToViewport(20);
	}
	bShopOpen = true;
	ApplyShopInputMode(true);
	Model->SetOpen(true);
}

// 关闭流程：恢复输入和视口状态，并把关闭投影交给 Model；重复关闭不反向打开。
void UCatShopPageController::CloseShop()
{
	if (!bShopOpen)
	{
		return;
	}
	bShopOpen = false;
	ApplyShopInputMode(false);
	if (UCatShopModel* Model = BoundModel.Get())
	{
		Model->SetOpen(false);
	}
	if (UCatShopWidget* View = BoundView.Get())
	{
		View->RemoveFromParent();
	}
}

// 状态读取流程：返回 PageController 的唯一打开状态；Widget 可见性不是状态源。
bool UCatShopPageController::IsShopOpen() const
{
	return bShopOpen;
}

// 渲染转交流程：Model 已聚合商品、公款和结果文本，PageController 只交给 View。
void UCatShopPageController::HandleModelViewStateChanged()
{
	UCatShopModel* Model = BoundModel.Get();
	UCatShopWidget* View = BoundView.Get();
	if (Model && View)
	{
		View->RenderShop(Model->GetViewState());
	}
}

// 关闭意图流程：先关闭当前页，再通知交互组件销毁本次商店 UI 实例。
void UCatShopPageController::HandleViewCloseRequested()
{
	CloseShop();
	OnPageCloseRequested.Broadcast();
}

// 商品动作流程：
// 1. 从 Model 当前投影确认条目仍存在、经济快照可用且没有 pending。
// 2. 要求领取/购买类型匹配条目免费标记，避免蓝图行把免费入口当付费入口发出。
// 3. 生成 RequestId，先写 pending，再调用 PlayerController 的正式服务器 RPC。
void UCatShopPageController::HandleViewEntryActionRequested(const FName EntryId, const ECatShopUIAction Action)
{
	UCatShopModel* Model = BoundModel.Get();
	ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(BoundPlayerController.Get());
	if (!Model || !CatController || EntryId.IsNone() || Action == ECatShopUIAction::None)
	{
		return;
	}
	const FCatShopViewState& State = Model->GetViewState();
	FCatShopEntryView Entry;
	if (!State.bOpen || !State.bEconomyAvailable || State.bActionPending || !Model->TryFindEntryView(EntryId, Entry))
	{
		Model->MarkActionRejected(Action, EntryId, FText::FromString(TEXT("商店：当前商品或公款数据未就绪")));
		return;
	}
	const bool bFreeAction = Action == ECatShopUIAction::ClaimFreeEntry;
	if (bFreeAction != Entry.bFreeClaim)
	{
		Model->MarkActionRejected(Action, EntryId, FText::FromString(TEXT("商店：商品动作类型不匹配")));
		return;
	}

	const FGuid RequestId = FGuid::NewGuid();
	const int64 ExpectedWalletRevision = State.Economy.WalletRevision;
	Model->MarkActionSubmitted(Action, EntryId);
	if (bFreeAction)
	{
		if (CatController->HasAuthority())
		{
			CatController->ServerClaimFreeShopEntry_Implementation(EntryId, RequestId, ExpectedWalletRevision);
		}
		else
		{
			CatController->ServerClaimFreeShopEntry(EntryId, RequestId, ExpectedWalletRevision);
		}
	}
	else
	{
		if (CatController->HasAuthority())
		{
			CatController->ServerSubmitShopPurchase_Implementation(EntryId, RequestId, ExpectedWalletRevision);
		}
		else
		{
			CatController->ServerSubmitShopPurchase(EntryId, RequestId, ExpectedWalletRevision);
		}
	}
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_shop_action_submitted EntryId=%s Action=%s WalletRevision=%lld"),
		*EntryId.ToString(),
		*UEnum::GetValueAsString(Action),
		ExpectedWalletRevision);
}

// 输入模式流程：商店打开时聚焦本次交互 View 并显示鼠标；关闭时恢复 GameOnly 和旧鼠标状态。
void UCatShopPageController::ApplyShopInputMode(const bool bOpen)
{
	APlayerController* Controller = BoundPlayerController.Get();
	if (!Controller)
	{
		return;
	}
	if (bOpen)
	{
		if (UCatShopWidget* View = BoundView.Get())
		{
			FInputModeUIOnly InputMode;
			InputMode.SetWidgetToFocus(View->TakeWidget());
			InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			Controller->SetInputMode(InputMode);
			Controller->bShowMouseCursor = true;
			View->SetKeyboardFocus();
		}
		return;
	}
	Controller->SetInputMode(FInputModeGameOnly());
	Controller->bShowMouseCursor = bPreviousMouseCursorVisible;
}
