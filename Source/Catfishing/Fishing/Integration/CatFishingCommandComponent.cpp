#include "Fishing/Integration/CatFishingCommandComponent.h"

#include "GameFramework/PlayerController.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Environment/CatChumPlacementService.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/Integration/CatFishingAimLibrary.h"
#include "Fishing/Actors/CatFishEncounterActor.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/Presentation/CatFishingCameraComponent.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Logging/CatLog.h"
#include "Logging/CatLogContext.h"
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

namespace
{
	FString BuildRodAimControllerFields(const AController* Controller)
	{
		return FString::Printf(TEXT("World=%s Authority=%s LocalRole=%d %s"),
			*GetNameSafe(Controller ? Controller->GetWorld() : nullptr),
			Controller && Controller->HasAuthority() ? TEXT("true") : TEXT("false"),
			Controller ? static_cast<int32>(Controller->GetLocalRole()) : INDEX_NONE,
			*CatLogContext::BuildControllerFields(Controller));
	}

	/** 构造阶段 gate 拒绝时的统一竿命令回执；旧直连 RPC 用它保留 RequestId，让 UI/Ability 能结束等待态。 */
	FCatFishingCommandResult MakeRodCommandsClosedResult(const ECatFishingCommandType CommandType, const FGuid RequestId)
	{
		FCatFishingCommandResult Result;
		Result.CommandType = CommandType;
		Result.RequestId = RequestId;
		Result.Error = ECatFishingCommandError::CommandsClosed;
		Result.bCommitted = false;
		return Result;
	}
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
	const FString ControllerFields = CatLogContext::BuildControllerFields(Controller);
	if (Result.bCommitted)
	{
		UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_command_result Type=%s Committed=true Request=%s Session=%s Revision=%lld %s"),
			*UEnum::GetValueAsString(Result.CommandType), *Result.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			*Result.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), Result.Revision, *ControllerFields);
	}
	else
	{
		UE_LOG(LogCatFishing, Warning, TEXT("Event=fishing_command_result Type=%s Committed=false Error=%s Request=%s Session=%s Revision=%lld %s"),
			*UEnum::GetValueAsString(Result.CommandType), *UEnum::GetValueAsString(Result.Error),
			*Result.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			*Result.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), Result.Revision, *ControllerFields);
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
	const FString ControllerFields = CatLogContext::BuildControllerFields(Controller);
	if (Result.bCommitted)
	{
		UE_LOG(LogCatFishing, Log, TEXT("Event=place_chum_result Committed=true Request=%s Field=%s Center=%s %s"),
			*Result.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			*Result.FieldId.ToString(EGuidFormats::DigitsWithHyphens), *Result.ServerCorrectedCenter.ToString(),
			*ControllerFields);
	}
	else
	{
		UE_LOG(LogCatFishing, Warning, TEXT("Event=place_chum_result Committed=false Error=%s Request=%s %s"),
			*UEnum::GetValueAsString(Result.Error), *Result.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			*ControllerFields);
	}
	if (Controller->IsLocalController()) ReceivePlaceChumResultLocally(Result);
	else ClientReceivePlaceChumResult(Result);
}

void UCatFishingCommandComponent::DeliverBeginCastResultFromAuthority(const FCatBeginCastResult& Result)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority() || !Result.Command.RequestId.IsValid()) return;
	const FString ControllerFields = CatLogContext::BuildControllerFields(Controller);
	if (Result.Command.bCommitted)
	{
		UE_LOG(LogCatFishing, Log, TEXT("Event=begin_cast_result Committed=true Request=%s Session=%s Landing=%s %s"),
			*Result.Command.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			*Result.Command.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			*Result.ServerCorrectedLandingWorldPoint.ToString(), *ControllerFields);
	}
	else
	{
		UE_LOG(LogCatFishing, Warning, TEXT("Event=begin_cast_result Committed=false Error=%s Request=%s %s"),
			*UEnum::GetValueAsString(Result.Command.Error),
			*Result.Command.RequestId.ToString(EGuidFormats::DigitsWithHyphens), *ControllerFields);
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

	// 仍持有旧输入域时先尽力发送停止；鼠标样本/段序号和累计量贯穿Controller生命周期，不回绕。
	StopLocalRodAimInput();
	// 清空离散命令与结果缓存，避免关卡切换后留下旧RequestId；转杆累计流由独立单调序号保护。
	ResultsByRequestId.Reset();
	ResultOrder.Reset();
	PlaceChumResultsByRequestId.Reset();
	PlaceChumResultOrder.Reset();
	BeginCastResultsByRequestId.Reset();
	BeginCastResultOrder.Reset();
	PrimaryActivationCorrelationId.Invalidate();
	ServerAimingCorrelationId.Invalidate();
	bServerPrimaryHeld = false;
	bServerSlackHeld = false;
	bLocalSlackHeld = false;
	LocalMouseAimRodActorId.Invalidate();
	LocalMouseAimEpoch = 0;
	LocalPitchAimRod.Reset();
	LocalPitchAimEpoch = 0;
	bLocalPitchAimInitialized = false;
	LocalChumChargeStartTime = -1.0; // 关卡/会话切换时收起残留的蓄力预览线。
	ChumChargeStartServerTime = -1.0;
	ScoopCooldownGate.Reset(); // 世界时间会在旅行时重建，旧世界的绝对时间戳不能带入新地图。
	// This component lives on the Controller across pawn changes. Preserve monotonic sequence fences.
}

bool UCatFishingCommandComponent::TryGetHeldFightInputStateFromAuthority(bool& OutPrimaryHeld,
	bool& OutSlackHeld, int64& OutInputSequence) const
{
	OutPrimaryHeld = false;
	OutSlackHeld = false;
	OutInputSequence = 0;
	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority())
	{
		return false;
	}
	OutPrimaryHeld = bServerPrimaryHeld;
	OutSlackHeld = bServerSlackHeld;
	OutInputSequence = LastServerHeldInputSequence;
	return true;
}

void UCatFishingCommandComponent::ClearHeldFightInputForControlTransferFromAuthority()
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	StopLocalRodAimInput();
	bServerPrimaryHeld = false;
	bServerSlackHeld = false;
	ServerAimingCorrelationId.Invalidate();
}

void UCatFishingCommandComponent::ClearHeldInputForLifecycle(const FName Reason)
{
	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || (!Controller->HasAuthority() && !Controller->IsLocalController())) return;
	StopLocalRodAimInput();
	PrimaryActivationCorrelationId.Invalidate();
	bLocalSlackHeld = false;
	LocalChumChargeStartTime = -1.0;
	NextInputSequence = FMath::Max(NextInputSequence, LastServerHeldInputSequence);
	FCatFishingInputEdge Edge = MakeDiscreteEdge();
	Edge.InputSequence = ++NextInputSequence; // Two distinct runner edges clear primary and slack atomically in this RPC.
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_input_lifecycle_clear_requested Reason=%s RequestId=%s RodActorId=%s ControlEpoch=%u InputSequence=%lld %s"),
		*Reason.ToString(), *Edge.RequestId.ToString(), *Edge.ControlRodActorId.ToString(), Edge.ControlEpoch,
		Edge.InputSequence, *CatLogContext::BuildControllerFields(Controller));
	if (Controller->HasAuthority()) ServerClearHeldInputForLifecycle_Implementation(Reason, Edge);
	else ServerClearHeldInputForLifecycle(Reason, Edge);
}

