#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "CatShopEconomyTransactionEffect.generated.h"

/** 商店购买扣款与售鱼收入共用的即时 GE；执行器从服务器 source 读取扣款或逐鱼计算收入，效果本身不保存业务状态。 */
UCLASS()
class CATFISHING_API UCatGE_ShopEconomyTransaction : public UGameplayEffect
{
	GENERATED_BODY()

public:
	/** 配置瞬时交易执行器，保证每次购买或整批售鱼只对团队余额产生一次 GAS 写入。 */
	UCatGE_ShopEconomyTransaction();
};
