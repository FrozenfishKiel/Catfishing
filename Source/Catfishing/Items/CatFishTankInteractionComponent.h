#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CatFishTankInteractionComponent.generated.h"

class ACatFishTankActor;

/** 共享鱼缸 Actor 的背包 UI 助手；不是第二个交互入口，E 键只调用 Owner 上的 ICatInteractable::Interact。 */
UCLASS(ClassGroup = (Catfishing), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class CATFISHING_API UCatFishTankInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 构造鱼缸交互默认值；组件不 Tick，只等待本地交互扫描器调用。 */
	UCatFishTankInteractionComponent();

	/** 把 Owner 鱼缸的复制组件作为外部容器上下文交给本地背包。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Items")
	bool OpenInventoryForPlayer(APlayerController* PlayerController);

private:
	/** 当前交互目标代表的共享鱼缸 Actor；只有它成立时，本组件才知道要把哪份容器复制组件交给背包。 */
	ACatFishTankActor* GetOwningFishTank() const;
};
