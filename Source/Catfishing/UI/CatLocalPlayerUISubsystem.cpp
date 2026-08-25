#include "UI/CatLocalPlayerUISubsystem.h"

#include "Character/CatCharacter.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/PlayerController.h"
#include "Logging/CatLog.h"
#include "Online/CatOnlineSubsystem.h"
#include "UI/CatLakeReachModel.h"
#include "UI/CatLakeReachPageController.h"
#include "UI/CatLakeReachWidget.h"
#include "UI/CatTravelWidget.h"
#include "UI/CatUISettings.h"

// 初始化流程：先订阅唯一 Online 快照，再弱绑定当前 Controller 的 Pawn notifier；LakeReach 是否装配由 AttachLakePawn 统一验证 WBP 配置。
void UCatLocalPlayerUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (UGameInstance* GameInstance = LocalPlayer->GetGameInstance())
		{
			if (UCatOnlineSubsystem* Online = GameInstance->GetSubsystem<UCatOnlineSubsystem>())
			{
				OnlineSnapshotHandle = Online->OnSnapshotChanged.AddUObject(this, &ThisClass::HandleOnlineSnapshotChanged);
			}
		}
		BindController(LocalPlayer->GetPlayerController(GetWorld()));
	}
	RefreshOnlineWidgetForCurrentController();
}

// 销毁流程：先释放 LakeReach MVC，让菜单输入和 View 意图不再触达旧 Controller；再解绑 Controller、移除 Frontend View 与 Online 快照订阅。
void UCatLocalPlayerUISubsystem::Deinitialize()
{
	DetachLakePawn();
	UnbindController();
	RemoveOnlineWidget();
	if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (UGameInstance* GameInstance = LocalPlayer->GetGameInstance())
		{
			if (UCatOnlineSubsystem* Online = GameInstance->GetSubsystem<UCatOnlineSubsystem>())
			{
				Online->OnSnapshotChanged.Remove(OnlineSnapshotHandle);
			}
		}
	}
	OnlineSnapshotHandle.Reset();
	Super::Deinitialize();
}

// Controller 替换流程：旧 Controller 仍可访问时先拆掉 LakeReach 和 Frontend UI；父类切换后再只针对 NewController 装配当前 Pawn。
void UCatLocalPlayerUISubsystem::PlayerControllerChanged(APlayerController* NewController)
{
	DetachLakePawn();
	UnbindController();
	RemoveOnlineWidget();
	Super::PlayerControllerChanged(NewController);
	BindController(NewController);
	RefreshOnlineWidgetForCurrentController();
}

// 菜单切换流程：把输入、焦点和 ViewState 更新全部交给 PageController；Subsystem 不再持有菜单布尔值或渲染细节。
void UCatLocalPlayerUISubsystem::ToggleLakeMenu()
{
	if (LakeReachPageController)
	{
		LakeReachPageController->ToggleLakeMenu();
	}
}

// 状态读取流程：从 PageController 读取唯一菜单状态；未装配 LakeReach 时固定返回 false，避免从 Widget 可见性拼第二份状态。
bool UCatLocalPlayerUISubsystem::IsLakeMenuOpen() const
{
	return LakeReachPageController ? LakeReachPageController->IsLakeMenuOpen() : false;
}

// 快照消费流程：Online 变更时先调和 Frontend TravelWidget，再要求 LakeReach Model 重读离局 gate；不把 Online 事件参数拼进 View。
void UCatLocalPlayerUISubsystem::HandleOnlineSnapshotChanged()
{
	RefreshOnlineWidgetForCurrentController();
	if (LakeReachPageController)
	{
		LakeReachPageController->RefreshModel();
	}
}

