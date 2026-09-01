#include "Environment/Presentation/CatEnvironmentPresentationActor.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Framework/Game/CatGameplayTypes.h"

namespace
{
	// 表现阶段折叠流程：白天细分来自 Environment 的 TimeOfDay，夜晚来自 Run Phase；缺事实时保持 Unavailable，避免蓝图猜测。
	ECatEnvironmentPresentationPhase ResolveEnvironmentPresentationPhase(const ECatRunPhase RunPhase,
		const ECatEnvironmentTimeOfDay TimeOfDay)
	{
		if (RunPhase == ECatRunPhase::DayActive)
		{
			switch (TimeOfDay)
			{
			case ECatEnvironmentTimeOfDay::Morning:
				return ECatEnvironmentPresentationPhase::Morning;
			case ECatEnvironmentTimeOfDay::Day:
				return ECatEnvironmentPresentationPhase::Day;
			case ECatEnvironmentTimeOfDay::Dusk:
				return ECatEnvironmentPresentationPhase::Dusk;
			case ECatEnvironmentTimeOfDay::Unknown:
			default:
				return ECatEnvironmentPresentationPhase::Unavailable;
			}
		}
		if (RunPhase == ECatRunPhase::NormalNight)
		{
			return ECatEnvironmentPresentationPhase::Night;
		}
		if (RunPhase == ECatRunPhase::FailureSettlementNight || RunPhase == ECatRunPhase::SuccessSettlementNight)
		{
			return ECatEnvironmentPresentationPhase::SettlementNight;
		}
		if (RunPhase == ECatRunPhase::Ending || RunPhase == ECatRunPhase::Ended)
		{
			return ECatEnvironmentPresentationPhase::Ended;
		}
		return ECatEnvironmentPresentationPhase::Unavailable;
	}

	// 白天进度计算流程：只消费公开的服务器锚点和截止秒；非法区间返回 0，让表现层关闭连续过渡。
	double CalculateDayProgress(const FCatRunPhaseSnapshot& RunPhase, const double ServerNowSeconds)
	{
		if (RunPhase.Phase != ECatRunPhase::DayActive || !RunPhase.bHasDeadline
			|| !FMath::IsFinite(ServerNowSeconds) || !FMath::IsFinite(RunPhase.ServerTimeAnchorSeconds)
			|| !FMath::IsFinite(RunPhase.DeadlineServerTimeSeconds)
			|| RunPhase.DeadlineServerTimeSeconds <= RunPhase.ServerTimeAnchorSeconds)
		{
			return 0.0;
		}
		return FMath::Clamp((ServerNowSeconds - RunPhase.ServerTimeAnchorSeconds)
			/ (RunPhase.DeadlineServerTimeSeconds - RunPhase.ServerTimeAnchorSeconds), 0.0, 1.0);
	}
}

// 构造流程：创建一个不复制、不碰撞的本地表现根；Tick 默认开启只是为了连续推送 DayProgress，离散状态仍等 GameState 通知。
ACatEnvironmentPresentationActor::ACatEnvironmentPresentationActor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	SetRootComponent(VisualRoot);
	VisualRoot->SetCanEverAffectNavigation(false);
	SetActorEnableCollision(false);
}

// BeginPlay 流程：专用服务器不需要本地表现；客户端或 listen server 本机先绑定 GameState，再把已有复制值推给蓝图。
void ACatEnvironmentPresentationActor::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer)
	{
		SetActorTickEnabled(false);
		return;
	}
	RefreshGameStateBinding();
	ApplyPresentationState();
}

// Tick 流程：必要时重试 GameState 绑定，并在允许连续推送时用当前服务器时间刷新表现进度；不修改缓存里的公开状态。
void ACatEnvironmentPresentationActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	if (!BoundGameState.IsValid())
	{
		RefreshGameStateBinding();
	}
	if (bApplyEveryTick)
	{
		ApplyPresentationState();
	}
}

// EndPlay 流程：先解除 GameState 委托再进入父类收口，避免蓝图表现 Actor 在 World 销毁阶段收到旧快照。
void ACatEnvironmentPresentationActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearGameStateBinding();
	Super::EndPlay(EndPlayReason);
}

// 读取流程：返回最近蓝图投影的值拷贝；外部读取不会接触 GameState 委托或玩法真相。
FCatEnvironmentPresentationState ACatEnvironmentPresentationActor::GetPresentationState() const
{
	return PresentationState;
}

