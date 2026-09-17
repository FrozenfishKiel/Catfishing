#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Inventory/CatInventoryUseTarget.h"
#include "CatItemAbilityTargetData.generated.h"

class UCatInventoryComponent;
class ACatFishPickupActor;

/** 一次使用的来源意图；客户端只提供定位信息，服务器重查库存或当前嘴叼实物，不接受客户端效果数值。 */
USTRUCT()
struct CATFISHING_API FCatItemAbilityTargetData : public FGameplayAbilityTargetData
{
	GENERATED_BODY()
	/** 来源库存组件；随身和公共鱼容器都引用当前真实组件，服务器复核访问权限。 */
	UPROPERTY() TObjectPtr<UCatInventoryComponent> Inventory;
	/** 嘴部保管的真实世界鱼；与 Inventory 二选一，服务器验证角色唯一携带引用。 */
	UPROPERTY() TObjectPtr<ACatFishPickupActor> WorldFish;
	/** 按下时锁定的物品实例身份；换格不能把本次请求改为另一件物品。 */
	UPROPERTY() FGuid ItemId;
	/** 一次输入的关联身份；用于成本防重、日志及界面回执，不代替 GAS 预测键。 */
	UPROPERTY() FGuid RequestId;
	/** 按下瞬间的目标采样；目标型行为使用，服务器必须重新做距离和命中校验。 */
	UPROPERTY() FCatInventoryUseTarget Aim;
	/** 本次动作是否由按住输入发起；菜单为 false，能力将其传入领域上下文，松开处理由具体连续行为实现。 */
	UPROPERTY() bool bContinuousInput = false;
	/** 网络目标的结构身份；接收方据此验证载荷类型，避免把瞄准点等其他目标数据解释为物品请求。 */
	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }
	/** GAS 复制来源、视线目标、持续输入标记及请求身份；不接收客户端声明的物品定义、数量或经验。 */
	bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess);
};

template<> struct TStructOpsTypeTraits<FCatItemAbilityTargetData> : TStructOpsTypeTraitsBase2<FCatItemAbilityTargetData>
{
	enum { WithNetSerializer = true, WithCopy = true };
};
