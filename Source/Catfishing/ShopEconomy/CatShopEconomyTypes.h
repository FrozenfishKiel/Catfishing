#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "GameFramework/PlayerState.h"

class UTexture2D;

#include "CatShopEconomyTypes.generated.h"

/** 商店目录条目的展示/账本类别；真正入库方式由物品定义和收货库存共同裁决，不再由商店条目单独拍板。 */
UENUM(BlueprintType)
enum class ECatShopEntryKind : uint8
{
	/** 未声明类别；运行目录必须拒绝。 */
	Unknown,
	/** 购买项指向鱼竿、鱼漂、抄网这类单件装备；商店只把它写到账本和 UI，不直接决定目标库存实现。 */
	EquipmentGrant,
	/** 购买项指向鱼饵、窝料、草药这类数量物；具体堆叠规则仍来自装备定义和库存对象。 */
	InventoryQuantityGrant
};

/**
 * 一条被卖给商人猫的鱼来自哪个持有位置；商人猫三种来源都收，这个枚举只让账本能说清钱是从哪条鱼来的。
 * 它不表达定价差异：飞书商店册 rev285 §2 只写"自己的、缸里的、偷来的都收"，没有给任何按来源的折价或加价。
 */
UENUM(BlueprintType)
enum class ECatShopFishSaleSource : uint8
{
	/** 调用方没有声明来源；售鱼写口必须拒绝，避免来源不明的钱进公款账本。 */
	Unknown,
	/** 卖鱼者从明确地面鱼护中选择的鱼。 */
	FishGuard,
	/** 共用鱼缸里的鱼；缸是全队共有，钱同样进公款。 */
	SharedFishTank,
	/** 偷来的、仍在 Social escrow 里的鱼。 */
	StolenEscrow
};

/** 经济交易类别；账本用它区分玩家购物车购买和售鱼入账。 */
UENUM(BlueprintType)
enum class ECatShopTransactionKind : uint8
{
	/** 交易类别未知；只作为默认空值。 */
	Unknown,
	/** 消耗团队公款购买一条商店目录项；购物车会为每种 EntryId 写一条购买账本。 */
	Purchase,
	/** Items 已不可逆移除鱼以后，把售鱼收入记入团队公款。 */
	FishSale
};

/** 订单交付进度；Shop 只记录下游回执，不直接修改营地公共仓库、玩家随身库存或 Items。 */
UENUM(BlueprintType)
enum class ECatShopDeliveryState : uint8
{
	/** 该交易不需要交付；售鱼入账属于这种账本。 */
	None,
	/** 公款和商店库存已经提交，等待下游库存给出交付回执。 */
	Pending,
	/** 下游领域已经以独立回执确认交付完成；重复确认只读取这条事实。 */
	Delivered
};

/** 一条商店可交易目录项；价格、库存、购买数量和交付定义都由配置显式给出。 */
USTRUCT(BlueprintType)
struct FCatShopCatalogEntry
{
	GENERATED_BODY()

	/** 商店目录稳定 ID；客户端意图只引用它，不能直接指定价格或库存。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	FName EntryId = NAME_None;

	/** 该订单最终交给哪个下游领域；ShopEconomy 本身只写订单和账本。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	ECatShopEntryKind Kind = ECatShopEntryKind::Unknown;

	/** 被订单引用的装备或耗材定义 ID；具体定义有效性由下游 Equipment/Data 继续校验。 */
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
	 * 单次购买消耗的公款数额。0 是合法取值，表达"这一项显式免费"，免费普通饵就靠它；负数不允许，商店不能反过来发钱。
	 * 默认值刻意取 -1 作为"这一列还没填"的哨兵，而不是 0：两者在运行期必须能区分开，
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

