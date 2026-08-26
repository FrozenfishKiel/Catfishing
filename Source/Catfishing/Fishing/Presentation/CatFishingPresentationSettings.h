#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatFishingPresentationSettings.generated.h"

class ACatChumFieldPresentationActor;
class ACatFishEncounterActor;
class ACatFishingHookActor;
class ACatFishingRodActor;
class UAnimMontage;
class UCatRodSkinDefinition;

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Catfishing Fishing Presentation"))
class CATFISHING_API UCatFishingPresentationSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	const UCatRodSkinDefinition* FindRuntimeRodSkin(FName RodSkinDefinitionId, FName RodDefinitionId) const;
	/** 按“重量约等于体积”计算鱼 Mesh 的统一线性缩放；非法输入安全回退为 1。 */
	double ComputeFishUniformVisualScale(double WeightKilograms) const;
	UPROPERTY(Config, EditAnywhere) TSoftClassPtr<ACatFishingRodActor> RodActorClass;
	UPROPERTY(Config, EditAnywhere) TSoftClassPtr<ACatFishingHookActor> HookActorClass;
	UPROPERTY(Config, EditAnywhere) TSoftClassPtr<ACatFishEncounterActor> FishEncounterActorClass;

	/** 当前鱼 Mesh 在 UniformScale=1 时所代表的重量。当前共用一套鱼 Mesh，未来可迁入鱼种表现 DA。 */
	UPROPERTY(Config, EditAnywhere, Category="FishScale", meta=(ClampMin="0.001", Units="kg"))
	double FishMeshReferenceWeightKilograms = 1.0;

	/** 体重换算后的最小/最大统一视觉缩放，只裁 Mesh 表现，不改变任何玩法碰撞。 */
	UPROPERTY(Config, EditAnywhere, Category="FishScale", meta=(ClampMin="0.01"))
	double FishMeshMinimumUniformScale = 0.75;
	UPROPERTY(Config, EditAnywhere, Category="FishScale", meta=(ClampMin="0.01"))
	double FishMeshMaximumUniformScale = 1.75;
	/**
	 * 服务器确认抛竿并让 Hook 进入 CastFlight 后，由每台客户端在抛竿者角色上本地播放的 Montage。
	 * Montage 本身不复制；复制的是 CastFlight 这个玩法事实，因此主机、发起客户端和旁观客户端走同一触发条件。
	 */
	UPROPERTY(Config, EditAnywhere, Category="Animation")
	TSoftObjectPtr<UAnimMontage> CastMontage;

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
