#include "Environment/Presentation/CatEnvironmentPresentationActor.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Environment/CatEnvironmentSettings.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "Logging/CatLog.h"

namespace
{
	constexpr float DefaultMorningEndFraction = 0.3f;
	constexpr float DefaultDuskStartFraction = 0.8f;
	const FName SunLightTag(TEXT("Sun"));
	const FName CatSunLightTag(TEXT("CatSun"));
	const FName MoonLightTag(TEXT("Moon"));
	const FName CatMoonLightTag(TEXT("CatMoon"));
	const FName SkyLightTag(TEXT("SkyLight"));
	const FName CatSkyLightTag(TEXT("CatSkyLight"));
	const FName HeightFogTag(TEXT("HeightFog"));
	const FName CatHeightFogTag(TEXT("CatHeightFog"));
	const FName PostProcessTag(TEXT("PostProcess"));
	const FName CatPostProcessTag(TEXT("CatPostProcess"));

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

	// Actor 可用性检查：自动发现和应用都只认还在当前 World 生命周期里的场景对象。
	template <typename TActor>
	bool IsUsableSceneActor(const TActor* Actor)
	{
		return IsValid(Actor);
	}

	// Tag 查找流程：用于给关卡侧一个稳定命名入口；找不到时调用方再退回同类第一个对象。
	template <typename TActor>
	TActor* FindFirstSceneActorWithAnyTag(UWorld* World, const FName FirstTag, const FName SecondTag,
		const AActor* ExcludedActor = nullptr)
	{
		if (!World)
		{
			return nullptr;
		}
		for (TActorIterator<TActor> It(World); It; ++It)
		{
			TActor* Candidate = *It;
			if (IsUsableSceneActor(Candidate) && Candidate != ExcludedActor
				&& (Candidate->ActorHasTag(FirstTag) || Candidate->ActorHasTag(SecondTag)))
			{
				return Candidate;
			}
		}
		return nullptr;
	}

	// 同类兜底查找流程：保证没有专门 Tag 的旧关卡也能先跑起来，但不会覆盖已手动指定的对象。
	template <typename TActor>
	TActor* FindFirstUsableSceneActor(UWorld* World, const AActor* ExcludedActor = nullptr)
	{
		if (!World)
		{
			return nullptr;
		}
		for (TActorIterator<TActor> It(World); It; ++It)
		{
			TActor* Candidate = *It;
			if (IsUsableSceneActor(Candidate) && Candidate != ExcludedActor)
			{
				return Candidate;
			}
		}
		return nullptr;
	}

	// 后处理兜底查找流程：优先全局体，避免找到一个玩家不在体积内的局部 PostProcessVolume。
	APostProcessVolume* FindBestPostProcessVolume(UWorld* World)
	{
		APostProcessVolume* FirstUsableVolume = nullptr;
		if (!World)
		{
			return nullptr;
		}
		for (TActorIterator<APostProcessVolume> It(World); It; ++It)
		{
			APostProcessVolume* Candidate = *It;
			if (!IsUsableSceneActor(Candidate))
			{
				continue;
			}
			if (Candidate->ActorHasTag(PostProcessTag) || Candidate->ActorHasTag(CatPostProcessTag))
			{
				return Candidate;
			}
			if (!FirstUsableVolume)
			{
				FirstUsableVolume = Candidate;
			}
			if (Candidate->bUnbound)
			{
				return Candidate;
			}
		}
		return FirstUsableVolume;
	}