	/** 该目录项是否参与运行目录；关闭时配置仍可留在表里，但不会进入当前商店货架。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	bool bEnabled = true;

	/** 商店层面的上架解锁条件；当前还没有商店解锁事实源，留空才可运行，非空会让该条目 fail-closed。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	FName RequiredShopUnlockId = NAME_None;

	/**
	 * 这一项是不是飞书商店册 §3.2 的"每日进货"商品，也就是每天开市时把剩余库存重置回当日进货量的那一类。
	 * 只有它为 true 的条目会被 AdvanceShopDay 补货；永不缺货的竿和基础补给用 bUnlimitedStock 表达，不走这条。
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	bool bDailyRestock = false;

	/**
	 * 每天开市时这一项被重置回的库存数量。飞书 §6 的进货量锚点目前是空的，所以这里默认 0 且不接受 0：
	 * 一旦有人把条目标成每日进货却没给数量，IsRuntimeReady 就判它非法，整份目录随之关闭，
	 * 而不是替飞书猜一个"每天进几个"。
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Catalog", meta = (ClampMin = "0"))
	int32 DailyRestockQuantity = 0;

	/** 商店展示名覆盖；为空时 UI 回退到物品定义或稳定 ID，后端不读取它做交易裁决。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FText DisplayNameOverride;

	/** 商店描述覆盖；只给 View 展示当前售卖口径，不能改变装备定义或购买结果。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Presentation", meta = (MultiLine = "true"))
	FText DescriptionOverride;

	/** 商店图标覆盖；为空时可回退到装备定义图标，后端库存只保存 DefinitionId 和数量。 */
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
 * RowName 可作为 EntryId 兜底，这样策划批量增删商品时不用在两列里维护同一个稳定主键。
 */
USTRUCT(BlueprintType)
struct FCatShopCatalogTableRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 商店目录稳定 ID；留空时运行期使用 DataTable 的 RowName，避免策划重复填写同一主键。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	FName EntryId = NAME_None;

	/** 该订单最终交给哪个下游领域；ShopEconomy 只写订单，具体入库规则由营地公共仓库和装备定义裁决。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	ECatShopEntryKind Kind = ECatShopEntryKind::Unknown;

	/** 被订单引用的装备或耗材定义 ID；商店只保存引用，不在表里复制装备定义本身。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	FName DefinitionId = NAME_None;

	/** 商品页展示分类；鱼竿、鱼饵、鱼窝等分类都由这列决定，程序不再内置分类枚举。 */
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

	/** 本行进入货架时的默认库存；有限库存必须大于 0，无限库存只用它做展示兜底。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog", meta = (ClampMin = "0"))
	int32 InitialStock = 0;

	/** 是否忽略货架库存扣减；基础补给或长期供应品可以用它表达永不售罄。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catalog")
	bool bUnlimitedStock = false;

	/** 是否参与运行商店；关闭后本行保留在表里但不会进入货架候选或 UI 候选。 */
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

	/** 商店展示名覆盖；为空时 UI 回退到装备定义名或稳定 ID。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	FText DisplayNameOverride;

	/** 商店描述覆盖；只影响展示，不参与交易裁决。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation", meta = (MultiLine = "true"))
	FText DescriptionOverride;

	/** 商店图标覆盖；为空时 WBP 可以回退到装备定义图标或默认图标。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	TSoftObjectPtr<UTexture2D> IconOverride;

	/** 商店展示排序值；同序号再按 EntryId 稳定排序。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation")
	int32 SortOrder = 0;

	/** 把策划表行转换为运行目录项；RowName 只在 EntryId 留空时作为主键兜底。 */
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

/**
 * 收鱼价体重轴上的一档：重量达到 MinimumWeightKilograms 的鱼按 Price 收购。
 * 用离散档位而不是一条直线，是因为飞书商店册 §3.1 只写了"收购价挂体重轴打底（鱼体重重＝钱多）"，
 * 斜率、截距和窝料轴系数一个都没给；档位表的每一行都是将来能被逐条拍板的数值，直线则要先编一个斜率出来。
 */
USTRUCT(BlueprintType)
struct FCatShopFishWeightPrice
{
	GENERATED_BODY()

	/** 进入这一档需要达到的鱼体重下限，单位千克，取到等号；比最轻一档还轻的鱼没有档位，估价直接失败。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Appraisal", meta = (ClampMin = "0.0"))
	double MinimumWeightKilograms = 0.0;

	/** 落在这一档的鱼的收购价；必须为正，否则等于让商人猫白收鱼。 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Appraisal", meta = (ClampMin = "0"))
	int32 Price = 0;

	/** 校验这一档自身是否可用；重量必须是有限非负数，价格必须为正。 */
	bool IsRuntimeReady() const;
};

/** 团队公款复制/查询快照；当前只在服务器服务内维护，UI 接线后可用它做只读展示。 */
USTRUCT(BlueprintType)
struct FCatShopWalletSnapshot
{
	GENERATED_BODY()

