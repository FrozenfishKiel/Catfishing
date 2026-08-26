#pragma once

#include "CoreMinimal.h"
#include "Components/StateTreeComponentSchema.h"
#include "CatFishingSessionStateTreeSchema.generated.h"

/** ST_FishingSession 专用 Schema：把资产 Context Actor 固定为 ACatFishingSession。 */
UCLASS()
class CATFISHING_API UCatFishingSessionStateTreeSchema : public UStateTreeComponentSchema
{
	GENERATED_BODY()

public:
	UCatFishingSessionStateTreeSchema();
};
