#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatRunContracts.h"
#include "GameFramework/Actor.h"

#include "CatEnvironmentPresentationActor.generated.h"

class ACatfishingGameState;
class ADirectionalLight;
class AExponentialHeightFog;
class APostProcessVolume;
class ASkyLight;
class USceneComponent;

/** 场景表现使用的环境阶段；它由 Run Phase 与白天时段折叠而来，不反向驱动玩法状态。 */
UENUM(BlueprintType)
enum class ECatEnvironmentPresentationPhase : uint8
{
	/** 当前没有足够公开事实驱动表现，蓝图应回到安全默认状态。 */
	Unavailable,
	/** 可钓白天的清晨表现段。 */
	Morning,
	/** 可钓白天的主体表现段。 */
	Day,
	/** 可钓白天接近截止的黄昏表现段。 */
	Dusk,
	/** 额度达成后的普通夜晚表现段。 */
	Night,
	/** 失败或成功终局后的结算夜表现段。 */
	SettlementNight,
	/** Run 已进入收口或结束，表现应停止依赖一局时钟。 */
	Ended
};

/** 给蓝图表现层消费的只读环境投影；字段全部来自同一份 RunPublicState，不引入第二套昼夜真相。 */
USTRUCT(BlueprintType)
struct FCatEnvironmentPresentationState
{
	GENERATED_BODY()

	/** 当前一局的公开标识；蓝图可用它区分旧世界残留事件，不应用它写回玩法。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid RunId;

	/** 当前公开天数；首个可玩白天为 1，夜晚继续显示同一天。 */
	UPROPERTY(BlueprintReadOnly)
	int32 DayIndex = 0;

	/** 当前 Run 的权威阶段；蓝图只用它选择表现分支，不自行推进 StateTree。 */
	UPROPERTY(BlueprintReadOnly)
	ECatRunPhase RunPhase = ECatRunPhase::NotStarted;

	/** 场景表现折叠后的阶段；用于灯光、天空、雾和音频切换。 */
	UPROPERTY(BlueprintReadOnly)
	ECatEnvironmentPresentationPhase PresentationPhase = ECatEnvironmentPresentationPhase::Unavailable;

	/** 当前天气语义；来自 Environment provider，Unknown 表示表现层应关闭天气特效。 */
	UPROPERTY(BlueprintReadOnly)
	ECatEnvironmentWeather Weather = ECatEnvironmentWeather::Unknown;

	/** 当前白天时段；夜晚保持 Unknown，由 PresentationPhase 表达夜晚表现。 */
	UPROPERTY(BlueprintReadOnly)
	ECatEnvironmentTimeOfDay TimeOfDay = ECatEnvironmentTimeOfDay::Unknown;

	/** 当前是否存在已裁决的公共环境事件；蓝图只读显示，不创建玩法事件。 */
	UPROPERTY(BlueprintReadOnly)
	bool bHasActiveEvent = false;

	/** 当前公共环境事件名称；没有事件时为 None。 */
	UPROPERTY(BlueprintReadOnly)
	FName ActiveEventId = NAME_None;

	/** 当前白天进度 0..1；只有 DayActive 且有有效截止时连续推进，夜晚或缺失时保持 0。 */
	UPROPERTY(BlueprintReadOnly)
	double DayProgress = 0.0;

	/** 本投影对应的 Run Revision；用于蓝图过滤迟到动画或诊断显示不同步。 */
	UPROPERTY(BlueprintReadOnly)
	int64 RunRevision = 0;

	/** 生成本地表现投影时观察到的服务器秒；用于蓝图做连续过渡，不作为玩法时钟。 */
	UPROPERTY(BlueprintReadOnly)
	double ServerNowSeconds = 0.0;
};

/** 一组原生场景对象的目标表现值；构造默认值、编辑器或蓝图写入这些表现参数，Actor 读取后只影响本机画面，不进入 RunPublicState 网络合同。 */
USTRUCT(BlueprintType)
struct FCatEnvironmentScenePresentationValues
{
	GENERATED_BODY()

