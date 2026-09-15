#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatPhysicalEffortSettings.generated.h"

/** Personal cat motion prices. Distances are cm; prices are stamina points per metre. */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Catfishing Physical Effort"))
class CATFISHING_API UCatPhysicalEffortSettings : public UDeveloperSettings
{
	GENERATED_BODY()
public:
	UPROPERTY(Config, EditAnywhere, Category="Effort", meta=(ClampMin="0"))
	double StaminaPerUnfulfilledMeter = 2.0;
	/** Equivalent full-effort intent for a stationary stance, independent of walk speed. */
	UPROPERTY(Config, EditAnywhere, Category="Effort", meta=(ClampMin="0"))
	double SupportReferenceSpeedCmS = 100.0;
	/** 搏斗外自然恢复速率（点/秒）。钓鱼规则 §6.2：站立、岸上替补、摸鱼一律同速率，没有场景乘数。 */
	UPROPERTY(Config, EditAnywhere, Category="Recovery", meta=(ClampMin="0"))
	double RecoveryPerSecond = 5.0;
	// 墓碑（2026-09-14）：RecoveryDelaySeconds / ExhaustionResumeRatio 删除；
	// Knowledge/Design/设计修改记录.md 2026-09-13 裁决⑥，不设静止等待或体力再入比例。
	/** 钓鱼搏斗中按 W 前移（靠水）的腿部附加，点/秒。只看操作方向，不看位移也不看力量差。 */
	UPROPERTY(Config, EditAnywhere, Category="Fishing", meta=(ClampMin="0"))
	double FishingForwardMoveStaminaPerSecond = 1.5;
	/** 钓鱼搏斗中按 S 后退（离水）的腿部附加，点/秒。与前移同为按秒常数，W/S 在钓鱼中不翻转。 */
	UPROPERTY(Config, EditAnywhere, Category="Fishing", meta=(ClampMin="0"))
	double FishingBackwardMoveStaminaPerSecond = 3.0;
	bool IsValid() const;
};
