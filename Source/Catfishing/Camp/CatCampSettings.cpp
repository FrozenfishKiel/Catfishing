#include "Camp/CatCampSettings.h"

// 营地 gate 流程：只接受显式启用和有限正交互范围；未裁时所有营地命令 fail-closed，Actor 仍可作为固定美术宿主。
bool UCatCampSettings::IsRuntimeReady() const
{
	return bEnableCampRuntime && FMath::IsFinite(InteractionRadiusCentimeters) && InteractionRadiusCentimeters > 0.0;
}