// 动作转交流程：Frontend TravelWidget 的每个意图只调用 Online 的一个公开入口；LakeReach 离局意图由 PageController 直接走同一 Online 管线。
void UCatLocalPlayerUISubsystem::HandleActionRequested(const ECatOnlineUIAction Action, const FGuid OpaqueHandle)
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	UGameInstance* GameInstance = LocalPlayer ? LocalPlayer->GetGameInstance() : nullptr;
	UCatOnlineSubsystem* Online = GameInstance ? GameInstance->GetSubsystem<UCatOnlineSubsystem>() : nullptr;
	if (!Online)
	{
		return;
	}

	FCatOnlineResult Result;
	switch (Action)
	{
	case ECatOnlineUIAction::Host:
		Result = Online->RequestCreateSession();
		break;
	case ECatOnlineUIAction::Find:
		Result = Online->RequestFindSessions();
		break;
	case ECatOnlineUIAction::Join:
	{
		FCatSessionSearchHandle Handle;
		Handle.Value = OpaqueHandle;
		Result = Online->RequestJoinSession(Handle);
		break;
	}
	case ECatOnlineUIAction::AcceptInvite:
	{
		FCatSessionInviteHandle Handle;
		Handle.Value = OpaqueHandle;
		Result = Online->RequestAcceptInvite(Handle);
		break;
	}
	case ECatOnlineUIAction::Leave:
		Result = Online->RequestLeave();
		break;
	default:
		return;
	}

	const UWorld* World = GetWorld();
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_online_action Action=%s RequestId=%s World=%s NetMode=%d Result=%s Error=%s"),
		*UEnum::GetValueAsString(Action),
		*Result.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		World ? *World->GetName() : TEXT("None"),
		World ? static_cast<int32>(World->GetNetMode()) : -1,
		Result.bAccepted ? TEXT("accepted") : TEXT("rejected"),
		*UEnum::GetValueAsString(Result.Error));
	if (OnlineWidget)
	{
		OnlineWidget->Configure(Online->GetSnapshot());
	}
}

// Frontend View 调和流程：读取当前完整 Online 快照；只有 Frontend 或前往 Lake 的等待态保留 TravelWidget，Lake 内正式入口交给 UIReach MVC。
void UCatLocalPlayerUISubsystem::RefreshOnlineWidgetForCurrentController()
{
	ULocalPlayer* LocalPlayer = GetLocalPlayer();
	APlayerController* Controller = LocalPlayer ? LocalPlayer->GetPlayerController(GetWorld()) : nullptr;
	const UGameInstance* GameInstance = LocalPlayer ? LocalPlayer->GetGameInstance() : nullptr;
	const UCatOnlineSubsystem* Online = GameInstance ? GameInstance->GetSubsystem<UCatOnlineSubsystem>() : nullptr;
	if (!Controller || !Online)
	{
		RemoveOnlineWidget();
		return;
	}

	const FCatOnlineSnapshot Snapshot = Online->GetSnapshot();
	if (!ShouldShowOnlineTravelWidget(Snapshot))
	{
		RemoveOnlineWidget();
		return;
	}

	if (!OnlineWidget)
	{
		OnlineWidget = CreateWidget<UCatTravelWidget>(Controller, UCatTravelWidget::StaticClass());
		if (!OnlineWidget)
		{
			return;
		}
		ActionHandle = OnlineWidget->OnActionRequested.AddUObject(this, &ThisClass::HandleActionRequested);
		OnlineWidget->AddToViewport();
		UE_LOG(LogCatUI, Log, TEXT("Event=ui_online_widget_created World=%s"), GetWorld() ? *GetWorld()->GetName() : TEXT("None"));
	}
	OnlineWidget->Configure(Snapshot);
}

// Frontend 面板判断流程：只承认前台和从前台出发去 Lake 的旅行等待；到达 Lake 后玩家入口由正式 LakeReach WBP 接管。
bool UCatLocalPlayerUISubsystem::ShouldShowOnlineTravelWidget(const FCatOnlineSnapshot& Snapshot)
{
	return Snapshot.WorldState == ECatOnlineWorldState::Frontend
		|| Snapshot.WorldState == ECatOnlineWorldState::TravelingToLake;
}

// Frontend View 移除流程：先解绑动作广播再移出视口；空实例保持幂等，避免 Controller 切换时重复 removed 日志干扰验收。
void UCatLocalPlayerUISubsystem::RemoveOnlineWidget()
{
	if (!OnlineWidget)
	{
		return;
	}
	OnlineWidget->OnActionRequested.Remove(ActionHandle);
	ActionHandle.Reset();
	OnlineWidget->RemoveFromParent();
	OnlineWidget = nullptr;
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_online_widget_removed World=%s"), GetWorld() ? *GetWorld()->GetName() : TEXT("None"));
}

// Controller 绑定流程：保存弱引用并订阅当前 Controller 的 Pawn notifier；随后消费当前 Pawn，覆盖绑定前已经完成占有的冷启动情况。
void UCatLocalPlayerUISubsystem::BindController(APlayerController* Controller)
{
	if (!Controller)
	{
		return;
	}
	BoundPlayerController = Controller;
	PawnChangedHandle = Controller->GetOnNewPawnNotifier().AddUObject(this, &ThisClass::HandleControllerPawnChanged);
	AttachLakePawn(Cast<ACatCharacter>(Controller->GetPawn()));
}

