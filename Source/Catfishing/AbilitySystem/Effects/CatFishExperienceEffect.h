#pragma once
#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "CatFishExperienceEffect.generated.h"

/** 已有吃鱼成长的即时效果；仅写入本次经验元属性，不持有累计经验。 */
UCLASS()
class CATFISHING_API UCatGE_FishExperience : public UGameplayEffect
{
	GENERATED_BODY()
public:
	/** 配置即时、加法经验修饰，数值由被食用鱼的实际重量计算后传入。 */
	UCatGE_FishExperience();
	/** 鱼实例构造 Spec 时使用的经验参数身份；GE 只读取这一项，名称不一致会导致本次实物消费被拒绝。 */
	static FGameplayTag GetExperienceTag();
};
