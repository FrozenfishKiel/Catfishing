#include "UI/CatTravelWidget.h"

#include "Logging/CatLog.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Engine/World.h"

// 初始化流程：创建纵向根容器和状态文本，再为五个最小意图各创建原生 Button/TextBlock；WidgetTree 持有全部 UObject 子项。
void UCatTravelWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>();
	WidgetTree->RootWidget = Root;
	StatusText = WidgetTree->ConstructWidget<UTextBlock>();
	Root->AddChildToVerticalBox(StatusText);

	HostButton = WidgetTree->ConstructWidget<UButton>();
	UTextBlock* HostText = WidgetTree->ConstructWidget<UTextBlock>();
	HostText->SetText(FText::FromString(TEXT("Host Session")));
	HostButton->AddChild(HostText);
	Root->AddChildToVerticalBox(HostButton);

	FindButton = WidgetTree->ConstructWidget<UButton>();
	UTextBlock* FindText = WidgetTree->ConstructWidget<UTextBlock>();
	FindText->SetText(FText::FromString(TEXT("Find Sessions")));
	FindButton->AddChild(FindText);
	Root->AddChildToVerticalBox(FindButton);

	JoinButton = WidgetTree->ConstructWidget<UButton>();
	UTextBlock* JoinText = WidgetTree->ConstructWidget<UTextBlock>();
	JoinText->SetText(FText::FromString(TEXT("Join First Result")));
	JoinButton->AddChild(JoinText);
	Root->AddChildToVerticalBox(JoinButton);

	InviteButton = WidgetTree->ConstructWidget<UButton>();
	UTextBlock* InviteText = WidgetTree->ConstructWidget<UTextBlock>();
	InviteText->SetText(FText::FromString(TEXT("Join Accepted Invite")));
	InviteButton->AddChild(InviteText);
	Root->AddChildToVerticalBox(InviteButton);

	LeaveButton = WidgetTree->ConstructWidget<UButton>();
	UTextBlock* LeaveText = WidgetTree->ConstructWidget<UTextBlock>();
	LeaveText->SetText(FText::FromString(TEXT("Leave Session")));
	LeaveButton->AddChild(LeaveText);
	Root->AddChildToVerticalBox(LeaveButton);
}

// 构造流程：先恢复父类 Slate 生命周期，再对每个按钮执行 Remove/Add 配对，使多次 Construct 后仍各只有一个 UObject 回调。
void UCatTravelWidget::NativeConstruct()
{
	Super::NativeConstruct();
	HostButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleHostClicked);
	HostButton->OnClicked.AddDynamic(this, &ThisClass::HandleHostClicked);
	FindButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleFindClicked);
	FindButton->OnClicked.AddDynamic(this, &ThisClass::HandleFindClicked);
	JoinButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleJoinClicked);
	JoinButton->OnClicked.AddDynamic(this, &ThisClass::HandleJoinClicked);
	InviteButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleInviteClicked);
	InviteButton->OnClicked.AddDynamic(this, &ThisClass::HandleInviteClicked);
	LeaveButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleLeaveClicked);
	LeaveButton->OnClicked.AddDynamic(this, &ThisClass::HandleLeaveClicked);
}

// 销毁流程：解除五个按钮对本对象的动态绑定并清空临时 opaque 句柄，再让父类销毁 Slate 资源；业务广播不持有 World。
void UCatTravelWidget::NativeDestruct()
{
	HostButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleHostClicked);
	FindButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleFindClicked);
	JoinButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleJoinClicked);
	InviteButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleInviteClicked);
	LeaveButton->OnClicked.RemoveDynamic(this, &ThisClass::HandleLeaveClicked);
	FirstSearchHandle.Invalidate();
	FirstInviteHandle.Invalidate();
	Super::NativeDestruct();
}