void UCatFishingCommandComponent::ServerClearHeldInputForLifecycle_Implementation(const FName Reason, const FCatFishingInputEdge Edge)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority()) return;
	if (!Edge.RequestId.IsValid() || Edge.InputSequence <= LastServerHeldInputSequence || Edge.InputSequence < 2)
	{
		UE_LOG(LogCatFishing, Warning, TEXT("Event=fishing_input_lifecycle_clear_rejected Reason=StaleSequence RequestId=%s RodActorId=%s InputSequence=%lld AcceptedSequence=%lld %s"),
			*Edge.RequestId.ToString(), *Edge.ControlRodActorId.ToString(), Edge.InputSequence,
			LastServerHeldInputSequence, *CatLogContext::BuildControllerFields(Controller));
		ClientReceiveHeldInputCleared(Reason, Edge.InputSequence, false);
		return;
	}
	const bool bCancelledAim = ServerAimingCorrelationId.IsValid();
	ServerAimingCorrelationId.Invalidate();
	ChumChargeStartServerTime = -1.0;
	bServerPrimaryHeld = false;
	bServerSlackHeld = false;
	LastServerHeldInputSequence = Edge.InputSequence;
	FGuid SessionId;
	if (UCatFishingService* Fishing = GetWorld()->GetSubsystem<UCatFishingService>())
		if (ACatFishingRodActor* Rod = Fishing->FindRodOperatedBy(Controller->PlayerState);
			Rod && Rod->GetPresentationState().RodActorId == Edge.ControlRodActorId && Rod->GetControlEpoch() == Edge.ControlEpoch)
		{
			Rod->StopHeldAimInputFromAuthority(Controller->PlayerState);
			if (ACatFishingSession* Session = Fishing->FindActiveSessionByRod(Rod))
			{
				SessionId = Session->GetSnapshot().FishingSessionId;
				Session->SetReelingFromAuthority(Controller->PlayerState, Edge.InputSequence - 1, false);
				Session->SetSlackingFromAuthority(Controller->PlayerState, Edge.InputSequence, false);
			}
		}
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_input_lifecycle_cleared Reason=%s RequestId=%s SessionId=%s RodActorId=%s ControlEpoch=%u InputSequence=%lld CancelledAim=%d Result=SessionPreserved %s"),
		*Reason.ToString(), *Edge.RequestId.ToString(), *SessionId.ToString(), *Edge.ControlRodActorId.ToString(),
		Edge.ControlEpoch, Edge.InputSequence, bCancelledAim, *CatLogContext::BuildControllerFields(Controller));
	ClientReceiveHeldInputCleared(Reason, Edge.InputSequence, true);
}

void UCatFishingCommandComponent::ClientReceiveHeldInputCleared_Implementation(const FName Reason, const int64 InputSequence, const bool bAccepted)
{
	if (bAccepted) NextInputSequence = FMath::Max(NextInputSequence, InputSequence);
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_input_lifecycle_clear_received Reason=%s InputSequence=%lld Accepted=%d %s"),
		*Reason.ToString(), InputSequence, bAccepted, *CatLogContext::BuildControllerFields(Cast<APlayerController>(GetOwner())));
}

void UCatFishingCommandComponent::TrackHeldFightInputFromAuthority(
	const ECatFishingCommandType CommandType, const FCatFishingInputEdge& Edge)
{
	if (Edge.InputSequence <= LastServerHeldInputSequence)
	{
		return;
	}
	bool* HeldState = nullptr;
	switch (CommandType)
	{
	case ECatFishingCommandType::RequestHook:
		HeldState = &bServerPrimaryHeld;
		break;
	case ECatFishingCommandType::PrimaryReleased:
		HeldState = &bServerPrimaryHeld;
		break;
	case ECatFishingCommandType::SlackPressed:
		HeldState = &bServerSlackHeld;
		break;
	case ECatFishingCommandType::SlackReleased:
		HeldState = &bServerSlackHeld;
		break;
	default:
		return;
	}
	*HeldState = CommandType == ECatFishingCommandType::RequestHook
		|| CommandType == ECatFishingCommandType::SlackPressed;
	LastServerHeldInputSequence = Edge.InputSequence;
}

FCatFishingInputEdge UCatFishingCommandComponent::MakeDiscreteEdge()
{
	// 每条离散命令都配一个新 Guid（去重/幂等用）和自增的输入序号（用于时序判断，如收线/松线的先后）
	FCatFishingInputEdge Edge;
	Edge.RequestId = FGuid::NewGuid();
	Edge.InputSequence = ++NextInputSequence;
	if (const ACatFishingRodActor* Rod = UCatFishingCameraComponent::FindHeldRodOperatedBy(
		Cast<APlayerController>(GetOwner())))
	{
		Edge.ControlRodActorId = Rod->GetPresentationState().RodActorId;
		Edge.ControlEpoch = Rod->GetControlEpoch();
	}
	return Edge;
}

FCatFishingInputEdge UCatFishingCommandComponent::SubmitRodInteract()
{
	// R explicitly takes or releases owner control; physical assistance has no command role.
	FCatFishingInputEdge Edge = MakeDiscreteEdge();
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_rod_interact_requested RequestId=%s InputSequence=%lld %s"),
		*Edge.RequestId.ToString(), Edge.InputSequence,
		*BuildRodAimControllerFields(Cast<APlayerController>(GetOwner())));
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
	Edge.bHasCastViewRay = UCatFishingAimLibrary::TryGetLocalCastViewRay(Cast<APlayerController>(GetOwner()),
		Edge.CastViewOrigin, Edge.CastViewDirection);
	DispatchAbilityCommand(ECatFishingCommandType::PrimaryReleased, Edge);
	PrimaryActivationCorrelationId.Invalidate(); // 松开后立即失效，避免误配对到下一次按下
	return Edge;
}

// 以下 SubmitXxx 都是薄封装：生成一条离散命令边沿并转发给统一分发口 DispatchAbilityCommand，
// 具体的合法性校验、阶段判断、权威写入全部在服务器侧的 HandleAbilityCommandFromAuthority 完成。
FCatFishingInputEdge UCatFishingCommandComponent::SubmitSlackPressed()
{
	FCatFishingInputEdge Edge = MakeDiscreteEdge();
	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	const ACatFishingRodActor* Rod = UCatFishingCameraComponent::FindFightRodHeldBy(Controller);
	if (!bLocalSlackHeld)
	{
		const ACatFishingRodActor* VisibleRod = Rod ? Rod : UCatFishingCameraComponent::FindHeldRodOperatedBy(Controller);
		const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
		LocalPitchAimRod = Rod;
		LocalPitchAimEpoch = Rod ? Rod->GetCarrierConstraintState().AimInputEpoch : 0;
		const double VisiblePitch = VisibleRod ? VisibleRod->GetGripWorldTransform().Rotator().Pitch
			: Controller ? Controller->GetControlRotation().Pitch : 0.0;
		LocalRequestedRodPitch = FMath::Clamp(FRotator::NormalizeAxis(VisiblePitch),
			Settings->HeldRodMinimumPitchDegrees, Settings->HeldRodMaximumPitchDegrees);
		bLocalPitchAimInitialized = true;
	}
	bLocalSlackHeld = true;
	Edge.RodAimSample = MakeRodAimSample(Rod);
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_slack_aim_requested RequestId=%s InputSequence=%lld RodActorId=%s AimInputEpoch=%u "
			"AimSequence=%lld CumulativeLookDegrees=%s MouseActive=%s MouseStrokeSequence=%lld MouseStrokeStartLookDegrees=%s %s"),
		*Edge.RequestId.ToString(), Edge.InputSequence, *Edge.RodAimSample.RodActorId.ToString(),
		Edge.RodAimSample.InputEpoch, Edge.RodAimSample.Sequence, *Edge.RodAimSample.CumulativeLookDegrees.ToString(),
		Edge.RodAimSample.bMouseActive ? TEXT("true") : TEXT("false"), Edge.RodAimSample.MouseStrokeSequence,
		*Edge.RodAimSample.MouseStrokeStartLookDegrees.ToString(),
		*BuildRodAimControllerFields(Controller));
	DispatchAbilityCommand(ECatFishingCommandType::SlackPressed, Edge);
	return Edge;
}

FCatFishingRodAimSample UCatFishingCommandComponent::MakeRodAimSample(const ACatFishingRodActor* Rod)
{
	FCatFishingRodAimSample Sample;
	Sample.Sequence = ++NextRodAimSequence;
	Sample.CumulativeLookDegrees = CumulativeRodLookDegrees;
	Sample.MouseStrokeSequence = MouseStrokeSequence;
	Sample.MouseStrokeStartLookDegrees = MouseStrokeStartLookDegrees;
	if (Rod)
	{
		Sample.RodActorId = Rod->GetPresentationState().RodActorId;
		Sample.InputEpoch = Rod->GetCarrierConstraintState().AimInputEpoch;
		// 右键只携带已有鼠标事实；构造样本本身不能开启活动或把上一根竿的活动带过来。
		Sample.bMouseActive = bLocalMouseActive && Sample.RodActorId == LocalMouseAimRodActorId
			&& Sample.InputEpoch == LocalMouseAimEpoch;
	}
	return Sample;
}

