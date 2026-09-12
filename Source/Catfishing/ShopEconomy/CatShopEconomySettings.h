#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ShopEconomy/Catalog/CatShopCatalogTypes.h"
#include "CatShopEconomySettings.generated.h"

class UDataTable;

/** 商店经济运行设置；它只提供初始团队余额和两张策划表入口，成交金额始终由服务器运行时数据计算。 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Catfishing Shop Economy"))
class CATFISHING_API UCatShopEconomySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** 运行 gate 表示本局是否允许创建团队公款和交易入口；摊位目录合法性由各自库存组件检查。 */
	bool IsRuntimeEnabled() const;

	/** ShopEconomy 总运行 gate；关闭时购物车支付和售鱼入账全部 fail-closed。 */
	UPROPERTY(Config, EditAnywhere, Category = "Runtime")
	bool bEnableShopEconomyRuntime = false;

	/**
	 * 每局团队公款的初始余额。0 是合法取值，表示"裁定过了，本局白手起家"；负数非法。
	 * 默认值取 -1 作为"未裁定起始资金"的哨兵，好让"裁定 0 起始资金"和"未裁定起始资金"在运行期保持可区分——
	 * 后者必须让整个 ShopEconomy 保持 fail-closed，而不是悄悄按 0 元开局跑下去。
	 * 不设 ClampMin，避免编辑器把哨兵夹成 0。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Wallet")
	int32 StartingTeamWalletBalance = -1;

	/** 项目默认商店出售表；单个摊位没单独指定表时读取它，商品、分类、价格和随机池仍全部由策划 DataTable 配置。 */
	UPROPERTY(Config, EditAnywhere, Category = "Catalog")
	TSoftObjectPtr<UDataTable> DefaultShopCatalogTable;

	/** 默认鱼类收购价表；行按鱼种 ID 提供每千克金币系数，缺表、缺行或坏系数由服务端整单拒绝。 */
	UPROPERTY(Config, EditAnywhere, Category = "FishSale")
	TSoftObjectPtr<UDataTable> DefaultFishSalePriceTable;

	/**
	 * 小鱼干的库存稳定 ID（商店册 §3.1.2：收摊后把剩余公款换成小鱼干，给猫猫们在篝火旁娱乐）。
	 * 留空＝这件道具还没有资产，收摊时跳过兑换并记一行 Log —— 这不是 fail-closed，是「这条玩法还没有载体」。
	 * 资产做好并登记进 CatInventorySettings.Definitions 之后，把稳定 ID 填在这里就能跑，C++ 侧不用再改。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Settlement")
	FName SettlementDriedFishDefinitionId = NAME_None;

	/**
	 * 一条小鱼干折合多少公款。<= 0 表示这条兑换率未裁，收摊时同样跳过兑换。
	 * 兑换按整除取，余下不足一条的零钱留在账上不处理——公款本来就跟局走，不需要清零。
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Settlement")
	int32 SettlementDriedFishCoinCost = 0;

	/** 一次收摊最多兑出多少条小鱼干；防止余额极大时一口气塞爆公库。<= 0 表示不设上限。 */
	UPROPERTY(Config, EditAnywhere, Category = "Settlement")
	int32 SettlementDriedFishMaxCount = 0;
};
