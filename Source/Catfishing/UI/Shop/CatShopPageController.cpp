#include "UI/Shop/CatShopPageController.h"

#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/PlayerController.h"
#include "Logging/CatLog.h"
#include "ShopEconomy/CatShopKioskActor.h"
#include "UI/Shop/CatShopModel.h"
#include "UI/Shop/CatShopWidget.h"

// 绑定流程：先解除旧绑定，再保存 Controller/Model/View 和来源摊位；随后订阅投影、关闭、购物车意图和服务器结果，最后渲染首帧。
bool UCatShopPageController::Bind(APlayerController* InController, UCatShopModel* InModel, UCatShopWidget* InView,
	ACatShopKioskActor* InSourceShop)
{
	Unbind();
	if (!InController || !InModel || !InView || !InSourceShop)
	{
		return false;
	}
	BoundPlayerController = InController;
	BoundModel = InModel;
	BoundView = InView;
	BoundSourceShop = InSourceShop;
	ModelViewChangedHandle = InModel->OnViewStateChanged.AddUObject(this, &ThisClass::HandleModelViewStateChanged);
	ViewCloseHandle = InView->OnCloseRequested.AddUObject(this, &ThisClass::HandleViewCloseRequested);
	ViewAddEntryHandle = InView->OnEntryAddToCartRequested.AddUObject(
		this, &ThisClass::HandleViewAddEntryToCartRequested);
	ViewRemoveCartLineHandle = InView->OnCartLineRemoveRequested.AddUObject(
		this, &ThisClass::HandleViewRemoveCartLineRequested);
	ViewPayCartHandle = InView->OnCartPayRequested.AddUObject(this, &ThisClass::HandleViewPayCartRequested);
	if (ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(InController))
	{
		CampCommandResultHandle = CatController->OnCampCommandResultReceived.AddUObject(
			this, &ThisClass::HandleCampCommandResultReceived);
	}
	HandleModelViewStateChanged();
	return true;
}

// 解绑流程：先恢复本页接管的输入并把 Model 标记为关闭，再移除 View/Model/服务器结果委托；视口移除只处理仍挂着的页面，避免组件销毁链路重复拆同一个 WBP。
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
		View->OnEntryAddToCartRequested.Remove(ViewAddEntryHandle);
		View->OnCartLineRemoveRequested.Remove(ViewRemoveCartLineHandle);
		View->OnCartPayRequested.Remove(ViewPayCartHandle);
		if (View->IsInViewport())
		{
			View->RemoveFromParent();
		}
	}
	if (ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(BoundPlayerController.Get()))
	{
		CatController->OnCampCommandResultReceived.Remove(CampCommandResultHandle);
	}
	ModelViewChangedHandle.Reset();
	ViewCloseHandle.Reset();
	ViewAddEntryHandle.Reset();
	ViewRemoveCartLineHandle.Reset();
	ViewPayCartHandle.Reset();
	CampCommandResultHandle.Reset();
	BoundPlayerController.Reset();
	BoundModel.Reset();
	BoundView.Reset();
	BoundSourceShop.Reset();
	PendingShopRequestId = FGuid();
	PendingShopAction = ECatShopUIAction::None;
	PendingShopEntryId = NAME_None;
	ModalInputModeState = FCatUIModalInputModeState();
	OnPageCloseRequested.Clear();
}

// 打开流程：把本次交互创建的商店 View 放入视口，应用模态输入锁并通知 Model 刷新打开投影；动态按钮重建后再把焦点拉回根页，关闭键优先进入商店页面。
void UCatShopPageController::OpenShop()
{
	APlayerController* Controller = BoundPlayerController.Get();
	UCatShopWidget* View = BoundView.Get();
	UCatShopModel* Model = BoundModel.Get();
	if (!Controller || !View || !Model || bShopOpen)
	{
		return;
	}
	if (!View->IsInViewport())
	{
		View->AddToViewport(20);
	}
	bShopOpen = true;
	ApplyShopInputMode(true);
	Model->SetOpen(true);
	View->SetKeyboardFocus();
}

// 关闭流程：恢复本页接管的输入并通知 Model 关闭；只有页面仍在视口中才移除它，避免关闭按钮和组件清理重复拆同一实例。
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
		if (View->IsInViewport())
		{
			View->RemoveFromParent();
		}
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

// 加购意图流程：把商品点击交给 Model 的本地购物车；支付 pending 期间的迟到点击直接忽略，避免清掉等待中的订单状态。
void UCatShopPageController::HandleViewAddEntryToCartRequested(const FName EntryId)
{
	UCatShopModel* Model = BoundModel.Get();
	if (!Model)
	{
		return;
	}
	if (Model->GetViewState().bActionPending)
	{
		return;
	}
	FText FailureReason;
	if (!Model->AddEntryToCart(EntryId, FailureReason))
	{
		Model->MarkActionRejected(ECatShopUIAction::AddEntryToCart, EntryId, FailureReason);
		return;
	}
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_shop_entry_added_to_cart EntryId=%s"),
		*EntryId.ToString());
}

