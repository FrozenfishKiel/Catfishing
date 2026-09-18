#pragma once
#include "AbilitySystem/Items/Abilities/CatItemGameplayAbility.h"
#include "CatGA_Horn.generated.h"

/** 确认文字后按实例次数付费，并向当前队伍广播一次屏幕喊话。 */
UCLASS()
class CATFISHING_API UCatGA_Horn : public UCatItemGameplayAbility
{
	GENERATED_BODY()
public:
	/** 输入路由先打开文字窗口，不以按下使用键作为付费时刻。 */
	virtual bool RequiresMessageInput() const override { return true; }
	/** 共用来源检查后拒绝空白、超长及控制字符；发送者身份来自服务器 ActorInfo。 */
	virtual bool ValidateUse() const override;
	/** 只允许有限次数且耗尽消费本体，不配置额外件数成本或 GE。 */
	virtual bool ValidateUseConfiguration(const UCatItemUseFragment& Configuration, FText& OutError) const override;
	/** 成本提交成功后发布唯一广播；不能由本地 UI 直接调用此结算。 */
	virtual void OnUseCommitted(UCatInventoryItemInstance* ConsumedItem) override;
};
