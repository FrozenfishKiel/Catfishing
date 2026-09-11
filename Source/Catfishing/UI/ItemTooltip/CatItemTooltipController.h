#pragma once

#include "CoreMinimal.h"
#include "Tickable.h"
#include "UObject/Object.h"
#include "CatItemTooltipController.generated.h"

class UCatItemTooltipModel;
class UCatItemTooltipWidget;
class UCatInventorySlotWidget;

/** 本地玩家唯一悬停来源的控制器；只在有活动格子时读取实例，不改变库存或网络。 */
UCLASS()
class CATFISHING_API UCatItemTooltipController : public UObject, public FTickableGameObject
{
	GENERATED_BODY()
public:
	/** LocalPlayer 装配时注入正式 View 并创建只读 Model；旧来源先清理。 */
	void Bind(UCatItemTooltipWidget* InView);
	/** UI 卸载时立即清理来源并释放 Model、View 引用。 */
	void Unbind();
	/** 格子进入时提交自己和鼠标屏幕坐标；有效物品接管全局提示，空格不显示。 */
	void ShowTooltip(UCatInventorySlotWidget* Source, const FVector2D& ScreenPosition);
	/** 只有当前来源可以隐藏；旧格子的 Leave/Destruct 不影响新格子的提示。 */
	void HideTooltip(const UCatInventorySlotWidget* Source);
	/** 库存页关闭时强制清除当前提示，立即隐藏避免跨页残留。 */
	void ForceHideTooltip();
	/** 仅活动来源参与 Tick；默认对象和未绑定状态不参与。 */
	virtual bool IsTickable() const override;
	/** 暂停菜单中仍允许更新本地提示；不推进任何玩法时间。 */
	virtual bool IsTickableWhenPaused() const override;
	/** 将 Tick 限定在所属 View 的 World，避免不同 PIE World 串用。 */
	virtual UWorld* GetTickableGameObjectWorld() const override;
	/** 提供引擎性能统计身份，不记录业务状态。 */
	virtual TStatId GetStatId() const override;
	/** 活动期间检查来源生命周期并刷新实例；单纯数值变化不重启动画。 */
	virtual void Tick(float DeltaTime) override;
private:
	/** 当前鼠标来源的弱引用；Show 替换，Hide 校验，绝不为显示延长格子生命周期。 */
	TWeakObjectPtr<UCatInventorySlotWidget> ActiveSource;
	/** 此玩家的纯显示投影器；Bind 创建，Unbind 释放。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatItemTooltipModel> Model;
	/** 此玩家的正式提示 View；Subsystem 创建并注入，Controller 只发显示命令。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatItemTooltipWidget> View;
};
