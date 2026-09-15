#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "GameplayTagContainer.h"
#include "CatInventoryContextMenuWidget.generated.h"

class UButton;
class USpinBox;
class UTextBlock;
class UVerticalBox;
class UWidget;
struct FCatInventoryActionDefinition;

/** 动态菜单行选中事件；行只保存自己的动作标识，菜单负责数量分流和控制器转交。 */
DECLARE_DELEGATE_OneParam(FCatInventoryContextActionRowChosen, FGameplayTag);

/** 普通原生动作按钮；它在自己的 UFUNCTION 回调中携带动作标识，避免动态按钮委托丢失点击来源。 */
UCLASS()
class CATFISHING_API UCatInventoryContextActionButton : public UButton
{
	GENERATED_BODY()
public:
	/** 每行以定义标签作为身份；菜单创建时绑定该身份，点击通过原生委托转交，避免本地化文本影响动作分发。 */
	void SetAction(FGameplayTag InAction);
	/** 读取该行稳定动作标识；自动化和作者器按标签定位，不能按本地化显示文本猜测动作。 */
	FGameplayTag GetAction() const;
	/** 菜单监听行选择；禁用按钮不会触发这条事件。 */
	FCatInventoryContextActionRowChosen OnActionRowChosen;
private:
	/** UButton 的无参数动态点击回调；从本行保存的标识恢复来源后广播给菜单。 */
	UFUNCTION()
	void HandleClicked();
	/** 本动作行代表的定义动作；创建后不再修改，行释放时随控件一起失效。 */
	FGameplayTag Action;
};

/** 库存菜单选择事件；页面控制器接收操作标识和最终数量，再发起唯一服务器请求。 */
DECLARE_DELEGATE_TwoParams(FCatInventoryContextActionChosen, FGameplayTag, int32);

/** 库存菜单取消事件；页面控制器清除上下文并决定 Tooltip 何时重新允许显示。 */
DECLARE_DELEGATE(FCatInventoryContextMenuCancelled);

/** 所有库存共用的右键菜单视图；只显示控制器给出的动作和原因，不持有库存、物品或网络权限。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API UCatInventoryContextMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 以一份已校验的动作清单打开菜单；数组顺序保留定义顺序，置灰项显示本机只读原因。 */
	void PresentActions(const TArray<FCatInventoryActionDefinition>& Actions, const TArray<FText>& UnavailableReasons,
		int32 MaximumQuantity, const FVector2D& ScreenPosition);

	/** 立即收起菜单并清空动态行与数量冻结；页面切换、来源换物和提交后均使用这条路径。 */
	void Dismiss();

	/** 返回菜单是否正在展示一份可提交的上下文；页面控制器用它统一处理 Escape 和来源切换。 */
	bool IsMenuOpen() const;

	/** 页面控制器监听动作选择；菜单只传动作和数量，不直接访问 PlayerController。 */
	FCatInventoryContextActionChosen OnActionChosen;

	/** 页面控制器监听取消；外部关闭和数量页取消都回到同一条上下文清理路径。 */
	FCatInventoryContextMenuCancelled OnMenuCancelled;

protected:
	/** 构建时连接数量确认和取消按钮；动作行按每次上下文动态生成。 */
	virtual void NativeConstruct() override;

	/** 销毁时解除按钮回调并清空展示状态，避免页面移除后保留旧的动作身份。 */
	virtual void NativeDestruct() override;

	/** 数量输入或动作按钮取得焦点时，预览层仍统一处理祭坛、取消和背包关闭键。 */
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	/** 菜单取得焦点后优先消费 Escape；页面仍负责实际取消与 Tooltip 状态恢复。 */
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;


private:
	/** 点击动作行后判断该动作是否需要数量；单件或 Single 直接上交，堆叠 Select 切到数量确认。 */
	void ChooseAction(FGameplayTag Action, bool bRequiresQuantity);

	/** 数量确认时把 SpinBox 值裁剪到冻结上限并上交选择；来源真相由控制器在提交前再次验证。 */
	UFUNCTION()
	void HandleQuantityConfirmed();

	/** 数量取消只关闭当前菜单；不会向库存或服务器写入任何状态。 */
	UFUNCTION()
	void HandleQuantityCancelled();

	/** 清理动作行、冻结动作和数量控件；重复调用安全，供 Present 和 Dismiss 共用。 */
	void ResetPresentation();

	/** 动态动作行的正式容器；作者器创建并命名，运行期只增删子控件。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> ActionList;

	/** 菜单内数量确认区；只有数量动作面对多件堆叠时显示。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UWidget> QuantityPanel;

	/** 本次操作请求的整数数量；打开数量页时由当前堆叠数设定范围，确认时再裁剪。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<USpinBox> QuantitySpinBox;

	/** 菜单内数量确认按钮；只提交已冻结的动作标识和经裁剪的数量。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> QuantityConfirmButton;

	/** 菜单内数量取消按钮；用于放弃当前操作并关闭整个菜单。 */
	UPROPERTY(Transient, meta = (BindWidgetOptional))
	TObjectPtr<UButton> QuantityCancelButton;

	/** 正在等待数量确认的操作标识；选择新来源或关闭时清空，不能跨物品复用。 */
	FGameplayTag PendingQuantityAction;

	/** 打开菜单时读取的堆叠上限；它只限制输入，最终服务器数量由控制器提交前重读来源格。 */
	int32 PendingMaximumQuantity = 0;

	/** 菜单当前是否展示某个来源的操作；由 Present 写入、Dismiss 清除。 */
	bool bMenuOpen = false;
};