	// 数值混合流程：只混表现参数，不混 Run 状态；旋转使用四元数插值避免角度跨零时猛跳。
	FCatEnvironmentScenePresentationValues LerpScenePresentationValues(
		const FCatEnvironmentScenePresentationValues& From,
		const FCatEnvironmentScenePresentationValues& To,
		const float Alpha)
	{
		const float ClampedAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
		FCatEnvironmentScenePresentationValues Result;
		Result.SunIntensity = FMath::Lerp(From.SunIntensity, To.SunIntensity, ClampedAlpha);
		Result.SunColor = FLinearColor::LerpUsingHSV(From.SunColor, To.SunColor, ClampedAlpha);
		Result.SunRotation = FQuat::Slerp(From.SunRotation.Quaternion(), To.SunRotation.Quaternion(),
			ClampedAlpha).Rotator();
		Result.MoonIntensity = FMath::Lerp(From.MoonIntensity, To.MoonIntensity, ClampedAlpha);
		Result.MoonColor = FLinearColor::LerpUsingHSV(From.MoonColor, To.MoonColor, ClampedAlpha);
		Result.MoonRotation = FQuat::Slerp(From.MoonRotation.Quaternion(), To.MoonRotation.Quaternion(),
			ClampedAlpha).Rotator();
		Result.SkyLightIntensity = FMath::Lerp(From.SkyLightIntensity, To.SkyLightIntensity, ClampedAlpha);
		Result.FogDensity = FMath::Lerp(From.FogDensity, To.FogDensity, ClampedAlpha);
		Result.FogColor = FLinearColor::LerpUsingHSV(From.FogColor, To.FogColor, ClampedAlpha);
		Result.ExposureBias = FMath::Lerp(From.ExposureBias, To.ExposureBias, ClampedAlpha);
		Result.SceneTint = FLinearColor::LerpUsingHSV(From.SceneTint, To.SceneTint, ClampedAlpha);
		return Result;
	}

	// 阶段名转换流程：日志只需要人能读懂的枚举名，不把它变成新的同步字段。
	FString FormatEnvironmentPresentationPhase(const ECatEnvironmentPresentationPhase Phase)
	{
		const UEnum* Enum = StaticEnum<ECatEnvironmentPresentationPhase>();
		return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Phase)) : TEXT("Unknown");
	}
}

