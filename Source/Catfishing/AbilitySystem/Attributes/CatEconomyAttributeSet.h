#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Attributes/CatAttributeSet.h"
#include "CatEconomyAttributeSet.generated.h"

/** 团队经济属性集；TeamWalletBalance 是 GameState ASC 上唯一可写的本局公款，商店服务和 UI 只读取它的投影。 */
UCLASS()
class CATFISHING_API UCatEconomyAttributeSet : public UCatAttributeSet
{
	GENERATED_BODY()

public:
	/** 注册团队公款的复制；客户端只观察 GAS 最终余额，不能通过复制回调创建本地钱包。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 在 GAS 修改基础余额时将非有限值归零，并规整到 0..16777216 的整数；Init 访问器直接播种，因此交易仍须独立校验初始化结果。 */
	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;

	/** 在聚合后的余额变化前沿用同一金额边界，保证所有消费者读取的都是安全整数。 */
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

	/** 团队余额的 GE modifier 真正执行后确认 source 回执；服务据此区分合法零收入和未执行的效果。 */
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

	/** 本局全队共同持有的金币余额；仅 GameState ASC 的初始化和经济 GE 写入，商店服务、账本及 UI 读取其投影。 */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_TeamWalletBalance, Category = "Catfishing|Economy")
	FGameplayAttributeData TeamWalletBalance;
	ATTRIBUTE_ACCESSORS_BASIC(UCatEconomyAttributeSet, TeamWalletBalance)

protected:
	/** 收到余额复制时交给 GAS 标准属性通知，让 UI 订阅同一份权威余额而不维护本地副本。 */
	UFUNCTION()
	void OnRep_TeamWalletBalance(const FGameplayAttributeData& OldTeamWalletBalance);
};
