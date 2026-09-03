#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatFishingPresentationSettings.generated.h"

class ACatChumFieldPresentationActor;
class ACatFishEncounterActor;
class ACatFishingHookActor;
class UAnimMontage;
class UCatRodSkinDefinition;

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Catfishing Fishing Presentation"))
class CATFISHING_API UCatFishingPresentationSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	const UCatRodSkinDefinition* FindRuntimeRodSkin(FName RodSkinDefinitionId, FName RodDefinitionId) const;
	/** 抛竿后生成的浮漂/鱼钩表现 Actor 类；它属于 Fishing 表现链，不代表某个库存物品实例。 */
	UPROPERTY(Config, EditAnywhere) TSoftClassPtr<ACatFishingHookActor> HookActorClass;
	/** 上钩鱼在世界中的表现 Actor 类；它由 Fishing Session 生成，不进入玩家库存。 */
	UPROPERTY(Config, EditAnywhere) TSoftClassPtr<ACatFishEncounterActor> FishEncounterActorClass;
	/**
	 * 服务器确认抛竿并让 Hook 进入 CastFlight 后，由每台客户端在抛竿者角色上本地播放的 Montage。
	 * Montage 本身不复制；复制的是 CastFlight 这个玩法事实，因此主机、发起客户端和旁观客户端走同一触发条件。
	 */
	UPROPERTY(Config, EditAnywhere, Category="Animation")
	TSoftObjectPtr<UAnimMontage> CastMontage;

	/** 服务器确认断线终局后，在当前钓手身上播放的一次性受力/拉空表现。 */
	UPROPERTY(Config, EditAnywhere, Category="Animation")
	TSoftObjectPtr<UAnimMontage> LineBrokenMontage;

	/** Condition 确认猫脚点进入危险水深后，在当前钓手身上播放的一次性落水表现。 */
	UPROPERTY(Config, EditAnywhere, Category="Animation")
	TSoftObjectPtr<UAnimMontage> CatInWaterMontage;

	/** 浮漂等待时的上下振幅（厘米）与频率（Hz）；只作用于 Hook 的 VisualRoot。 */
	UPROPERTY(Config, EditAnywhere, Category="Bobber", meta=(ClampMin="0", Units="cm"))
	double BobberCalmAmplitudeCentimeters = 4.0;
	UPROPERTY(Config, EditAnywhere, Category="Bobber", meta=(ClampMin="0", Units="Hz"))
	double BobberCalmFrequencyHz = 0.4;
	/** 真咬前预警的上下振幅与频率。 */
	UPROPERTY(Config, EditAnywhere, Category="Bobber", meta=(ClampMin="0", Units="cm"))
	double BobberWarningAmplitudeCentimeters = 7.0;
	UPROPERTY(Config, EditAnywhere, Category="Bobber", meta=(ClampMin="0", Units="Hz"))
	double BobberWarningFrequencyHz = 2.5;
	/** 真咬时相对水面的瞬时下沉深度（正数，内部转换为负 Z）。 */
	UPROPERTY(Config, EditAnywhere, Category="Bobber", meta=(ClampMin="0", Units="cm"))
	double BobberBiteSinkDepthCentimeters = 28.0;

	/** Cable 纯表现更新间隔；玩法模拟和网络复制仍使用各自的服务器频率。 */
	UPROPERTY(Config, EditAnywhere, Category="FishingLine", meta=(ClampMin="0.005", Units="s"))
	double FishingLineVisualUpdateIntervalSeconds = 1.0 / 60.0;
	/** Hook 复制落点到本地 Cable 起点的插值速度，消除 20Hz 端点阶跃。 */
	UPROPERTY(Config, EditAnywhere, Category="FishingLine", meta=(ClampMin="0"))
	double FishingLineEndpointInterpolationSpeed = 18.0;
	/** L_paid 到本地 CableLength 的插值速度。 */
	UPROPERTY(Config, EditAnywhere, Category="FishingLine", meta=(ClampMin="0"))
	double FishingLineLengthInterpolationSpeed = 14.0;
	/** SlackRatio 到本地重力表现的插值速度。 */
	UPROPERTY(Config, EditAnywhere, Category="FishingLine", meta=(ClampMin="0"))
	double FishingLineSlackInterpolationSpeed = 10.0;
	/** Cable 内部 Verlet 子步；只影响本地视觉稳定性。 */
	UPROPERTY(Config, EditAnywhere, Category="FishingLine", meta=(ClampMin="0.005", ClampMax="0.1", Units="s"))
	double FishingLineSimulationSubstepSeconds = 0.01;
	/** 固定约束求解次数；不再在松弛/绷紧间硬切。 */
	UPROPERTY(Config, EditAnywhere, Category="FishingLine", meta=(ClampMin="1", ClampMax="16"))
	int32 FishingLineSolverIterations = 10;
	UPROPERTY(Config, EditAnywhere, Category="FishingLine", meta=(ClampMin="0"))
	double FishingLineTautGravityScale = 0.08;
	UPROPERTY(Config, EditAnywhere, Category="FishingLine", meta=(ClampMin="0"))
	double FishingLineSlackGravityScale = 1.0;
	/** 客户端窝点表现 Actor 类；留空则用原生基类（无任何可见表现）。 */
	UPROPERTY(Config, EditAnywhere) TSoftClassPtr<ACatChumFieldPresentationActor> ChumFieldPresentationClass;
	UPROPERTY(Config, EditAnywhere) TArray<TSoftObjectPtr<UCatRodSkinDefinition>> RodSkinCatalog;
};
