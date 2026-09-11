#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "UI/WorldInfo/CatWorldInfoTypes.h"
#include "CatWorldInfoComponent.generated.h"

class APlayerController;
class UCatWorldInfoWidget;

/** 可挂到任意 Actor 的只读信息锚点；策略和数据由对象扩展，公共 UI 不依赖 Actor 的具体类型。 */
UCLASS(Blueprintable, ClassGroup=(UI), meta=(BlueprintSpawnableComponent))
class CATFISHING_API UCatWorldInfoComponent : public USceneComponent
{
	GENERATED_BODY()
public:
	/** 关闭组件逐帧运行并设置正式 WBP 默认路径；不会创建替代控件。 */
	UCatWorldInfoComponent();
	/** 对象自己的显示资格与展开层级；距离、焦点来自公共观察，蓝图可覆盖状态相关策略。 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="World Info")
	ECatWorldInfoDetail EvaluateDisplay(APlayerController* Viewer, bool bFocused, double DistanceCentimeters) const;
	/** 内置三策略求值；不调用 CanInteract，因此禁用对象仍可公开说明。 */
	virtual ECatWorldInfoDetail EvaluateDisplay_Implementation(APlayerController* Viewer, bool bFocused, double DistanceCentimeters) const;
	/** 按当前观察者构建只读信息；返回 false 时隐藏，不复用上一对象的数据。 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="World Info")
	bool BuildInfo(APlayerController* Viewer, ECatWorldInfoDetail Detail, FCatWorldInfoViewData& OutData) const;
	/** 普通蓝图对象可直接使用 StaticInfo；复杂对象覆盖此入口读取自己的事实源。 */
	virtual bool BuildInfo_Implementation(APlayerController* Viewer, ECatWorldInfoDetail Detail, FCatWorldInfoViewData& OutData) const;
	/** 数据源变化后请求本地视图重建；蓝图和复制回调都使用此入口，不发送网络命令。 */
	UFUNCTION(BlueprintCallable, Category="World Info")
	void NotifyInfoChanged();
	/** 显示控制器读取本地内容通知序号，与上次渲染序号比较以决定是否重读；直接返回计数，不参与业务版本或并发校验。 */
	uint32 GetInfoSerial() const;
	/** 该对象采用的内置显隐策略；设计者可逐实例配置，运行时仍可由 EvaluateDisplay 覆盖。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World Info")
	ECatWorldInfoPolicy DisplayPolicy = ECatWorldInfoPolicy::NearbySummary;
	/** 从玩家身体到对象原点的可读距离上限，单位厘米；设计者配置，EvaluateDisplay 读取，控制器同时用它声明观察射线范围，不改变按键交互距离。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World Info", meta=(ClampMin="1", Units="cm"))
	float DisplayDistanceCentimeters = 500.0f;
	/** 信息牌拥挤时的排序权重；设计者配置、控制器读取，焦点对象仍优先，其余对象先按较高权重再按较近距离保留。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World Info")
	int32 DisplayPriority = 0;
	/** 本对象是否要求遮挡时隐藏信息；设计者配置、控制器读取，射线只观察相机至锚点，不用于权威交互裁决。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World Info")
	bool bHideWhenOccluded = true;
	/** 对象的信息展示总开关；设计者或蓝图写入，策略与控制器读取，只影响阅读，不停用对象原来的玩法或交互。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World Info")
	bool bInfoEnabled = true;
	/** 本对象正式信息牌的软类引用；构造时给出默认值，设计者可替换，控制器同步加载；缺失只关闭本对象展示，不创建原生替身。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World Info")
	TSoftClassPtr<UCatWorldInfoWidget> WidgetClass;
	/** 没有动态数据源时的直接展示内容；设计者或蓝图写入，默认 BuildInfo 复制给视图；运行时改值后须调用 NotifyInfoChanged 使已显示内容重读。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World Info")
	FCatWorldInfoViewData StaticInfo;
protected:
	/** 进入世界后登记锚点；本地玩家可能稍后才创建，由注册表保存发现入口。 */
	virtual void BeginPlay() override;
	/** 离开世界前注销锚点，避免旅行、销毁后残留信息牌。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
	/** 本地内容变更通知计数；NotifyInfoChanged 写入，UI 仅读取，不参与复制或存档。 */
	uint32 InfoSerial = 1;
};