// Controller 解绑流程：旧对象仍存活时从同一个 notifier 精确移除；弱引用失效时只清本地句柄，不延长 Controller 生命周期。
void UCatLocalPlayerUISubsystem::UnbindController()
{
	if (APlayerController* Controller = BoundPlayerController.Get(); Controller && PawnChangedHandle.IsValid())
	{
		Controller->GetOnNewPawnNotifier().Remove(PawnChangedHandle);
	}
	PawnChangedHandle.Reset();
	BoundPlayerController.Reset();
}

// Pawn 变化流程：无论 NewPawn 类型如何都先完整拆掉上一套 MVC；只有新的 ACatCharacter 通过配置校验时才重新装配。
void UCatLocalPlayerUISubsystem::HandleControllerPawnChanged(APawn* NewPawn)
{
	DetachLakePawn();
	AttachLakePawn(Cast<ACatCharacter>(NewPawn));
}

// LakeReach 装配流程：
// 1. 验证本地设置、当前 Controller/Pawn 和 World；WBP 类缺失或无效时直接 fail-closed，不创建原生白盒替身。
// 2. 创建 Model、PageController 和配置的 WBP View；Model 先绑定只读 Query/Fishing 源并缓存首份 ViewState。
// 3. 根 View 入视口后由 PageController 订阅 Model/View、安装可降级输入，并主动渲染当前状态；日志暴露 RootClass 供自动化确认真正加载的是 WBP。
void UCatLocalPlayerUISubsystem::AttachLakePawn(ACatCharacter* Character)
{
	const UCatUISettings* Settings = GetDefault<UCatUISettings>();
	if (!Settings || !Settings->IsLakeReachViewEnabled() || !Character || Character->GetWorld() != GetWorld())
	{
		return;
	}
	APlayerController* Controller = BoundPlayerController.Get();
	if (!Controller || Controller->GetPawn() != Character)
	{
		return;
	}

	const TSubclassOf<UCatLakeReachWidget> ViewClass = Settings->LoadLakeReachWidgetClass();
	if (!ViewClass)
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=ui_reach_view_class_missing Class=%s"),
			*Settings->LakeReachWidgetClass.ToSoftObjectPath().ToString());
		return;
	}

	LakeReachModel = NewObject<UCatLakeReachModel>(this);
	LakeReachPageController = NewObject<UCatLakeReachPageController>(this);
	LakeReachWidget = CreateWidget<UCatLakeReachWidget>(Controller, ViewClass);
	if (!LakeReachModel || !LakeReachPageController || !LakeReachWidget)
	{
		DetachLakePawn();
		return;
	}
	if (!LakeReachModel->Bind(GetLocalPlayer(), Controller, Character))
	{
		DetachLakePawn();
		return;
	}
	LakeReachWidget->AddToViewport(1);
	if (!LakeReachPageController->Bind(GetLocalPlayer(), Controller, LakeReachModel, LakeReachWidget))
	{
		DetachLakePawn();
		return;
	}
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_reach_attached World=%s Controller=%s RootCount=1 RootClass=%s"),
		GetWorld() ? *GetWorld()->GetName() : TEXT("None"),
		*GetNameSafe(Controller),
		*GetNameSafe(LakeReachWidget->GetClass()));
}

// LakeReach 解绑流程：PageController 先恢复输入和解绑 View 意图，Model 再解除玩法 Query/Fishing 订阅，最后移除 WBP 根并清引用。
void UCatLocalPlayerUISubsystem::DetachLakePawn()
{
	if (LakeReachPageController)
	{
		LakeReachPageController->Unbind();
		LakeReachPageController = nullptr;
	}
	if (LakeReachModel)
	{
		LakeReachModel->Unbind();
		LakeReachModel = nullptr;
	}
	if (LakeReachWidget)
	{
		LakeReachWidget->RemoveFromParent();
		LakeReachWidget = nullptr;
		UE_LOG(LogCatUI, Log, TEXT("Event=ui_reach_detached World=%s RootCount=0"),
			GetWorld() ? *GetWorld()->GetName() : TEXT("None"));
	}
}
