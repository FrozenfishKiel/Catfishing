#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "CatConsumableEffectFragment.generated.h"
class APawn;
class UGameplayEffect;

/** 消耗品的一次即时效果配置；数量事务由库存或容器持有，本片段只提交效果。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class CATFISHING_API UCatConsumableEffectFragment : public UCatInventoryItemFragment
{
	GENERATED_BODY()
public:
	/** 每次效果成功需要消费的真实库存数量；库存事务读取它，取消和失败不扣量。 */
	UPROPERTY(EditDefaultsOnly, Category="Use", meta=(ClampMin="1")) int32 ConsumeCount = 1;
	/** 本次使用施加的即时 GE；定义资产作者配置，使用者按此创建 Spec。 */
	UPROPERTY(EditDefaultsOnly, Category="Use") TSubclassOf<UGameplayEffect> EffectClass;
	/** 效果等级；只影响 GE 求值，不改变物品堆叠数量。 */
	UPROPERTY(EditDefaultsOnly, Category="Use", meta=(ClampMin="0.0")) float EffectLevel = 1.0f;
	/** 本轮消费只允许已经实现的单项即时经验效果；数量回滚无法撤销任意副作用，因此未知效果结构必须在消费前拒绝。 */
	virtual bool IsRuntimeReady() const override;
	/** 在实物暂扣前检查服务器 ASC 和稳定属性集是否可接收本效果。 */
	bool ValidateForUser(APawn* User) const;
	/** 用本次事务身份与实例提供的参数申请效果；返回属性执行回调确认的结果。 */
	FCatDomainCommandResult ApplyFromAuthority(APawn* User, FGuid RequestId, UObject* SourceObject, const TMap<FGameplayTag, float>& SetByCallerMagnitudes) const;
};
