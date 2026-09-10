#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/ItemTooltip/CatItemTooltipModel.h"
#include "CatItemTooltipWidget.generated.h"

class UBorder;
class UImage;
class UTextBlock;

/** Aegis 悬停框的 MVC View；只消费快照和显示命令，布局由迁移的正式 WBP 持有。 */
UCLASS()
class CATFISHING_API UCatItemTooltipWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	/** 更新物品文本和图标；不会重启动画或改变首次进入时的位置。 */
	void RenderItem(const FCatItemTooltipViewData& Data);
	/** 在来源格中心开始显示；屏幕绝对坐标只在此处转换一次。 */
	void ShowAt(const FVector2D& ScreenPosition);
	/** 普通离开保留旧内容淡出；页面退出或解绑时可立即清空可见性。 */
	void HideTooltip(bool bImmediate = false);
protected:
	/** Slate 控件创建后的初始化入口；完成后提示框处于隐藏且不可命中状态，正式布局仍完全来自迁移 WBP。 */
	virtual void NativeOnInitialized() override;
	/** 仅推进当前透明度向目标变化，淡出结束后收起控件。 */
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	/** 原 WBP 的定位根边框；View 写入平移，保持其他布局参数。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UBorder> RootBorder;
	/** 原 WBP 的物品图标；快照缺图时收起，不沿用上一物品图片。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> ItemIconImage;
	/** 原 WBP 的名称文本；只由 RenderItem 写入。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ItemNameText;
	/** 原 WBP 的说明文本；只由 RenderItem 写入。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> ItemDescriptionText;
	/** 说明下方的实例信息区；RenderItem 更新重量、耐久或收起空区域。 */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> InstanceDetailsText;
	/** 从透明到完全显示的秒数；WBP 可调，默认与 Aegis 一致。 */
	UPROPERTY(EditDefaultsOnly, Category = "Tooltip|Animation", meta = (ClampMin = "0.0"))
	float FadeInDurationSeconds = 0.2f;
	/** 从完全显示到透明的秒数；离开时读取，零值表示立即隐藏。 */
	UPROPERTY(EditDefaultsOnly, Category = "Tooltip|Animation", meta = (ClampMin = "0.0"))
	float FadeOutDurationSeconds = 0.2f;
private:
	/** 控制器期望的可见方向；动画从当前透明度转向目标，避免换格时闪断。 */
	bool bWantsVisible = false;
};
