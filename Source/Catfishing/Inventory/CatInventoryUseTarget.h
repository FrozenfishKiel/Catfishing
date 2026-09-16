#pragma once

#include "CoreMinimal.h"
#include "CatInventoryUseTarget.generated.h"

/** 使用键按下时的目标意图；单位为世界厘米，领域服务必须复核射线与目标。 */
USTRUCT()
struct FCatInventoryUseTarget
{
	GENERATED_BODY()
	UPROPERTY() bool bHasViewRay = false;
	UPROPERTY() FVector ViewOrigin = FVector::ZeroVector;
	UPROPERTY() FVector ViewDirection = FVector::ZeroVector;
	UPROPERTY() TObjectPtr<AActor> Actor = nullptr;
};
