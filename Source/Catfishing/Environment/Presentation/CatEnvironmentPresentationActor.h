#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatRunContracts.h"
#include "GameFramework/Actor.h"

#include "CatEnvironmentPresentationActor.generated.h"

class ACatfishingGameState;
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

/** 全局环境表现消费者；本地读取 GameState 的组合快照，向蓝图推送场景氛围投影。 */
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

	/** 将当前缓存转换后推给蓝图；缺少 GameState 时保持静默，等待下一次 Tick 重试绑定。 */
	void ApplyPresentationState();

	/** 表现 Actor 的稳定根组件；给蓝图挂灯光、天空和后处理组件，不参与碰撞或导航。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Environment|Presentation",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> VisualRoot;

	/** 是否每帧推送连续进度；关闭后只在 RunPublicState 复制变化时刷新离散表现。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|Environment|Presentation",
		meta = (AllowPrivateAccess = "true"))
	bool bApplyEveryTick = true;

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
};
