#include "UI/Frontend/CatFrontendRoomModel.h"

#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Online/CatOnlineSubsystem.h"

namespace CatFrontendRoomModelText
{
	/** 按 Online 的稳定错误选择本地可读结果；邀请拒绝明确给出重新接受或先离房的恢复动作，不转发平台错误、连接串或账号。 */
	static FText MakeErrorText(const ECatOnlineError Error)
	{
		switch (Error)
		{
		case ECatOnlineError::None:
			return FText::GetEmpty();
		case ECatOnlineError::CommandAlreadyPending:
			return FText::FromString(TEXT("请求仍在处理中。"));
		case ECatOnlineError::InvalidState:
			return FText::FromString(TEXT("当前状态不能执行此操作。"));
		case ECatOnlineError::PolicyUndecided:
			return FText::FromString(TEXT("房间权限尚未配置。"));
		case ECatOnlineError::OnlineSubsystemUnavailable:
		case ECatOnlineError::SessionInterfaceUnavailable:
			return FText::FromString(TEXT("Steam 联机服务暂不可用。"));
		case ECatOnlineError::RequestRejected:
			return FText::FromString(TEXT("平台未接受本次请求。"));
		case ECatOnlineError::CreateFailed:
			return FText::FromString(TEXT("创建房间失败。"));
		case ECatOnlineError::JoinFailed:
			return FText::FromString(TEXT("加入房间失败。"));
		case ECatOnlineError::SessionCompatibilityMismatch:
			return FText::FromString(TEXT("该房间与当前游戏版本或地图配置不兼容。"));
		case ECatOnlineError::InviteAcceptanceBusy:
			return FText::FromString(TEXT("当前请求尚未完成，请稍后在 Steam 重新接受邀请。"));
		case ECatOnlineError::InviteSessionConflict:
			return FText::FromString(TEXT("已在房间中，请先离开当前房间，再在 Steam 重新接受邀请。"));
		case ECatOnlineError::InviteAcceptanceUnavailable:
			return FText::FromString(TEXT("无法使用此邀请，请确认 Steam 账号并返回主界面后重新接受邀请。"));
		case ECatOnlineError::InviteAcceptanceExpired:
			return FText::FromString(TEXT("等待 Steam 登录或主界面就绪超时，请就绪后重新接受邀请。"));
		case ECatOnlineError::HostSaveFailed:
			return FText::FromString(TEXT("世界存档未能完成，已保留当前房间，请重试。"));
		case ECatOnlineError::ActiveRunReleaseFailed:
			return FText::FromString(TEXT("已离开房间，但本局存档状态未能释放，暂时无法切换存档。"));
		case ECatOnlineError::ConnectStringUnavailable:
			return FText::FromString(TEXT("无法取得房主连接地址。"));
		case ECatOnlineError::NetworkFailure:
			return FText::FromString(TEXT("网络连接失败。"));
		case ECatOnlineError::TravelRejected:
		case ECatOnlineError::TravelFailed:
			return FText::FromString(TEXT("进入游戏失败。"));
		default:
			return FText::FromString(TEXT("房间操作未完成。"));
		}
	}
}

// 初始化流程：先成对拆除旧来源，再从 LocalPlayer 的 GameInstance 获取唯一 Online 子系统并订阅；绑定后立即读取已有快照，接住 Model 创建前到达的冷启动邀请或失败，来源缺失则发布不可用文本。
void UCatFrontendRoomModel::Initialize(ULocalPlayer* InLocalPlayer)
{
	Shutdown();
	LocalPlayer = InLocalPlayer;
	UCatOnlineSubsystem* OnlineSubsystem = GetOnline();
	if (OnlineSubsystem)
	{
		Online = OnlineSubsystem;
		OnlineChangedHandle = OnlineSubsystem->OnSnapshotChanged.AddUObject(this, &ThisClass::HandleOnlineChanged);
		HandleOnlineChanged();
		return;
	}
	else
	{
		LastResultText = CatFrontendRoomModelText::MakeErrorText(ECatOnlineError::OnlineSubsystemUnavailable);
	}
	OnChanged.Broadcast();
}