	/** 太阳方向光的目标强度；构造默认值、编辑器或蓝图写入，表现 Actor 读取后写入本地 DirectionalLightComponent，数值越高白天越亮。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Environment|Presentation",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SunIntensity = 10.0f;

	/** 太阳方向光的目标颜色；构造默认值、编辑器或蓝图写入，表现 Actor 读取后写到太阳方向光，用于区分清晨、正午和黄昏。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Environment|Presentation")
	FLinearColor SunColor = FLinearColor(1.0f, 0.95f, 0.82f, 1.0f);

	/** 太阳方向光的目标朝向；构造默认值、编辑器或蓝图写入，表现 Actor 按同步白天进度读取并应用，因此不会本地自由漂移。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Environment|Presentation")
	FRotator SunRotation = FRotator(-42.0f, -35.0f, 0.0f);

	/** 月亮方向光的目标强度；构造默认值、编辑器或蓝图写入，表现 Actor 读取后写入月光组件，没有 Moon Actor 时安全跳过。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Environment|Presentation",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MoonIntensity = 0.0f;

	/** 月亮方向光的目标颜色；构造默认值、编辑器或蓝图写入，表现 Actor 读取后只影响本地夜间画面，不参与玩法天气判断。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Environment|Presentation")
	FLinearColor MoonColor = FLinearColor(0.45f, 0.55f, 1.0f, 1.0f);

	/** 月亮方向光的目标朝向；构造默认值、编辑器或蓝图写入，表现 Actor 独立读取并应用，避免和太阳朝向混成同一份表现参数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Environment|Presentation")
	FRotator MoonRotation = FRotator(-35.0f, 145.0f, 0.0f);

	/** 天空光的目标强度；构造默认值、编辑器或蓝图写入，表现 Actor 读取后只改本地 SkyLightComponent，不触发网络复制。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Environment|Presentation",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SkyLightIntensity = 1.0f;

	/** 雾密度目标值；构造默认值、编辑器或蓝图写入，表现 Actor 读取后写入 HeightFogComponent，用于让黄昏和夜晚可见地区分。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Environment|Presentation",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FogDensity = 0.012f;

	/** 雾颜色目标值；构造默认值、编辑器或蓝图写入，表现 Actor 读取后只调整场景氛围，不表达公共环境事件。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Environment|Presentation")
	FLinearColor FogColor = FLinearColor(0.78f, 0.86f, 1.0f, 1.0f);

	/** 后处理曝光补偿；构造默认值、编辑器或蓝图写入，表现 Actor 读取后写入 PostProcessVolume，让夜晚整体压暗。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Environment|Presentation")
	float ExposureBias = 0.0f;

	/** 后处理画面色调；构造默认值、编辑器或蓝图写入，表现 Actor 读取后写入 SceneColorTint，用轻微色偏区分时段。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Catfishing|Environment|Presentation")
	FLinearColor SceneTint = FLinearColor::White;
};

/** 全局环境表现消费者；本地读取 GameState 的组合快照，向蓝图与 UE 原生场景对象推送场景氛围投影。 */
UCLASS(Blueprintable)
class CATFISHING_API ACatEnvironmentPresentationActor : public AActor
{
	GENERATED_BODY()

public:
	/** 建立纯本地表现 Actor；不复制、不碰玩法写口，只准备根组件和可选 Tick。 */
	ACatEnvironmentPresentationActor();

	/** 返回最近一次推送给蓝图的环境表现投影；调用方只能读取，不影响 RunPublicState。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Environment|Presentation")
	FCatEnvironmentPresentationState GetPresentationState() const;

	/** 蓝图实现的表现应用入口；C++ 保证传入状态来自同一份 RunPublicState。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|Environment|Presentation")
	void BP_ApplyEnvironmentPresentation(const FCatEnvironmentPresentationState& State);

protected:
	/** BeginPlay 时订阅本地 GameState 并立即推送一次当前投影；专用服务器上关闭 Tick。 */
	virtual void BeginPlay() override;

	/** 每帧在本地用服务器时间刷新连续白天进度；离散阶段变化仍来自 GameState 委托。 */
	virtual void Tick(float DeltaSeconds) override;

