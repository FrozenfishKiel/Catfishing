#pragma once

#include "CoreMinimal.h"
#include "Tickable.h"
#include "UI/WorldInfo/CatWorldInfoTypes.h"
#include "UObject/Object.h"
#include "CatWorldInfoController.generated.h"

class APlayerController;
class UCatWorldInfoComponent;
class UCatWorldInfoWidget;

/** 一个玩家观察某个锚点的显示缓存；不与另一玩家共享显隐、位置或控件。 */
USTRUCT()
struct FCatWorldInfoDisplay
{
	GENERATED_BODY()
	/** 当前信息源的非拥有引用；源销毁后控制器移除本地视图。 */
	TWeakObjectPtr<UCatWorldInfoComponent> Source;
	/** 由本地玩家创建的正式 WBP；反射引用保证可见及复用期间不会被 GC 清掉。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatWorldInfoWidget> Widget = nullptr;
	/** 最近一次策略判定的层级；定期观察写入，内容与布局按它更新。 */
	ECatWorldInfoDetail Detail = ECatWorldInfoDetail::Hidden;
	/** 最近一次观察是否命中本对象；用于拥挤时保留玩家正在看的信息牌。 */
	bool bFocused = false;
	/** 最近一次距离，单位厘米；用于稳定优先级排序，不用于服务器交互许可。 */
	double Distance = 0;
	/** 这个玩家已经消费的数据通知序号；与源不同时重读，不参与业务并发控制。 */
	uint32 RenderedSerial = 0;
	/** 当前已尝试的正式 WBP 软路径；配置替换时移除旧视图并重新加载，不保留旧对象布局。 */
	FSoftObjectPath RequestedWidgetClass;
	/** 当前路径加载失败的标志；避免重复刷日志，配置替换或组件重建后可再次尝试。 */
	bool bClassUnavailable = false;
};

/** 本地玩家的信息牌控制器；只在注册目录中选源并投影正式 WBP，不拥有玩法事实或另一套交互输入。 */
UCLASS()
class CATFISHING_API UCatWorldInfoController : public UObject, public FTickableGameObject
{
	GENERATED_BODY()
public:
	/** 绑定本地玩家，首次 Tick 从当前世界目录发现已存在锚点。 */
	void Bind(APlayerController* Controller);
	/** 移除本玩家所有信息牌并释放观察缓存；旅行和 LocalPlayer UI 拆除时调用。 */
	void Unbind();
	/** 以低频更新候选、逐帧投影可见项；只读取源数据和镜头，不发送 RPC。 */
	virtual void Tick(float DeltaTime) override;
	/** 只有绑定本地控制器的实例运行，默认对象与解绑实例不注册有效工作。 */
	virtual bool IsTickable() const override;
	/** 把 Tick 归入当前控制器世界，避免多 PIE 世界串帧。 */
	virtual UWorld* GetTickableGameObjectWorld() const override;
	/** 为 Tickable 接口提供独立计时分组；引擎据此把本类刷新和投影成本记入性能统计，不与玩法 Tick 混算。 */
	virtual TStatId GetStatId() const override;
	/** 可复查的当前可见牌数量；调试与已有运行验证只读取，不影响选择。 */
	UFUNCTION(BlueprintPure, Category="World Info")
	int32 GetVisibleInfoCount() const;
protected:
	/** UObject 销毁兜底移除视图，避免控制器更换时保留旧玩家 UI。 */
	virtual void BeginDestroy() override;
private:
	/** 从注册表刷新源并求值距离、焦点、遮挡；只对候选创建正式 WBP。 */
	void RefreshCandidates();
	/** 本显示控制器所属的玩家；Bind 写入，Unbind 释放，不延长 Actor 生命周期。 */
	TWeakObjectPtr<APlayerController> BoundController;
	/** 本玩家已经发现的信息源与各自控件；只持有视图，不持有 Actor。 */
	UPROPERTY(Transient)
	TArray<FCatWorldInfoDisplay> Displays;
	/** 下次候选刷新前剩余的秒数；Tick 累计，不使用 Run 的玩法时间和截止点。 */
	float RefreshRemainingSeconds = 0;
};