void UCatFishingCommandComponent::UpdateLocalRodAimInput(const double DeltaSeconds, const FRotator& LookDeltaDegrees)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->IsLocalController() || !GetWorld() || LookDeltaDegrees.ContainsNaN()
		|| !FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.0)
	{
		StopLocalRodAimInput();
		return;
	}
	// RotationInput 已经经过 AddYaw/PitchInput 的灵敏度与 IgnoreLookInput 处理。
	// 不用 ControlRotation 差量：旧隐藏目标顶到镜头 Pitch 限位后，仍必须能从实际竿角重新抬竿。
	const ACatFishingRodActor* Rod = UCatFishingCameraComponent::FindFightRodHeldBy(Controller);
	const FGuid RodActorId = Rod ? Rod->GetPresentationState().RodActorId : FGuid{};
	const uint32 AimEpoch = Rod ? Rod->GetCarrierConstraintState().AimInputEpoch : 0;
	const bool bHasAimDomain = RodActorId.IsValid() && AimEpoch != 0;
	const bool bDomainChanged = LocalMouseAimRodActorId != RodActorId || LocalMouseAimEpoch != AimEpoch;
	if (bDomainChanged)
	{
		// 先给旧域送停止，再绑定新域；已经离竿时服务器仍会复查归属，不会影响其他操作者。
		StopLocalRodAimInput();
		LocalMouseAimRodActorId = RodActorId;
		LocalMouseAimEpoch = AimEpoch;
	}
	const FRotator EffectiveLookDelta = Controller->IsLookInputIgnored() ? FRotator::ZeroRotator : LookDeltaDegrees;
	// 活动看夹限前的原始有效鼠标量。顶在Pitch边界仍在推鼠标，是用力而不是停手。
	const bool bMouseActive = bHasAimDomain
		&& (EffectiveLookDelta.Yaw != 0.0 || EffectiveLookDelta.Pitch != 0.0);
	const bool bNewStroke = bHasAimDomain && (bDomainChanged || (bMouseActive && !bLocalMouseActive));
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	if (!FMath::IsFinite(Settings->HeldRodMinimumPitchDegrees)
		|| !FMath::IsFinite(Settings->HeldRodMaximumPitchDegrees)
		|| Settings->HeldRodMinimumPitchDegrees > Settings->HeldRodMaximumPitchDegrees)
	{
		StopLocalRodAimInput();
		return;
	}
	if (bNewStroke)
	{
		++MouseStrokeSequence;
		// 起点一定先于本帧增量；首个可靠包迟到时，后续完整快照也能恢复本段的首帧输入。
		MouseStrokeStartLookDegrees = CumulativeRodLookDegrees;
		LocalPitchAimRod = Rod;
		LocalPitchAimEpoch = AimEpoch;
		LocalRequestedRodPitch = FMath::Clamp(FRotator::NormalizeAxis(Rod->GetGripWorldTransform().Rotator().Pitch),
			Settings->HeldRodMinimumPitchDegrees, Settings->HeldRodMaximumPitchDegrees);
		bLocalPitchAimInitialized = true;
	}
	double AppliedPitchDelta = EffectiveLookDelta.Pitch;
	// 尚未看到约束的右键仍可建立本地Pitch过滤；真正的新Rod/Epoch抵达时从可见姿态开新段。
	if (bLocalPitchAimInitialized && (LocalPitchAimEpoch == 0
		|| (Rod && LocalPitchAimRod.Get() == Rod && LocalPitchAimEpoch == AimEpoch)))
	{
		const double NextPitch = FMath::Clamp(LocalRequestedRodPitch + AppliedPitchDelta,
			Settings->HeldRodMinimumPitchDegrees, Settings->HeldRodMaximumPitchDegrees);
		AppliedPitchDelta = NextPitch - LocalRequestedRodPitch;
		LocalRequestedRodPitch = NextPitch;
	}
	else
	{
		LocalPitchAimRod.Reset();
		LocalPitchAimEpoch = 0;
		bLocalPitchAimInitialized = false;
	}
	const FVector2D NextCumulativeLook = CumulativeRodLookDegrees + FVector2D(EffectiveLookDelta.Yaw, AppliedPitchDelta);
	if (NextCumulativeLook.ContainsNaN() || FMath::Abs(NextCumulativeLook.X) >= 1.e12
		|| FMath::Abs(NextCumulativeLook.Y) >= 1.e12)
	{
		StopLocalRodAimInput();
		return;
	}
	CumulativeRodLookDegrees = NextCumulativeLook;
	const bool bTransition = bNewStroke || bLocalMouseActive != bMouseActive;
	bLocalMouseActive = bMouseActive;
	if (!bHasAimDomain)
	{
		RodAimSendElapsedSeconds = 0.0;
		return;
	}
	RodAimSendElapsedSeconds += DeltaSeconds;
	if (!Controller->HasAuthority() && !bTransition && RodAimSendElapsedSeconds < 1.0 / 30.0) return;
	RodAimSendElapsedSeconds = 0.0;
	const FCatFishingRodAimSample Sample = MakeRodAimSample(Rod);
	SendLocalRodAimSample(Sample, bTransition);
}

void UCatFishingCommandComponent::StopLocalRodAimInput()
{
	const bool bWasActive = bLocalMouseActive;
	bLocalMouseActive = false;
	RodAimSendElapsedSeconds = 0.0;
	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!bWasActive || !Controller || !Controller->IsLocalController() || !GetWorld()
		|| !LocalMouseAimRodActorId.IsValid() || LocalMouseAimEpoch == 0) return;
	FCatFishingRodAimSample Sample = MakeRodAimSample(nullptr);
	Sample.RodActorId = LocalMouseAimRodActorId;
	Sample.InputEpoch = LocalMouseAimEpoch;
	SendLocalRodAimSample(Sample, true);
}

void UCatFishingCommandComponent::SendLocalRodAimSample(const FCatFishingRodAimSample& Sample, const bool bTransition)
{
	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->IsLocalController() || !GetWorld()) return;
	if (Controller->HasAuthority()) HandleRodAimSampleFromAuthority(Sample, bTransition);
	else if (bTransition) ServerSubmitRodAimTransition(Sample);
	else ServerSubmitRodAimSample(Sample);
	// 启停当帧可靠发送，持续活动和静止心跳仍发30Hz全量快照；重发的idle不会恢复旧主动目标。
	if (bTransition || GetWorld()->GetTimeSeconds() >= NextLocalRodAimDiagnosticSeconds)
	{
		const FString Message = FString::Printf(
			TEXT("Event=%s RodActorId=%s AimInputEpoch=%u AimSequence=%lld CumulativeLookDegrees=%s "
				"MouseActive=%s MouseStrokeSequence=%lld MouseStrokeStartLookDegrees=%s Transition=%s Transport=%s %s"),
			bTransition ? TEXT("fishing_rod_aim_transition_sent") : TEXT("fishing_rod_aim_sent"),
			*Sample.RodActorId.ToString(), Sample.InputEpoch, Sample.Sequence, *Sample.CumulativeLookDegrees.ToString(),
			Sample.bMouseActive ? TEXT("true") : TEXT("false"), Sample.MouseStrokeSequence,
			*Sample.MouseStrokeStartLookDegrees.ToString(),
			bTransition ? (Sample.bMouseActive ? TEXT("Start") : TEXT("Stop")) : TEXT("Sample"),
			Controller->HasAuthority() ? TEXT("AuthorityDirect") : bTransition ? TEXT("Reliable") : TEXT("Unreliable"),
			*BuildRodAimControllerFields(Controller));
		if (bTransition) { UE_LOG(LogCatFishing, Display, TEXT("%s"), *Message); }
		else { UE_LOG(LogCatFishing, Log, TEXT("%s"), *Message); }
		NextLocalRodAimDiagnosticSeconds = GetWorld()->GetTimeSeconds() + 1.0;
	}
}

void UCatFishingCommandComponent::ServerSubmitRodAimSample_Implementation(const FCatFishingRodAimSample Sample)
{
	HandleRodAimSampleFromAuthority(Sample, false);
}

void UCatFishingCommandComponent::ServerSubmitRodAimTransition_Implementation(const FCatFishingRodAimSample Sample)
{
	HandleRodAimSampleFromAuthority(Sample, true);
}