	/** EndPlay 成对解除 GameState 订阅，避免 World 切换后旧 Actor 继续响应复制回调。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 查找并订阅当前 World 的 GameState；绑定成功后缓存一份公开快照供表现投影使用。 */
	bool RefreshGameStateBinding();

	/** 解除当前 GameState 订阅并清空本地缓存；只影响表现 Actor 自己的弱引用。 */
	void ClearGameStateBinding();

	/** Run 公开快照变化时更新缓存并推送新投影；不在客户端预测下一阶段。 */
	void HandleRunPublicStateChanged();

	/** 从缓存的 RunPublicState 构建蓝图投影；连续进度只按公开锚点和服务器时间计算。 */
	FCatEnvironmentPresentationState BuildPresentationState(double ServerNowSeconds) const;

	/** 将当前缓存转换后推给原生场景对象和蓝图；缺少 GameState 时保持静默，等待下一次 Tick 重试绑定。 */
	void ApplyPresentationState();

	/** 查找本地可驱动的 UE 场景对象；手动引用优先，自动发现只补空缺引用。 */
	void ResolveSceneTargets();

	/** 从表现阶段和白天进度生成一帧原生场景目标值；结果只用于本地灯光、雾和后处理。 */
	FCatEnvironmentScenePresentationValues BuildScenePresentationValues(
		const FCatEnvironmentPresentationState& State) const;

	/** 把当前表现投影应用到 UE 原生灯光、天空光、雾和后处理；缺少对象时安全跳过。 */
	void ApplyBuiltInScenePresentation(const FCatEnvironmentPresentationState& State);

	/** 读取环境配置中的晨昏分界，失败时回退到当前默认值；只影响本地表现插值。 */
	void ResolveDaySegmentFractions(float& OutMorningEndFraction, float& OutDuskStartFraction) const;

