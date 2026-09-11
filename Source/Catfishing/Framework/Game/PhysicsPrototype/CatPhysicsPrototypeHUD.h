#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CatPhysicsPrototypeHUD.generated.h"

/** Read-only controls and hand feedback for the isolated physics experiment. */
UCLASS()
class CATFISHING_API ACatPhysicsPrototypeHUD : public AHUD
{
	GENERATED_BODY()
public:
	virtual void DrawHUD() override;
};
