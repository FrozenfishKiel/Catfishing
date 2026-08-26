#include "Fishing/Integration/CatFishingCommandComponent.h"

#include "GameFramework/PlayerController.h"
#include "AbilitySystem/CatFishingAbilityTags.h"
#include "Character/CatCharacter.h"
#include "Environment/CatChumPlacementService.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/Integration/CatFishingAimLibrary.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/CatFishingSession.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "Logging/CatLog.h"
#include "GameFramework/PlayerState.h"

bool FCatFishingCooldownGate::TryConsume(const double NowSeconds, const double DurationSeconds,
	double& OutRemainingSeconds)
{
	OutRemainingSeconds = 0.0;
	if (!FMath::IsFinite(NowSeconds) || NowSeconds < 0.0
		|| !FMath::IsFinite(DurationSeconds) || DurationSeconds <= 0.0)
	{
		return false;
	}
	if (NextAllowedServerTime > NowSeconds)
	{
		OutRemainingSeconds = NextAllowedServerTime - NowSeconds;
		return false;
	}
	NextAllowedServerTime = NowSeconds + DurationSeconds;
	return true;
}

UCatFishingCommandComponent::UCatFishingCommandComponent()
{
	SetIsReplicatedByDefault(true); // PresentationState 之外，这个组件自身也要在网络上存在（承载 RPC）
	PrimaryComponentTick.bCanEverTick = false; // 纯事件驱动，不需要每帧轮询
}

void UCatFishingCommandComponent::DeliverResultFromAuthority(const FCatFishingCommandResult& Result)
{
	// 这个函数只能由服务器（拥有 Controller 权威）调用，且结果必须带上有效的 RequestId 才能对应到某次提交
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority() || !Result.RequestId.IsValid())
	{
		return;
	}

	// 唯一命令回执出口：每条结果都留结构化日志，失败用 Warning 便于在 Output Log 里过滤。
	if (Result.bCommitted)
	{
		UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_command_result Type=%s Committed=true Request=%s Session=%s Revision=%lld"),
			*UEnum::GetValueAsString(Result.CommandType), *Result.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			*Result.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), Result.Revision);
	}
	else
	{
		UE_LOG(LogCatFishing, Warning, TEXT("Event=fishing_command_result Type=%s Committed=false Error=%s Request=%s Session=%s Revision=%lld"),
			*UEnum::GetValueAsString(Result.CommandType), *UEnum::GetValueAsString(Result.Error),
			*Result.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			*Result.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), Result.Revision);
	}

	// 服务器就是本机（单机/监听服务器且这就是本地玩家）时直接走本地路径，不需要多绕一次 RPC 网络往返
	if (Controller->IsLocalController())
	{
		ReceiveResultLocally(Result);
	}
	else
	{
		ClientReceiveFishingCommandResult(Result);
	}
}

void UCatFishingCommandComponent::DeliverPlaceChumResultFromAuthority(const FCatPlaceChumResult& Result)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority() || !Result.RequestId.IsValid()) return;
	if (Result.bCommitted)
	{
		UE_LOG(LogCatFishing, Log, TEXT("Event=place_chum_result Committed=true Request=%s Field=%s Center=%s"),
			*Result.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			*Result.FieldId.ToString(EGuidFormats::DigitsWithHyphens), *Result.ServerCorrectedCenter.ToString());
	}
	else
	{
		UE_LOG(LogCatFishing, Warning, TEXT("Event=place_chum_result Committed=false Error=%s Request=%s"),
			*UEnum::GetValueAsString(Result.Error), *Result.RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	}
	if (Controller->IsLocalController()) ReceivePlaceChumResultLocally(Result);
	else ClientReceivePlaceChumResult(Result);
}

void UCatFishingCommandComponent::DeliverBeginCastResultFromAuthority(const FCatBeginCastResult& Result)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority() || !Result.Command.RequestId.IsValid()) return;
	if (Result.Command.bCommitted)
	{
		UE_LOG(LogCatFishing, Log, TEXT("Event=begin_cast_result Committed=true Request=%s Session=%s Landing=%s"),
			*Result.Command.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			*Result.Command.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			*Result.ServerCorrectedLandingWorldPoint.ToString());
	}
	else
	{
		UE_LOG(LogCatFishing, Warning, TEXT("Event=begin_cast_result Committed=false Error=%s Request=%s"),
			*UEnum::GetValueAsString(Result.Command.Error), *Result.Command.RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	}
	if (Controller->IsLocalController()) ReceiveBeginCastResultLocally(Result);
	else ClientReceiveBeginCastResult(Result);
}

// 以下 SubmitXxx 系列共用同一套模式：只能由本地控制的 Controller 发起；
// 如果这台机器本身就是服务器（HasAuthority）就直接同步调用 _Implementation，省一次 RPC 往返；
// 否则通过 Server RPC 把命令送去真正的服务器执行。
void UCatFishingCommandComponent::SubmitBeginCast(const FCatBeginCastCommand& Command)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->IsLocalController() || !Command.RequestId.IsValid()) return;
	if (Controller->HasAuthority()) ServerSubmitBeginCast_Implementation(Command);
	else ServerSubmitBeginCast(Command);
}

void UCatFishingCommandComponent::SubmitPlaceRod(const FCatPlaceRodCommand& Command)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->IsLocalController() || !Command.RequestId.IsValid()) return;
	if (Controller->HasAuthority()) ServerSubmitPlaceRod_Implementation(Command); else ServerSubmitPlaceRod(Command);
}

void UCatFishingCommandComponent::SubmitOperateRod(const FCatOperateRodCommand& Command)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->IsLocalController() || !Command.Context.RequestId.IsValid()) return;
	if (Controller->HasAuthority()) ServerSubmitOperateRod_Implementation(Command); else ServerSubmitOperateRod(Command);
}

void UCatFishingCommandComponent::SubmitLeaveRod(const FCatLeaveRodCommand& Command)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->IsLocalController() || !Command.Context.RequestId.IsValid()) return;
	if (Controller->HasAuthority()) ServerSubmitLeaveRod_Implementation(Command); else ServerSubmitLeaveRod(Command);
}

void UCatFishingCommandComponent::SubmitPackRod(const FCatPackRodCommand& Command)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->IsLocalController() || !Command.Context.RequestId.IsValid()) return;
	if (Controller->HasAuthority()) ServerSubmitPackRod_Implementation(Command); else ServerSubmitPackRod(Command);
}

