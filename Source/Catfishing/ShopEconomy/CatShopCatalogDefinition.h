#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ShopEconomy/CatShopEconomyTypes.h"
#include "CatShopCatalogDefinition.generated.h"

class UTexture2D;

/** 商店出售条目的策划配置形态；它说明这一单卖什么、多少钱、给多少和怎么展示，不保存运行期剩余库存。 */
USTRUCT(BlueprintType)
struct FCatShopSaleEntry
{
	GENERATED_BODY()

	/** 售卖条目的稳定 ID；UI 点击和交易账本只引用它，不直接提交价格、数量或 DefinitionId。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	FName EntryId = NAME_None;

	/** 展示和旧账本使用的商品类别提示；真正入库仍由目标库存读取装备定义后裁决。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	ECatShopEntryKind Kind = ECatShopEntryKind::Unknown;

	/** 本条出售项最终发放的装备定义 ID；必须存在于装备定义表，商店不会从资产目录自动扫描。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	FName DefinitionId = NAME_None;

	/** 单次购买向公共仓库发放的数量；货架库存按“订单次数”扣减，不按该数量扣减。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog", meta = (ClampMin = "1"))
	int32 PurchaseQuantity = 1;

	/** 单次购买消耗的团队公款；-1 表示价格未裁定，运行目录必须拒绝这条配置。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	int32 UnitPrice = -1;

	/** 每次进入当前货架时的订单库存；无限库存条目可保持 0。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog", meta = (ClampMin = "0"))
	int32 InitialStock = 0;

	/** 该商品是否不扣货架库存；保底竿、保底饵和保底漂可以用它表达永远可拿。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	bool bUnlimitedStock = false;

	/** 是否把这条配置纳入运行货架候选；关闭时保留数据但不进入刷新结果。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	bool bEnabled = true;

	/** 商店自己的上架条件；当前没有商店解锁事实源，留空才会进入运行货架，非空表示先不上架。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	FName RequiredShopUnlockId = NAME_None;

	/** 商店展示名覆盖；为空时 UI 可回退到装备定义名或稳定 ID。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FText DisplayNameOverride;

	/** 商店描述覆盖；只影响货架说明，不改变物品定义。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation", meta = (MultiLine = "true"))
	FText DescriptionOverride;

	/** 商店图标覆盖；为空时 UI 可继续使用装备定义缩略图。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UTexture2D> IconOverride;

	/** 当前货架展示排序；固定商品和随机商品进入同一货架后按它稳定排列。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	int32 SortOrder = 0;

	/** 把策划出售条目转换成运行目录项；返回 false 表示这条配置还不足以进入商店库存。 */
	bool TryBuildCatalogEntry(FCatShopCatalogEntry& OutEntry) const;
};

/** 商店随机池的一项候选；它只描述刷新抽取规则，购买和发货语义继续复用内部 SaleEntry。 */
USTRUCT(BlueprintType)
struct FCatShopRandomSaleEntry
{
	GENERATED_BODY()

	/** 被抽中后进入当前货架的出售条目；价格、发放数量和展示文案仍集中写在这里。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	FCatShopSaleEntry SaleEntry;

	/** 本候选在随机池里的权重；小于等于 0 时明确不参与本轮刷新抽取。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Refresh")
	int32 RefreshWeight = 1;

	/** 抽中后覆盖货架库存的随机下限；-1 表示使用 SaleEntry.InitialStock。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Refresh")
	int32 MinRefreshedStockOverride = -1;

	/** 抽中后覆盖货架库存的随机上限；-1 表示使用 SaleEntry.InitialStock。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Refresh")
	int32 MaxRefreshedStockOverride = -1;

	/** 为一次刷新计算本候选的实际货架库存；无限库存直接沿用 SaleEntry.InitialStock。 */
	bool TryResolveRefreshedStock(FRandomStream& RandomStream, int32& OutStock) const;
};

/** 一次商店刷新如何从固定条目和随机池生成当前货架；固定条目永远保留，触发时机不在这里配置。 */
USTRUCT(BlueprintType)
struct FCatShopRefreshRule
{
	GENERATED_BODY()

	/** 每次刷新从随机池抽取多少条；超过候选数量时只抽到候选耗尽，不重复上架同一 EntryId。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Refresh", meta = (ClampMin = "0"))
	int32 RandomEntryCount = 3;
};

/** 商店出售表资产；它是固定保底货架、随机池和刷新规则的同一份权威数据源。 */
UCLASS(BlueprintType)
class CATFISHING_API UCatShopCatalogDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 固定货架条目；每次刷新都会先把这里的有效商品放进当前商店库存。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Catalog")
	TArray<FCatShopSaleEntry> FixedEntries;

	/** 随机货架候选池；刷新时按权重抽取后才会进入当前商店库存。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Catalog")
	TArray<FCatShopRandomSaleEntry> RandomEntries;

	/** 本表的刷新规则；只决定抽取数量和固定条目保留，不绑定具体触发时机。 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Refresh")
	FCatShopRefreshRule RefreshRule;

	/** 按本资产生成一次当前货架目录；失败时清空输出并给出人类可读原因。 */
	bool BuildRefreshedCatalogEntries(FRandomStream& RandomStream, TArray<FCatShopCatalogEntry>& OutEntries,
		FString& OutError) const;

	/** 收集本资产所有可展示候选；UI 用它把公开库存 EntryId 还原成展示行，不把未抽中的随机项当库存。 */
	void CollectDisplayCatalogEntries(TArray<FCatShopCatalogEntry>& OutEntries) const;

	/** 从任意固定/随机数组生成当前货架；摊位组件内联表和 DataAsset 复用同一套数组构建规则。 */
	static bool BuildRefreshedCatalogEntriesFromArrays(const TArray<FCatShopSaleEntry>& FixedEntries,
		const TArray<FCatShopRandomSaleEntry>& RandomEntries, const FCatShopRefreshRule& RefreshRule,
		FRandomStream& RandomStream, TArray<FCatShopCatalogEntry>& OutEntries, FString& OutError);

	/** 从任意固定/随机数组收集展示候选；客户端 Model 和编辑器配置表共用，避免两套展示口径。 */
	static void CollectDisplayCatalogEntriesFromArrays(const TArray<FCatShopSaleEntry>& FixedEntries,
		const TArray<FCatShopRandomSaleEntry>& RandomEntries, TArray<FCatShopCatalogEntry>& OutEntries);
};