// 绑定流程：按当前 World 找到唯一 GameState；如果目标发生变化先解绑旧委托，再缓存当前公开快照并监听后续复制。
bool ACatEnvironmentPresentationActor::RefreshGameStateBinding()
{
	UWorld* World = GetWorld();
	ACatfishingGameState* CurrentGameState = World ? World->GetGameState<ACatfishingGameState>() : nullptr;
	if (!CurrentGameState)
	{
		return false;
	}
	if (BoundGameState.Get() == CurrentGameState)
	{
		CachedRunPublicState = CurrentGameState->GetRunPublicState();
		bHasCachedRunPublicState = true;
		return true;
	}
	ClearGameStateBinding();
	BoundGameState = CurrentGameState;
	RunPublicStateChangedHandle = CurrentGameState->OnRunPublicStateChanged.AddUObject(
		this, &ThisClass::HandleRunPublicStateChanged);
	CachedRunPublicState = CurrentGameState->GetRunPublicState();
	bHasCachedRunPublicState = true;
	return true;
}

// 清理流程：只移除本 Actor 加到 GameState 的 Run 快照订阅，并清掉本地缓存和投影，防止重绑时混用旧世界状态。
void ACatEnvironmentPresentationActor::ClearGameStateBinding()
{
	if (ACatfishingGameState* GameState = BoundGameState.Get())
	{
		GameState->OnRunPublicStateChanged.Remove(RunPublicStateChangedHandle);
	}
	RunPublicStateChangedHandle.Reset();
	BoundGameState.Reset();
	CachedRunPublicState = FCatRunPublicState();
	bHasCachedRunPublicState = false;
	PresentationState = FCatEnvironmentPresentationState();
}

// Run 快照变化流程：从 GameState 重读整份组合事实，然后把新投影交给蓝图；不按旧状态做增量推断。
void ACatEnvironmentPresentationActor::HandleRunPublicStateChanged()
{
	if (const ACatfishingGameState* GameState = BoundGameState.Get())
	{
		CachedRunPublicState = GameState->GetRunPublicState();
		bHasCachedRunPublicState = true;
		ApplyPresentationState();
	}
}

// 投影构建流程：从同一份 RunPublicState 拷贝天数、阶段、天气、事件和 Revision，再按公开服务器时间计算连续白天进度。
FCatEnvironmentPresentationState ACatEnvironmentPresentationActor::BuildPresentationState(
	const double ServerNowSeconds) const
{
	FCatEnvironmentPresentationState State;
	State.ServerNowSeconds = ServerNowSeconds;
	if (!bHasCachedRunPublicState)
	{
		return State;
	}
	State.RunId = CachedRunPublicState.Phase.RunId;
	State.DayIndex = CachedRunPublicState.Phase.DayIndex;
	State.RunPhase = CachedRunPublicState.Phase.Phase;
	State.Weather = CachedRunPublicState.Environment.Weather;
	State.TimeOfDay = CachedRunPublicState.Environment.TimeOfDay;
	State.bHasActiveEvent = CachedRunPublicState.Environment.bHasActiveEvent;
	State.ActiveEventId = CachedRunPublicState.Environment.ActiveEventId;
	State.DayProgress = CalculateDayProgress(CachedRunPublicState.Phase, ServerNowSeconds);
	State.RunRevision = CachedRunPublicState.Revision;
	State.PresentationPhase = ResolveEnvironmentPresentationPhase(State.RunPhase, State.TimeOfDay);
	return State;
}

// 应用流程：优先用 GameState 提供的服务器世界时间生成本地投影；GameState 不可用时清旧缓存并保持静默，成功后只调用蓝图表现事件。
void ACatEnvironmentPresentationActor::ApplyPresentationState()
{
	if (!BoundGameState.IsValid())
	{
		ClearGameStateBinding();
	}
	if (!bHasCachedRunPublicState && !RefreshGameStateBinding())
	{
		return;
	}
	const ACatfishingGameState* GameState = BoundGameState.Get();
	UWorld* World = GetWorld();
	const double ServerNowSeconds = GameState ? GameState->GetServerWorldTimeSeconds()
		: (World ? World->GetTimeSeconds() : 0.0);
	PresentationState = BuildPresentationState(ServerNowSeconds);
	BP_ApplyEnvironmentPresentation(PresentationState);
}
