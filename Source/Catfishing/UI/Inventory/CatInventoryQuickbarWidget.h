#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CatInventoryQuickbarWidget.generated.h"

class UCatBackPackComponent;
class UCatInventoryModel;
class UCatInventorySlotWidget;
class UWrapBox;

/** 常驻快捷栏的只读背包视图；它不拥有容量、物品或选中状态，只把背包列表与 Controller 的本地物品栏焦点显示在独立底部格子。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatInventoryQuickbarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 绑定当前玩家的唯一随身背包；切换 Pawn 时先解绑旧 Model，再按新背包列表与 Controller 的物品栏焦点重建。 */
	void SetBackPackContext(UCatBackPackComponent* InBackPack);

	/** 指定动态创建格子使用的正式 WBP 类；类由 UI Settings 提供，快捷栏不回退创建原生白盒。 */
	void SetInventorySlotWidgetClass(TSubclassOf<UCatInventorySlotWidget> InSlotWidgetClass);

	/** 返回当前正式 WBP 实际创建的格子数；自动化和 UI 审查只读它验证格数来自同一背包 Model。 */
	int32 GetDisplayedSlotCount() const;

	/** 查询指定格的选中外圈是否可见；自动化用它比对Controller 的本地物品栏选择，不能通过它修改表现。 */
	bool IsDisplayedSlotSelected(int32 SlotIndex) const;

protected:
	/** 构建时补齐正式格子类并刷新已注入背包；本控件不安装输入，也不抢模态页面焦点。 */
	virtual void NativeConstruct() override;

	/** 销毁时解除 Model 通知和动态格子引用；物品栏焦点留在 Controller，不跟随 View 生命周期清空。 */
	virtual void NativeDestruct() override;

private:
	/** 从同一库存 Model 重建全部可见格；格数始终取列表长度，禁止写快捷栏独立容量。 */
	void RefreshSlots();

	/** 只刷新已有格子的本地选择外圈；选择变化不会重建列表，避免鼠标悬停与 Tooltip 无意义中断。 */
	void RefreshSelectedSlot(int32 SelectedSlotIndex);

	/** 分别从当前库存 Model 和本地 Controller 移除精确委托句柄；背包切换、销毁和装配失败共用这条配对清理路径。 */
	void UnbindInventoryModel();

	/** 解除全部动态格子的 Tooltip 与点击委托并释放引用；快捷栏格子只展示，点击不会提交选择。 */
	void UnbindSlotWidgets();

	/** 当前快捷栏观察的角色随身背包；LocalPlayer 子系统在 Pawn 有效后写入，View 不持有其他库存。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatBackPackComponent> BackPack;

	/** 注册在同一背包 Model 列表通知上的句柄；库存提交和复制完成后通过它重建所有格。 */
	FDelegateHandle InventoryModelChangedHandle;

	/** 注册在本地 Controller 物品栏选择通知上的句柄；独立于背包 Model，销毁或重绑时解除。 */
	FDelegateHandle SelectionChangedHandle;

	/** 动态库存格使用的正式 WBP 类；Settings 写入，缺失时快捷栏保持空而不是创建替身。 */
	UPROPERTY(Transient)
	TSubclassOf<UCatInventorySlotWidget> InventorySlotWidgetClass;

	/** 正式 Quickbar WBP 的格子容器；生成器以这个稳定名称建立底部横向布局。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UWrapBox> QuickbarSlotWrapBox;

	/** 当前由本 View 创建的格子；列表刷新和销毁时由本 View 成对解绑。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UCatInventorySlotWidget>> SlotWidgets;
};