// 构造流程：创建一个不复制、不碰撞的本地表现根；再写入一套可见的昼夜默认值，供没有蓝图资产时也能驱动原生灯光。
ACatEnvironmentPresentationActor::ACatEnvironmentPresentationActor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	SetRootComponent(VisualRoot);
	VisualRoot->SetCanEverAffectNavigation(false);
	SetActorEnableCollision(false);

	MorningSceneValues.SunIntensity = 2.8f;
	MorningSceneValues.SunColor = FLinearColor(1.0f, 0.58f, 0.34f, 1.0f);
	MorningSceneValues.SunRotation = FRotator(-10.0f, -70.0f, 0.0f);
	MorningSceneValues.MoonIntensity = 0.02f;
	MorningSceneValues.MoonRotation = FRotator(-18.0f, 110.0f, 0.0f);
	MorningSceneValues.SkyLightIntensity = 0.45f;
	MorningSceneValues.FogDensity = 0.018f;
	MorningSceneValues.FogColor = FLinearColor(0.84f, 0.78f, 0.70f, 1.0f);
	MorningSceneValues.ExposureBias = -0.15f;
	MorningSceneValues.SceneTint = FLinearColor(1.0f, 0.86f, 0.74f, 1.0f);

	DaySceneValues.SunIntensity = 10.0f;
	DaySceneValues.SunColor = FLinearColor(1.0f, 0.96f, 0.84f, 1.0f);
	DaySceneValues.SunRotation = FRotator(-45.0f, -35.0f, 0.0f);
	DaySceneValues.MoonIntensity = 0.0f;
	DaySceneValues.SkyLightIntensity = 1.0f;
	DaySceneValues.FogDensity = 0.010f;
	DaySceneValues.FogColor = FLinearColor(0.78f, 0.86f, 1.0f, 1.0f);
	DaySceneValues.ExposureBias = 0.25f;
	DaySceneValues.SceneTint = FLinearColor::White;

	DuskSceneValues.SunIntensity = 1.8f;
	DuskSceneValues.SunColor = FLinearColor(1.0f, 0.38f, 0.20f, 1.0f);
	DuskSceneValues.SunRotation = FRotator(-6.0f, 48.0f, 0.0f);
	DuskSceneValues.MoonIntensity = 0.06f;
	DuskSceneValues.MoonRotation = FRotator(-12.0f, 140.0f, 0.0f);
	DuskSceneValues.SkyLightIntensity = 0.32f;
	DuskSceneValues.FogDensity = 0.026f;
	DuskSceneValues.FogColor = FLinearColor(0.60f, 0.42f, 0.34f, 1.0f);
	DuskSceneValues.ExposureBias = -0.35f;
	DuskSceneValues.SceneTint = FLinearColor(1.0f, 0.62f, 0.48f, 1.0f);

	NightSceneValues.SunIntensity = 0.0f;
	NightSceneValues.SunColor = FLinearColor(0.22f, 0.26f, 0.38f, 1.0f);
	NightSceneValues.SunRotation = FRotator(18.0f, 48.0f, 0.0f);
	NightSceneValues.MoonIntensity = 0.24f;
	NightSceneValues.MoonColor = FLinearColor(0.45f, 0.55f, 1.0f, 1.0f);
	NightSceneValues.MoonRotation = FRotator(-34.0f, 145.0f, 0.0f);
	NightSceneValues.SkyLightIntensity = 0.08f;
	NightSceneValues.FogDensity = 0.042f;
	NightSceneValues.FogColor = FLinearColor(0.045f, 0.060f, 0.120f, 1.0f);
	NightSceneValues.ExposureBias = -1.20f;
	NightSceneValues.SceneTint = FLinearColor(0.58f, 0.68f, 1.0f, 1.0f);

	SettlementNightSceneValues = NightSceneValues;
	SettlementNightSceneValues.MoonIntensity = 0.18f;
	SettlementNightSceneValues.SkyLightIntensity = 0.06f;
	SettlementNightSceneValues.FogDensity = 0.050f;
	SettlementNightSceneValues.ExposureBias = -1.45f;
	SettlementNightSceneValues.SceneTint = FLinearColor(0.46f, 0.52f, 0.82f, 1.0f);
}

// BeginPlay 流程：专用服务器先停 Tick 并退出；客户端或 Listen Server 本机绑定 GameState、补齐可驱动的场景对象引用，再通过 ApplyPresentationState 同步驱动原生灯光/天空光/雾/后处理和蓝图事件。
void ACatEnvironmentPresentationActor::BeginPlay()
{
	Super::BeginPlay();
	if (GetNetMode() == NM_DedicatedServer)
	{
		SetActorTickEnabled(false);
		return;
	}
	RefreshGameStateBinding();
	ResolveSceneTargets();
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

// 应用流程：优先用 GameState 提供的服务器世界时间生成本地投影；成功后先套原生场景对象，再调用蓝图扩展事件。
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
	if (bApplyBuiltInScenePresentation)
	{
		ApplyBuiltInScenePresentation(PresentationState);
	}
	BP_ApplyEnvironmentPresentation(PresentationState);
}