bool UCatFishingCommandComponent::TryGetBeginCastResult(const FGuid RequestId, FCatBeginCastResult& OutResult) const
{
	// 查询式接口：本地按 RequestId 从已收到的结果缓存里取值，不发起任何网络请求
	OutResult = FCatBeginCastResult{};
	if (!IsSupportedOwner() || !RequestId.IsValid()) return false;
	const FCatBeginCastResult* Found = BeginCastResultsByRequestId.Find(RequestId);
	if (!Found) return false;
	OutResult = *Found;
	return true;
}

void UCatFishingCommandComponent::SubmitPlaceChum(const FCatPlaceChumCommand& Command)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->IsLocalController() || !Command.RequestId.IsValid()) return;
	if (Controller->HasAuthority()) ServerSubmitPlaceChum_Implementation(Command);
	else ServerSubmitPlaceChum(Command);
}

bool UCatFishingCommandComponent::TryGetPlaceChumResult(const FGuid RequestId,
	FCatPlaceChumResult& OutResult) const
{
	OutResult = FCatPlaceChumResult();
	if (!IsSupportedOwner() || !RequestId.IsValid()) return false;
	const FCatPlaceChumResult* Result = PlaceChumResultsByRequestId.Find(RequestId);
	if (!Result) return false;
	OutResult = *Result;
	return true;
}

bool UCatFishingCommandComponent::TryGetResult(const FGuid RequestId,
	FCatFishingCommandResult& OutResult) const
{
	OutResult = FCatFishingCommandResult();
	if (!IsSupportedOwner() || !RequestId.IsValid())
	{
		return false;
	}

	const FCatFishingCommandResult* Result = ResultsByRequestId.Find(RequestId);
	if (!Result)
	{
		return false;
	}

	OutResult = *Result;
	return true;
}

void UCatFishingCommandComponent::ConsumeResult(const FGuid RequestId)
{
	if (!IsSupportedOwner() || !RequestId.IsValid())
	{
		return;
	}

	if (ResultsByRequestId.Remove(RequestId) > 0)
	{
		ResultOrder.RemoveSingle(RequestId);
	}
}

void UCatFishingCommandComponent::ResetTransientCommandState()
{
	if (!IsSupportedOwner())
	{
		return;
	}

	// 一次性清空所有命令结果缓存与序号计数器，通常在会话/关卡切换等边界调用，避免旧 RequestId 残留造成误判重复
	ResultsByRequestId.Reset();
	ResultOrder.Reset();
	PlaceChumResultsByRequestId.Reset();
	PlaceChumResultOrder.Reset();
	BeginCastResultsByRequestId.Reset();
	BeginCastResultOrder.Reset();
	PrimaryActivationCorrelationId.Invalidate();
	LocalChumChargeStartTime = -1.0; // 关卡/会话切换时收起残留的蓄力预览线。
	ScoopCooldownGate.Reset(); // 世界时间会在旅行时重建，旧世界的绝对时间戳不能带入新地图。
	NextInputSequence = 0;
}

FCatFishingInputEdge UCatFishingCommandComponent::MakeDiscreteEdge()
{
	// 每条离散命令都配一个新 Guid（去重/幂等用）和自增的输入序号（用于时序判断，如收线/松线的先后）
	FCatFishingInputEdge Edge;
	Edge.RequestId = FGuid::NewGuid();
	Edge.InputSequence = ++NextInputSequence;
	return Edge;
}

FCatFishingInputEdge UCatFishingCommandComponent::SubmitRodInteract()
{
	// R 对应的鱼竿 Ability：具体是插竿/操作/离开由服务器根据当前竿状态三态判定，见 HandleAbilityCommandFromAuthority。
	FCatFishingInputEdge Edge = MakeDiscreteEdge();
	DispatchAbilityCommand(ECatFishingCommandType::OperateRod, Edge);
	return Edge;
}

FCatFishingInputEdge UCatFishingCommandComponent::SubmitPrimaryPressed()
{
	// 左键按下：先在本地生成一个“本次按住”的关联 ID，之后松开时把同一个 ID 带回去，
	// 让服务器能分辨“这次松开对应的是不是这次按下”（防止跨越两次不同意图的按住/松开）
	PrimaryActivationCorrelationId = FGuid::NewGuid();
	FCatFishingInputEdge Edge = MakeDiscreteEdge();
	Edge.ActivationCorrelationId = PrimaryActivationCorrelationId;
	DispatchAbilityCommand(ECatFishingCommandType::RequestHook, Edge);
	return Edge;
}

FCatFishingInputEdge UCatFishingCommandComponent::SubmitPrimaryReleased()
{
	FCatFishingInputEdge Edge = MakeDiscreteEdge();
	Edge.ActivationCorrelationId = PrimaryActivationCorrelationId; // 带上按下时记录的关联 ID
	DispatchAbilityCommand(ECatFishingCommandType::PrimaryReleased, Edge);
	PrimaryActivationCorrelationId.Invalidate(); // 松开后立即失效，避免误配对到下一次按下
	return Edge;
}

// 以下 SubmitXxx 都是薄封装：生成一条离散命令边沿并转发给统一分发口 DispatchAbilityCommand，
// 具体的合法性校验、阶段判断、权威写入全部在服务器侧的 HandleAbilityCommandFromAuthority 完成。
FCatFishingInputEdge UCatFishingCommandComponent::SubmitSlackPressed()
{
	FCatFishingInputEdge Edge = MakeDiscreteEdge();
	DispatchAbilityCommand(ECatFishingCommandType::SlackPressed, Edge);
	return Edge;
}

FCatFishingInputEdge UCatFishingCommandComponent::SubmitSlackReleased()
{
	FCatFishingInputEdge Edge = MakeDiscreteEdge();
	DispatchAbilityCommand(ECatFishingCommandType::SlackReleased, Edge);
	return Edge;
}

FCatFishingInputEdge UCatFishingCommandComponent::SubmitChumPressed()
{
	FCatFishingInputEdge Edge = MakeDiscreteEdge();
	// 本地蓄力起点只供抛物线预览用：这一行在主机和客户端都会执行，而权威那份只在服务器上写。
	LocalChumChargeStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0;
	DispatchAbilityCommand(ECatFishingCommandType::ChumPressed, Edge);
	return Edge;
}

FCatFishingInputEdge UCatFishingCommandComponent::SubmitChumReleased()
{
	FCatFishingInputEdge Edge = MakeDiscreteEdge();
	LocalChumChargeStartTime = -1.0; // 松开即收起预览；真正的蓄力时长由服务器那份时间戳换算。
	DispatchAbilityCommand(ECatFishingCommandType::ChumReleased, Edge);
	return Edge;
}

