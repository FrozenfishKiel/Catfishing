#pragma once

#include "CoreMinimal.h"
#include "UI/WorldInfo/CatWorldInfoComponent.h"
#include "CatAltarWorldInfoComponent.generated.h"

class UCatFishTankWorldInfoComponent;

/** 祭坛的只读信息适配器；从 Run、祭坛摘要及显式营地鱼缸读取，不拥有献祭或确认状态。 */
UCLASS(Blueprintable, ClassGroup=(UI), meta=(BlueprintSpawnableComponent))
class CATFISHING_API UCatAltarWorldInfoComponent : public UCatWorldInfoComponent
{
	GENERATED_BODY()
public:
	/** 设置八米完整展示与低频依赖观察；这些只是默认配置，蓝图仍可逐对象修改。 */
	UCatAltarWorldInfoComponent();
	/** 控制器选定可见层级后读取世界进度、今日目标、储备/地面点数、确认计数和最近成功结果；仅挂载者不是祭坛时返回 false，未知字段显示不可用。 */
	virtual bool BuildInfo_Implementation(APlayerController* Viewer, ECatWorldInfoDetail Detail, FCatWorldInfoViewData& OutData) const override;
	/** 低频观察 Run 与关联鱼缸通知序号；数据或关系变化才请求本地内容刷新，不枚举世界或发送 RPC。 */
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
private:
	/** 上次已观察的 Run 修订；Tick 比较并更新，仅用于抑制重复 UI 通知，不缓存玩法快照，无宿主时记为 -1。 */
	int64 ObservedRunRevision = -1;
	/** 上次已观察的鱼缸摘要通知序号；Tick 读取源序号并更新，变化时通知祭坛视图重读储备，无源时记为零。 */
	uint32 ObservedTankSerial = 0;
	/** 上次显式关系解析到的鱼缸提供者；Tick 比较并更新弱引用，以识别迟到、替换或销毁，不延长生命周期。 */
	TWeakObjectPtr<UCatFishTankWorldInfoComponent> ObservedTank;
};