// 关闭流程：先从仍有效的精确 Online 实例解绑委托，再清理所有弱引用和展示结果；重复调用保持幂等，避免 LocalPlayer/World 销毁顺序影响后续初始化。
void UCatFrontendRoomModel::Shutdown()
{
	if (UCatOnlineSubsystem* OnlineSubsystem = Online.Get(); OnlineSubsystem && OnlineChangedHandle.IsValid())
	{
		OnlineSubsystem->OnSnapshotChanged.Remove(OnlineChangedHandle);
	}
	OnlineChangedHandle.Reset();
	Online.Reset();
	LocalPlayer.Reset();
	LastResultText = FText::GetEmpty();
}

// 创建房间流程：先取得已初始化的 Online 来源；来源缺失时生成与 Online 一致的同步拒绝，否则只转交产品语义 Create 请求并记录可展示结果，异步终态交由快照通知更新。
FCatOnlineResult UCatFrontendRoomModel::CreateRoom()
{
	UCatOnlineSubsystem* OnlineSubsystem = Online.Get();
	FCatOnlineResult Result;
	if (!OnlineSubsystem)
	{
		Result.RequestId = FGuid::NewGuid();
		Result.Error = ECatOnlineError::OnlineSubsystemUnavailable;
		CaptureResult(Result);
		return Result;
	}
	Result = OnlineSubsystem->RequestCreateSession();
	CaptureResult(Result);
	return Result;
}

// 离开房间流程：先确认 Online 来源仍绑定；有效时直接转交 Leave，前台新局不会触发保存，玩法内 Host 的保存收口完全保留在 Online/GameMode 链路。
FCatOnlineResult UCatFrontendRoomModel::LeaveRoom()
{
	UCatOnlineSubsystem* OnlineSubsystem = Online.Get();
	FCatOnlineResult Result;
	if (!OnlineSubsystem)
	{
		Result.RequestId = FGuid::NewGuid();
		Result.Error = ECatOnlineError::OnlineSubsystemUnavailable;
		CaptureResult(Result);
		return Result;
	}
	Result = OnlineSubsystem->RequestLeave();
	CaptureResult(Result);
	return Result;
}

// 开始游戏流程：先由 Online 核验 Host、房间和已加载存档前置条件；受理后不在前端 Model 自行旅行，进入过程只交给 LocalPlayer 全局遮罩读取 Online 真实阶段。
FCatOnlineResult UCatFrontendRoomModel::StartGame()
{
	UCatOnlineSubsystem* OnlineSubsystem = Online.Get();
	FCatOnlineResult Result;
	if (!OnlineSubsystem)
	{
		Result.RequestId = FGuid::NewGuid();
		Result.Error = ECatOnlineError::OnlineSubsystemUnavailable;
		CaptureResult(Result);
		return Result;
	}
	Result = OnlineSubsystem->RequestStartHostedGame();
	CaptureResult(Result);
	return Result;
}

// 刷新好友流程：来源有效时转交 OSS Friends 刷新；结果数组只在 Online 回调确认后更新，Model 不保留第二份好友缓存。
FCatOnlineResult UCatFrontendRoomModel::RefreshFriends()
{
	UCatOnlineSubsystem* OnlineSubsystem = Online.Get();
	FCatOnlineResult Result;
	if (!OnlineSubsystem)
	{
		Result.RequestId = FGuid::NewGuid();
		Result.Error = ECatOnlineError::OnlineSubsystemUnavailable;
		CaptureResult(Result);
		return Result;
	}
	Result = OnlineSubsystem->RequestRefreshFriends();
	CaptureResult(Result);
	return Result;
}