// 场景对象解析流程：
// 1. 只在允许自动发现的本地 World 中工作，专用服务器和关闭自动发现时保持现有引用不动。
// 2. 先记录旧引用，再按手动引用优先、Tag 次之、同类兜底最后的顺序补齐 Sun、Moon、SkyLight、HeightFog 和 PostProcess。
// 3. 引用变化时写 resolved 日志；必要对象仍缺失时只设置一次 bHasLoggedMissingSceneTargets 并写 missing 日志，避免未接线关卡刷屏。
void ACatEnvironmentPresentationActor::ResolveSceneTargets()
{
	if (!bAutoDiscoverSceneActors)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World || GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	ADirectionalLight* PreviousSun = SunLightActor.Get();
	ADirectionalLight* PreviousMoon = MoonLightActor.Get();
	ASkyLight* PreviousSkyLight = SkyLightActor.Get();
	AExponentialHeightFog* PreviousHeightFog = HeightFogActor.Get();
	APostProcessVolume* PreviousPostProcessVolume = PostProcessVolumeActor.Get();

	if (!IsUsableSceneActor(SunLightActor.Get()))
	{
		SunLightActor = FindFirstSceneActorWithAnyTag<ADirectionalLight>(
			World, SunLightTag, CatSunLightTag, MoonLightActor.Get());
		if (!SunLightActor)
		{
			SunLightActor = FindFirstUsableSceneActor<ADirectionalLight>(World, MoonLightActor.Get());
		}
	}
	if (!IsUsableSceneActor(MoonLightActor.Get()))
	{
		MoonLightActor = FindFirstSceneActorWithAnyTag<ADirectionalLight>(
			World, MoonLightTag, CatMoonLightTag, SunLightActor.Get());
		if (!MoonLightActor)
		{
			MoonLightActor = FindFirstUsableSceneActor<ADirectionalLight>(World, SunLightActor.Get());
		}
	}
	if (!IsUsableSceneActor(SkyLightActor.Get()))
	{
		SkyLightActor = FindFirstSceneActorWithAnyTag<ASkyLight>(World, SkyLightTag, CatSkyLightTag);
		if (!SkyLightActor)
		{
			SkyLightActor = FindFirstUsableSceneActor<ASkyLight>(World);
		}
	}
	if (!IsUsableSceneActor(HeightFogActor.Get()))
	{
		HeightFogActor = FindFirstSceneActorWithAnyTag<AExponentialHeightFog>(World, HeightFogTag, CatHeightFogTag);
		if (!HeightFogActor)
		{
			HeightFogActor = FindFirstUsableSceneActor<AExponentialHeightFog>(World);
		}
	}
	if (!IsUsableSceneActor(PostProcessVolumeActor.Get()))
	{
		PostProcessVolumeActor = FindBestPostProcessVolume(World);
	}

	if (PreviousSun != SunLightActor.Get() || PreviousMoon != MoonLightActor.Get()
		|| PreviousSkyLight != SkyLightActor.Get() || PreviousHeightFog != HeightFogActor.Get()
		|| PreviousPostProcessVolume != PostProcessVolumeActor.Get())
	{
		UE_LOG(LogCatEnvironment, Log,
			TEXT("Event=environment_presentation_targets_resolved Actor=%s Sun=%s Moon=%s SkyLight=%s HeightFog=%s PostProcess=%s"),
			*GetNameSafe(this), *GetNameSafe(SunLightActor.Get()), *GetNameSafe(MoonLightActor.Get()),
			*GetNameSafe(SkyLightActor.Get()), *GetNameSafe(HeightFogActor.Get()),
			*GetNameSafe(PostProcessVolumeActor.Get()));
	}

	if (!bHasLoggedMissingSceneTargets
		&& (!IsUsableSceneActor(SunLightActor.Get()) || !IsUsableSceneActor(SkyLightActor.Get())
			|| !IsUsableSceneActor(HeightFogActor.Get()) || !IsUsableSceneActor(PostProcessVolumeActor.Get())))
	{
		bHasLoggedMissingSceneTargets = true;
		UE_LOG(LogCatEnvironment, Log,
			TEXT("Event=environment_presentation_targets_missing Actor=%s Sun=%s Moon=%s SkyLight=%s HeightFog=%s PostProcess=%s"),
			*GetNameSafe(this), *GetNameSafe(SunLightActor.Get()), *GetNameSafe(MoonLightActor.Get()),
			*GetNameSafe(SkyLightActor.Get()), *GetNameSafe(HeightFogActor.Get()),
			*GetNameSafe(PostProcessVolumeActor.Get()));
	}
}

