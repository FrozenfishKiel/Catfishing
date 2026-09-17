#pragma once

#include "CoreMinimal.h"
#include "UI/Inventory/CatInventoryWidget.h"
#include "CatCampInventoryWidget.generated.h"

/** 正式营地 WBP 的原生父类；数据源由打开页面时注入，嵌套背包绑定自己的库存，不再按聚合数组分流。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatCampInventoryWidget : public UCatInventoryWidget
{
	GENERATED_BODY()
public:
	/** 接收共享 Model 的已确认追踪投影；无追踪隐藏推荐区，有追踪显示鱼名和指定物品图片，缺图留空。 */
	void RenderTracking(const struct FCatCollectionEntryView* Entry);
protected:
	/** 构建现有库存后绑定关闭按钮；不接管拖放和装配请求。 */
	virtual void NativeConstruct() override;
	/** 移出页面时解除关闭绑定并清空推荐，库存监听由父类清理。 */
	virtual void NativeDestruct() override;
	/** 列表变化时沿用父类重建格子，再更新真实占用数量和容量。 */
	virtual void RefreshInventorySlots() override;
private:
	/** 页面内追踪区域；控制器根据确认状态显隐，无独立持久化状态。 */
	UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<class UPanelWidget> TrackingPanel;
	/** 当前追踪鱼名；只从脱敏后的共享投影写入。 */
	UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> TrackingNameText;
	/** 推荐鱼饵图；推荐 ID 经总表解析后写入，未配置留空。 */
	UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<class UImage> RecommendedBaitImage;
	/** 推荐窝料图；推荐 ID 经总表解析后写入，不自动猜选。 */
	UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<class UImage> RecommendedChumImage;
	/** 仓库占用格数与实际容量；库存列表刷新时更新，不使用概念图的虚构数量。 */
	UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> CapacityText;
	/** 右上角关闭按钮；仅转交既有库存关闭入口。 */
	UPROPERTY(Transient, meta=(BindWidgetOptional)) TObjectPtr<class UButton> CloseInventoryButton;
};