	/** 公款聚合版本；每次余额改变递增，购买和售鱼命令以它做并发前提。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;

	/** 团队当前余额；没有个人公款分叉，也不允许客户端直接提交增量。 */
	UPROPERTY(BlueprintReadOnly)
	int32 Balance = 0;
};

/** 单个商店库存的只读快照；无限库存不通过 RemainingStock 表示容量。 */
USTRUCT(BlueprintType)
struct FCatShopStockSnapshot
{
	GENERATED_BODY()

	/** 这条库存来自哪个商店摊位库存；EntryId 只在这个 ID 范围内解释，不能全局混用。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid ShopInventoryId;

	/** 对应的商店目录稳定 ID。 */
	UPROPERTY(BlueprintReadOnly)
	FName EntryId = NAME_None;

	/** 本轮上架时的初始库存；随机库存覆盖后，UI 用它和 RemainingStock 组成同一轮真实库存展示。 */
	UPROPERTY(BlueprintReadOnly)
	int32 InitialStock = 0;

	/** 当前剩余库存；无限库存时保持初始值，只以 bUnlimitedStock 判断是否扣减。 */
	UPROPERTY(BlueprintReadOnly)
	int32 RemainingStock = 0;

	/** 该项是否无限库存；无限项成功交易不推进库存 Revision。 */
	UPROPERTY(BlueprintReadOnly)
	bool bUnlimitedStock = false;

	/** 库存聚合版本；有限库存每次扣减递增。 */
	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;
};

/** 一条经济账本记录；金额、公款和库存事实不可回写，交付状态只由下游回执推进。 */
USTRUCT(BlueprintType)
struct FCatShopTransactionRecord
{
	GENERATED_BODY()

	/** ShopEconomy 为首次提交分配的账本 ID；重复请求返回同一条记录。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid TransactionId;

	/** 客户端或上层命令 RequestId；只用于幂等关联，不作为账本主键。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid RequestId;

	/** 服务器重建的操作者身份；团队公款仍是共享的，不按该身份拆分余额。 */
	UPROPERTY(BlueprintReadOnly)
	FString StableNetId;

	/** 本条账本的交易类别。 */
	UPROPERTY(BlueprintReadOnly)
	ECatShopTransactionKind Kind = ECatShopTransactionKind::Unknown;

	/** 购买类订单的下游交付类别；售鱼没有交付目标，保持 Unknown。 */
	UPROPERTY(BlueprintReadOnly)
	ECatShopEntryKind EntryKind = ECatShopEntryKind::Unknown;

	/** 订单当前交付进度；购买先进入 Pending，只有下游回执能推进到 Delivered。 */
	UPROPERTY(BlueprintReadOnly)
	ECatShopDeliveryState DeliveryState = ECatShopDeliveryState::None;

	/** 下游领域成功交付时返回的回执 ID；Pending 或售鱼记录保持无效。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid DeliveryReceiptId;

	/** 下游领域成功交付后的聚合版本；用于审计交付发生在哪个下游库存版本之后。 */
	UPROPERTY(BlueprintReadOnly)
	int64 DeliveryRevision = 0;

	/** 购物车购买时的商店目录项；售鱼可保持 None。 */
	UPROPERTY(BlueprintReadOnly)
	FName EntryId = NAME_None;

	/** 购物车购买来源的摊位库存；账本用它说明这条 EntryId 是按哪一个商店表解释的。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid ShopInventoryId;

	/** 订单要交付的下游定义；售鱼可保持 None。 */
	UPROPERTY(BlueprintReadOnly)
	FName DefinitionId = NAME_None;

	/** 本订单成功后应发放给目标库存的数量；货架库存只扣一单，目标库存按这个数量接收入库。 */
	UPROPERTY(BlueprintReadOnly)
	int32 PurchaseQuantity = 0;