void UCatFishingCommandComponent::HandleRodAimSampleFromAuthority(const FCatFishingRodAimSample& Sample, const bool bTransition)
{
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	UWorld* World = GetWorld();
	if (!Controller || !Controller->HasAuthority() || !World) return;
	UCatFishingService* Fishing = World->GetSubsystem<UCatFishingService>();
	ACatFishingRodActor* Rod = Fishing && Controller->PlayerState
		? Fishing->FindRodOperatedBy(Controller->PlayerState) : nullptr;
	const TCHAR* Result = TEXT("IgnoredStaleOrInactiveInput");
	if (!Sample.IsValid()) Result = TEXT("InvalidSample");
	else if (!Rod) Result = TEXT("NoOperatedRod");
	else if (!Sample.RodActorId.IsValid() || Sample.InputEpoch == 0
		|| Sample.RodActorId != Rod->GetPresentationState().RodActorId
		|| Sample.InputEpoch != Rod->GetCarrierConstraintState().AimInputEpoch) Result = TEXT("InputDomainMismatch");
	else if (Rod->AcceptHeldAimSampleFromAuthority(Controller->PlayerState, Sample)) Result = TEXT("Accepted");
	// 两个RPC与房主直连只在此收口；Actor/AimState独占权限、生命周期与Sequence的接受裁决。
	if (bTransition || World->GetTimeSeconds() >= NextServerRodAimDiagnosticSeconds)
	{
		const FString Message = FString::Printf(
			TEXT("Event=%s RodActorId=%s AimInputEpoch=%u AimSequence=%lld "
				"CumulativeLookDegrees=%s MouseActive=%s MouseStrokeSequence=%lld MouseStrokeStartLookDegrees=%s "
				"Transition=%s Result=%s %s"),
			bTransition ? TEXT("fishing_rod_aim_transition_received") : TEXT("fishing_rod_aim_received"),
			*Sample.RodActorId.ToString(), Sample.InputEpoch, Sample.Sequence, *Sample.CumulativeLookDegrees.ToString(),
			Sample.bMouseActive ? TEXT("true") : TEXT("false"), Sample.MouseStrokeSequence,
			*Sample.MouseStrokeStartLookDegrees.ToString(),
			bTransition ? (Sample.bMouseActive ? TEXT("Start") : TEXT("Stop")) : TEXT("Sample"),
			Result, *BuildRodAimControllerFields(Controller));
		if (FCString::Strcmp(Result, TEXT("InvalidSample")) == 0)
		{
			UE_LOG(LogCatFishing, Warning, TEXT("%s"), *Message);
		}
		else if (bTransition) { UE_LOG(LogCatFishing, Display, TEXT("%s"), *Message); }
		else { UE_LOG(LogCatFishing, Log, TEXT("%s"), *Message); }
		NextServerRodAimDiagnosticSeconds = World->GetTimeSeconds() + 1.0;
	}
}