// 邀请好友流程：先保留 opaque 句柄的所有权边界，再把请求交给 Online；无效句柄、非 Host 和平台拒绝都由 Online 返回结构化结果，不在 UI 层推断原因。
FCatOnlineResult UCatFrontendRoomModel::InviteFriend(const FCatOnlineFriendHandle FriendHandle)
{
	UCatOnlineSubsystem* OnlineSubsystem = Online.Get();
	FCatOnlineResult Result;
	if (!OnlineSubsystem)
	{
		Result.RequestId = FGuid::NewGuid();
		Result.Error = ECatOnlineError::OnlineSubsystemUnavailable;
		CaptureResult(Result);
		return Result;
	}
	Result = OnlineSubsystem->RequestInviteFriend(FriendHandle);
	CaptureResult(Result);
	return Result;
}

// 快照读取流程：只从已绑定 Online 复制最新事实；Model 从不缓存房间、好友或邀请码，所以来源失效时返回默认空值而非上一局残影。
FCatOnlineSnapshot UCatFrontendRoomModel::GetSnapshot() const
{
	if (const UCatOnlineSubsystem* OnlineSubsystem = Online.Get())
	{
		return OnlineSubsystem->GetSnapshot();
	}
	return FCatOnlineSnapshot();
}

// 结果文本读取流程：仅返回最后一次同步提交或异步快照错误的展示文本；读取不改变 Online 状态或清除错误。
FText UCatFrontendRoomModel::GetLastResultText() const
{
	return LastResultText;
}

// 开始权限查询流程：读取同一份 Online 快照，要求本地已确认 Host、没有任何活动操作且预载未开始；最终的 Save 已加载校验仍在 RequestStartHostedGame 内完成。
bool UCatFrontendRoomModel::CanStartGame() const
{
	const FCatOnlineSnapshot Snapshot = GetSnapshot();
	return Snapshot.bIsHost
		&& Snapshot.WorldState == ECatOnlineWorldState::Frontend
		&& Snapshot.SessionState == ECatOnlineSessionState::Host
		&& Snapshot.ActiveOperation == ECatOnlineOperation::None
		&& !Snapshot.bIsGameplayLoadPending;
}

// Online 通知流程：先读取唯一快照，错误优先，其次为已接受邀请的有界等待和真实 Join 提交生成文本，其他状态清除旧文本；最后广播，Controller 再读取房间事实决定显示，不要求玩家再次确认邀请。
void UCatFrontendRoomModel::HandleOnlineChanged()
{
	const FCatOnlineSnapshot Snapshot = GetSnapshot();
	LastResultText = CatFrontendRoomModelText::MakeErrorText(Snapshot.LastError);
	if (Snapshot.LastError == ECatOnlineError::None)
	{
		if (Snapshot.bIsAcceptedInvitePending)
		{
			LastResultText = FText::FromString(TEXT("已接受邀请，正在等待 Steam 登录和主界面就绪。"));
		}
		else if (Snapshot.ActiveOperation == ECatOnlineOperation::Join)
		{
			LastResultText = FText::FromString(TEXT("正在加入受邀房间。"));
		}
	}
	OnChanged.Broadcast();
}

// 来源定位流程：先读取弱 LocalPlayer，再通过其仍有效的 GameInstance 查询既有 Online 子系统；任何中间层缺失都返回空，不创建旁路对象。
UCatOnlineSubsystem* UCatFrontendRoomModel::GetOnline() const
{
	const ULocalPlayer* CurrentLocalPlayer = LocalPlayer.Get();
	UGameInstance* GameInstance = CurrentLocalPlayer ? CurrentLocalPlayer->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UCatOnlineSubsystem>() : nullptr;
}

// 结果捕获流程：同步拒绝按本次返回值生成文本；受理则重读 Online 当前快照，保留已同步完成的正式错误或仍在处理的状态，不能用 accepted 抹掉回调先写入的结果。两条分支均只广播一次。
void UCatFrontendRoomModel::CaptureResult(const FCatOnlineResult& Result)
{
	if (Result.bAccepted)
	{
		HandleOnlineChanged();
		return;
	}
	LastResultText = CatFrontendRoomModelText::MakeErrorText(Result.Error);
	OnChanged.Broadcast();
}
