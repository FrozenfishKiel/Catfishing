#pragma once

#include "CoreMinimal.h"
#include "UI/CatUIModalInputMode.h"
#include "UObject/Object.h"
#include "CatInventoryPageController.generated.h"

class APlayerController;
class UCatInventoryComponent;
class UCatInventoryWidget;
class UEnhancedInputComponent;
class UInputAction;

/** 库存窗口控制器；只管理页面打开、输入与焦点，物品交互由格子提交，显示由各自 Model 通知。 */
UCLASS()
class CATFISHING_API UCatInventoryPageController : public UObject
{
	GENERATED_BODY()
public:
	/** 绑定本地 Controller 和默认背包页，并给背包注入角色库存；成功后安装库存按键。 */
	bool Bind(APlayerController* InController, UCatInventoryWidget* InView);

	/** 关闭页面并解除输入绑定；换 Pawn 或离开世界时调用，释放本控制器持有的页面。 */
	void Unbind();

	/** 切换普通背包窗口；从交互页面关闭后再次打开使用默认背包。 */
	void ToggleInventory();

	/** 用调用方指定的 WBP 显示一份库存；营地、鱼护和鱼缸共用此入口，创建失败返回 false。 */
	bool OpenInventory(UCatInventoryComponent* Inventory, TSubclassOf<UCatInventoryWidget> InventoryViewClass);

	/** 读取页面控制器维护的唯一窗口状态；调用方不得从 Widget 可见性、鼠标模式或库存 Model 再拼一套开关判断。 */
	bool IsInventoryOpen() const;

	/** Controller 输入组件就绪或替换后重新安装库存按键；安装前移除旧绑定。 */
	void RefreshInputBinding();

	/** 处理页面关闭意图；已经关闭时不重新打开。 */
	void RequestCloseInventoryFromWidget();

private:
	/** 成对加入/移出视口并申请/释放模态输入；关闭交互页后释放它并切回默认背包引用。 */
	void SetInventoryOpen(bool bOpen);

	/** 绑定现有 IMC 中的库存 Action；与主菜单共享 Action 时避让，不创建额外映射。 */
	void InstallInventoryInput();

	/** 从原输入组件删除确切句柄并释放 Action 引用；重复解绑不产生副作用。 */
	void RemoveInventoryInput();

	/** 本窗口所属本地玩家 Controller；按键安装与输入恢复都针对它。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> BoundPlayerController;

	/** 当前页面；默认背包或本次交互创建的页面，控制器持有到关闭/解绑。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatInventoryWidget> BoundView;

	/** 常规背包页面；由 LocalPlayer UI 持有，关闭交互页后重新使用。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatInventoryWidget> DefaultInventoryView;

	/** 已安装的库存开关 Action；保持引用直到解除输入绑定。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> AppliedInventoryToggleAction;

	/** Action 实际绑定到的输入组件；替换 Controller 时从这里移除原句柄。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UEnhancedInputComponent> BoundInventoryInputComponent;

	/** 库存 Action 的绑定句柄；0 表示尚未绑定，安装写入、解绑清零。 */
	uint32 InventoryInputBindingHandle = 0;

	/** 本控制器唯一的窗口打开状态；切换写入，关闭键与菜单读取。 */
	bool bInventoryOpen = false;

	/** 本页面申请模态输入时的恢复记录；关闭时只释放本页面持有的输入锁。 */
	FCatUIModalInputModeState ModalInputModeState;
};