FCatFishingInputEdge UCatFishingCommandComponent::SubmitSlackReleased()
{
	FCatFishingInputEdge Edge = MakeDiscreteEdge();
	bLocalSlackHeld = false;
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

FCatFishingInputEdge UCatFishingCommandComponent::SubmitCutLine()
{
	FCatFishingInputEdge Edge = MakeDiscreteEdge();
	DispatchAbilityCommand(ECatFishingCommandType::CutLine, Edge);
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

// 权威表现广播流程：只允许服务器从当前 Controller 的 Pawn 触发 multicast；缺少拥有者、非 authority 或事件未配置时直接跳过，避免客户端伪造全局表现。
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
	// 权威输入收口流程：
	// 1. 先验证拥有者、服务器权威和 RequestId，非法入口不产生任何结果。
	// 2. 再统一读取 Fishing 白天 gate；被关闭时回送 CommandsClosed，防止 UI 卡在等待态。
	// 3. gate 通过后才允许抄网/提竿表现及服务器抔网冷却裁决。
	// 4. 本函数只处理 Fishing/玩家打窝意图，Social、ready 和结算仍由 Controller 的宽玩法 gate 收口。
	APlayerController* Controller = Cast<APlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority() || !Edge.RequestId.IsValid())
	{
		return;
	}
	FCatFishingCommandResult Result;
	Result.CommandType = CommandType;
	Result.RequestId = Edge.RequestId;
	Result.bCommitted = false;
	UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr;
	const bool bRodControlEdge = CommandType == ECatFishingCommandType::RequestHook
		|| CommandType == ECatFishingCommandType::PrimaryReleased
		|| CommandType == ECatFishingCommandType::SlackPressed
		|| CommandType == ECatFishingCommandType::SlackReleased;
	if (bRodControlEdge)
	{
		ACatFishingRodActor* CurrentRod = Fishing ? Fishing->FindRodOperatedBy(Controller->PlayerState) : nullptr;
		// Resolve an explicit target before reading this player's control slot. Physical helpers have no slot.
		ACatFishingRodActor* TargetRod = Fishing && Edge.ControlRodActorId.IsValid()
			? Fishing->FindDeployedRodById(Edge.ControlRodActorId) : CurrentRod;
		const TCHAR* RejectReason = nullptr;
		if (Edge.ControlRodActorId.IsValid() && !TargetRod)
		{
			Result.Error = Fishing ? ECatFishingCommandError::NoRod : ECatFishingCommandError::DependencyUnavailable;
			RejectReason = Fishing ? TEXT("UnknownTargetRod") : TEXT("FishingServiceUnavailable");
		}
		else if (TargetRod)
		{
			const bool bOwner = Controller->PlayerState
				&& TargetRod->GetPresentationState().OwnerPlayerState == Controller->PlayerState;
			const bool bPrimary = bOwner && TargetRod->IsPrimaryOperator(Controller->PlayerState);
			const bool bCurrentControl = CurrentRod == TargetRod
				&& Edge.ControlRodActorId == TargetRod->GetPresentationState().RodActorId
				&& Edge.ControlEpoch != 0 && Edge.ControlEpoch == TargetRod->GetControlEpoch();
			if (!bPrimary || !bCurrentControl)
			{
				Result.Error = bPrimary ? ECatFishingCommandError::InputSequenceStale : ECatFishingCommandError::NotFisher;
				RejectReason = !bOwner ? TEXT("NotRodOwner") : !bPrimary ? TEXT("NotCurrentOperator") : TEXT("StaleControl");
			}
		}
		if (RejectReason)
		{
			Result.RodActorId = TargetRod ? TargetRod->GetPresentationState().RodActorId : Edge.ControlRodActorId;
			Result.RodActorRevision = TargetRod ? TargetRod->GetPresentationState().RodActorRevision : 0;
			if (const ACatFishingSession* BoundSession = Fishing && TargetRod ? Fishing->FindActiveSessionByRod(TargetRod) : nullptr)
			{
				Result.FishingSessionId = BoundSession->GetSnapshot().FishingSessionId;
				Result.Revision = BoundSession->GetSnapshot().Revision;
			}
			UE_LOG(LogCatFishing, Warning,
				TEXT("Event=fishing_control_input_rejected RequestId=%s RodActorId=%s SessionId=%s InputSequence=%lld InputControlEpoch=%u CurrentControlEpoch=%u Reason=%s World=%s Authority=%d LocalRole=%d %s"),
				*Edge.RequestId.ToString(), *Result.RodActorId.ToString(), *Result.FishingSessionId.ToString(), Edge.InputSequence,
				Edge.ControlEpoch, TargetRod ? TargetRod->GetControlEpoch() : 0, RejectReason,
				*GetNameSafe(GetWorld()), Controller->HasAuthority(), int32(Controller->GetLocalRole()),
				*CatLogContext::BuildControllerFields(Controller));
			DeliverResultFromAuthority(Result);
			return;
		}
	}

	// 只有当前操竿权下的边沿才能改持续按键；无竿时仍接受 Release 清除物理持有状态。
	TrackHeldFightInputFromAuthority(CommandType, Edge);
	if (CommandType == ECatFishingCommandType::CancelFishing)
	{
		// X 先作为通用身体动作取消键处理：只取消还停在 GAS 提交窗口里的 BodyAction，不会吞掉后续 Fishing 收竿/会话取消语义。
		if (UCatAbilitySystemComponent* AbilitySystem =
			UCatAbilitySystemComponent::FindCatAbilitySystemFromActor(Controller->GetPawn()))
		{
			AbilitySystem->CancelBodyActionAbilitiesFromAuthority();
		}
	}
	if (const ACatfishingPlayerController* CatController = Cast<ACatfishingPlayerController>(Controller);
		!CatController || !CatController->CanForwardFishingCommand())
	{
		// 钓鱼/玩家打窝只在 DayActive 且 bFishingAllowed 时开放；夜晚 ready、结算和 Social 继续走 GameMode 的宽 gate，不在这里误封。
		Result.Error = ECatFishingCommandError::CommandsClosed;
		DeliverResultFromAuthority(Result);
		return;
	}

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
				TEXT("Event=scoop_cooldown_rejected Request=%s RemainingSeconds=%.3f %s"),
				*Edge.RequestId.ToString(EGuidFormats::DigitsWithHyphens), RemainingSeconds,
				*CatLogContext::BuildControllerFields(Controller));
			DeliverResultFromAuthority(Result);
			return;
		}

		// 本地 Ability 已给发起者播放挥网；服务器在真正接受本次尝试后把动作广播给其他客户端。
		BroadcastCosmeticEventFromAuthority(CatFishingAbilityTags::Cosmetic_Fishing_ScoopSwing);
		const ACatCharacter* ScoopingCharacter = Cast<ACatCharacter>(Controller->GetPawn());
		ACatFishingSession* TargetSession = ScoopingCharacter
			? Fishing->FindNearestScoopableSession(ScoopingCharacter->GetActorLocation(), 1500.0) : nullptr;
		if (!TargetSession)
		{
			Result.Error = ECatFishingCommandError::NotNearShore;
			UE_LOG(LogCatFishing, Warning,
				TEXT("Event=scoop_target_selection_failed Request=%s Reason=NoEligibleSession SearchOrigin=%s MaxDistanceCm=1500.000 %s"),
				*Edge.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
				ScoopingCharacter ? *ScoopingCharacter->GetActorLocation().ToCompactString() : TEXT("None"),
				*CatLogContext::BuildControllerFields(Controller));
			DeliverResultFromAuthority(Result);
			return;
		}

		const FCatFishingSessionSnapshot& TargetSnapshot = TargetSession->GetSnapshot();
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=scoop_target_selected Request=%s SessionId=%s Phase=%s Revision=%lld SearchOrigin=%s FishLocation=%s DistanceCm=%.3f %s"),
			*Edge.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			*TargetSnapshot.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			*UEnum::GetValueAsString(TargetSnapshot.Phase), TargetSnapshot.Revision,
			ScoopingCharacter ? *ScoopingCharacter->GetActorLocation().ToCompactString() : TEXT("None"),
			TargetSnapshot.FishEncounterActor
				? *TargetSnapshot.FishEncounterActor->GetActorLocation().ToCompactString() : TEXT("None"),
			ScoopingCharacter && TargetSnapshot.FishEncounterActor
				? FVector::Dist(ScoopingCharacter->GetActorLocation(), TargetSnapshot.FishEncounterActor->GetActorLocation()) : -1.0,
			*CatLogContext::BuildControllerFields(Controller));
		Result.FishingSessionId = TargetSnapshot.FishingSessionId;
		FCatScoopCommand ScoopCommand;
		ScoopCommand.Context.RequestId = Edge.RequestId;
		ScoopCommand.Context.ExpectedRevision = TargetSnapshot.Revision;
		const FCatScoopResult ScoopResult = Fishing->RequestScoop(
			TargetSnapshot.FishingSessionId, Controller, ScoopCommand);
		Result.bCommitted = ScoopResult.Command.bCommitted;
		Result.Error = MapDomainCommandError(ScoopResult.Command.Error);
		const FCatFishingSessionSnapshot& UpdatedSnapshot = TargetSession->GetSnapshot();
		Result.Revision = UpdatedSnapshot.Revision;
		Result.SnapshotSequence = UpdatedSnapshot.SnapshotSequence;
		Result.PhaseEpoch = UpdatedSnapshot.PhaseEpoch;
		Result.CastAttemptId = UpdatedSnapshot.CastAttemptId;
		DeliverResultFromAuthority(Result);
		return;
	}
	if (Fishing)
	{
		// R 架住当前主控竿；空手优先拾回附近本人架竿，否则部署本人库存实体竿。
		if (CommandType == ECatFishingCommandType::OperateRod)
		{
			const ACatCharacter* Character = Cast<ACatCharacter>(Controller->GetPawn());
			// 分支一：正在操作某根竿 → R 表示"离开竿位"；会话生命周期归鱼竿，不因角色离开而写终态。
			if (ACatFishingRodActor* OperatedRod = Fishing->FindRodOperatedBy(Controller->PlayerState))
			{
				const FCatFishingRodPresentationState& OperatedState = OperatedRod->GetPresentationState();
				FCatLeaveRodCommand LeaveCommand;
				LeaveCommand.Context.RequestId = Edge.RequestId;
				LeaveCommand.Context.RodActorId = OperatedState.RodActorId;
				LeaveCommand.Context.ExpectedRodActorRevision = OperatedState.RodActorRevision;
				DeliverResultFromAuthority(Fishing->LeaveRod(Controller, LeaveCommand));
				return;
			}
			if (const UCatPhysicalBodyComponent* Body = Character ? Character->GetPhysicalBodyComponent() : nullptr)
			{
				for (const bool bLeft : {true, false})
				{
					UPrimitiveComponent* Target = Body->GetGrab()->GetGripTargetComponent(bLeft);
					auto* HeldOwnRod = Target ? Cast<ACatFishingRodActor>(Target->GetOwner()) : nullptr;
					if (!HeldOwnRod || HeldOwnRod->GetPresentationState().OwnerPlayerState != Controller->PlayerState) continue;
					FCatOperateRodCommand OperateCommand;
					OperateCommand.Context.RequestId = Edge.RequestId;
					OperateCommand.Context.RodActorId = HeldOwnRod->GetPresentationState().RodActorId;
					OperateCommand.Context.ExpectedRodActorRevision = HeldOwnRod->GetPresentationState().RodActorRevision;
					DeliverResultFromAuthority(Fishing->OperateRod(Controller, OperateCommand));
					return;
				}
			}
			if (ACatFishingRodActor* ParkedRod = Character
				? Fishing->FindNearestOperableOwnedRod(Controller->PlayerState, Character->GetActorLocation(), 250.0) : nullptr)
			{
				FCatOperateRodCommand OperateCommand;
				OperateCommand.Context.RequestId = Edge.RequestId;
				OperateCommand.Context.RodActorId = ParkedRod->GetPresentationState().RodActorId;
				OperateCommand.Context.ExpectedRodActorRevision = ParkedRod->GetPresentationState().RodActorRevision;
				DeliverResultFromAuthority(Fishing->OperateRod(Controller, OperateCommand));
				return;
			}
			const UCatInventoryComponent* OwnerInventory = Character ? Character->GetInventoryComponent() : nullptr;
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
		// 辅助位已在统一权限门禁拒绝；搏斗与力竭回收只接受当前主位的线杯输入。
		if (ACatFishingRodActor* OperatedRod = Fishing->FindRodOperatedBy(Controller->PlayerState))
		{
			if (ACatFishingSession* OperatedSession = Fishing->FindActiveSessionByRod(OperatedRod))
			{
				const FCatFishingSessionSnapshot& OperatedSnapshot = OperatedSession->GetSnapshot();
				if ((OperatedSnapshot.Phase == ECatFishingPhase::HookedFight
					|| OperatedSnapshot.Phase == ECatFishingPhase::ExhaustedReel)
					&& (CommandType == ECatFishingCommandType::RequestHook
						|| CommandType == ECatFishingCommandType::PrimaryReleased
						|| CommandType == ECatFishingCommandType::SlackPressed
						|| CommandType == ECatFishingCommandType::SlackReleased))
				{
					Result.FishingSessionId = OperatedSnapshot.FishingSessionId;
					if (CommandType == ECatFishingCommandType::RequestHook
						|| CommandType == ECatFishingCommandType::PrimaryReleased)
					{
						Result.bCommitted = OperatedSession->SetReelingFromAuthority(
							Controller->PlayerState, Edge.InputSequence,
							CommandType == ECatFishingCommandType::RequestHook);
					}
					else
					{
						Result.bCommitted = OperatedSession->SetSlackingFromAuthority(
							Controller->PlayerState, Edge.InputSequence,
							CommandType == ECatFishingCommandType::SlackPressed,
							CommandType == ECatFishingCommandType::SlackPressed ? &Edge.RodAimSample : nullptr, Edge.RequestId);
					}
					Result.Error = Result.bCommitted
						? ECatFishingCommandError::None : ECatFishingCommandError::InvalidPhase;
					DeliverResultFromAuthority(Result);
					return;
				}
			}
		}
		// 按当前主操作位对应的鱼竿判断是否有会话；玩家留在其他鱼竿上的会话不会截获这里的输入。
		if (!Fishing->TryGetActiveSessionForController(Controller, SessionId, Snapshot))
		{
			// 地面无人值守的上钩会话仍可就近切线：会话会再次校验竿主/最后持竿者和 250cm 距离。
			// 普通 Cancel 只在可切线阶段优先走止损；等待期仍保留后面的收竿/拒绝语义。
			if (CommandType == ECatFishingCommandType::CancelFishing
				|| CommandType == ECatFishingCommandType::CutLine)
			{
				const ACatCharacter* Character = Cast<ACatCharacter>(Controller->GetPawn());
				ACatFishingRodActor* UnattendedRod = Character
					? Fishing->FindNearestUnattendedSessionRod(Character->GetActorLocation(), 250.0) : nullptr;
				ACatFishingSession* UnattendedSession = UnattendedRod
					? Fishing->FindActiveSessionByRod(UnattendedRod) : nullptr;
				if (UnattendedSession)
				{
					const FCatFishingSessionSnapshot& Unattended = UnattendedSession->GetSnapshot();
					const bool bCuttable = Unattended.Phase == ECatFishingPhase::HookedFight
						|| Unattended.Phase == ECatFishingPhase::NearShore
						|| Unattended.Phase == ECatFishingPhase::ExhaustedReel
						|| Unattended.Phase == ECatFishingPhase::AutoHauling;
					if (bCuttable)
					{
						FCatFishingSessionCommandContext Context;
						Context.RequestId = Edge.RequestId;
						Context.FishingSessionId = Unattended.FishingSessionId;
						Context.ExpectedRevision = Unattended.Revision;
						Context.CastAttemptId = Unattended.CastAttemptId;
						DeliverResultFromAuthority(UnattendedSession->CutLineFromAuthority(Controller, Context));
						return;
					}
				}
				if (CommandType == ECatFishingCommandType::CutLine)
				{
					Result.Error = ECatFishingCommandError::SessionNotFound;
					DeliverResultFromAuthority(Result);
					return;
				}
			}
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
					BeginCastFromViewOnAuthority(Controller, Edge);
				}
				return;
			}
			// X 无会话 = 收竿回包（规格：咬钩前收竿零损失）。正在操作则先离开竿位再收。
			if (CommandType == ECatFishingCommandType::CancelFishing)
			{
				ACatFishingRodActor* Rod = Fishing->FindRodOperatedBy(Controller->PlayerState);
				// 多人：正在操作别人的竿 → X 只是离开竿位（不能收走别人的竿）。
				if (Rod)
				{
					const FCatFishingRodPresentationState& OperatedState = Rod->GetPresentationState();
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
				if (!Rod)
				{
					const ACatCharacter* Character = Cast<ACatCharacter>(Controller->GetPawn());
					Rod = Character ? Fishing->FindNearestPackableRod(Controller->PlayerState,
						Character->GetActorLocation(), 250.0) : nullptr;
				}
				if (!Rod)
				{
					// 压根没竿可收，直接返回 NoRod 错误
					Result.CommandType = ECatFishingCommandType::PackRod;
					Result.Error = ECatFishingCommandError::NoRod;
					DeliverResultFromAuthority(Result);
					return;
				}
				if (Rod->GetOperatorSlotIndex(Controller->PlayerState) != INDEX_NONE)
				{
					// 收竿前必须先释放操作权，否则竿处于“被占用”状态无法直接打包
					FCatLeaveRodCommand Leave;
					Leave.Context.RequestId = FGuid::NewGuid();
					Leave.Context.RodActorId = Rod->GetPresentationState().RodActorId;
					Leave.Context.ExpectedRodActorRevision = Rod->GetPresentationState().RodActorRevision;
					const FCatFishingCommandResult Left = Fishing->LeaveRod(Controller, Leave);
					if (!Left.bCommitted)
					{
						FCatFishingCommandResult Rejected = Left;
						Rejected.RequestId = Edge.RequestId;
						Rejected.CommandType = ECatFishingCommandType::PackRod;
						DeliverResultFromAuthority(Rejected);
						return;
					}
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
						Result.bCommitted = Session->SetReelingFromAuthority(
							Controller->PlayerState, Edge.InputSequence, true);
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
						Result.bCommitted = Session->SetReelingFromAuthority(
							Controller->PlayerState, Edge.InputSequence, false);
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
					// 回收沿用搏斗的线杯状态；跨越鱼力竭阶段的右键松开仍必须清除放线意图。
					if (Snapshot.Phase == ECatFishingPhase::HookedFight
						|| Snapshot.Phase == ECatFishingPhase::ExhaustedReel)
					{
						// 按下/松开都转成同一个权威写口，用命令类型本身当作“是否按下”的布尔值
						Result.bCommitted = Session->SetSlackingFromAuthority(Controller->PlayerState,
							Edge.InputSequence,
							CommandType == ECatFishingCommandType::SlackPressed,
							CommandType == ECatFishingCommandType::SlackPressed ? &Edge.RodAimSample : nullptr, Edge.RequestId);
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
				if (CommandType == ECatFishingCommandType::CancelFishing
					|| CommandType == ECatFishingCommandType::CutLine)
				{
					const bool bCuttablePhase = Snapshot.Phase == ECatFishingPhase::HookedFight
						|| Snapshot.Phase == ECatFishingPhase::NearShore
						|| Snapshot.Phase == ECatFishingPhase::ExhaustedReel
						|| Snapshot.Phase == ECatFishingPhase::AutoHauling;
					if (CommandType == ECatFishingCommandType::CutLine || bCuttablePhase)
					{
						FCatFishingSessionCommandContext Context;
						Context.RequestId = Edge.RequestId;
						Context.FishingSessionId = SessionId;
						Context.ExpectedRevision = Snapshot.Revision;
						Context.CastAttemptId = Snapshot.CastAttemptId;
						DeliverResultFromAuthority(Session->CutLineFromAuthority(Controller, Context));
					}
					else
					{
						// 上钩前仍保留普通取消：收回未形成鱼战的会话，不伪装成切线或丢鱼。
						DeliverResultFromAuthority(Session->CancelFromAuthority(Edge.RequestId));
					}
					return;
				}
			}
		}
	}
	// 玩家命令入口先保持单一拒绝出口；只有上方能重建出合法会话、鱼竿和载荷时，才允许返回成功回执。
	Result.Error = ECatFishingCommandError::DependencyUnavailable;
	DeliverResultFromAuthority(Result);
}


// 服务器抛竿流程：要求本人处于某根竿的主操作位（鱼竿可以属于别人）；视线射线∩水面得到候选落点；
// RodActorId/Revision、Equipment Revision、WaterRegion Handle 全部由服务器事实填充，客户端不传任何载荷。
void UCatFishingCommandComponent::BeginCastFromViewOnAuthority(APlayerController* Controller, const FCatFishingInputEdge& Edge)
{
	const FGuid RequestId = Edge.RequestId;
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
	UE_LOG(LogCatFishing, Log, TEXT("Event=cast_aim_request World=%s Request=%s HasViewRay=%d Origin=%s Direction=%s %s"),
		*GetNameSafe(GetWorld()), *RequestId.ToString(EGuidFormats::DigitsWithHyphens), Edge.bHasCastViewRay,
		*Edge.CastViewOrigin.ToString(), *Edge.CastViewDirection.ToString(), *CatLogContext::BuildControllerFields(Controller));
	if (!Edge.bHasCastViewRay || !UCatFishingAimLibrary::IsCastViewRayValid(Edge.CastViewOrigin,
		Edge.CastViewDirection, Character->GetPawnViewLocation(), Controller->GetControlRotation().Vector()))
	{
		Result.Command.Error = ECatFishingCommandError::InvalidPayload;
		DeliverBeginCastResultFromAuthority(Result);
		return;
	}
	if (!UCatFishingAimLibrary::ResolveCastAimPoint(this, Edge.CastViewOrigin,
		Edge.CastViewDirection.Rotation(), Region, Landing))
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

// 服务器打窝流程：按按住时长算蓄力 → 与客户端预览同一套弹道预测得到落点 → 优先从正式库存选一份可用窝料 → 交给 PlaceChum 做射程、夹角、视线、水域和正式库存提交。
void UCatFishingCommandComponent::ThrowChumFromChargeOnAuthority(APlayerController* Controller, const FGuid& RequestId,
	const double HeldSeconds)
{
	FCatPlaceChumResult Result;
	Result.RequestId = RequestId;
	ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr;
	const UCatInventoryComponent* OwnerInventory = Character ? Character->GetInventoryComponent() : nullptr;
	UCatChumPlacementService* Service = GetWorld() ? GetWorld()->GetSubsystem<UCatChumPlacementService>() : nullptr;
	if (!Character || !OwnerInventory || !Service)
	{
		Result.Error = ECatChumFieldError::DependencyUnavailable;
		DeliverPlaceChumResultFromAuthority(Result);
		return;
	}
	// 选窝料实例流程：只在正式库存条目里按槽位顺序找足量 Chum；命令层只携带 PlaceChum 复核所需的定义和实例。
	const int32 ChumQuantity = FMath::Max(1, GetDefault<UCatFishingSettings>()->ChumThrowQuantity);
	FName SelectedChumDefinitionId = NAME_None;
	FGuid SelectedChumItemInstanceId;
	bool bHasChumSlot = false;
	const auto TrySelectFormalChumSlot = [&](const UCatInventoryComponent& OwnerInventory,
		const FName RequiredDefinitionId)
	{
		// 正式库存自动选料：按槽位顺序读取实例和数量，避免 Equipment 选择投影决定本次要扣哪一堆窝料。
		for (int32 SlotIndex = 0; SlotIndex < OwnerInventory.GetInventorySlotCount(); ++SlotIndex)
		{
			const FCatInventoryEntry* Entry = OwnerInventory.GetInventoryEntryAtSlot(SlotIndex);
			const UCatInventoryItemInstance* Instance = Entry != nullptr ? Entry->Instance : nullptr;
			UCatEquipmentDefinition* Definition =
				Instance != nullptr ? Cast<UCatEquipmentDefinition>(Instance->GetItemDefinition()) : nullptr;
			if (Instance != nullptr
				&& Entry->StackCount >= ChumQuantity
				&& (RequiredDefinitionId.IsNone() || Instance->GetItemDefinitionId() == RequiredDefinitionId)
				&& Definition != nullptr
				&& Definition->IsRuntimeDefinitionReady()
				&& Definition->CanServeChumPlacement()
				&& Instance->ConsumesInventoryQuantityOnUse())
			{
				SelectedChumDefinitionId = Instance->GetItemDefinitionId();
				SelectedChumItemInstanceId = Instance->GetItemInstanceId();
				bHasChumSlot = true;
				return true;
			}
		}
		return false;
	};
	TrySelectFormalChumSlot(*OwnerInventory, NAME_None);
	if (!bHasChumSlot)
	{
		// 库存里没有一份能完整支付本次投放数量的窝料实例，直接拒绝，不进入弹道计算。
		int32 OccupiedSlotCount = 0;
		int32 ReadyChumSlotCount = 0;
		int32 InsufficientReadyChumSlotCount = 0;
		int32 InvalidChumCandidateSlotCount = 0;
		for (int32 SlotIndex = 0; SlotIndex < OwnerInventory->GetInventorySlotCount(); ++SlotIndex)
		{
			const FCatInventoryEntry* Entry = OwnerInventory->GetInventoryEntryAtSlot(SlotIndex);
			const UCatInventoryItemInstance* Instance = Entry != nullptr ? Entry->Instance : nullptr;
			if (Instance == nullptr || Entry->StackCount <= 0)
			{
				continue;
			}
			++OccupiedSlotCount;
			UCatEquipmentDefinition* Definition =
				Cast<UCatEquipmentDefinition>(Instance->GetItemDefinition());
			const bool bReadyChum = Definition != nullptr
				&& Definition->IsRuntimeDefinitionReady()
				&& Definition->CanServeChumPlacement()
				&& Instance->ConsumesInventoryQuantityOnUse();
			if (!bReadyChum)
			{
				++InvalidChumCandidateSlotCount;
				continue;
			}
			if (Entry->StackCount < ChumQuantity)
			{
				++InsufficientReadyChumSlotCount;
				continue;
			}
			++ReadyChumSlotCount;
		}
		Result.Error = ECatChumFieldError::EquipmentUnavailable;
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=chum_throw_chum_unavailable RequiredQuantity=%d InventorySlots=%d OccupiedSlots=%d ReadyChumSlots=%d InsufficientReadyChumSlots=%d InvalidCandidateSlots=%d"),
			ChumQuantity,
			OwnerInventory->GetInventorySlotCount(), OccupiedSlotCount, ReadyChumSlotCount,
			InsufficientReadyChumSlotCount, InvalidChumCandidateSlotCount);
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
	// 组装真正的打窝命令；服务端按实际库存实例和数量裁决窝料扣量。
	FCatPlaceChumCommand Command;
	Command.RequestId = RequestId;
	Command.ExpectedWaterRegionHandle = Region;
	Command.ChumItemInstanceId = SelectedChumItemInstanceId;
	Command.ChumDefinitionId = SelectedChumDefinitionId;
	Command.Quantity = ChumQuantity;
	Command.ClientCandidateWorldPoint = Landing;
	UE_LOG(LogCatFishing, Log, TEXT("Event=chum_throw Held=%.2f Alpha=%.2f Landing=%s Chum=%s ChumItem=%s"),
		HeldSeconds, Alpha, *Landing.ToString(), *SelectedChumDefinitionId.ToString(),
		*SelectedChumItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens));
	DeliverPlaceChumResultFromAuthority(Service->PlaceChum(Controller, Command));
}

// 旧版搏斗协作转发流程：先复查 Fishing 白天 gate，再把会话键、幂等键和期望 Revision 交给 Fishing Service；Session 继续裁 Giant、阶段和版本。
void UCatFishingCommandComponent::ForwardLegacyAssist(const FGuid FishingSessionId, const FGuid RequestId,
	const int64 ExpectedRevision)
{
	ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(GetOwner());
	if (Controller && Controller->HasAuthority() && Controller->CanForwardFishingCommand())
	{
		if (UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr)
		{
			Fishing->SubmitFightAssist(FishingSessionId, Controller, RequestId, ExpectedRevision);
		}
	}
}

// 旧版抢抄 RPC 兼容流程：保留显式 SessionId/ExpectedRevision，但服务器重建身份且不接受任何容器目标。
void UCatFishingCommandComponent::ForwardLegacyScoop(const FGuid FishingSessionId, FCatScoopCommand Command)
{
	ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority() || !Controller->CanForwardFishingCommand())
	{
		return;
	}
	FCatFishingCommandResult Result;
	Result.CommandType = ECatFishingCommandType::RequestScoop;
	Result.RequestId = Command.Context.RequestId;
	Result.FishingSessionId = FishingSessionId;
	double CooldownSeconds = 0.0;
	double RemainingSeconds = 0.0;
	if (!GetDefault<UCatFishingSettings>()->TryGetScoopCooldown(CooldownSeconds)
		|| !ScoopCooldownGate.TryConsume(GetWorld()->GetTimeSeconds(), CooldownSeconds, RemainingSeconds))
	{
		Result.Error = CooldownSeconds > 0.0
			? ECatFishingCommandError::CooldownActive : ECatFishingCommandError::DependencyUnavailable;
		DeliverResultFromAuthority(Result);
		return;
	}

	Command.Context.StableNetId.Reset();
	BroadcastCosmeticEventFromAuthority(CatFishingAbilityTags::Cosmetic_Fishing_ScoopSwing);
	if (UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr)
	{
		const FCatScoopResult ScoopResult = Fishing->RequestScoop(FishingSessionId, Controller, Command);
		Result.bCommitted = ScoopResult.Command.bCommitted;
		Result.Error = MapDomainCommandError(ScoopResult.Command.Error);
		Result.Revision = ScoopResult.Command.Revision;
		if (ACatFishingSession* Session = Fishing->FindSession(FishingSessionId))
		{
			const FCatFishingSessionSnapshot& UpdatedSnapshot = Session->GetSnapshot();
			Result.Revision = UpdatedSnapshot.Revision;
			Result.SnapshotSequence = UpdatedSnapshot.SnapshotSequence;
			Result.PhaseEpoch = UpdatedSnapshot.PhaseEpoch;
			Result.CastAttemptId = UpdatedSnapshot.CastAttemptId;
		}
	}
	else
	{
		Result.Error = ECatFishingCommandError::DependencyUnavailable;
	}
	DeliverResultFromAuthority(Result);
}