FCatFishingInputEdge UCatFishingCommandComponent::SubmitCancel()
{
	FCatFishingInputEdge Edge = MakeDiscreteEdge();
	DispatchAbilityCommand(ECatFishingCommandType::CancelFishing, Edge);
	return Edge;
}

FCatFishingInputEdge UCatFishingCommandComponent::SubmitScoop()
{
	FCatFishingInputEdge Edge = MakeDiscreteEdge();
	DispatchAbilityCommand(ECatFishingCommandType::RequestScoop, Edge);
	return Edge;
}

FCatFishingInputEdge UCatFishingCommandComponent::SubmitChum()
{
	FCatFishingInputEdge Edge = MakeDiscreteEdge();
	DispatchAbilityCommand(ECatFishingCommandType::PlaceChum, Edge);
	return Edge;
}

void UCatFishingCommandComponent::DispatchAbilityCommand(const ECatFishingCommandType CommandType,
	const FCatFishingInputEdge& Edge)
{
	// 所有“离散输入命令”（E/左键/右键/Q/X/抄网）的统一入口，只能由本地控制的玩家发起
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->IsLocalController() || !GetWorld() || !Edge.RequestId.IsValid())
	{
		return;
	}
	if (Controller->HasAuthority())
	{
		// 本机即服务器：跳过 RPC，直接同步走权威处理
		HandleAbilityCommandFromAuthority(CommandType, Edge);
	}
	else
	{
		// 客户端：把命令类型和边沿数据一起送到服务器，由服务器的同名 _Implementation 真正执行
		ServerSubmitFishingAbilityCommand(CommandType, Edge);
	}
}

void UCatFishingCommandComponent::ServerSubmitFishingAbilityCommand_Implementation(
	const ECatFishingCommandType CommandType, const FCatFishingInputEdge Edge)
{
	// Server RPC 落地后统一转给权威处理函数，和本地直连路径共用同一套逻辑，保证行为一致
	HandleAbilityCommandFromAuthority(CommandType, Edge);
}

void UCatFishingCommandComponent::BroadcastCosmeticEventFromAuthority(const FGameplayTag& EventTag) const
{
	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority() || !EventTag.IsValid())
	{
		return;
	}
	if (ACatCharacter* Character = Cast<ACatCharacter>(Controller->GetPawn()))
	{
		Character->Multicast_PlayCosmeticEvent(EventTag);
	}
}

