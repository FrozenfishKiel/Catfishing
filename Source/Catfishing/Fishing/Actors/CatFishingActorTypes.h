#pragma once

#include "CoreMinimal.h"
#include "Fishing/CatFishingTypes.h"
#include "CatFishingActorTypes.generated.h"

class APlayerState;

UENUM(BlueprintType)
enum class ECatFishingHookPresentationPhase : uint8
{
	Unconfigured,
	CastFlight,
	Landed,
	Failed
};

/** 浮漂只复制离散表现模式；连续上下位移由每台客户端按服务器起始时间本地计算。 */
UENUM(BlueprintType)
enum class ECatFishingBobberPresentationMode : uint8
{
	None,
	/** 等鱼时的小幅慢浮。 */
	Calm,
	/** 真咬前三秒的快速点动预警。 */
	BiteWarning,
	/** 真咬响应窗口中的黑漂下沉。 */
	Sunk
};

USTRUCT(BlueprintType)
struct FCatFishingRodPresentationState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FGuid RodActorId;
	UPROPERTY(BlueprintReadOnly) int64 RodActorRevision = 0;
	/** 这根场景鱼竿对应的运行期物品实例 ID；收杆按它 UnUse，避免同定义鱼竿在背包和场上重复存在。 */
	UPROPERTY(BlueprintReadOnly) FGuid ItemInstanceId;
	UPROPERTY(BlueprintReadOnly) FName RodDefinitionId = NAME_None;
	UPROPERTY(BlueprintReadOnly) FName RodSkinDefinitionId = NAME_None;
	UPROPERTY(BlueprintReadOnly) TObjectPtr<APlayerState> OwnerPlayerState = nullptr;
	/** 当前主操作手（OperatorPlayerStates[0]）的兼容快捷字段；只有主位驱动现有单人钓鱼会话。 */
	UPROPERTY(BlueprintReadOnly) TObjectPtr<APlayerState> OperatorPlayerState = nullptr;
	/** 有序占位容器：加入时追加，离开时压紧；0=主位，之后按编号公式左右交替向外扩展，始终无空洞。 */
	UPROPERTY(BlueprintReadOnly) TArray<TObjectPtr<APlayerState>> OperatorPlayerStates;
	UPROPERTY(BlueprintReadOnly) bool bDeployed = false;
	UPROPERTY(BlueprintReadOnly) bool bBroken = false;
};

USTRUCT(BlueprintType)
struct FCatFishingHookPresentationState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FGuid FishingSessionId;
	UPROPERTY(BlueprintReadOnly) FGuid CastAttemptId;
	UPROPERTY(BlueprintReadOnly) ECatFishingHookPresentationPhase Phase = ECatFishingHookPresentationPhase::Unconfigured;
	UPROPERTY(BlueprintReadOnly) ECatFishingBobberPresentationMode BobberMode = ECatFishingBobberPresentationMode::None;
	/** 当前浮漂模式在服务器开始的世界时间；客户端据此保持多人相位一致，不逐帧复制 Transform。 */
	UPROPERTY(BlueprintReadOnly) double BobberModeStartedServerTime = 0.0;
	/** 权威模拟的已放出线长 L_paid；Cable 只把它当作本地表现长度。 */
	UPROPERTY(BlueprintReadOnly) double PaidOutLineLengthCentimeters = 0.0;
	/** 竿尖到 Hook/Fish 的实际直线距离 D。 */
	UPROPERTY(BlueprintReadOnly) double StraightLineDistanceCentimeters = 0.0;
	/** max(L_paid-D,0)；客户端据此增加 Cable 垂坠。 */
	UPROPERTY(BlueprintReadOnly) double SlackLineLengthCentimeters = 0.0;
	UPROPERTY(BlueprintReadOnly) float NormalizedTension = 0.0f;
	UPROPERTY(BlueprintReadOnly) bool bLineTaut = false;
};

USTRUCT(BlueprintType)
struct FCatFishEncounterPresentationState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FGuid FishingSessionId;
	UPROPERTY(BlueprintReadOnly) FGuid CastAttemptId;
	UPROPERTY(BlueprintReadOnly) FName FishDefinitionId = NAME_None;
	/** 服务器由本鱼冻结重量计算的一次性统一 Mesh 缩放；客户端只消费，不自行随机。 */
	UPROPERTY(BlueprintReadOnly) double VisualScale = 1.0;
	UPROPERTY(BlueprintReadOnly) ECatFishMotionIntent MotionIntent = ECatFishMotionIntent::None;
	/** 行为意图选择的自由游速（cm/s），不受鱼线、岸线或最终位移限制；用于驱动 AnimBP 播放倍率。 */
	UPROPERTY(BlueprintReadOnly) float IntendedSwimSpeedCentimetersPerSecond = 0.0f;
	UPROPERTY(BlueprintReadOnly) double CurrentLineLength = 0.0;
	/** 鱼当前游向与鱼线向外方向夹角余弦，[-1,1]；供所有客户端驱动转向/受力表现。 */
	UPROPERTY(BlueprintReadOnly) float FishLineAlignment = 0.0f;
	/** 经过性格幂曲线后的归一化鱼线受力，[0,1]。 */
	UPROPERTY(BlueprintReadOnly) float NormalizedLineLoad = 0.0f;
	/** 服务器已确认进入强对抗角度区间；动画/UI 不需要在客户端重新计算。 */
	UPROPERTY(BlueprintReadOnly) bool bStrongConfrontation = false;
};