// 显式打窝 RPC 流程：先保留 RequestId，再用 Fishing 白天 gate 裁阶段；gate 关闭也回送 CommandsClosed，合法路径才进入 ChumPlacementService 的水域、库存和幂等校验。
void UCatFishingCommandComponent::ServerSubmitPlaceChum_Implementation(const FCatPlaceChumCommand& Command)
{
	ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(GetOwner());
	FCatPlaceChumResult Result;
	Result.RequestId = Command.RequestId;
	if (!Controller || !Controller->HasAuthority()) return;
	if (!Controller->CanForwardFishingCommand())
	{
		// 显式 PlaceChum RPC 与 Q 蓄力路径共用同一个白天 gate；拒绝也投递终态，避免 UI 在夜晚挂着 pending。
		Result.Error = ECatChumFieldError::CommandsClosed;
		DeliverPlaceChumResultFromAuthority(Result);
		return;
	}
	UCatChumPlacementService* Service = GetWorld()
		? GetWorld()->GetSubsystem<UCatChumPlacementService>() : nullptr;
	if (Service) Result = Service->PlaceChum(Controller, Command);
	DeliverPlaceChumResultFromAuthority(Result);
}

// 显式抛竿 RPC 流程：先构造 BeginCast 回执，再用 Fishing 白天 gate 裁阶段；gate 关闭回送 CommandsClosed，合法路径才交 Fishing Service 重做射程、视线和装备校验。
void UCatFishingCommandComponent::ServerSubmitBeginCast_Implementation(const FCatBeginCastCommand& Command)
{
	ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(GetOwner());
	FCatBeginCastResult Result;
	Result.Command.CommandType = ECatFishingCommandType::BeginCast;
	Result.Command.RequestId = Command.RequestId;
	if (!Controller || !Controller->HasAuthority()) return;
	if (!Controller->CanForwardFishingCommand())
	{
		Result.Command.Error = ECatFishingCommandError::CommandsClosed;
		DeliverBeginCastResultFromAuthority(Result);
		return;
	}
	if (UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr)
	{
		Result = Fishing->BeginCast(Controller, Command);
	}
	DeliverBeginCastResultFromAuthority(Result);
}

