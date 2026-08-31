#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "ShopEconomy/CatShopEconomyTypes.h"
#include "CatShopEconomySettings.generated.h"

class UDataTable;

/** 商店经济运行设置；它裁定团队公款、售鱼估价和项目默认出售表入口，商品内容仍由 DataTable 资产维护。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Shop Economy"))
class CATFISHING_API UCatShopEconomySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 运行 gate 表示本局是否允许创建团队公款和交易入口；摊位目录合法性由各自库存组件检查。 */
	bool IsRuntimeEnabled() const;

	/**
	 * 声明：按体重轴查出一条鱼的收购价；查不到就返回 false，调用方必须据此整笔拒绝售鱼。
	 * 实现：先整表校验（非空、每档自身合法、重量严格递增、价格不递减），任何一条不满足都判定这张表还不能用来定价；
	 *       再要求重量是有限正数，最后取重量落到的最高一档的价格。比最轻一档还轻的鱼没有档位，同样返回 false。
	 * 边界：不外推、不插值、不给兜底价。飞书商店册 §3.1 只给了"体重重＝钱多"这个方向，没给任何斜率或截距，
	 *       在这里补一个线性公式等于替产品拍了定价，所以宁可让售鱼整体走不通。
	 * 参数取整表而不是读自身字段，是为了让服务能在开局冻结一份档位表之后继续用同一套判定，不会中途被改配置带偏。
	 */
	static bool TryEvaluateFishPurchasePrice(const TArray<FCatShopFishWeightPrice>& Anchors, double WeightKilograms,
		int32& OutPrice);

	/** ShopEconomy 总运行 gate；关闭时购物车支付和售鱼入账全部 fail-closed。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bEnableShopEconomyRuntime = false;

	/**
	 * 每局团队公款的初始余额。0 是合法取值，表示"裁定过了，本局白手起家"；负数非法。
	 * 默认值取 -1 作为"这条还没裁过"的哨兵，好让"裁定 0 起始资金"和"没人裁过"在运行期不再是同一个状态——
	 * 后者必须让整个 ShopEconomy 保持 fail-closed，而不是悄悄按 0 元开局跑下去。
	 * 不设 ClampMin，避免编辑器把哨兵夹成 0。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Wallet")
	int32 StartingTeamWalletBalance = -1;

	/** 允许售鱼入账的最小金额；小于该值的命令视为估价证据不足。 */
	UPROPERTY(Config, EditAnywhere, Category = "Wallet", meta = (ClampMin = "1"))
	int32 MinimumFishSaleValue = 1;

	/** 项目默认商店出售表；单个摊位没单独指定表时读取它，商品、分类、价格和随机池仍全部由策划 DataTable 配置。 */
	UPROPERTY(Config, EditAnywhere, Category = "Catalog")
	TSoftObjectPtr<UDataTable> DefaultShopCatalogTable;

	/**
	 * 收鱼价体重轴是否已经被产品显式裁定。默认 Unset 表示"还没人拍过"，此时售鱼整体 fail-closed；
	 * 它和下面的档位表分成两个字段，是为了让"裁定了但表暂时清空"和"根本没裁过"在运行期还能分开看。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Appraisal")
	ECatDomainPolicy FishPurchasePricePolicy = ECatDomainPolicy::Unset;

	/**
	 * 收鱼价的体重轴档位表，按重量下限从小到大排列。飞书「参数与校准记录」rev10 里商店相关一条都没挂号，
	 * 所以这里默认是空的，任何售鱼请求都会因为查不到档位被拒。要开这条路必须先把飞书拍定的数值逐档填进来。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Appraisal")
	TArray<FCatShopFishWeightPrice> FishPurchasePriceAnchors;
};