// 配置流程：先复制搜索/邀请首项 opaque 句柄，再分别依据 World、SessionRole 和 ActiveOperation 控制按钮；状态文本完整显示四类事实而不合并其语义。
void UCatTravelWidget::Configure(const FCatOnlineSnapshot& Snapshot)
{
	FirstSearchHandle = Snapshot.SearchResults.IsEmpty() ? FGuid() : Snapshot.SearchResults[0].Handle.Value;
	FirstInviteHandle = Snapshot.AcceptedInvites.IsEmpty() ? FGuid() : Snapshot.AcceptedInvites[0].Handle.Value;
	const bool bIdle = Snapshot.ActiveOperation == ECatOnlineOperation::None;
	const bool bFrontendWithoutSession = Snapshot.WorldState == ECatOnlineWorldState::Frontend
		&& Snapshot.SessionRole == ECatOnlineSessionRole::None;
	HostButton->SetIsEnabled(bIdle && bFrontendWithoutSession);
	FindButton->SetIsEnabled(bIdle && bFrontendWithoutSession);
	JoinButton->SetIsEnabled(bIdle && bFrontendWithoutSession && FirstSearchHandle.IsValid());
	InviteButton->SetIsEnabled(bIdle && bFrontendWithoutSession && FirstInviteHandle.IsValid());
	LeaveButton->SetIsEnabled(bIdle && Snapshot.WorldState == ECatOnlineWorldState::Lake
		&& Snapshot.SessionRole != ECatOnlineSessionRole::None);

	const FString Status = FString::Printf(TEXT("World=%s\nSession=%s Role=%s\nTransport=%s Operation=%s\nResults=%d Invites=%d\nError=%s"),
		*UEnum::GetValueAsString(Snapshot.WorldState),
		*UEnum::GetValueAsString(Snapshot.SessionState),
		*UEnum::GetValueAsString(Snapshot.SessionRole),
		*UEnum::GetValueAsString(Snapshot.TransportState),
		*UEnum::GetValueAsString(Snapshot.ActiveOperation),
		Snapshot.SearchResults.Num(),
		Snapshot.AcceptedInvites.Num(),
		*UEnum::GetValueAsString(Snapshot.LastError));
	StatusText->SetText(FText::FromString(Status));
}

// Host 点击流程：广播无句柄创建意图；订阅者决定策略 gate、平台请求与旅行，View 不读取 World 或 Session 接口。
void UCatTravelWidget::HandleHostClicked()
{
	OnActionRequested.Broadcast(ECatOnlineUIAction::Host, FGuid());
}

// Find 点击流程：广播无句柄搜索意图；当前搜索结果只有 Online 子系统可以清空或替换。
void UCatTravelWidget::HandleFindClicked()
{
	OnActionRequested.Broadcast(ECatOnlineUIAction::Find, FGuid());
}

// Join 点击流程：只在保存的搜索句柄含有效 GUID 时广播；并发操作 gate 与私有映射是否仍含该句柄由 Online 子系统复核，View 不把 GUID 有效性当作平台结果仍新鲜。
void UCatTravelWidget::HandleJoinClicked()
{
	if (FirstSearchHandle.IsValid())
	{
		OnActionRequested.Broadcast(ECatOnlineUIAction::Join, FirstSearchHandle);
	}
}

// Invite 点击流程：只把平台已接受邀请的 opaque 句柄交还协调层，不解析邀请者、SessionId 或连接地址。
void UCatTravelWidget::HandleInviteClicked()
{
	if (FirstInviteHandle.IsValid())
	{
		OnActionRequested.Broadcast(ECatOnlineUIAction::AcceptInvite, FirstInviteHandle);
	}
}

// Leave 点击流程：广播统一离局意图；View 不根据 NetMode 猜 Host/Client，也不直接 DestroySession 或旅行。
void UCatTravelWidget::HandleLeaveClicked()
{
	OnActionRequested.Broadcast(ECatOnlineUIAction::Leave, FGuid());
}