// 旧式放竿入口流程：先校验拥有者和服务器权威；Fishing gate 关闭时用命令本体 RequestId 回送 CommandsClosed，gate 通过后才交 Fishing Service 裁决鱼竿占用、版本和装备状态。
void UCatFishingCommandComponent::ServerSubmitPlaceRod_Implementation(const FCatPlaceRodCommand& Command)
{
	ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority()) return;
	if (!Controller->CanForwardFishingCommand())
	{
		DeliverResultFromAuthority(MakeRodCommandsClosedResult(ECatFishingCommandType::PlaceRod, Command.RequestId));
		return;
	}
	UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr;
	if (Fishing) DeliverResultFromAuthority(Fishing->PlaceRod(Controller, Command));
}

// 旧式操作竿入口流程：沿用 Command.Context.RequestId 作为回执键；阶段 gate 关闭时只返回 CommandsClosed，不让旧 Ability 静默等待或绕过服务层状态裁决。
void UCatFishingCommandComponent::ServerSubmitOperateRod_Implementation(const FCatOperateRodCommand& Command)
{
	ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority()) return;
	if (!Controller->CanForwardFishingCommand())
	{
		DeliverResultFromAuthority(MakeRodCommandsClosedResult(ECatFishingCommandType::OperateRod, Command.Context.RequestId));
		return;
	}
	UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr;
	if (Fishing) DeliverResultFromAuthority(Fishing->OperateRod(Controller, Command));
}

