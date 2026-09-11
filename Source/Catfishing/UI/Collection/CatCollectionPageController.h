#pragma once

#include "CoreMinimal.h"
#include "UI/CatUIModalInputMode.h"
#include "UObject/Object.h"
#include "CatCollectionPageController.generated.h"

class APlayerController;
class UCatCollectionModel;
class UCatCollectionWidget;
class UEnhancedInputComponent;
class UInputAction;
class ULocalPlayer;

/** 个人图鉴页面控制器；它只管理图鉴页的打开、输入与焦点，记录本身仍由 Profile durable 快照经 Collection Model 单向提供。 */
UCLASS()
class CATFISHING_API UCatCollectionPageController : public UObject
{
	GENERATED_BODY()

public:
	/** 绑定本地玩家、Controller 和图鉴页；成功后创建并绑定 Collection Model，再安装图鉴按键。 */
	bool Bind(ULocalPlayer* InLocalPlayer, APlayerController* InController, UCatCollectionWidget* InView);

	/** 关闭页面、解除 Model 订阅与输入绑定；换 Pawn、旅行或 LocalPlayer 销毁时调用。 */
	void Unbind();

	/** 切换图鉴页；HUD 猫爪印、局内派对菜单和图鉴按键三个入口共用这一个开关。 */
	void ToggleCollection();

	/** 读取本控制器维护的唯一图鉴打开态；调用方不得从 Widget 可见性或鼠标模式再拼一套开关判断。 */
	bool IsCollectionOpen() const;

	/** Controller 输入组件就绪或替换后重新安装图鉴按键；安装前移除旧绑定。 */
	void RefreshInputBinding();

	/** 处理页面关闭意图；已经关闭时不重新打开。 */
	void RequestCloseCollectionFromWidget();

private:
	/** 成对加入/移出视口并申请/释放模态输入；打开时先用当前 Model 投影重绘一次，避免显示上一次的过期记录。 */
	void SetCollectionOpen(bool bOpen);

	/** 绑定现有 IMC 中的图鉴 Action；与主菜单或背包共享 Action 时避让，不创建额外映射。 */
	void InstallCollectionInput();

	/** 从原输入组件删除确切句柄并释放 Action 引用；重复解绑不产生副作用。 */
	void RemoveCollectionInput();

	/** Collection Model 投影变化入口；只在页面打开时重绘，关闭期间不做无意义的文本合成。 */
	void HandleCollectionViewStateChanged();

	/** 本页面所属本地玩家 Controller；按键安装与输入恢复都针对它。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> BoundPlayerController;

	/** 当前图鉴页；由 LocalPlayer UI 创建并持有生命周期，本控制器只加入或移出视口。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCatCollectionWidget> BoundView;

	/** 本页面唯一的图鉴 Model；它只读 LocalPlayer Profile 的 durable 图鉴快照，不访问实物鱼容器。 */
	UPROPERTY(Transient)
	TObjectPtr<UCatCollectionModel> CollectionModel;

	/** 已安装的图鉴开关 Action；保持引用直到解除输入绑定。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> AppliedCollectionToggleAction;

	/** Action 实际绑定到的输入组件；替换 Controller 时从这里移除原句柄。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UEnhancedInputComponent> BoundCollectionInputComponent;

	/** Collection Model 投影变化的配对解绑句柄；Bind 写入，Unbind 消费。 */
	FDelegateHandle CollectionModelChangedHandle;

	/** 图鉴 Action 的绑定句柄；0 表示尚未绑定，安装写入、解绑清零。 */
	uint32 CollectionInputBindingHandle = 0;

	/** 本控制器唯一的页面打开状态；切换写入，关闭键与菜单读取。 */
	bool bCollectionOpen = false;

	/** 本页面申请模态输入时的恢复记录；关闭时只释放本页面持有的输入锁。 */
	FCatUIModalInputModeState ModalInputModeState;
};
