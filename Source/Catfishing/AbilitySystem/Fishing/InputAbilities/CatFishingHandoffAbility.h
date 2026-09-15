#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Fishing/CatFishingGameplayAbility.h"
#include "CatFishingHandoffAbility.generated.h"

/**
 * 换人握手输入 Ability（多人钓鱼附篇 §2.4）；按下时提交一次「换人」意图。
 *
 * 同一个键在两种身份下含义不同，但这里不分叉：本地只发意图，挂牌（主钓手）还是接手（岸上替补）
 * 由服务器按当时的身份判——客户端对自己是不是主控的认知可能已经过期。
 *
 * 键位随装备栏重构另定（09-11 裁决①：「E 换人」保留，指的是主钓手交接，不是已作废的辅助位）。
 * 授予与按键绑定都在资产侧（AbilitySet ＋ AbilityInputConfig ＋ InputAction），C++ 侧到这里为止。
 */
UCLASS()
class CATFISHING_API UCatGA_FishingHandoff : public UCatFishingGameplayAbility
{
	GENERATED_BODY()

public:
	/** 绑定换人 Ability Tag，授予后由对应输入 Tag 激活。 */
	UCatGA_FishingHandoff();

	/** 输入按下时提交一次换人命令；提交失败会取消本次 Ability，成功后立即结束。 */
	virtual void ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};
