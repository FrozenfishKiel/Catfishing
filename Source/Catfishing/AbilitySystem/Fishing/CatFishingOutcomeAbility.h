#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "CatFishingOutcomeAbility.generated.h"

/** Fishing 结果表现 Ability 的基类；只承接服务器发起的结果呈现，不参与玩家输入预测。 */
UCLASS(Abstract)
class CATFISHING_API UCatGA_FishingOutcomeBase : public UGameplayAbility
{
	GENERATED_BODY()

public:
	/** 配置结果 Ability 的服务器发起策略，避免客户端在权威结论产生前预测播放成功或失败表现。 */
	UCatGA_FishingOutcomeBase();
};