	/** 表现 Actor 的稳定根组件；给蓝图挂灯光、天空和后处理组件，不参与碰撞或导航。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Environment|Presentation",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> VisualRoot;

	/** 是否每帧推送连续进度；关闭后只在 RunPublicState 复制变化时刷新离散表现。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|Environment|Presentation",
		meta = (AllowPrivateAccess = "true"))
	bool bApplyEveryTick = true;

	/** 原生场景对象自动应用开关，表示本 Actor 是否直接驱动灯光、天空光、雾和后处理；编辑器或蓝图默认值写入，ApplyPresentationState 读取，关闭后仍会调用蓝图事件。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|Environment|Presentation|Built In",
		meta = (AllowPrivateAccess = "true"))
	bool bApplyBuiltInScenePresentation = true;

	/** 场景对象自动发现开关，表示空引用是否允许由当前 World 的 Actor 扫描补齐；编辑器或蓝图默认值写入，ResolveSceneTargets 读取，只补表现接线，不创建玩法状态。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|Environment|Presentation|Built In",
		meta = (AllowPrivateAccess = "true"))
	bool bAutoDiscoverSceneActors = true;

	/** 白天主太阳方向光引用，代表本地画面里承担 0 号 SkyAtmosphere 太阳的 Actor；关卡或蓝图可手动写入，ResolveSceneTargets 只在空缺或失效时补写，ApplyBuiltInScenePresentation 读取并应用强度、颜色和朝向。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|Environment|Presentation|Built In",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<ADirectionalLight> SunLightActor;

	/** 夜晚月光方向光引用，代表本地画面里承担 1 号 SkyAtmosphere 月光的 Actor；关卡或蓝图可手动写入，ResolveSceneTargets 可补空引用，ApplyBuiltInScenePresentation 在缺失或与太阳共用时安全跳过。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|Environment|Presentation|Built In",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<ADirectionalLight> MoonLightActor;

	/** 场景天空光引用，代表昼夜表现要调节的环境补光对象；关卡或蓝图可手动写入，ResolveSceneTargets 可补空引用，ApplyBuiltInScenePresentation 读取后只改本地补光强度和色调。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|Environment|Presentation|Built In",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<ASkyLight> SkyLightActor;

	/** 场景高度雾引用，代表昼夜表现要调节的氛围雾对象；关卡或蓝图可手动写入，ResolveSceneTargets 可补空引用，ApplyBuiltInScenePresentation 读取后只改本地雾密度和雾色。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|Environment|Presentation|Built In",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<AExponentialHeightFog> HeightFogActor;

	/** 场景后处理体引用，代表昼夜表现要调节的曝光和画面色调对象；关卡或蓝图可手动写入，ResolveSceneTargets 优先补全局体，ApplyBuiltInScenePresentation 读取后只改本地后处理 override。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|Environment|Presentation|Built In",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<APostProcessVolume> PostProcessVolumeActor;

	/** 清晨起点表现值，表示 DayActive 开始时的灯光、雾和后处理目标；编辑器或蓝图默认值写入，BuildScenePresentationValues 读取，并向白天主体表现平滑过渡。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|Environment|Presentation|Built In",
		meta = (AllowPrivateAccess = "true"))
	FCatEnvironmentScenePresentationValues MorningSceneValues;

	/** 白天主体表现值，表示 Morning 结束后的稳定昼间目标；编辑器或蓝图默认值写入，BuildScenePresentationValues 读取，并作为黄昏过渡起点。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|Environment|Presentation|Built In",
		meta = (AllowPrivateAccess = "true"))
	FCatEnvironmentScenePresentationValues DaySceneValues;

	/** 黄昏终点表现值，表示 DayProgress 接近 1 时的昼夜交界目标；编辑器或蓝图默认值写入，BuildScenePresentationValues 读取并从白天主体表现插值到这里。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|Environment|Presentation|Built In",
		meta = (AllowPrivateAccess = "true"))
	FCatEnvironmentScenePresentationValues DuskSceneValues;

	/** 普通夜晚表现值，表示额度达成后 NormalNight 的本地画面目标；编辑器或蓝图默认值写入，BuildScenePresentationValues 读取，阶段来源只认 RunPhase。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|Environment|Presentation|Built In",
		meta = (AllowPrivateAccess = "true"))
	FCatEnvironmentScenePresentationValues NightSceneValues;

	/** 结算夜表现值，表示失败或成功终局夜晚共用的本地画面目标；编辑器或蓝图默认值写入，BuildScenePresentationValues 读取，后续美术需要细分时可在蓝图事件里继续分支。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|Environment|Presentation|Built In",
		meta = (AllowPrivateAccess = "true"))
	FCatEnvironmentScenePresentationValues SettlementNightSceneValues;

	/** 当前订阅的 GameState；它是唯一公开事实源，表现 Actor 不直接找 GameMode。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ACatfishingGameState> BoundGameState;

	/** GameState RunPublicState 变化解绑句柄；EndPlay 和重绑时成对移除。 */
	FDelegateHandle RunPublicStateChangedHandle;

	/** 最近一次从 GameState 读取的组合公开事实；仅作为本 Actor 的蓝图投影输入。 */
	UPROPERTY(Transient)
	FCatRunPublicState CachedRunPublicState;

	/** 当前是否已经拿到至少一份 RunPublicState；没有时蓝图不会收到伪造白天或夜晚。 */
	UPROPERTY(Transient)
	bool bHasCachedRunPublicState = false;

	/** 最近一次推送给蓝图的表现投影；用于 BlueprintPure 读取和本地诊断。 */
	UPROPERTY(Transient)
	FCatEnvironmentPresentationState PresentationState;

	/** 上一次写过诊断日志的原生表现阶段，表示本地场景对象最近一次落盘的阶段口径；ApplyBuiltInScenePresentation 写入并读取它抑制 Tick 日志，不参与玩法判断。 */
	ECatEnvironmentPresentationPhase LastLoggedBuiltInPhase = ECatEnvironmentPresentationPhase::Unavailable;

	/** 本 World 缺少可驱动对象的日志降噪标记；ResolveSceneTargets 首次发现必要对象缺失时写入，后续 Tick 读取它避免未接线关卡每帧刷日志。 */
	bool bHasLoggedMissingSceneTargets = false;

	/** 太阳和月亮共用同一方向光的日志降噪标记；ApplyBuiltInScenePresentation 首次发现时写入，后续读取它避免重复提示，同时月光应用会被跳过以免同帧互相覆盖。 */
	bool bHasLoggedSharedSunMoonTarget = false;
};
