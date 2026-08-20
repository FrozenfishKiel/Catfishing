#include "UI/CatUISettings.h"

// Lake View gate 流程：直接返回显式项目配置；不读取阶段 C 诊断开关、不推导当前地图，也不改变任何领域状态。
bool UCatUISettings::IsLakeStatusViewEnabled() const
{
	return bEnableLakeStatusView;
}

// 命令面板 gate 流程：直接返回显式项目配置；它独立于 Lake 状态 View 开关，但子系统只在状态 View 已装配的路径里才会读它。
bool UCatUISettings::IsCommandPanelEnabled() const
{
	return bEnableCommandPanel;
}
