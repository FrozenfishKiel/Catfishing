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
	/** 开始恢复前要求的连续静止秒数。设计里没有这道闸，2026-09-11 仍未裁，保留现状待裁。 */
	UPROPERTY(Config, EditAnywhere, Category="Recovery", meta=(ClampMin="0"))
	double RecoveryDelaySeconds = 2.0;
	/** 钓鱼搏斗中按 W 前移（靠水）的腿部附加，点/秒。只看操作方向，不看位移也不看力量差。 */
	UPROPERTY(Config, EditAnywhere, Category="Fishing", meta=(ClampMin="0"))
	double FishingForwardMoveStaminaPerSecond = 1.5;
	/** 钓鱼搏斗中按 S 后退（离水）的腿部附加，点/秒。与前移同为按秒常数，W/S 在钓鱼中不翻转。 */
	UPROPERTY(Config, EditAnywhere, Category="Fishing", meta=(ClampMin="0"))
	double FishingBackwardMoveStaminaPerSecond = 3.0;
	UPROPERTY(Config, EditAnywhere, Category="Recovery", meta=(ClampMin="0.01", ClampMax="1"))
	double ExhaustionResumeRatio = 0.2;
	bool IsValid() const;
};