	/** 售鱼时被 Items 不可逆移除的鱼实例；购买可保持无效。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid FishInstanceId;

	/** 这条鱼卖出时在谁手里；购物车购买保持 Unknown。账本记录它是为了让"钱从哪来"事后可追。 */
	UPROPERTY(BlueprintReadOnly)
	ECatShopFishSaleSource FishSource = ECatShopFishSaleSource::Unknown;

	/** 对团队公款的真实增量；购物车购买为负或 0，售鱼为正。 */
	UPROPERTY(BlueprintReadOnly)
	int32 WalletDelta = 0;

	/** 交易提交后的公款版本；0 元购物车不改余额时保留当前版本。 */
	UPROPERTY(BlueprintReadOnly)
	int64 WalletRevision = 0;

	/** 交易提交后的库存版本；售鱼不涉及库存时为 0。 */
	UPROPERTY(BlueprintReadOnly)
	int64 StockRevision = 0;
};

/**
 * 一条对全队公开的经济交易记录。飞书 §3.3 要求收入支出全体可见、§3.2 要求每笔购买全队广播，
 * 所以这份形态只留下"谁、做了什么、钱怎么动的"，不带服务器私有身份键，也不带库存内部结构。
 */
USTRUCT(BlueprintType)
struct FCatShopPublicTransaction
{
	GENERATED_BODY()

	/** 对应账本记录的稳定 ID；客户端拿它去重，不用它反查服务器账本。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid TransactionId;

	/**
	 * 这笔交易记录的公开操作者。ShopEconomy 服务手上只有服务器私有 StableNetId，按项目约定不能进复制 DTO，
	 * 所以这一项由复制挂载点按身份映射解析后填入；服务自己构造快照时一律留空。
	 */
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<APlayerState> ActorPlayerState = nullptr;

	/** 购物车购买还是售鱼入账。 */
	UPROPERTY(BlueprintReadOnly)
	ECatShopTransactionKind Kind = ECatShopTransactionKind::Unknown;

	/** 买的是哪一条目录项；售鱼保持 None。 */
	UPROPERTY(BlueprintReadOnly)
	FName EntryId = NAME_None;

	/** 购买发生在哪个摊位库存上；客户端用它区分同名 EntryId 来自不同商店。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid ShopInventoryId;

	/** 订单指向的下游定义；售鱼保持 None。 */
	UPROPERTY(BlueprintReadOnly)
	FName DefinitionId = NAME_None;

	/** 购物车购买实际发放的数量；售鱼保持 0，客户端只能展示，不能据此补发物品。 */
	UPROPERTY(BlueprintReadOnly)
	int32 PurchaseQuantity = 0;

	/** 卖出的鱼来自哪里；购物车购买保持 Unknown。 */
	UPROPERTY(BlueprintReadOnly)
	ECatShopFishSaleSource FishSource = ECatShopFishSaleSource::Unknown;

	/** 公款余额的真实增量；购物车购买为负或 0，售鱼为正。 */
	UPROPERTY(BlueprintReadOnly)
	int32 WalletDelta = 0;

	/** 这笔订单的交付进度；售鱼没有交付目标，保持 None。 */
	UPROPERTY(BlueprintReadOnly)
	ECatShopDeliveryState DeliveryState = ECatShopDeliveryState::None;
};

/**
 * 团队经济对外的完整只读形态。飞书 §7 要求公款余额全队常时可见，所以余额、货架库存和公开交易记录放在同一份快照里，
 * 客户端拿到它就能同时渲染余额、库存和交易反馈，不需要再发第二次查询。
 * 这份结构本身不复制：挂到 GameState 还是独立组件由 Framework 决定，ShopEconomy 只负责它的内容正确。
 */
USTRUCT(BlueprintType)
struct FCatShopPublicEconomySnapshot
{
	GENERATED_BODY()

	/** 当前公款余额版本；客户端用它判断手上的快照是不是最新的。 */
	UPROPERTY(BlueprintReadOnly)
	int64 WalletRevision = 0;

	/** 当前公款余额。 */
	UPROPERTY(BlueprintReadOnly)
	int32 Balance = 0;

	/** 商店当前处在第几天；每日进货刷新后它会跟着走，客户端据此知道货架已经换过一轮。 */
	UPROPERTY(BlueprintReadOnly)
	int32 ShopDayIndex = 0;

