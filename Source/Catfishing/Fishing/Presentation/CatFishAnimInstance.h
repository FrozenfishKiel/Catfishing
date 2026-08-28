#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Fishing/CatFishingTypes.h"
#include "CatFishAnimInstance.generated.h"

/**
 * FishEncounter 的项目级动画实例基类。
 *
 * 它只读取服务器复制的表现事实，不从 Actor 位移反推速度，也不修改任何玩法状态或 Transform。
 * AnimBP 重设父类后，只需把 SwimPlayRate 接到游泳 Sequence Player/BlendSpace 的播放倍率。
 */
UCLASS(Blueprintable, Transient)
class CATFISHING_API UCatFishAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

protected:
	/** 动画资源以该自由游速制作时应按 1.0 倍播放，单位 cm/s。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Fishing|Animation",
		meta=(ClampMin="1.0", Units="cm/s"))
	float ReferenceSwimSpeedCentimetersPerSecond = 75.0f;

	/** 活鱼游泳循环允许的最低播放倍率；None/AutoHauling 不使用此下限，输出中性 1.0。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Fishing|Animation", meta=(ClampMin="0.0"))
	float MinimumSwimPlayRate = 0.5f;

	/** 活鱼游泳循环允许的最高播放倍率。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Fishing|Animation", meta=(ClampMin="0.0"))
	float MaximumSwimPlayRate = 2.0f;

	/** 播放倍率追向目标值的插值速度；0 表示立即切换。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Fishing|Animation", meta=(ClampMin="0.0"))
	float SwimPlayRateInterpolationSpeed = 8.0f;

	/** 复制到本机的高层运动意图，可在 AnimGraph 中切换平静、挣扎和力竭状态。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Fishing|Animation")
	ECatFishMotionIntent MotionIntent = ECatFishMotionIntent::None;

	/** 在线长/岸线约束前由鱼主动选择的自由游速；鱼被挡住时仍保持冲刺值。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Fishing|Animation", meta=(Units="cm/s"))
	float IntendedSwimSpeedCentimetersPerSecond = 0.0f;

	/** IntendedSwimSpeed / ReferenceSwimSpeed 得到并经过本地平滑、裁剪后的最终播放倍率。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Fishing|Animation")
	float SwimPlayRate = 1.0f;

	/** 鱼游向与鱼线向外方向夹角余弦，供方向性受力叠加层使用。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Fishing|Animation")
	float FishLineAlignment = 0.0f;

	/** 连续鱼线负载 [0,1]，适合驱动身体弯曲、水花或受力抖动，不作为基础游速。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Fishing|Animation")
	float NormalizedLineLoad = 0.0f;

	/** 服务器确认的强对抗开关，适合切换爆发性 Montage/VFX/SFX。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category="Fishing|Animation")
	bool bStrongConfrontation = false;

private:
	void RefreshFromFishOwner(float DeltaSeconds, bool bSnapPlayRate);
};
