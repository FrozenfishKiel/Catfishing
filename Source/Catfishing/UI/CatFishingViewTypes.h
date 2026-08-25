#pragma once

#include "CoreMinimal.h"
#include "Fishing/CatFishingTypes.h"
#include "CatFishingViewTypes.generated.h"

/** UI 层消费的单次钓鱼会话只读投影；来源是复制 Snapshot，Widget 和蓝图只能读取它，不能通过它写回 FishingSession。 */
USTRUCT(BlueprintType)
struct CATFISHING_API FCatFishingViewState
{
	GENERATED_BODY()

	/** 当前投影对应的 FishingSession 身份；FishingSession 复制快照写入，UI 用它区分同一玩家连续会话的显示来源。 */
	UPROPERTY(BlueprintReadOnly) FGuid FishingSessionId;

	/** 当前投影对应的会话版本；FishingSession 每次公开事实变化推进它，UI 只用来展示或判断快照新旧，不参与命令校验。 */
	UPROPERTY(BlueprintReadOnly) int64 Revision = 0;

	/** 当前会话阶段；FishingSession 是唯一写者，HUD 用它选择等待、搏斗、近岸或终态提示。 */
	UPROPERTY(BlueprintReadOnly) ECatFishingPhase Phase = ECatFishingPhase::Created;

	/** 当前会话终局结果；只有会话进入终态后才有展示意义，UI 不根据它触发结算或物品写入。 */
	UPROPERTY(BlueprintReadOnly) ECatFishingOutcome Outcome = ECatFishingOutcome::None;

	/** 当前鱼种定义 ID；会话快照提供它，UI 用来显示鱼种文本，图鉴记录仍由 Profile/Collection 的正式链路负责。 */
	UPROPERTY(BlueprintReadOnly) FName FishDefinitionId = NAME_None;

	/** 鱼侧搏斗体力的展示比例；数值来自 FishingSession 的运行态归一化结果，UI 不反推鱼真实体力或搏斗公式。 */
	UPROPERTY(BlueprintReadOnly) double NormalizedFishStamina = 0.0;

	/** 当前玩家是否正在收线；命令组件和会话裁决状态才是写口，UI 只展示最近复制结果。 */
	UPROPERTY(BlueprintReadOnly) bool bReeling = false;

	/** 当前玩家是否正在放线；它与 bReeling 都是会话快照事实，Widget 不把按钮状态当成第二份真相。 */
	UPROPERTY(BlueprintReadOnly) bool bSlacking = false;

	/** 本次刺鱼是否获得完美响应；会话进入搏斗时写入，UI 只把它作为反馈和高光提示。 */
	UPROPERTY(BlueprintReadOnly) bool bPerfectHook = false;

	/** 鱼当前运动意图的公开提示；FishingSession 裁决，UI 用它提示收放线方向，不驱动鱼运动。 */
	UPROPERTY(BlueprintReadOnly) ECatFishMotionIntent FishMotionIntent = ECatFishMotionIntent::None;

	/** 从 FishingSession 复制快照创建 UI DTO；结果只包含展示字段，不携带会话 Actor、组件或任何写口。 */
	static FCatFishingViewState FromSnapshot(const FCatFishingSessionSnapshot& Snapshot);
};

/** C++ UI 协调器订阅的只读会话变化通知；Bridge 每次完成快照投影后广播完整 DTO。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatFishingViewStateChanged, const FCatFishingViewState&);
