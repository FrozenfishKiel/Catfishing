#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
class UTexture2D;

#include "CatShopCatalogTypes.generated.h"

/** 一条商店可交易目录项；价格、库存、购买数量和交付定义都由配置显式给出。 */
USTRUCT(BlueprintType)
struct FCatShopCatalogEntry
{
	GENERATED_BODY()

	/** 商店目录稳定 ID；客户端意图只引用它，不能直接指定价格或库存。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	FName EntryId = NAME_None;

	/** 订单交付给下游库存时使用的定义 ID；具体定义是否能入库由营地公共仓库和装备定义裁决。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	FName DefinitionId = NAME_None;

	/** 商品在商店页里归属的展示分类；空值只出现在“全部”，非空值由 WBP 分类按钮按同一个 FName 过滤。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FName DisplayCategoryId = NAME_None;

	/** 商品分类按钮的显示名覆盖；为空时 UI 直接用 DisplayCategoryId，避免程序内置“鱼竿/鱼饵/鱼窝”等分类文案。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FText DisplayCategoryNameOverride;

	/** 单次选购向目标库存发放的数量；商店库存扣一次货架库存，但目标库存可以收到多份鱼饵或窝料。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Catalog", meta = (ClampMin = "1"))
	int32 PurchaseQuantity = 1;

	/**
	 * 单次购买消耗的公款数额。0 是合法取值，表达“这一项显式免费”，免费普通饵就靠它；负数不允许，商店不能反过来发钱。
	 * 默认值刻意取 -1 作为“这一列尚未填写”的哨兵，而不是 0：两者在运行期必须能区分开，
	 * 否则漏填价格的目录项会静默变成免费品，玩家能白拿本该收费的东西。校验因此只放行显式写过的非负价格。
	 * 这里不设 ClampMin，否则编辑器会把哨兵夹成 0，等于把这条区分能力又抹掉。
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	int32 UnitPrice = -1;

	/** 初始可购买库存；非无限库存必须大于 0。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Catalog", meta = (ClampMin = "0"))
	int32 InitialStock = 0;

	/** 是否忽略库存扣减；普通免费饵通常使用无限库存，有限商品仍要扣减。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	bool bUnlimitedStock = false;

	/** 该目录项是否参与运行目录；关闭时配置只作为编辑数据存在，不会进入当前商店货架。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	bool bEnabled = true;

	/** 商店层面的上架解锁条件；当前没有商店解锁事实源，留空才可运行，非空会让该条目 fail-closed。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	FName RequiredShopUnlockId = NAME_None;

	/**
	 * 这一项是不是“每日进货”商品，也就是每天开市时把剩余库存重置回当日进货量的那一类。
	 * 只有它为 true 的条目会被 AdvanceShopDay 补货；永不缺货的竿和基础补给用 bUnlimitedStock 表达，不走这条。
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	bool bDailyRestock = false;

	/**
	 * 每天开市时这一项被重置回的库存数量。默认 0 且不接受 0：
	 * 一旦有人把条目标成每日进货却没给数量，IsRuntimeReady 就判它非法，整份目录随之关闭，
	 * 而不是替产品猜一个“每天进几个”。
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Catalog", meta = (ClampMin = "0"))
	int32 DailyRestockQuantity = 0;

	/** 商店展示名覆盖；为空时 UI 使用物品定义或稳定 ID，后端不读取它做交易裁决。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FText DisplayNameOverride;

	/** 商店描述覆盖；只给 View 展示当前售卖口径，不能改变装备定义或购买结果。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Presentation", meta = (MultiLine = "true"))
	FText DescriptionOverride;

	/** 商店图标覆盖；为空时 UI 可使用装备定义图标，后端库存只保存 DefinitionId 和数量。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UTexture2D> IconOverride;

	/** 商店展示排序值；固定保底项可排前，随机池抽中的条目按同一字段稳定排序。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	int32 SortOrder = 0;

	/** 校验目录项是否足以进入运行库存；不检查下游定义是否存在，避免 ShopEconomy 偷做 Equipment/Data 的事实判断。 */
	bool IsRuntimeReady() const;
};

/**
 * 策划维护的商店出售表行；一行既描述商品事实，也描述它是固定上架还是参与刷新随机池。
 * EntryId 留空时使用 RowName，这样策划批量增删商品时不用在两列里维护同一个稳定主键。
 */
