#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CatFishRevealWidget.generated.h"

class UImage;
class UTextBlock;
class UTexture2D;

/** 玩家看完一次首解锁特写的原生广播；LocalPlayer UI 协调层收到后移除浮层并归还焦点。 */
DECLARE_MULTICAST_DELEGATE(FCatFishRevealDismissed);

/** 首次解锁鱼种时的一次性特写投影；它只装展示字段，不持有 Profile 记录，也不代表图鉴写入成功与否。 */
USTRUCT(BlueprintType)
struct FCatFishRevealViewData
{
	GENERATED_BODY()

	/** 鱼种展示名；取自鱼定义，未配置展示名时由 Model 退回鱼种 ID。 */
	UPROPERTY(BlueprintReadOnly)
	FText NameText;

	/** 品种介绍；取自鱼定义的图鉴描述，未配置时为空，由 WBP 自行决定留白还是折叠。 */
	UPROPERTY(BlueprintReadOnly)
	FText DescriptionText;

	/** 这一条的真实重量文案（主界面.md:121 的「重量：0.12kg」）；重量非法时为空。 */
	UPROPERTY(BlueprintReadOnly)
	FText WeightText;

	/** 继续提示文案；主界面参考稿里是右下角的「Space 继续」。 */
	UPROPERTY(BlueprintReadOnly)
	FText ContinueHintText;

	/** 鱼种彩页图；取自鱼定义的图鉴贴图，未配置时为空，WBP 不得用占位图冒充。 */
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<UTexture2D> Portrait = nullptr;
};

/**
 * 首次解锁新鱼种的特写浮层基类（ui 表第 14 行「钓点附近 · 首次解锁新品种 · 自动显示」）。
 *
 * 它是一次性揭示层，不是图鉴页：图鉴页随时可开、读的是 durable 快照；这一层只在收集层第一次落盘的那一刻出现，
 * 看完就收。因此它不持有 Collection Model，也不提供任何翻页或筛选入口。
 *
 * 正式 WBP 资产不在本轮范围。C++ 侧做到「资产一挂上就能用」：控件全部是 BindWidgetOptional，
 * 缺控件时浮层照样能开、能关、能吞 Space，只是没有文字。
 */
UCLASS(Blueprintable)
class CATFISHING_API UCatFishRevealWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 写入一次揭示内容并记录本次展示；WBP 只渲染，不据此推断图鉴记录是否写成功。 */
	void RenderReveal(const FCatFishRevealViewData& ViewData);

	/** 暴露最近一次揭示内容给蓝图表现；它不持有鱼定义或 Profile，蓝图拿不到写口。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Collection")
	const FCatFishRevealViewData& GetLastRevealViewData() const;

	/** 提交「看完了」意图；Space、点击与 WBP 自己的动画结束都汇到这里，由订阅方决定何时移出视口。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Collection")
	void RequestDismiss();

	/** 玩家已经看完这一次揭示；订阅方决定何时把浮层移出视口，Widget 自己不结束自己的生命周期。 */
	FCatFishRevealDismissed OnDismissRequested;

protected:
	/** 入视口后取得焦点，使 Space 能被本层接住；不修改输入模式，玩家仍可移动、交互和继续钓鱼。 */
	virtual void NativeConstruct() override;

	/** Space 与 Enter 都收；其余按键一律放行——这一层不打断操作（主界面.md「你仍可移动，交互和继续钓鱼」）。 */
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** WBP 可选渲染扩展点；Designer 可用它接出场动画或镜头推进，原生层不替蓝图决定演出。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Catfishing|Collection")
	void BP_RenderReveal(const FCatFishRevealViewData& ViewData);

private:
	/** 最近一次写入的揭示内容；本对象不持有鱼定义资产的强引用之外的任何领域状态。 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Catfishing|Collection", meta = (AllowPrivateAccess = "true"))
	FCatFishRevealViewData LastRevealViewData;

	/** WBP Designer 中的鱼种名控件。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> FishNameTextBlock;

	/** WBP Designer 中的品种介绍控件。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> FishDescriptionTextBlock;

	/** WBP Designer 中的重量控件。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> FishWeightTextBlock;

	/** WBP Designer 中的继续提示控件。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ContinueHintTextBlock;

	/** WBP Designer 中的鱼种彩页图控件；鱼定义没配贴图时保持折叠，不显示占位图。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UImage> FishPortraitImage;
};