	/** 当前货架库存快照；UI 用它展示剩余数量和禁用已售罄条目，不能据此绕过服务器购买裁决。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatShopStockSnapshot> Stocks;

	/** 本局至今的全部公开交易记录，按发生顺序排列。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatShopPublicTransaction> Transactions;
};

/** 客户端购物车提交给服务器的一行意图；它只包含商品 ID 和选购次数，不携带价格、库存或发货数量。 */
USTRUCT(BlueprintType)
struct FCatShopCartLineCommand
{
	GENERATED_BODY()

	/** 要结算的商店目录项；服务器会限定到同一个 ShopInventoryId 下解释。 */
	UPROPERTY(BlueprintReadWrite)
	FName EntryId = NAME_None;

	/** 该目录项被选购了几次；每次扣一份货架库存，并发放 PurchaseQuantity 对应的物品数量。 */
	UPROPERTY(BlueprintReadWrite, meta = (ClampMin = "1"))
	int32 CartCount = 1;
};

namespace CatShopCartLimits
{
	/** 一次购物车 RPC 允许携带的最大原始行数；它限制客户端输入面，不限制策划表总商品数量。 */
	inline constexpr int32 MaxCartLines = 64;

	/** 同一目录项在一车里允许聚合的最大选购次数；服务器归一化后也按这个上限拒绝异常载荷。 */
	inline constexpr int32 MaxCartCountPerEntry = 999;
}

/** 玩家一次支付整个购物车的经济命令；ExpectedRevision 对应团队公款版本。 */
USTRUCT(BlueprintType)
struct FCatShopCartCommand
{
	GENERATED_BODY()

	/** RequestId、ExpectedRevision 与服务器身份；客户端不能提交总价或仓库发货结果。 */
	UPROPERTY(BlueprintReadWrite)
	FCatDomainCommandContext Context;

	/** 服务器确认的来源商店库存；购物车里所有 EntryId 都只在这个摊位范围内解释。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid ShopInventoryId;

	/** 本次要结算的购物车行；服务器会合并重复 EntryId 并重新计算总价。 */
	UPROPERTY(BlueprintReadWrite)
	TArray<FCatShopCartLineCommand> Lines;
};

/** 服务器解析后的一行购物车；它把表中单次购买事实扩成这次整单的总价和交付数量。 */
struct FCatShopResolvedCartLine
{
	/** 服务器从当前摊位货架读取到的目录项原文；价格、库存和交付定义都以它为准。 */
	FCatShopCatalogEntry Entry;

	/** 本次整单里该目录项被选购的次数；有限库存会按这个次数扣减。 */
	int32 CartCount = 0;

	/** 本行应发往营地公共仓库的总数量；等于目录 PurchaseQuantity 乘以 CartCount。 */
	int32 DeliveryQuantity = 0;

	/** 本行应扣除的总价；等于目录 UnitPrice 乘以 CartCount。 */
	int32 LineTotalPrice = 0;
};

/** 服务器对购物车的只读报价结果；协调器用它在扣钱前先询问营地公共仓库能否整批接收。 */
struct FCatShopResolvedCart
{
	/** 通过服务器归一化后的购物车命令；重复 EntryId 已合并，行顺序只用于稳定提交。 */
	FCatShopCartCommand Command;

	/** 每行目录项、选购次数、交付数量和小计；调用方不能写回商店库存。 */
	TArray<FCatShopResolvedCartLine> Lines;

	/** 整个购物车应扣除的总公款；0 元整车合法，但仍必须走支付按钮提交。 */
	int32 TotalPrice = 0;

	/** 报价时看到的团队公款快照；用于前端回包和日志说明当前余额版本。 */
	FCatShopWalletSnapshot Wallet;
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

/** Items 已完成不可逆售鱼后提交给经济系统的入账命令。 */
USTRUCT(BlueprintType)
struct FCatShopFishSaleCommand
{
	GENERATED_BODY()

	/** RequestId、ExpectedRevision 与服务器身份；ExpectedRevision 仍指向团队公款版本。 */
	UPROPERTY(BlueprintReadWrite)
	FCatDomainCommandContext Context;