USTRUCT(BlueprintType)
struct FCatShopCatalogTableRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 商店目录稳定 ID；留空时运行期使用 DataTable 的 RowName，避免策划重复填写同一主键。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	FName EntryId = NAME_None;

	/** 订单交付给下游库存时使用的定义 ID；商店只保存引用，不在表里复制装备定义本身。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	FName DefinitionId = NAME_None;

	/** 商品页展示分类；鱼竿、鱼饵、鱼窝等分类都由这列决定，程序不内置分类枚举。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FName DisplayCategoryId = NAME_None;

	/** 分类按钮显示名；同一 DisplayCategoryId 多行重复填写时，UI 使用排序最靠前商品上的第一个非空显示名。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FText DisplayCategoryNameOverride;

	/** 单次选购会交付到营地公共仓库的数量；购物车里同一商品选多次时会按次数累加。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog", meta = (ClampMin = "1"))
	int32 PurchaseQuantity = 1;

	/** 单次选购消耗的公款数额；0 表示策划明确配置的免费商品，负数表示价格未裁定并会被运行期拒绝。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	int32 UnitPrice = -1;

	/** 本行进入货架时的默认库存；有限库存必须大于 0，无限库存只用它做展示用数量。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog", meta = (ClampMin = "0"))
	int32 InitialStock = 0;

	/** 是否忽略货架库存扣减；基础补给或长期供应品可以用它表达永不售罄。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	bool bUnlimitedStock = false;

	/** 是否参与运行商店；关闭后本行只作为编辑数据存在，不会进入货架候选或 UI 候选。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	bool bEnabled = true;

	/** 商店层面的上架解锁条件；当前没有可信事实源，非空值仍会让本行 fail-closed。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	FName RequiredShopUnlockId = NAME_None;

	/** 是否每天进货时把有限库存重置到 DailyRestockQuantity；无限库存不需要每日进货。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	bool bDailyRestock = false;

	/** 每日进货重置后的库存数量；只有 bDailyRestock 为 true 时参与运行校验。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog", meta = (ClampMin = "0"))
	int32 DailyRestockQuantity = 0;

	/** 是否每次刷新都固定上架；关闭时本行必须有正权重才可能被随机抽入货架。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Refresh")
	bool bAlwaysStocked = true;

	/** 随机刷新候选的抽取权重；仅在 bAlwaysStocked 为 false 时使用，0 表示不会进入当前货架。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Refresh", meta = (ClampMin = "0"))
	int32 RefreshWeight = 0;

	/** 随机抽中时的最小库存覆盖；-1 表示不覆盖，必须和最大值同时填写才生效。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Refresh")
	int32 MinRefreshedStockOverride = -1;

	/** 随机抽中时的最大库存覆盖；-1 表示不覆盖，必须和最小值同时填写才生效。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Refresh")
	int32 MaxRefreshedStockOverride = -1;

	/** 商店展示名覆盖；为空时 UI 使用装备定义名或稳定 ID。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FText DisplayNameOverride;

	/** 商店描述覆盖；只影响展示，不参与交易裁决。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation", meta = (MultiLine = "true"))
	FText DescriptionOverride;

	/** 商店图标覆盖；为空时 WBP 可以使用装备定义图标或默认图标。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UTexture2D> IconOverride;

	/** 商店展示排序值；同序号再按 EntryId 稳定排序。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	int32 SortOrder = 0;

	/** 把策划表行转换为运行目录项；RowName 只在 EntryId 留空时作为主键。 */
	bool TryBuildCatalogEntry(FName RowName, FCatShopCatalogEntry& OutEntry) const;

	/** 解析随机抽中后的库存数量；没有覆盖区间时沿用 InitialStock。 */
	bool TryResolveRefreshedStock(FRandomStream& RandomStream, int32& OutStock) const;
};

/** 商店刷新规则；它只描述本次从随机候选池抽几条，刷新发生在哪个时机由上层调用方决定。 */
USTRUCT(BlueprintType)
struct FCatShopCatalogRefreshRule
{
	GENERATED_BODY()

	/** 每次刷新从非固定候选里抽取的商品数量；0 表示只展示固定上架行。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Refresh", meta = (ClampMin = "0"))
	int32 RandomEntryCount = 0;
};

/** 收购价格表的一行；DataTable RowName 就是鱼种 ID，行内只保留每千克金币系数。 */
USTRUCT(BlueprintType)
struct FCatShopFishSalePriceRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 每千克对应的金币系数；只允许有限正数，缺失或非法行会让关联售鱼整单拒绝。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FishSale", meta = (ClampMin = "0.0"))
	double MoneyCoefficient = 0.0;

	/** 校验该行是否能作为服务器收购定价依据；鱼种主键由 DataTable RowName 提供，不提供保底价格或缺行回退。 */
	bool IsRuntimeReady() const;
};

/** 显式刷新当前商店货架的服务器请求；它只描述随机数来源，不决定刷新时机。 */
USTRUCT(BlueprintType)
struct FCatShopRefreshRequest
{
	GENERATED_BODY()

	/** 是否使用调用方给出的随机种子；测试、调试或运营复现需要稳定结果时打开，正常运行可关闭。 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Refresh")
	bool bUseExplicitRandomSeed = false;

	/** 调用方显式指定的随机种子；只有 bUseExplicitRandomSeed 为 true 时参与抽取。 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Refresh")
	int32 RandomSeed = 0;
};