// 分界读取流程：从 Environment 设置里复用服务器侧晨昏比例；配置不可用时只退回表现默认值，避免阻塞画面闭环。
void ACatEnvironmentPresentationActor::ResolveDaySegmentFractions(float& OutMorningEndFraction,
	float& OutDuskStartFraction) const
{
	OutMorningEndFraction = DefaultMorningEndFraction;
	OutDuskStartFraction = DefaultDuskStartFraction;
	const UCatEnvironmentSettings* Settings = GetDefault<UCatEnvironmentSettings>();
	if (Settings && FMath::IsFinite(Settings->MorningEndFraction) && FMath::IsFinite(Settings->DuskStartFraction)
		&& Settings->MorningEndFraction > 0.0 && Settings->DuskStartFraction > Settings->MorningEndFraction
		&& Settings->DuskStartFraction < 1.0)
	{
		OutMorningEndFraction = static_cast<float>(Settings->MorningEndFraction);
		OutDuskStartFraction = static_cast<float>(Settings->DuskStartFraction);
	}
}

// 目标值生成流程：
// 1. 先读取晨昏分界并夹住公开 DayProgress，所有插值都只发生在本地表现值之间。
// 2. Morning 和 Dusk 用同一份公开白天进度做连续过渡，Day 直接取稳定白天值。
// 3. NormalNight、结算夜和 Ended 直接取各自夜晚目标；缺同步事实时回到白天值，避免把未知阶段渲染成终局夜。
FCatEnvironmentScenePresentationValues ACatEnvironmentPresentationActor::BuildScenePresentationValues(
	const FCatEnvironmentPresentationState& State) const
{
	float MorningEndFraction = DefaultMorningEndFraction;
	float DuskStartFraction = DefaultDuskStartFraction;
	ResolveDaySegmentFractions(MorningEndFraction, DuskStartFraction);
	const float DayProgress = FMath::Clamp(static_cast<float>(State.DayProgress), 0.0f, 1.0f);

	switch (State.PresentationPhase)
	{
	case ECatEnvironmentPresentationPhase::Morning:
		return LerpScenePresentationValues(MorningSceneValues, DaySceneValues,
			MorningEndFraction > KINDA_SMALL_NUMBER ? DayProgress / MorningEndFraction : 1.0f);
	case ECatEnvironmentPresentationPhase::Day:
		return DaySceneValues;
	case ECatEnvironmentPresentationPhase::Dusk:
		return LerpScenePresentationValues(DaySceneValues, DuskSceneValues,
			DuskStartFraction < 1.0f - KINDA_SMALL_NUMBER
				? (DayProgress - DuskStartFraction) / (1.0f - DuskStartFraction) : 1.0f);
	case ECatEnvironmentPresentationPhase::Night:
		return NightSceneValues;
	case ECatEnvironmentPresentationPhase::SettlementNight:
	case ECatEnvironmentPresentationPhase::Ended:
		return SettlementNightSceneValues;
	case ECatEnvironmentPresentationPhase::Unavailable:
	default:
		return DaySceneValues;
	}
}