// 旧式离竿入口流程：先走同一 Fishing gate；关闭时按 Context.RequestId 写入失败终态，开放时才由 Fishing Service 检查会话归属和可离开边界。
void UCatFishingCommandComponent::ServerSubmitLeaveRod_Implementation(const FCatLeaveRodCommand& Command)
{
	ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority()) return;
	if (!Controller->CanForwardFishingCommand())
	{
		DeliverResultFromAuthority(MakeRodCommandsClosedResult(ECatFishingCommandType::LeaveRod, Command.Context.RequestId));
		return;
	}
	UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr;
	if (Fishing) DeliverResultFromAuthority(Fishing->LeaveRod(Controller, Command));
}

// 旧式收竿入口流程：关闭 gate 返回 PackRod/Context.RequestId 对应的 CommandsClosed；开放路径仍交服务层处理装备和竿状态，不在组件里复制业务判断。
void UCatFishingCommandComponent::ServerSubmitPackRod_Implementation(const FCatPackRodCommand& Command)
{
	ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority()) return;
	if (!Controller->CanForwardFishingCommand())
	{
		DeliverResultFromAuthority(MakeRodCommandsClosedResult(ECatFishingCommandType::PackRod, Command.Context.RequestId));
		return;
	}
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

	if (Result.CommandType == ECatFishingCommandType::SlackPressed || Result.CommandType == ECatFishingCommandType::SlackReleased)
	{
		// 只确认按键结果；迟到回执不能再重设转向，更不能抹掉按下后的鼠标输入。
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=fishing_slack_aim_result_received RequestId=%s SessionId=%s Command=%s Committed=%s Error=%s %s"),
			*Result.RequestId.ToString(), *Result.FishingSessionId.ToString(), *UEnum::GetValueAsString(Result.CommandType),
			Result.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Error),
			*BuildRodAimControllerFields(Cast<APlayerController>(GetOwner())));
	}
	if (Result.CommandType == ECatFishingCommandType::PlaceRod
		|| Result.CommandType == ECatFishingCommandType::OperateRod
		|| Result.CommandType == ECatFishingCommandType::LeaveRod)
	{
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=fishing_rod_result_received RequestId=%s RodActorId=%s RodActorRevision=%lld Command=%s Committed=%s Error=%s %s"),
			*Result.RequestId.ToString(), *Result.RodActorId.ToString(), Result.RodActorRevision,
			*UEnum::GetValueAsString(Result.CommandType), Result.bCommitted ? TEXT("true") : TEXT("false"),
			*UEnum::GetValueAsString(Result.Error), *BuildRodAimControllerFields(Cast<APlayerController>(GetOwner())));
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
	// 同时投影出一份“通用命令结果”；打窝版本来自窝料场集合。
	FCatFishingCommandResult Common;
	Common.CommandType = ECatFishingCommandType::PlaceChum;
	Common.bCommitted = Result.bCommitted;
	Common.RequestId = Result.RequestId;
	Common.Revision = Result.ChumFieldSetRevision;
	// 打窝子系统用自己的一套错误码，这里逐一映射到通用命令错误码，语义不对齐时归为 DependencyUnavailable。
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
	case ECatChumFieldError::AlreadyResolved: Common.Error = ECatFishingCommandError::AlreadyResolved; break;
	default: Common.Error = ECatFishingCommandError::DependencyUnavailable; break;
	}
	ReceiveResultLocally(Common);
}

void UCatFishingCommandComponent::ReceiveBeginCastResultLocally(const FCatBeginCastResult& Result)
{
	UE_LOG(LogCatFishing, Log, TEXT("Event=begin_cast_received World=%s Request=%s Session=%s CastAttempt=%s Committed=%d Error=%s Landing=%s %s"),
		*GetNameSafe(GetWorld()), *Result.Command.RequestId.ToString(EGuidFormats::DigitsWithHyphens), *Result.Command.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
		*Result.Command.CastAttemptId.ToString(EGuidFormats::DigitsWithHyphens), Result.Command.bCommitted, *UEnum::GetValueAsString(Result.Command.Error),
		*Result.ServerCorrectedLandingWorldPoint.ToString(), *CatLogContext::BuildControllerFields(Cast<AController>(GetOwner())));
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
