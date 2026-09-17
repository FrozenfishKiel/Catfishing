#pragma once

#include "CoreMinimal.h"
#include "Environment/CatChumFieldTypes.h"
#include "Subsystems/WorldSubsystem.h"

#include "CatChumPlacementService.generated.h"

class APlayerController;

UCLASS()
class CATFISHING_API UCatChumPlacementService final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 校验正式来源与水域并准备窝点，再同步调用 PayResource 支付已检查的资源；回调不被保存，成功后激活窝点并发布库存通知。 */
	FCatPlaceChumResult PlaceChum(APlayerController* RequestingController,
		const FCatPlaceChumCommand& Command, TFunctionRef<bool()> PayResource);
};
