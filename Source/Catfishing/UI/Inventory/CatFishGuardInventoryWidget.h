#pragma once

#include "CoreMinimal.h"
#include "UI/Inventory/CatInventoryWidget.h"
#include "CatFishGuardInventoryWidget.generated.h"

class ACatFishBuyerActor;
class ACatFishGuardActor;
class UButton;
class UTextBlock;
class UWidget;

/** 正式鱼护 WBP 的原生父类；打开时绑定鱼护库存，格子显示和交互复用通用库存面板。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatFishGuardInventoryWidget : public UCatInventoryWidget
{
	GENERATED_BODY()

public:
	/** 选中格变化后同时刷新单鱼出售按钮；普通库存选择行为仍由父类维护。 */
	virtual void RequestSelectSlot(int32 SlotIndex) override;

protected:
	/** 构建时绑定正式 WBP 的出售按钮并立即按当前地面鱼护状态刷新，不创建原生替代页面。 */
	virtual void NativeConstruct() override;

	/** 销毁时解除可选按钮回调；买家和鱼护均为弱读取，不留下库存或世界对象订阅。 */
	virtual void NativeDestruct() override;

	/** 每帧只立即检查鱼护是否仍在地面；买家扫描由 0.1 秒节流，离开范围时隐藏出售入口。 */
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** 库存通知或回执先刷新通用格子，再立即更新报价和出售资格，避免等到下一次买家扫描才撤销旧选择。 */
	virtual void RefreshInventorySlots() override;

private:
	/** 从当前显示库存解析其世界鱼护宿主；普通库存或已切换上下文时返回空，避免错误提交到其他容器。 */
	ACatFishGuardActor* ResolveGroundedGuard() const;

	/** 按地面资格、附近买家和当前选中鱼刷新出售区；不修改库存，也不缓存客户端估价作为交易事实。 */
	void RefreshSellActions();

	/** 设置正式 WBP 出售区及按钮的可见性；面板缺失时仍分别隐藏按钮，保证资产可渐进接线。 */
	void SetSellActionsVisible(bool bVisible);

	/** 单鱼出售按钮入口；冻结当前选中鱼实例 ID，并把价格和资格留给服务器重新计算。 */
	UFUNCTION()
	void HandleSellSelectedClicked();

	/** 全部出售按钮入口；只从当前打开且仍在地面的鱼护库存收集鱼实例 ID，不扫描附近其他容器。 */
	UFUNCTION()
	void HandleSellAllClicked();

	/** 向当前可用买家提交一批固定鱼实例 ID；服务器重新验证距离、鱼护和每条鱼，UI 不改变库存。 */
	void SubmitFishSale(ACatFishGuardActor* Guard, const TArray<FGuid>& FishInstanceIds);

	/** 正式鱼护 WBP 的单鱼出售按钮；只有选中一条有效鱼且附近存在买家时启用。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> SellFishButton;

	/** 正式鱼护 WBP 的全部出售按钮；只在当前地面鱼护位于买家范围时显示和启用。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> SellAllFishButton;

	/** 正式鱼护 WBP 的出售操作区域；没有绑定资产时按钮仍可独立隐藏，避免 C++ 临时布局。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UWidget> SellActionsPanel;

	/** 当前选中实物鱼的整数收购预估价；刷新时调用 Buyer 的正式估价 API，无选中鱼或估价失败时显示不可报价。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SellFishPriceText;

	/** 本鱼护全部有效鱼逐条取整后的总预估价；刷新时求和，空鱼护显示零，任一鱼不可报价时不展示部分总价。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SellAllFishPriceText;

	/** 自上次附近买家查询经过的本机秒数；NativeTick 累加，达到 0.1 秒才调用买家查找以避免逐帧世界扫描。 */
	float BuyerRefreshElapsedSeconds = 0.0f;
};
