#pragma once

#include "CoreMinimal.h"
#include "UI/Interaction/CatInteractionTargetComponent.h"
#include "CatFishTankInteractionComponent.generated.h"

class ACatFishTankActor;

/** 挂在共享鱼缸上的交互适配组件；它只把确认键转换成“打开带外部容器上下文的背包”，不直接写 Items 容器。 */
UCLASS(ClassGroup = (Catfishing), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class CATFISHING_API UCatFishTankInteractionComponent : public UCatInteractionTargetComponent
{
	GENERATED_BODY()

public:
	/** 构造鱼缸交互默认值；组件不 Tick，只等待本地交互扫描器调用。 */
	UCatFishTankInteractionComponent();

	/** 鱼缸目标的可交互判断；交互扫描器调用时先沿用通用启用条件，再要求组件确实挂在共享鱼缸 Actor 上。 */
	virtual bool CanInteract_Implementation(APlayerController* PlayerController) const override;

	/** 通用交互确认入口；把鱼缸复制组件作为外部容器上下文交给本地背包，返回值只表示 UI 是否成功打开。 */
	virtual bool Interact_Implementation(APlayerController* PlayerController) override;

	/** 鱼缸提示文本；交互 UI 查询时默认返回“鱼缸”，允许蓝图覆盖组件上的目标名称。 */
	virtual FText GetInteractionTargetText_Implementation(APlayerController* PlayerController) const override;

private:
	/** 当前交互目标代表的共享鱼缸 Actor；只有它成立时，本组件才知道要把哪份容器复制组件交给背包。 */
	ACatFishTankActor* GetOwningFishTank() const;
};
