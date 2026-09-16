#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Collection/CatCollectionWidget.h"
#include "CatFishCardWidget.generated.h"

/** 页面接收一个已解锁鱼种的选择意图；库存追踪卡不绑定此委托。 */
DECLARE_DELEGATE_OneParam(FCatFishCardSelected, int32);

/** 可复用鱼卡；图鉴页和库存追踪使用同一控件，不各自解析物品数据。 */
UCLASS()
class CATFISHING_API UCatFishCardWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	/** 接收已经脱敏的鱼卡投影，更新图片与已确认追踪表现；不能自行开放未知字段。 */
	void RenderCard(const FCatCollectionEntryView& Entry, bool bTracked);
	/** 已解锁卡被点击时通知页面；未绑定时仅作只读追踪卡。 */
	FCatFishCardSelected OnSelected;
protected:
	/** 构建独立边框、鱼图与动态文字；未解锁时压暗对应鱼图作为当前获准的占位表现。 */
	virtual TSharedRef<SWidget> RebuildWidget() override;
private:
	/** 当前鱼卡投影；由共享 Model 生成，控件只读。 */
	UPROPERTY(Transient) FCatCollectionEntryView Data;
	/** 鱼图画刷；反射持有纹理引用，Slate 不拥有图片生命周期。 */
	UPROPERTY(Transient) FSlateBrush FishBrush;
	/** 卡片底纹与边框画刷；绘制独立于书本背景。 */
	UPROPERTY(Transient) FSlateBrush FrameBrush;
	/** 正式中文字体；控件保活以供所有文字读取。 */
	UPROPERTY(Transient) TObjectPtr<UObject> FontAsset;
	/** 最近投影确认的追踪态；仅由保存成功后的共享 Model 提供，点击不能自行置真。 */
	bool bIsTracked = false;
};