void UCatFishingCommandComponent::HandleAbilityCommandFromAuthority(const ECatFishingCommandType CommandType,
	const FCatFishingInputEdge& Edge)
{
	// 唯一的服务器权威命令处理函数：所有钓鱼相关离散输入（抛竿/打窝/收线/松线/抄鱼/取消等）最终都汇聚到这里
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority() || !Edge.RequestId.IsValid())
	{
		return;
	}
	// 默认先构造一个“未提交”的失败结果；只有走到具体分支成功时才会改写 bCommitted。
	FCatFishingCommandResult Result;
	Result.CommandType = CommandType;
	Result.RequestId = Edge.RequestId;
	Result.bCommitted = false;
	UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr;

	// 服务器是抄网冷却唯一裁决者：GAS 冷却提供正常客户端的预测手感，这层仍要挡住绕过 Ability 直发 RPC 的请求。
	// 第一次合法到达命令入口的挥网尝试立即消费冷却；没鱼/没对准也算挥空，只有系统依赖或配置损坏不消费。
	if (CommandType == ECatFishingCommandType::RequestScoop)
	{
		double CooldownSeconds = 0.0;
		if (!Fishing || !GetDefault<UCatFishingSettings>()->TryGetScoopCooldown(CooldownSeconds))
		{
			Result.Error = ECatFishingCommandError::DependencyUnavailable;
			DeliverResultFromAuthority(Result);
			return;
		}
		double RemainingSeconds = 0.0;
		if (!ScoopCooldownGate.TryConsume(GetWorld()->GetTimeSeconds(), CooldownSeconds, RemainingSeconds))
		{
			Result.Error = ECatFishingCommandError::CooldownActive;
			UE_LOG(LogCatFishing, Warning,
				TEXT("Event=scoop_cooldown_rejected Request=%s RemainingSeconds=%.3f"),
				*Edge.RequestId.ToString(EGuidFormats::DigitsWithHyphens), RemainingSeconds);
			DeliverResultFromAuthority(Result);
			return;
		}
	}
	// 向其他客户端广播挥网动作——必须在任何裁决之前，因为抄网"失败时不留任何权威痕迹"：
	// 挥空了服务器上什么都没变，表现层没有可读的复制事实，而发起者自己看到的前摇是 Ability 的
	// 本地钩子播的（服务器镜像在 IsRemoteAuthorityMirror 处直接 return），其他玩家看不到。
	// 放在裁决前也保证远端与本地时机口径一致：都是"按下就播"，成败由后续状态变化各自呈现。
	// F 可以无条件广播是因为它只有一种语义；左键（RequestHook）按下有三种语义，
	// 必须等下面分派出具体分支才知道该播哪个动作，广播点在各分支内部，不能提到这里。
	if (CommandType == ECatFishingCommandType::RequestScoop)
	{
		BroadcastCosmeticEventFromAuthority(CatFishingAbilityTags::Cosmetic_Fishing_ScoopSwing);
	}
	if (Fishing)
	{
		// R 的鱼竿三态（服务器按当前事实分派，客户端不需要知道自己处于哪一态；多人：竿不限竿主）：
		//   正在操作某根竿（自己的或别人的） → LeaveRod（离开竿位，自由活动）
		//   附近有空操作槽的竿（不限竿主）   → OperateRod（首位右主位、次位左辅助位；空主位可接力等口会话）
		//   附近没有可加入的竿               → PlaceRod（在脚下放自己的竿；已有部署竿会被服务器拒绝）
		// R 在会话期间同样可用（多人接力钓别人竿）：
		//   等待/试探/真咬阶段离开 → 会话照常走计时，竿与钩保持原状，任何人再 R 即可接手继续；
		//   搏斗/近岸阶段离开     → 视为弃战（鱼逃走），但竿保持部署不收回。
		if (CommandType == ECatFishingCommandType::OperateRod)
		{
			const ACatCharacter* Character = Cast<ACatCharacter>(Controller->GetPawn());
			// 分支一：正在操作某根竿 → E 表示"离开竿位"；
			// 离开前先检查是否处于搏斗/近岸这类不允许中途甩手的阶段；是的话视为弃战，强制终止会话（鱼逃走）
			if (ACatFishingRodActor* OperatedRod = Fishing->FindRodOperatedBy(Controller->PlayerState))
			{
				FGuid ActiveSessionId;
				FCatFishingSessionSnapshot ActiveSnapshot;
				if (Fishing->TryGetActiveSessionForController(Controller, ActiveSessionId, ActiveSnapshot)
					&& (ActiveSnapshot.Phase == ECatFishingPhase::HookedFight
						|| ActiveSnapshot.Phase == ECatFishingPhase::NearShore
						|| ActiveSnapshot.Phase == ECatFishingPhase::ExhaustedReel))
				{
					if (ACatFishingSession* ActiveSession = Fishing->FindSession(ActiveSessionId))
					{
						ActiveSession->TerminateSession(ECatFishingOutcome::Escaped,
							TEXT("Operator left during fight"));
					}
				}
				const FCatFishingRodPresentationState& OperatedState = OperatedRod->GetPresentationState();
				FCatLeaveRodCommand LeaveCommand;
				LeaveCommand.Context.RequestId = Edge.RequestId;
				LeaveCommand.Context.RodActorId = OperatedState.RodActorId;
				LeaveCommand.Context.ExpectedRodActorRevision = OperatedState.RodActorRevision;
				DeliverResultFromAuthority(Fishing->LeaveRod(Controller, LeaveCommand));
				return;
			}
			// 分支二：附近有空操作槽的竿（未损坏、不限竿主）→ 按顺序吸附到右主位/左辅助位；
			// 只有主位为空时，Fishing->OperateRod 才会把等口中的会话接力转移给进入者。
			if (ACatFishingRodActor* NearbyRod = Character
				? Fishing->FindNearestOperableRod(Character->GetActorLocation(), 250.0) : nullptr)
			{
				const FCatFishingRodPresentationState& NearbyState = NearbyRod->GetPresentationState();
				FCatOperateRodCommand OperateCommand;
				OperateCommand.Context.RequestId = Edge.RequestId;
				OperateCommand.Context.RodActorId = NearbyState.RodActorId;
				OperateCommand.Context.ExpectedRodActorRevision = NearbyState.RodActorRevision;
				DeliverResultFromAuthority(Fishing->OperateRod(Controller, OperateCommand));
				return;
			}
			// 分支三：近旁没有可接管的竿 → 在脚下放一根自己的竿；装备 Revision 由服务器当前事实读取，不信任客户端
			FCatPlaceRodCommand PlaceCommand;
			PlaceCommand.RequestId = Edge.RequestId;
			PlaceCommand.ExpectedEquipmentRevision = Character && Character->GetEquipmentComponent()
				? Character->GetEquipmentComponent()->GetSnapshot().Revision : 0;
			DeliverResultFromAuthority(Fishing->PlaceRod(Controller, PlaceCommand));
			return;
		}
		// Q 打窝蓄力：与是否有会话无关，等口/遛鱼中都可以补窝。按下只记时刻，松开才投放。
		if (CommandType == ECatFishingCommandType::ChumPressed)
		{
			// 只记录服务器时间戳，不做任何弹道/落点计算——真正的投放延后到松开那一刻才算蓄力时长
			ChumChargeStartServerTime = GetWorld()->GetTimeSeconds();
			Result.bCommitted = true;
			Result.Error = ECatFishingCommandError::None;
			DeliverResultFromAuthority(Result);
			return;
		}
		if (CommandType == ECatFishingCommandType::ChumReleased)
		{
			// 蓄力时长 = 松开时刻 - 按下时刻；若从未记录过按下（<0），按 0 秒（最小力度）处理，防御性容错
			const double Held = ChumChargeStartServerTime >= 0.0
				? GetWorld()->GetTimeSeconds() - ChumChargeStartServerTime : 0.0;
			ChumChargeStartServerTime = -1.0; // 立即复位，避免下次判断误用旧的按下时刻
			ThrowChumFromChargeOnAuthority(Controller, Edge.RequestId, Held);
			return;
		}
		FGuid SessionId;
		FCatFishingSessionSnapshot Snapshot;
		// 用服务器事实判断这名玩家当前有没有活跃的钓鱼会话，决定走“抛竿前”还是“会话内”两套分支
		if (!Fishing->TryGetActiveSessionForController(Controller, SessionId, Snapshot))
		{
			// 没有会话时：左键按下 = 开始瞄准（记录本次按住的关联 ID），左键松开 = 抛竿。
			// 只有"按下时就无会话"的那次按住的松开才抛竿——提竿把会话打终止后的松开不能误触发重抛。
			if (CommandType == ECatFishingCommandType::RequestHook)
			{
				// 记住这次按住的关联 ID，供松开时比对；此刻只是“进入瞄准态”，尚未真正抛竿
				ServerAimingCorrelationId = Edge.ActivationCorrelationId;
				Result.bCommitted = true;
				Result.Error = ECatFishingCommandError::None;
				DeliverResultFromAuthority(Result);
				return;
			}
			if (CommandType == ECatFishingCommandType::PrimaryReleased)
			{
				// 只有关联 ID 有效且与记录的瞄准 ID 一致，才认定这是“瞄准后松开=抛竿”的那次松开
				const bool bAimingRelease = Edge.ActivationCorrelationId.IsValid()
					&& Edge.ActivationCorrelationId == ServerAimingCorrelationId;
				ServerAimingCorrelationId.Invalidate(); // 无论是否命中，本次松开后瞄准态都结束
				if (bAimingRelease)
				{
					BeginCastFromViewOnAuthority(Controller, Edge.RequestId);
				}
				return;
			}
			// X 无会话 = 收竿回包（规格：咬钩前收竿零损失）。正在操作则先离开竿位再收。
			if (CommandType == ECatFishingCommandType::CancelFishing)
			{
				// 多人：正在操作别人的竿 → X 只是离开竿位（不能收走别人的竿）。
				if (ACatFishingRodActor* OperatedRod = Fishing->FindRodOperatedBy(Controller->PlayerState))
				{
					const FCatFishingRodPresentationState& OperatedState = OperatedRod->GetPresentationState();
					if (OperatedState.OwnerPlayerState != Controller->PlayerState)
					{
						FCatLeaveRodCommand Leave;
						Leave.Context.RequestId = Edge.RequestId;
						Leave.Context.RodActorId = OperatedState.RodActorId;
						Leave.Context.ExpectedRodActorRevision = OperatedState.RodActorRevision;
						DeliverResultFromAuthority(Fishing->LeaveRod(Controller, Leave));
						return;
					}
				}
				ACatFishingRodActor* Rod = Fishing->FindDeployedRod(Controller->PlayerState);
				if (!Rod)
				{
					// 压根没竿可收，直接返回 NoRod 错误
					Result.CommandType = ECatFishingCommandType::PackRod;
					Result.Error = ECatFishingCommandError::NoRod;
					DeliverResultFromAuthority(Result);
					return;
				}
				if (Rod->GetPresentationState().OperatorPlayerState == Controller->PlayerState)
				{
					// 收竿前必须先释放操作权，否则竿处于“被占用”状态无法直接打包
					FCatLeaveRodCommand Leave;
					Leave.Context.RequestId = FGuid::NewGuid();
					Leave.Context.RodActorId = Rod->GetPresentationState().RodActorId;
					Leave.Context.ExpectedRodActorRevision = Rod->GetPresentationState().RodActorRevision;
					Fishing->LeaveRod(Controller, Leave);
				}
				// LeaveRod 可能已经推进了 Revision，这里重新读一次最新状态再打包，避免用过期 Revision 触发冲突
				const FCatFishingRodPresentationState& Fresh = Rod->GetPresentationState();
				FCatPackRodCommand Pack;
				Pack.Context.RequestId = Edge.RequestId;
				Pack.Context.RodActorId = Fresh.RodActorId;
				Pack.Context.ExpectedRodActorRevision = Fresh.RodActorRevision;
				DeliverResultFromAuthority(Fishing->PackRod(Controller, Pack));
				return;
			}
			// F 无自己的会话 = 多人抢抄：找附近处于 NearShore 的别人会话，把抄鱼命令投给它。
			// 胜负判定（首个合法抄手 Compare-and-Commit）与半圆范围校验全部在 Session::RequestScoop 内完成，
			// 这里只做粗筛路由（水平 15 米内最近的可抄会话），不做任何裁决。
			if (CommandType == ECatFishingCommandType::RequestScoop)
			{
				const ACatCharacter* ScoopingCharacter = Cast<ACatCharacter>(Controller->GetPawn());
				ACatFishingSession* TargetSession = ScoopingCharacter
					? Fishing->FindNearestScoopableSession(ScoopingCharacter->GetActorLocation(), 1500.0) : nullptr;
				if (!TargetSession)
				{
					Result.Error = ECatFishingCommandError::NotNearShore;
					DeliverResultFromAuthority(Result);
					return;
				}
				const FCatFishingSessionSnapshot& TargetSnapshot = TargetSession->GetSnapshot();
				Result.FishingSessionId = TargetSnapshot.FishingSessionId;
				FCatScoopCommand ScoopCommand;
				ScoopCommand.Context.RequestId = Edge.RequestId;
				ScoopCommand.Context.ExpectedRevision = TargetSnapshot.Revision;
				ScoopCommand.TargetGuardContainerId = ScoopingCharacter->GetPersonalFishGuardId();
				const FCatScoopResult ScoopResult = TargetSession->RequestScoop(Controller, ScoopCommand);
				Result.bCommitted = ScoopResult.Command.bCommitted;
				Result.Error = MapDomainCommandError(ScoopResult.Command.Error);
				DeliverResultFromAuthority(Result);
				return;
			}
		}
		else
		{
			if (ACatFishingSession* Session = Fishing->FindSession(SessionId))
			{
				Result.FishingSessionId = SessionId;
				// Primary 输入在搏斗阶段兼任收线；HookedFight 之前它仍然是提竿意图。
				if (CommandType == ECatFishingCommandType::RequestHook)
				{
					// 按下时已有会话 → 这次按住不是瞄准；即使会话随后终止，松开也不得触发重抛。
					ServerAimingCorrelationId.Invalidate();
					if (Snapshot.Phase == ECatFishingPhase::HookedFight
						|| Snapshot.Phase == ECatFishingPhase::ExhaustedReel)
					{
						// 搏斗阶段：左键按下语义变成“开始收线”，InputSequence 用于时序仲裁
						Result.bCommitted = Session->SetReelingFromAuthority(Edge.InputSequence, true);
						Result.Error = Result.bCommitted
							? ECatFishingCommandError::None : ECatFishingCommandError::InvalidPhase;
						DeliverResultFromAuthority(Result);
						return;
					}
					// 非搏斗阶段：左键按下仍是“提竿”意图，交给会话自身的提竿状态机处理。
					// 提竿动作在这里广播而不是在函数顶部：只有分派到这个分支才确定左键是"提竿"
					// （无会话时是举竿瞄准、搏斗中是开始收线，三者动作完全不同）。
					// 提竿空竿时服务器不产生任何状态变化，其他玩家只能靠这条通道看到这个动作。
					BroadcastCosmeticEventFromAuthority(CatFishingAbilityTags::Cosmetic_Fishing_HookPull);
					DeliverResultFromAuthority(Session->RequestHookFromAuthority(Edge.RequestId));
					return;
				}
				if (CommandType == ECatFishingCommandType::PrimaryReleased)
				{
					if (Snapshot.Phase == ECatFishingPhase::HookedFight
						|| Snapshot.Phase == ECatFishingPhase::ExhaustedReel)
					{
						// 搏斗阶段松开左键 = 停止收线
						Result.bCommitted = Session->SetReelingFromAuthority(Edge.InputSequence, false);
						Result.Error = Result.bCommitted
							? ECatFishingCommandError::None : ECatFishingCommandError::InvalidPhase;
					}
					else
					{
						// 搏斗之外松开 Primary 没有对应的权威写口，视为无害 no-op。
						Result.bCommitted = true;
						Result.Error = ECatFishingCommandError::None;
					}
					DeliverResultFromAuthority(Result);
					return;
				}
				if (CommandType == ECatFishingCommandType::SlackPressed
					|| CommandType == ECatFishingCommandType::SlackReleased)
				{
					// 右键松开线杯只在搏斗阶段有意义；其他阶段视为无害 no-op，避免 UI 误报。
					if (Snapshot.Phase == ECatFishingPhase::HookedFight)
					{
						// 按下/松开都转成同一个权威写口，用命令类型本身当作“是否按下”的布尔值
						Result.bCommitted = Session->SetSlackingFromAuthority(Edge.InputSequence,
							CommandType == ECatFishingCommandType::SlackPressed);
						Result.Error = Result.bCommitted
							? ECatFishingCommandError::None : ECatFishingCommandError::InvalidPhase;
					}
					else
					{
						Result.bCommitted = true;
						Result.Error = ECatFishingCommandError::None;
					}
					DeliverResultFromAuthority(Result);
					return;
				}
				if (CommandType == ECatFishingCommandType::CancelFishing)
				{
					// 会话内取消（提竿失败/主动放弃等）交给会话自身的取消状态机
					DeliverResultFromAuthority(Session->CancelFromAuthority(Edge.RequestId));
					return;
				}
				if (CommandType == ECatFishingCommandType::RequestScoop)
				{
					// 抢抄命令由服务器重建抄手身份和鱼护 ID；客户端无法伪造目标容器。
					const ACatCharacter* ScoopingCharacter = Cast<ACatCharacter>(Controller->GetPawn());
					FCatScoopCommand ScoopCommand;
					ScoopCommand.Context.RequestId = Edge.RequestId;
					ScoopCommand.Context.ExpectedRevision = Snapshot.Revision; // 乐观并发：绑定客户端读到的会话版本
					ScoopCommand.TargetGuardContainerId = ScoopingCharacter
						? ScoopingCharacter->GetPersonalFishGuardId() : FGuid();
					// 真正的“首个合法抢抄 Compare-and-Commit”逻辑在 Session::RequestScoop 内部
					const FCatScoopResult ScoopResult = Session->RequestScoop(Controller, ScoopCommand);
					Result.bCommitted = ScoopResult.Command.bCommitted;
					Result.Error = MapDomainCommandError(ScoopResult.Command.Error); // 领域错误码转成命令层通用错误码
					DeliverResultFromAuthority(Result);
					return;
				}
			}
		}
	}
	// Stage B establishes the single command edge. Payload/session/rod resolution lands in C/D;
	// until then every input command has an explicit terminal refusal and never fabricates success.
	Result.Error = ECatFishingCommandError::DependencyUnavailable;
	DeliverResultFromAuthority(Result);
}


