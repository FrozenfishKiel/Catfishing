#include "UI/CatUISettings.h"

#include "UI/CatLakeReachWidget.h"

// 构造流程：为正式 LakeReach WBP 写入稳定软类路径；如果项目配置覆盖该值，DeveloperSettings 会按配置替换，不需要改 DefaultGame.ini。
UCatUISettings::UCatUISettings()
{
	LakeReachWidgetClass = TSoftClassPtr<UCatLakeReachWidget>(
		FSoftClassPath(TEXT("/Game/UI/WBP_CatLakeReach.WBP_CatLakeReach_C")));
}

// LakeReach View gate 流程：直接返回显式项目配置；不读取诊断开关、不推导当前地图，也不改变任何领域状态。
bool UCatUISettings::IsLakeReachViewEnabled() const
{
	return bEnableLakeReachView;
}

// WBP 类加载流程：同步解析配置的软类并验证它仍继承正式 View 基类；失败返回空，让 PageController fail-closed。
TSubclassOf<UCatLakeReachWidget> UCatUISettings::LoadLakeReachWidgetClass() const
{
	UClass* LoadedClass = LakeReachWidgetClass.LoadSynchronous();
	if (!LoadedClass || !LoadedClass->IsChildOf(UCatLakeReachWidget::StaticClass()))
	{
		return nullptr;
	}
	return LoadedClass;
}
