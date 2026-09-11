#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectExecutionCalculation.h"
#include "ShopEconomy/Trading/CatShopTradingTypes.h"
#include "CatShopEconomyTransactionExecutionCalculation.generated.h"

class UDataTable;

/** 一次同步经济 GE 的服务器输入和执行回执；不复制、不持久化，也不持有第二份钱包。 */
UCLASS(Transient)
class CATFISHING_API UCatShopEconomyTransactionSource : public UObject
{
	GENERATED_BODY()
	friend class UCatShopEconomyService;
	friend class UCatShopEconomyTransactionExecutionCalculation;
	friend class UCatEconomyAttributeSet;

private:
	/** 本次售鱼的实例、鱼种与千克重量副本；服务在提交前冻结，执行器只读，空数组表示购买扣款。 */
	UPROPERTY(Transient)
	TArray<FCatShopFishSaleLine> Fish;

	/** 本次售鱼使用的权威收购表；服务解析后固定引用，执行器逐行读取，GC 随 source 生命周期保留它。 */
	UPROPERTY(Transient)
	TObjectPtr<UDataTable> PriceTable;

	/** 本笔 GE 的诊断关联号；服务从命令复制，执行器用它关联拒绝日志，不包含玩家私有身份。 */
	FGuid RequestId;

	/** 购买时的整数扣款输入；售鱼时由执行器以逐鱼计算结果覆盖，服务只在 GE 成功后将结果记入账本。 */
	int32 WalletDelta = 0;

	/** 执行器是否已产出唯一余额 modifier；默认 false，执行器通过全部校验才置 true，服务据此排除空执行成功。 */
	bool bExecuted = false;

	/** 余额 modifier 是否已到达属性执行完成回调；属性集写入、服务核验，防止零收入或被拦截的 modifier 被误记为成功。 */
	bool bBalanceApplied = false;
};

/** 商店经济即时 GE 的计算器；它集中逐鱼估价和对唯一团队余额的单次增减，不持有钱包或交易状态。 */
UCLASS()
class CATFISHING_API UCatShopEconomyTransactionExecutionCalculation : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	/** 按鱼种价格表将一批服务器确认的重量逐条四舍五入后合计；缺行、坏系数、NaN 或溢出时整单失败。 */
	static bool TryCalculateFishSale(const UDataTable* PriceTable, const TArray<FCatShopFishSaleLine>& Fish, int32& OutSaleValue);

	/** 从 source 读取冻结鱼行与价格表，在 GAS 内逐鱼估价求和；购买读取整数扣款，校验余额后只输出一次修改和执行回执。 */
	virtual void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
		FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;
};
