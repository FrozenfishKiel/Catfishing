#pragma once

#include "CoreMinimal.h"
#include "UI/Interaction/CatInteractionTargetComponent.h"
#include "CatFishGuardInteractionComponent.generated.h"

class ACatFishGuardActor;

/** 挂在鱼护箱子上的交互适配组件；它只负责打开库存 UI，不提供任何 Items 写口。 */
UCLASS(ClassGroup = (Catfishing), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class CATFISHING_API UCatFishGuardInteractionComponent : public UCatInteractionTargetComponent
{
	GENERATED_BODY()

public:
	/** 构造鱼护箱子交互默认提示和半径；实际容器权限仍由 Items 与 UI 命令层裁决。 */
	UCatFishGuardInteractionComponent();

	/** 鱼护箱子的可交互判断；通用启用条件成立且组件确实挂在鱼护 Actor 上才允许提示。 */
	virtual bool CanInteract_Implementation(APlayerController* PlayerController) const override;

	/** 通用交互确认入口；把该鱼护箱子作为外部容器上下文打开。 */
	virtual bool Interact_Implementation(APlayerController* PlayerController) override;

	/** 鱼护提示文本读取；默认返回“鱼护”，蓝图仍可覆盖组件上的目标名称。 */
	virtual FText GetInteractionTargetText_Implementation(APlayerController* PlayerController) const override;

private:
	/** 当前交互目标代表的鱼护箱子 Actor；误挂到其他 Actor 时保持 fail-closed。 */
	ACatFishGuardActor* GetOwningFishGuard() const;
};
