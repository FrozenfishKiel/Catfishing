#include "CatUISettings.h"

// Lake View gate 流程：直接返回显式项目配置；不读取阶段 C 诊断开关、不推导当前地图，也不改变任何领域状态。
bool UCatUISettings::IsLakeStatusViewEnabled() const
{
	return bEnableLakeStatusView;
}
