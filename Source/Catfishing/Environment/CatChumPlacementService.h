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
	/** 提交玩家打窝命令；服务只协调水域、窝点和库存扣量，窝料实例事实优先来自正式库存。 */
	FCatPlaceChumResult PlaceChum(APlayerController* RequestingController,
		const FCatPlaceChumCommand& Command);
};
