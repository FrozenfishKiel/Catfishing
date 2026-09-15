#pragma once

#include "CoreMinimal.h"
#include "UI/Inventory/CatInventoryWidget.h"
#include "CatFishGuardInventoryWidget.generated.h"

class ACatFishBuyerActor;
class ACatFishGuardActor;
class UButton;
class UTextBlock;
class UWidget;

/** 正式鱼护 WBP 的原生父类；单鱼操作进入统一物品菜单，只保留鱼护本身的全部出售容器操作。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatFishGuardInventoryWidget : public UCatInventoryWidget
{
	GENERATED_BODY()
protected:
	/** 构建时绑定鱼护全部出售按钮并初始化可见性；不再创建单鱼选择或报价入口。 */
	virtual void NativeConstruct() override;
	/** 销毁时解除全部出售按钮；鱼护和买家始终通过即时弱读取解析。 */
	virtual void NativeDestruct() override;
	/** 每帧确认鱼护仍在地面；买家资格按 0.1 秒节流刷新，失地则关闭外部库存页。 */
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	/** 库存通知或回执先刷新通用格子，再重算全部出售入口，避免库存变化后遗留可点按钮。 */
	virtual void RefreshInventorySlots() override;
private:
	/** 从当前显示库存解析地面鱼护宿主；普通库存、已被拾起的鱼护或错误库存一律返回空。 */
	ACatFishGuardActor* ResolveGroundedGuard() const;
	/** 按地面资格、附近买家和当前全部鱼报价刷新唯一容器级出售入口。 */
	void RefreshSellAllAction();
	/** 同步正式 WBP 的全部出售区显隐和可用性；未接线资产安全跳过。 */
	void SetSellAllActionVisible(bool bVisible);
	/** 全部出售按钮入口；只从当前打开且仍在地面的鱼护库存收集鱼实例 ID。 */
	UFUNCTION()
	void HandleSellAllClicked();
	/** 向当前可用买家提交固定鱼实例 ID；服务器重新验证距离、鱼护和每条鱼，UI 不改变库存。 */
	void SubmitFishSale(ACatFishGuardActor* Guard, const TArray<FGuid>& FishInstanceIds);
	/** 正式鱼护 WBP 的全部出售按钮；只在当前地面鱼护位于买家范围时显示和启用。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> SellAllFishButton;
	/** 正式鱼护 WBP 的全部出售区域；没有绑定时按钮仍可独立隐藏。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UWidget> SellActionsPanel;
	/** 本鱼护全部有效鱼逐条取整后的总预估价；异常条目或不可报价时显示不可用。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SellAllFishPriceText;
	/** 自上次附近买家查询经过的本机秒数；达到 0.1 秒才扫描，避免逐帧世界查询。 */
	float BuyerRefreshElapsedSeconds = 0.0f;
};
