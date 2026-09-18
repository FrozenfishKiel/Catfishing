#pragma once
#include "Blueprint/UserWidget.h"
#include "UI/CatUIModalInputMode.h"
#include "CatHornMessageWidget.generated.h"
class UCatItemAbilityComponent;
class UCatInventoryComponent;
class SEditableTextBox;

/** 响响筒确认窗口；只收集文字和固定来源，消费由原物品能力执行。 */
UCLASS()
class CATFISHING_API UCatHornMessageWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	/** 打开前冻结来源；切快捷格后确认仍使用这一件筒，原物失效则服务器拒绝。 */
	void SetSource(UCatItemAbilityComponent* Items, UCatInventoryComponent* Inventory, FGuid ItemId);
protected:
	/** 建立输入框与确认/取消按钮，窗口居中且宽度有限。 */
	virtual TSharedRef<SWidget> RebuildWidget() override;
	/** 窗口进入视口后申请现有模态输入锁，输入框接收文字焦点。 */
	virtual void NativeConstruct() override;
	/** 关闭、销毁或旅行时只释放本窗口申请的输入锁。 */
	virtual void NativeDestruct() override;
	/** Escape 关闭不发请求；其他按键交给输入框和父类。 */
	virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
private:
	/** 请求接线组件的弱引用；角色销毁后不得再尝试使用旧物品。 */
	TWeakObjectPtr<UCatItemAbilityComponent> SourceItems;
	/** 被选择的原库存；确认时仍需由能力重新核对实例身份。 */
	TWeakObjectPtr<UCatInventoryComponent> SourceInventory;
	/** 打开窗口时的实例 GUID；只在 SetSource 写入。 */
	FGuid SourceItemId;
	/** 本窗口输入锁恢复记录；构造与析构成对使用。 */
	FCatUIModalInputModeState InputState;
	/** 文字编辑控件；只保存未提交文本，不复制或保存玩法状态。 */
	TSharedPtr<SEditableTextBox> Input;
	/** 文字有效才关闭并发起一次使用；空白或超长保留窗口供修改。 */
	FReply Confirm();
};

/** 顶部喊话展示；由服务器广播创建，超时移除，不占输入焦点。 */
UCLASS()
class CATFISHING_API UCatHornAnnouncementWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	/** 展示前设置完整显示文本；仅本地 UI 读取，不作为奖励或次数依据。 */
	FText Message;
protected:
	/** 生成顶部居中、可自动换行的喊话条。 */
	virtual TSharedRef<SWidget> RebuildWidget() override;
	/** 展示五秒后移除，不拦截玩家鼠标或移动。 */
	virtual void NativeConstruct() override;
};