// 删除意图流程：把购物车垃圾桶点击交给 Model；支付 pending 期间的迟到点击直接忽略，避免已提交购物车和本地队列脱节。
void UCatShopPageController::HandleViewRemoveCartLineRequested(const FName EntryId)
{
	UCatShopModel* Model = BoundModel.Get();
	if (!Model)
	{
		return;
	}
	if (Model->GetViewState().bActionPending)
	{
		return;
	}
	FText FailureReason;
	if (!Model->RemoveOneCartItem(EntryId, FailureReason))
	{
		Model->MarkActionRejected(ECatShopUIAction::RemoveCartEntry, EntryId, FailureReason);
		return;
	}
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_shop_entry_removed_from_cart EntryId=%s"),
		*EntryId.ToString());
}

// 支付意图流程：
// 1. 从 Model 当前投影确认页面打开、经济数据同步、购物车可支付且来源摊位还有效。
// 2. 只把 EntryId 和 CartCount 导出到服务器 RPC；价格、库存、交付数量和公共仓库容量都由服务器重读。
// 3. 生成 RequestId 后先写 pending，再提交整车 RPC；服务器成功时回包会清空购物车，失败时保留购物车。
void UCatShopPageController::HandleViewPayCartRequested()
{
	UCatShopModel* Model = BoundModel.Get();
	ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(BoundPlayerController.Get());
	ACatShopKioskActor* SourceShop = BoundSourceShop.Get();
	if (!Model)
	{
		return;
	}
	const FCatShopViewState& State = Model->GetViewState();
	if (!CatController || !SourceShop)
	{
		Model->MarkActionRejected(ECatShopUIAction::PayCart, NAME_None,
			FText::FromString(TEXT("商店：摊位或控制器上下文已失效")));
		return;
	}
	if (!State.bCanPayCart)
	{
		const FText Reason = State.PayDisabledReasonText.IsEmpty()
			? FText::FromString(TEXT("商店：购物车暂不可支付")) : State.PayDisabledReasonText;
		Model->MarkActionRejected(ECatShopUIAction::PayCart, NAME_None, Reason);
		return;
	}
	TArray<FCatShopCartLineCommand> Lines;
	if (!Model->BuildCartCommandLines(Lines))
	{
		Model->MarkActionRejected(ECatShopUIAction::PayCart, NAME_None,
			FText::FromString(TEXT("请先选购商品")));
		return;
	}

	const FGuid RequestId = FGuid::NewGuid();
	const int64 ExpectedWalletRevision = State.Economy.WalletRevision;
	PendingShopRequestId = RequestId;
	PendingShopAction = ECatShopUIAction::PayCart;
	PendingShopEntryId = NAME_None;
	Model->MarkActionSubmitted(ECatShopUIAction::PayCart, NAME_None);
	if (CatController->HasAuthority())
	{
		CatController->ServerSubmitShopCartAtKiosk_Implementation(
			SourceShop, Lines, RequestId, ExpectedWalletRevision);
	}
	else
	{
		CatController->ServerSubmitShopCartAtKiosk(SourceShop, Lines, RequestId, ExpectedWalletRevision);
	}
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_shop_cart_payment_submitted LineCount=%d WalletRevision=%lld"),
		Lines.Num(), ExpectedWalletRevision);
}

// 购物车结果流程：
// 1. 只接受当前页面自己提交的 RequestId，其他营地/身体/容器命令结果不会改商店提示。
// 2. 成功或合法重放会清空本地购物车；失败结果立即解除 pending 并保留购物车内容。
// 3. 依赖缺失时明确告诉玩家没有可用营地公共仓库，其余失败提示玩家重试或重新打开页面核对同步事实。
void UCatShopPageController::HandleCampCommandResultReceived(const FCatDomainCommandResult& Result)
{
	if (!PendingShopRequestId.IsValid() || Result.RequestId != PendingShopRequestId)
	{
		return;
	}
	if (Result.Error == ECatDomainCommandError::None || Result.Error == ECatDomainCommandError::AlreadyResolved)
	{
		if (UCatShopModel* Model = BoundModel.Get())
		{
			Model->MarkCartPaymentSucceeded();
		}
		PendingShopRequestId = FGuid();
		PendingShopAction = ECatShopUIAction::None;
		PendingShopEntryId = NAME_None;
		return;
	}
	UCatShopModel* Model = BoundModel.Get();
	if (!Model)
	{
		PendingShopRequestId = FGuid();
		PendingShopAction = ECatShopUIAction::None;
		PendingShopEntryId = NAME_None;
		return;
	}
	const FText Reason = Result.Error == ECatDomainCommandError::DependencyUnavailable
		? FText::FromString(TEXT("商店：没有可用营地公共仓库，未扣款"))
		: FText::FromString(TEXT("商店：支付没有完成，请重试或重新打开商店查看公款和仓库"));
	Model->MarkActionRejected(PendingShopAction, PendingShopEntryId, Reason);
	PendingShopRequestId = FGuid();
	PendingShopAction = ECatShopUIAction::None;
	PendingShopEntryId = NAME_None;
}

// 输入模式流程：打开时聚焦本次交互 View、锁住移动/视角并停止当前移动；关闭时释放本商店页申请的输入锁。
void UCatShopPageController::ApplyShopInputMode(const bool bOpen)
{
	APlayerController* Controller = BoundPlayerController.Get();
	if (bOpen)
	{
		CatUIModalInputMode::Open(Controller, BoundView.Get(), ModalInputModeState);
		return;
	}
	CatUIModalInputMode::Close(Controller, ModalInputModeState);
}
