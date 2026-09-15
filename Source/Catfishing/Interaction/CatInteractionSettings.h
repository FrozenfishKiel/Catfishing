#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "CatInteractionSettings.generated.h"

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Catfishing Interaction"))
class CATFISHING_API UCatInteractionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/**
	 * 全游戏统一交互半径，厘米（钓鱼规则 §5.5:273「落岸鱼的可拾距离是 1.5 米，全游戏统一交互半径」；
	 * 参数页「线长与收鱼」行 交互半径 1.5 米）。准星容差与落岸鱼拾取共用这一个事实源。
	 * 抄网射程是另一个参数（UCatFishingSettings::ScoopReachCentimeters，现值 2 米），两者互不替代。
	 * 墓碑：原 MaximumTargetingDistanceCentimeters（准星 300 厘米）2026-09-12 并入本字段后删除，
	 * 理由是它和落岸鱼拾取距离本来就应该是同一个数，分开配会让「准星亮了但按不动」。
	 */
	UPROPERTY(Config, EditAnywhere, Category="Targeting", meta=(ClampMin="1.0", Units="cm"))
	double InteractionRadiusCentimeters = 150.0;

	/**
	 * 服务器在统一交互半径之上额外放宽的网络/视点误差余量，厘米。它是技术容差，不是第二个交互半径，
	 * 也不出现在参数页；服务器复核距离＝InteractionRadiusCentimeters + 本值。
	 * 墓碑：2026-09-12 之前准星 300／服务器 350 是两个各自配置的距离，统一半径落地后只保留它们的差额。
	 */
	UPROPERTY(Config, EditAnywhere, Category="Authority", meta=(ClampMin="0.0", Units="cm"))
	double ServerInteractionSlackCentimeters = 50.0;

	/** 读取统一交互半径；非法配置返回 0，调用方据此 fail-closed 而不是退回某个硬编码距离。 */
	double GetInteractionRadiusCentimeters() const
	{
		return FMath::IsFinite(InteractionRadiusCentimeters) && InteractionRadiusCentimeters > 0.0
			? InteractionRadiusCentimeters : 0.0;
	}

	/** 读取服务器复核用的交互距离＝统一半径＋技术余量；统一半径非法时同样返回 0。 */
	double GetServerInteractionDistanceCentimeters() const
	{
		const double Radius = GetInteractionRadiusCentimeters();
		const double Slack = FMath::IsFinite(ServerInteractionSlackCentimeters)
			? FMath::Max(0.0, ServerInteractionSlackCentimeters) : 0.0;
		return Radius > 0.0 ? Radius + Slack : 0.0;
	}

	/** 本地准星检测频率；20 Hz 足以稳定跟随目标，同时不制造每帧接口调用。 */
	UPROPERTY(Config, EditAnywhere, Category="Targeting", meta=(ClampMin="0.016", Units="s"))
	double TargetingIntervalSeconds = 0.05;

	UPROPERTY(Config, EditAnywhere, Category="Targeting")
	TEnumAsByte<ECollisionChannel> TargetingTraceChannel = ECC_Visibility;

	UPROPERTY(Config, EditAnywhere, Category="Targeting")
	bool bTraceComplex = true;

	/**
	 * 旧的独立服务器交互距离。2026-09-12 统一交互半径落地后，落岸鱼拾取与准星容差都改读
	 * GetInteractionRadiusCentimeters()/GetServerInteractionDistanceCentimeters()，本字段只剩祭坛
	 * （Camp/CatAltarActor.cpp:148）一个读者，等营地属主一并收敛后删除。新代码不要再读它。
	 */
	UPROPERTY(Config, EditAnywhere, Category="Authority", meta=(ClampMin="1.0", Units="cm"))
	double MaximumServerInteractionDistanceCentimeters = 350.0;

	UPROPERTY(Config, EditAnywhere, Category="Authority")
	bool bRequireServerLineOfSight = true;

	/** 1 预留为普通白色交互描边。 */
	UPROPERTY(Config, EditAnywhere, Category="Presentation", meta=(ClampMin="1", ClampMax="255"))
	int32 FocusStencilValue = 1;
};