// 服务器抛竿流程：要求本人处于某根竿的主操作位（鱼竿可以属于别人）；视线射线∩水面得到候选落点；
// RodActorId/Revision、Equipment Revision、WaterRegion Handle 全部由服务器事实填充，客户端不传任何载荷。
void UCatFishingCommandComponent::BeginCastFromViewOnAuthority(APlayerController* Controller, const FGuid& RequestId)
{
	FCatBeginCastResult Result;
	Result.Command.CommandType = ECatFishingCommandType::BeginCast;
	Result.Command.RequestId = RequestId;
	UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr;
	ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr;
	// 竿必须已部署，装备组件用于读取当前鱼饵/浮标等的 Equipment Revision 供后续冲突检测
	// 必须按“正在操作”而不是“自己部署”解析；否则接管别人的鱼竿后永远找不到抛竿目标。
	ACatFishingRodActor* Rod = Fishing && Controller ? Fishing->FindRodOperatedBy(Controller->PlayerState) : nullptr;
	UCatEquipmentComponent* Equipment = Character ? Character->GetEquipmentComponent() : nullptr;
	if (!Fishing || !Character || !Rod || !Equipment)
	{
		// 没竿单独给出 NoRod 语义化错误，其余缺依赖统一归为 DependencyUnavailable
		Result.Command.Error = Rod ? ECatFishingCommandError::DependencyUnavailable : ECatFishingCommandError::NoRod;
		DeliverBeginCastResultFromAuthority(Result);
		return;
	}
	const FCatFishingRodPresentationState& RodState = Rod->GetPresentationState();
	if (!Rod->IsPrimaryOperator(Controller->PlayerState))
	{
		// 没在操作竿位就松开左键：不是抛竿意图，静默忽略（不投递回执，避免每次点击都刷失败日志）。
		return;
	}
	FCatWaterRegionHandle Region;
	FVector Landing;
	// 服务器用自己权威的视点位置/朝向重新解一次瞄准射线与水面的交点，完全不采信客户端上报的落点
	if (!UCatFishingAimLibrary::ResolveCastAimPoint(this, Character->GetPawnViewLocation(),
		Controller->GetControlRotation(), Region, Landing))
	{
		Result.Command.Error = ECatFishingCommandError::InvalidWaterTarget;
		DeliverBeginCastResultFromAuthority(Result);
		return;
	}
	// 组装真正的抛竿命令：Id/Revision/落点/水域全部来自服务器刚刚算出的权威事实
	FCatBeginCastCommand Command;
	Command.RequestId = RequestId;
	Command.RodActorId = RodState.RodActorId;
	Command.ExpectedRodActorRevision = RodState.RodActorRevision;
	Command.ExpectedEquipmentRevision = Equipment->GetSnapshot().Revision;
	Command.ClientCandidateWorldPoint = Landing;
	Command.ExpectedWaterRegionHandle = Region;
	// 真正的射程/视线/装备等业务校验都在 Fishing->BeginCast 内部完成，这里只负责组装权威输入
	DeliverBeginCastResultFromAuthority(Fishing->BeginCast(Controller, Command));
}

