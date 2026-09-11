#include "UI/WorldInfo/CatWorldInfoRegistry.h"
#include "UI/WorldInfo/CatWorldInfoComponent.h"

// 登记流程：只接纳本世界的有效组件；清掉过期项后去重，不延长任何 Actor 的生命。
void UCatWorldInfoRegistry::RegisterSource(UCatWorldInfoComponent* Source)
{
	if (!IsValid(Source) || Source->GetWorld() != GetWorld()) return;
	Sources.RemoveAll([](const auto& Entry) { return !Entry.IsValid(); });
	Sources.AddUnique(Source);
}

// 退出流程：移除指定锚点及已经销毁的引用，尚在显示的玩家会在下一帧收掉视图。
void UCatWorldInfoRegistry::UnregisterSource(UCatWorldInfoComponent* Source)
{
	Sources.RemoveAll([Source](const auto& Entry) { return !Entry.IsValid() || Entry.Get() == Source; });
}

// 目录读取：只返回组件登记的弱引用；视图与业务数据都不进入这个目录。
const TArray<TWeakObjectPtr<UCatWorldInfoComponent>>& UCatWorldInfoRegistry::GetSources() const
{
	return Sources;
}