// 原生应用流程：
// 1. 专用服务器和缺同步事实阶段直接跳过，不让表现 Actor 伪造灯光状态。
// 2. 解析本地场景对象并计算同一帧目标值后，分别写太阳、月亮、天空光、雾和后处理；缺任一对象都安全跳过。
// 3. 太阳/月亮共用同一 Actor 时只报告一次并跳过月光写入，避免同帧互相覆盖。
// 4. 只有表现阶段变化时更新 LastLoggedBuiltInPhase 并写结构化日志，用来保留 Development 包里的昼夜表现证据。
void ACatEnvironmentPresentationActor::ApplyBuiltInScenePresentation(
	const FCatEnvironmentPresentationState& State)
{
	if (GetNetMode() == NM_DedicatedServer || State.PresentationPhase == ECatEnvironmentPresentationPhase::Unavailable)
	{
		return;
	}

	ResolveSceneTargets();
	const FCatEnvironmentScenePresentationValues Values = BuildScenePresentationValues(State);

	if (UDirectionalLightComponent* SunComponent = IsUsableSceneActor(SunLightActor.Get())
		? Cast<UDirectionalLightComponent>(SunLightActor->GetLightComponent()) : nullptr)
	{
		SunLightActor->SetActorRotation(Values.SunRotation);
		SunComponent->SetIntensity(FMath::Max(0.0f, Values.SunIntensity));
		SunComponent->SetLightColor(Values.SunColor, false);
		SunComponent->SetAtmosphereSunLight(true);
		SunComponent->SetAtmosphereSunLightIndex(0);
		SunComponent->SetVisibility(Values.SunIntensity > KINDA_SMALL_NUMBER);
	}

	if (MoonLightActor && MoonLightActor.Get() == SunLightActor.Get())
	{
		if (!bHasLoggedSharedSunMoonTarget)
		{
			bHasLoggedSharedSunMoonTarget = true;
			UE_LOG(LogCatEnvironment, Log,
				TEXT("Event=environment_presentation_moon_skipped Actor=%s Reason=SunAndMoonShareActor Light=%s"),
				*GetNameSafe(this), *GetNameSafe(SunLightActor.Get()));
		}
	}
	else if (UDirectionalLightComponent* MoonComponent = IsUsableSceneActor(MoonLightActor.Get())
		? Cast<UDirectionalLightComponent>(MoonLightActor->GetLightComponent()) : nullptr)
	{
		MoonLightActor->SetActorRotation(Values.MoonRotation);
		MoonComponent->SetIntensity(FMath::Max(0.0f, Values.MoonIntensity));
		MoonComponent->SetLightColor(Values.MoonColor, false);
		MoonComponent->SetAtmosphereSunLight(true);
		MoonComponent->SetAtmosphereSunLightIndex(1);
		MoonComponent->SetVisibility(Values.MoonIntensity > KINDA_SMALL_NUMBER);
	}

	if (USkyLightComponent* SkyLightComponent = IsUsableSceneActor(SkyLightActor.Get())
		? SkyLightActor->GetLightComponent() : nullptr)
	{
		SkyLightComponent->SetIntensity(FMath::Max(0.0f, Values.SkyLightIntensity));
		SkyLightComponent->SetLightColor(Values.SceneTint);
	}

	if (UExponentialHeightFogComponent* FogComponent = IsUsableSceneActor(HeightFogActor.Get())
		? HeightFogActor->GetComponent() : nullptr)
	{
		FogComponent->SetFogDensity(FMath::Max(0.0f, Values.FogDensity));
		FogComponent->SetFogInscatteringColor(Values.FogColor);
	}

	if (IsUsableSceneActor(PostProcessVolumeActor.Get()))
	{
		PostProcessVolumeActor->bEnabled = true;
		PostProcessVolumeActor->Settings.bOverride_AutoExposureBias = true;
		PostProcessVolumeActor->Settings.AutoExposureBias = Values.ExposureBias;
		PostProcessVolumeActor->Settings.bOverride_SceneColorTint = true;
		PostProcessVolumeActor->Settings.SceneColorTint = Values.SceneTint;
	}

	if (LastLoggedBuiltInPhase != State.PresentationPhase)
	{
		LastLoggedBuiltInPhase = State.PresentationPhase;
		UE_LOG(LogCatEnvironment, Log,
			TEXT("Event=environment_presentation_phase_applied Actor=%s RunId=%s Revision=%lld Day=%d Phase=%s DayProgress=%.3f"),
			*GetNameSafe(this), *State.RunId.ToString(EGuidFormats::DigitsWithHyphensLower),
			State.RunRevision, State.DayIndex, *FormatEnvironmentPresentationPhase(State.PresentationPhase),
			State.DayProgress);
	}
}