// 服务器打窝流程：按按住时长算蓄力 → 与客户端预览同一套弹道预测得到落点 → 选一份可用窝料 → 交给 PlaceChum 做射程/夹角/视线/库存/水域校验。
void UCatFishingCommandComponent::ThrowChumFromChargeOnAuthority(APlayerController* Controller, const FGuid& RequestId,
	const double HeldSeconds)
{
	FCatPlaceChumResult Result;
	Result.RequestId = RequestId;
	ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr;
	UCatEquipmentComponent* Equipment = Character ? Character->GetEquipmentComponent() : nullptr;
	UCatChumPlacementService* Service = GetWorld() ? GetWorld()->GetSubsystem<UCatChumPlacementService>() : nullptr;
	if (!Character || !Equipment || !Service)
	{
		Result.Error = ECatChumFieldError::DependencyUnavailable;
		DeliverPlaceChumResultFromAuthority(Result);
		return;
	}
	// 选窝料：优先项目 starter 配置的种类，否则背包里第一份数量>0 的 Chum 类耗材。
	const FCatEquipmentLoadoutSnapshot& Loadout = Equipment->GetSnapshot();
	FName ChumId = GetDefault<UCatEquipmentSettings>()->StarterChumDefinitionId;
	// 小工具 lambda：判断某个耗材 ID 在当前背包快照里是否还有存量
	auto HasStock = [&Loadout](const FName Id)
	{
		for (const FCatRunConsumableStack& Stack : Loadout.Consumables)
		{
			if (Stack.DefinitionId == Id && Stack.Quantity > 0) return true;
		}
		return false;
	};
	if (ChumId.IsNone() || !HasStock(ChumId))
	{
		// 默认窝料未配置，或已配置但背包里已经用光了：退化成遍历背包找第一份 Kind==Chum 的耗材
		ChumId = NAME_None;
		for (const FCatRunConsumableStack& Stack : Loadout.Consumables)
		{
			const UCatEquipmentDefinition* Def = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(Stack.DefinitionId);
			if (Def && Def->Kind == ECatEquipmentKind::Chum && Stack.Quantity > 0) { ChumId = Stack.DefinitionId; break; }
		}
	}
	if (ChumId.IsNone())
	{
		// 背包里一份可用窝料都没有，直接拒绝，不进入弹道计算
		Result.Error = ECatChumFieldError::EquipmentUnavailable;
		DeliverPlaceChumResultFromAuthority(Result);
		return;
	}
	// 按住时长换算成蓄力比例，再用与客户端预览完全相同的弹道预测函数算权威落点
	const float Alpha = UCatFishingAimLibrary::ChargeAlphaFromHeldSeconds(static_cast<float>(HeldSeconds));
	TArray<FVector> Path;
	FVector Landing;
	FCatWaterRegionHandle Region;
	bool bHitWater = false;
	UCatFishingAimLibrary::PredictChumThrow(this, Character->GetActorLocation(), Controller->GetControlRotation(),
		Alpha, Path, Landing, Region, bHitWater);
	if (!bHitWater || !Region.IsValid())
	{
		Result.Error = ECatChumFieldError::InvalidWaterTarget;
		DeliverPlaceChumResultFromAuthority(Result);
		return;
	}
	// 组装真正的打窝命令，交给 ChumPlacementService 做射程/夹角/视线/库存/水域等完整校验并落地
	FCatPlaceChumCommand Command;
	Command.RequestId = RequestId;
	Command.ExpectedWaterRegionHandle = Region;
	Command.ExpectedEquipmentRevision = Loadout.Revision;
	Command.ChumDefinitionId = ChumId;
	Command.Quantity = FMath::Max(1, GetDefault<UCatFishingSettings>()->ChumThrowQuantity);
	Command.ClientCandidateWorldPoint = Landing;
	UE_LOG(LogCatFishing, Log, TEXT("Event=chum_throw Held=%.2f Alpha=%.2f Landing=%s Chum=%s"),
		HeldSeconds, Alpha, *Landing.ToString(), *ChumId.ToString());
	DeliverPlaceChumResultFromAuthority(Service->PlaceChum(Controller, Command));
}

