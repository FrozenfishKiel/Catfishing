#pragma once
#include "AbilitySystem/Items/Abilities/CatItemGameplayAbility.h"
#include "CatGA_PlaceDecoy.generated.h"
class ACatFishGuardActor;

/** 将精确来源的一件假鱼移入瞄准的鱼护；库存移动是唯一扣除来源的事务。 */
UCLASS()
class CATFISHING_API UCatGA_PlaceDecoy : public UCatItemGameplayAbility
{
	GENERATED_BODY()
public:
	/** 校验非堆叠来源和可访问鱼护，空格不足不消费物品。 */
	virtual bool ValidateUse() const override;
	/** 配置只允许库存转移，不允许额外件数成本或自用 GE。 */
	virtual bool ValidateUseConfiguration(const UCatItemUseFragment& Configuration, FText& OutError) const override;
protected:
	/** 复核目标后提交现有移格事务；成功失去来源能力也不会再次扣除假鱼。 */
	virtual void CommitUse() override;
private:
	/** 从服务器校验射线找可触达鱼护；不采信客户端 Actor 引用。 */
	ACatFishGuardActor* ResolveGuard() const;
};
