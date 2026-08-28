#include "UI/Shop/CatShopPageController.h"

#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/PlayerController.h"
#include "Logging/CatLog.h"
#include "ShopEconomy/CatShopKioskActor.h"
#include "UI/Shop/CatShopModel.h"
#include "UI/Shop/CatShopWidget.h"

// 绑定流程：先解除旧绑定，再保存 Controller/Model/View 和来源摊位；随后订阅投影、关闭、商品动作和服务器结果，最后渲染首帧。
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
	ViewEntryActionHandle = InView->OnEntryActionRequested.AddUObject(
		this, &ThisClass::HandleViewEntryActionRequested);
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
		View->OnEntryActionRequested.Remove(ViewEntryActionHandle);
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
	ViewEntryActionHandle.Reset();
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

// 商品动作流程：
// 1. 从 Model 当前投影确认条目仍存在、经济/货架快照可用且没有 pending，同时要求本页还持有来源摊位。
// 2. 要求领取/购买类型匹配条目免费标记，并尊重 UI 已推导出的售罄/余额不足状态。
// 3. 生成 RequestId，先写 pending，再把来源摊位和 EntryId 一起交给 PlayerController 的正式服务器 RPC。
void UCatShopPageController::HandleViewEntryActionRequested(const FName EntryId, const ECatShopUIAction Action)
{
	UCatShopModel* Model = BoundModel.Get();
	ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(BoundPlayerController.Get());
	ACatShopKioskActor* SourceShop = BoundSourceShop.Get();
	if (!Model || EntryId.IsNone() || Action == ECatShopUIAction::None)
	{
		return;
	}
	if (!CatController || !SourceShop)
	{
		Model->MarkActionRejected(Action, EntryId, FText::FromString(TEXT("商店：摊位或控制器上下文已失效")));
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
	if (!Entry.bActionEnabled)
	{
		const FText Reason = Entry.bSoldOut
			? FText::FromString(TEXT("商店：这件商品已经售罄"))
			: FText::FromString(TEXT("商店：团队公款不足或商品暂不可购买"));
		Model->MarkActionRejected(Action, EntryId, Reason);
		return;
	}

	const FGuid RequestId = FGuid::NewGuid();
	const int64 ExpectedWalletRevision = State.Economy.WalletRevision;
	PendingShopRequestId = RequestId;
	PendingShopAction = Action;
	PendingShopEntryId = EntryId;
	Model->MarkActionSubmitted(Action, EntryId);
	if (bFreeAction)
	{
		if (CatController->HasAuthority())
		{
			CatController->ServerClaimFreeShopEntryAtKiosk_Implementation(
				SourceShop, EntryId, RequestId, ExpectedWalletRevision);
		}
		else
		{
			CatController->ServerClaimFreeShopEntryAtKiosk(SourceShop, EntryId, RequestId, ExpectedWalletRevision);
		}
	}
	else
	{
		if (CatController->HasAuthority())
		{
			CatController->ServerSubmitShopPurchaseAtKiosk_Implementation(
				SourceShop, EntryId, RequestId, ExpectedWalletRevision);
		}
		else
		{
			CatController->ServerSubmitShopPurchaseAtKiosk(SourceShop, EntryId, RequestId, ExpectedWalletRevision);
		}
	}
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_shop_action_submitted EntryId=%s Action=%s WalletRevision=%lld"),
		*EntryId.ToString(),
		*UEnum::GetValueAsString(Action),
		ExpectedWalletRevision);
}

// 购买结果流程：
// 1. 只接受当前页面自己提交的 RequestId，其他营地/身体/容器命令结果不会改商店提示。
// 2. 成功结果继续等待商店经济快照刷新；失败结果立即解除 pending，因为没有扣款也不会触发库存快照变化。
// 3. 依赖缺失时明确告诉玩家没有可用营地公共仓库，其余失败保持通用未扣款提示。
void UCatShopPageController::HandleCampCommandResultReceived(const FCatDomainCommandResult& Result)
{
	if (!PendingShopRequestId.IsValid() || Result.RequestId != PendingShopRequestId)
	{
		return;
	}
	if (Result.Error == ECatDomainCommandError::None || Result.Error == ECatDomainCommandError::AlreadyResolved)
	{
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
		: FText::FromString(TEXT("商店：购买失败，未扣款"));
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
