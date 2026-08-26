#include "UI/Interaction/CatInteractionTargetComponent.h"

#include "GameFramework/PlayerController.h"

// 构造流程：目标组件只声明交互能力，不主动 Tick；LocalPlayer 扫描器负责定时发现和提示。
UCatInteractionTargetComponent::UCatInteractionTargetComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	InteractionTargetText = FText::FromString(TEXT("交互对象"));
}

// 可交互判断流程：基类只检查启用状态、Controller 和 Owner 有效性；具体对象状态由派生组件或蓝图继续收窄。
bool UCatInteractionTargetComponent::CanInteract_Implementation(APlayerController* PlayerController) const
{
	return bInteractionEnabled && PlayerController && GetOwner();
}

// 交互执行流程：基类不拥有任何功能 UI，因此固定返回 false；派生组件必须显式接管实际打开逻辑。
bool UCatInteractionTargetComponent::Interact_Implementation(APlayerController* PlayerController)
{
	(void)PlayerController;
	return false;
}

// 目标文本流程：返回对象配置的名称；未配置时给出稳定中文兜底，避免提示空白。
FText UCatInteractionTargetComponent::GetInteractionTargetText_Implementation(APlayerController* PlayerController) const
{
	(void)PlayerController;
	return InteractionTargetText.IsEmpty()
		? FText::FromString(TEXT("交互对象"))
		: InteractionTargetText;
}

// 半径读取流程：返回非负半径；扫描方用 0 表示不可被距离命中。
double UCatInteractionTargetComponent::GetInteractionRadiusCentimeters() const
{
	return FMath::Max(0.0, InteractionRadiusCentimeters);
}