void UCatFishingCommandComponent::ForwardLegacyAssist(const FGuid FishingSessionId, const FGuid RequestId,
	const int64 ExpectedRevision)
{
	// 旧版协作请求入口，只能在服务器上转发；这里不做任何额外校验，交给 Fishing Service/Session 内部把关
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (Controller && Controller->HasAuthority())
	{
		if (UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr)
		{
			Fishing->SubmitFightAssist(FishingSessionId, Controller, RequestId, ExpectedRevision);
		}
	}
}

void UCatFishingCommandComponent::ForwardLegacyScoop(const FGuid FishingSessionId, FCatScoopCommand Command)
{
	// 旧版抢抄入口，同样只能由服务器调用
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority())
	{
		return;
	}
	double CooldownSeconds = 0.0;
	double RemainingSeconds = 0.0;
	if (!GetDefault<UCatFishingSettings>()->TryGetScoopCooldown(CooldownSeconds)
		|| !ScoopCooldownGate.TryConsume(GetWorld()->GetTimeSeconds(), CooldownSeconds, RemainingSeconds))
	{
		FCatFishingCommandResult Rejected;
		Rejected.CommandType = ECatFishingCommandType::RequestScoop;
		Rejected.RequestId = Command.Context.RequestId;
		Rejected.FishingSessionId = FishingSessionId;
		Rejected.Error = CooldownSeconds > 0.0
			? ECatFishingCommandError::CooldownActive : ECatFishingCommandError::DependencyUnavailable;
		DeliverResultFromAuthority(Rejected);
		return;
	}
	// 清掉调用方可能带来的客户端身份字段，防止伪造 StableNetId
	Command.Context.StableNetId.Reset();
	// 抄手的鱼护 ID 必须由服务器根据当前 Pawn 重新读取，不信任传入 Command 里的值
	const ACatCharacter* Character = Cast<ACatCharacter>(Controller->GetPawn());
	Command.TargetGuardContainerId = Character ? Character->GetPersonalFishGuardId() : FGuid();
	if (UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr)
	{
		Fishing->RequestScoop(FishingSessionId, Controller, Command);
	}
}

void UCatFishingCommandComponent::ServerSubmitPlaceChum_Implementation(const FCatPlaceChumCommand& Command)
{
	// Server RPC 落地点：先检查发起者确实有服务器权威、且当前允许转发玩法命令（例如没被限制输入）
	ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority() || !Controller->CanForwardGameplayCommand()) return;
	UCatChumPlacementService* Service = GetWorld()
		? GetWorld()->GetSubsystem<UCatChumPlacementService>() : nullptr;
	FCatPlaceChumResult Result;
	Result.RequestId = Command.RequestId;
	if (Service) Result = Service->PlaceChum(Controller, Command);
	DeliverPlaceChumResultFromAuthority(Result);
}

void UCatFishingCommandComponent::ServerSubmitBeginCast_Implementation(const FCatBeginCastCommand& Command)
{
	ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority() || !Controller->CanForwardGameplayCommand()) return;
	FCatBeginCastResult Result;
	Result.Command.CommandType = ECatFishingCommandType::BeginCast;
	Result.Command.RequestId = Command.RequestId;
	if (UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr)
	{
		Result = Fishing->BeginCast(Controller, Command);
	}
	DeliverBeginCastResultFromAuthority(Result);
}

// 以下几个 ServerSubmitXxxRod_Implementation 模式相同：校验权威后直接转发给 Fishing Service 对应接口，
// 把服务结果原样投递回执，Service 内部负责真正的射程/占用/装备等业务校验。
void UCatFishingCommandComponent::ServerSubmitPlaceRod_Implementation(const FCatPlaceRodCommand& Command)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority()) return;
	UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr;
	if (Fishing) DeliverResultFromAuthority(Fishing->PlaceRod(Controller, Command));
}