	/** 已经由 Items 移除或预留提交的鱼实例 ID；ShopEconomy 只记录它，不再删除鱼。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid FishInstanceId;

	/** Items 不可逆提交或交易协调记录 ID；没有该证据时不能给公款入账。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid ItemsCommitId;

	/** 这条鱼卖出时在谁手里；Unknown 一律拒绝，商人猫收三种来源但不收来路不明的钱。 */
	UPROPERTY(BlueprintReadWrite)
	ECatShopFishSaleSource SourceKind = ECatShopFishSaleSource::Unknown;

	/** 这条鱼被捕获时服务器冻结下来的重量，单位千克；它是收购价的唯一输入，必须由 Items 从鱼实例上取，不接受客户端填写。 */
	UPROPERTY(BlueprintReadWrite)
	double WeightKilograms = 0.0;

	/**
	 * 调用方带进来的成交价。服务器会用 WeightKilograms 自己查一次体重轴，两个值不完全相等就拒绝这笔售鱼。
	 * 保留一个由调用方填的价格，是为了让玩家在界面上看到的报价和最终入账的钱必须是同一个数；
	 * 它不是定价权：客户端伪造一个大数只会让整笔交易被拒，不会让公款多出一分钱。
	 */
	UPROPERTY(BlueprintReadWrite)
	int32 SaleValue = 0;
};

/** 下游领域完成购买交付后的确认命令；它只推进账本状态，不重新扣公款或库存。 */
USTRUCT(BlueprintType)
struct FCatShopDeliveryConfirmationCommand
{
	GENERATED_BODY()

	/** 确认请求的幂等 ID 与服务器身份；StableNetId 必须匹配原订单买家。 */
	UPROPERTY(BlueprintReadWrite)
	FCatDomainCommandContext Context;

	/** 要确认的 Shop 账本 ID；客户端不能用 EntryId 或 DefinitionId 猜测待交付订单。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid TransactionId;

	/** 下游领域提交成功后生成的回执 ID；Shop 只保存它用于审计和重放恢复。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid DeliveryReceiptId;

	/** 下游领域提交成功后的聚合版本；0 表示调用方没有真实提交证据。 */
	UPROPERTY(BlueprintReadWrite)
	int64 DeliveryRevision = 0;
};

/** 经济命令的统一返回；包含公共终态、公款快照、库存快照和首次账本记录。 */
USTRUCT(BlueprintType)
struct FCatShopTransactionResult
{
	GENERATED_BODY()

	/** 公共命令终态；Revision 始终对齐团队公款版本。 */
	UPROPERTY(BlueprintReadOnly)
	FCatDomainCommandResult Command;

	/** 交易提交后团队公款只读快照；拒绝时返回当前公款。 */
	UPROPERTY(BlueprintReadOnly)
	FCatShopWalletSnapshot Wallet;

	/** 与这次单条账本结果相关的货架库存快照；售鱼、拒绝或缺项时保持默认。 */
	UPROPERTY(BlueprintReadOnly)
	FCatShopStockSnapshot Stock;

	/** 首次成功提交的账本记录；拒绝时保持默认，重放返回首次记录。 */
	UPROPERTY(BlueprintReadOnly)
	FCatShopTransactionRecord Transaction;
};

/** 购物车经济命令的统一返回；一车可生成多条账本记录和多条库存快照，但只有一个公款终态。 */
USTRUCT(BlueprintType)
struct FCatShopCartTransactionResult
{
	GENERATED_BODY()

	/** 整个购物车提交的公共终态；Revision 对齐团队公款版本。 */
	UPROPERTY(BlueprintReadOnly)
	FCatDomainCommandResult Command;

	/** 整车交易提交后团队公款只读快照；拒绝时返回当前公款。 */
	UPROPERTY(BlueprintReadOnly)
	FCatShopWalletSnapshot Wallet;

	/** 每个被扣减货架项提交后的库存快照；无限库存也会返回当前快照供 UI 刷新。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatShopStockSnapshot> Stocks;

	/** 整车首次成功提交生成的购买账本行；每种 EntryId 一条，免费商品也通过 0 元账本记录进入交付。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatShopTransactionRecord> Transactions;
};