void UCatFishingCommandComponent::ServerSubmitOperateRod_Implementation(const FCatOperateRodCommand& Command)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority()) return;
	UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr;
	if (Fishing) DeliverResultFromAuthority(Fishing->OperateRod(Controller, Command));
}

void UCatFishingCommandComponent::ServerSubmitLeaveRod_Implementation(const FCatLeaveRodCommand& Command)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority()) return;
	UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr;
	if (Fishing) DeliverResultFromAuthority(Fishing->LeaveRod(Controller, Command));
}

void UCatFishingCommandComponent::ServerSubmitPackRod_Implementation(const FCatPackRodCommand& Command)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority()) return;
	UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr;
	if (Fishing) DeliverResultFromAuthority(Fishing->PackRod(Controller, Command));
}

// 三个 ClientReceiveXxx_Implementation 都是 Client RPC 落地点：服务器发来的结果最终都汇入对应的本地 Receive 函数
void UCatFishingCommandComponent::ClientReceiveFishingCommandResult_Implementation(
	const FCatFishingCommandResult& Result)
{
	ReceiveResultLocally(Result);
}

void UCatFishingCommandComponent::ClientReceivePlaceChumResult_Implementation(
	const FCatPlaceChumResult& Result)
{
	ReceivePlaceChumResultLocally(Result);
}

void UCatFishingCommandComponent::ClientReceiveBeginCastResult_Implementation(const FCatBeginCastResult& Result)
{
	ReceiveBeginCastResultLocally(Result);
}

bool UCatFishingCommandComponent::IsSupportedOwner() const
{
	// 这个组件目前只设计给 PlayerController 挂载使用，其他 Owner 类型一律视为不支持
	return Cast<APlayerController>(GetOwner()) != nullptr;
}

void UCatFishingCommandComponent::ReceiveResultLocally(const FCatFishingCommandResult& Result)
{
	// 已经收到过同一个 RequestId 的结果时直接忽略，防止重复 RPC/本地直连双跑造成的重复广播
	if (!IsSupportedOwner() || !Result.RequestId.IsValid()
		|| ResultsByRequestId.Contains(Result.RequestId))
	{
		return;
	}

	ResultsByRequestId.Add(Result.RequestId, Result);
	ResultOrder.Add(Result.RequestId);
	// 缓存有上限，超出后按插入顺序淘汰最老的一条，避免长时间游玩后无限增长
	if (ResultOrder.Num() > MaxStoredResults)
	{
		const FGuid EvictedRequestId = ResultOrder[0];
		ResultOrder.RemoveAt(0);
		ResultsByRequestId.Remove(EvictedRequestId);
	}

	// 通知所有订阅者（通常是 GA/UI）这条命令有了终态结果
	OnResultReceived.Broadcast(Result);
}

void UCatFishingCommandComponent::ReceivePlaceChumResultLocally(const FCatPlaceChumResult& Result)
{
	if (!IsSupportedOwner() || !Result.RequestId.IsValid()
		|| PlaceChumResultsByRequestId.Contains(Result.RequestId)) return;
	// 专用的打窝结果缓存单独维护一份（携带 FieldId/中心点等打窝专属字段），同样按上限淘汰最老记录
	PlaceChumResultsByRequestId.Add(Result.RequestId, Result);
	PlaceChumResultOrder.Add(Result.RequestId);
	if (PlaceChumResultOrder.Num() > MaxStoredResults)
	{
		const FGuid Evicted = PlaceChumResultOrder[0];
		PlaceChumResultOrder.RemoveAt(0);
		PlaceChumResultsByRequestId.Remove(Evicted);
	}
	// 同时投影出一份“通用命令结果”，让只关心 bCommitted/Error 的通用监听者（不需要打窝专属字段）也能收到
	FCatFishingCommandResult Common;
	Common.CommandType = ECatFishingCommandType::PlaceChum;
	Common.bCommitted = Result.bCommitted;
	Common.RequestId = Result.RequestId;
	Common.EquipmentRevision = Result.EquipmentRevision;
	Common.Revision = Result.ChumFieldSetRevision;
	// 打窝子系统用自己的一套错误码，这里逐一映射到通用命令错误码，语义不对齐的兜底为 DependencyUnavailable
	switch (Result.Error)
	{
	case ECatChumFieldError::None: Common.Error = ECatFishingCommandError::None; break;
	case ECatChumFieldError::FeatureDisabled: Common.Error = ECatFishingCommandError::FeatureDisabled; break;
	case ECatChumFieldError::CommandsClosed: Common.Error = ECatFishingCommandError::CommandsClosed; break;
	case ECatChumFieldError::InvalidIdentity: Common.Error = ECatFishingCommandError::InvalidIdentity; break;
	case ECatChumFieldError::InvalidPayload: Common.Error = ECatFishingCommandError::InvalidPayload; break;
	case ECatChumFieldError::InvalidWaterTarget: Common.Error = ECatFishingCommandError::InvalidWaterTarget; break;
	case ECatChumFieldError::StaleGeometry: Common.Error = ECatFishingCommandError::RevisionConflict; break;
	case ECatChumFieldError::PlacementOutOfRange: Common.Error = ECatFishingCommandError::CastOutOfRange; break;
	case ECatChumFieldError::EquipmentRevisionConflict: Common.Error = ECatFishingCommandError::EquipmentRevisionConflict; break;
	case ECatChumFieldError::AlreadyResolved: Common.Error = ECatFishingCommandError::AlreadyResolved; break;
	default: Common.Error = ECatFishingCommandError::DependencyUnavailable; break;
	}
	ReceiveResultLocally(Common);
}

void UCatFishingCommandComponent::ReceiveBeginCastResultLocally(const FCatBeginCastResult& Result)
{
	const FGuid RequestId = Result.Command.RequestId;
	if (!IsSupportedOwner() || !RequestId.IsValid() || BeginCastResultsByRequestId.Contains(RequestId)) return;
	// 专用的抛竿结果缓存（携带服务器修正后的落点等抛竿专属字段），同样按上限淘汰最老记录
	BeginCastResultsByRequestId.Add(RequestId, Result);
	BeginCastResultOrder.Add(RequestId);
	if (BeginCastResultOrder.Num() > MaxStoredResults)
	{
		const FGuid Evicted = BeginCastResultOrder[0];
		BeginCastResultOrder.RemoveAt(0);
		BeginCastResultsByRequestId.Remove(Evicted);
	}
	// BeginCastResult 内部的 Command 字段本身就是通用结果结构，直接复用同一套广播路径
	ReceiveResultLocally(Result.Command);
}
