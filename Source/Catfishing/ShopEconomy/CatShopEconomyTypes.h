#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "GameFramework/PlayerState.h"
#include "CatShopEconomyTypes.generated.h"

/** 商店目录条目的交付类别；ShopEconomy 只记录订单语义，不直接写玩家随身库存或 Items。 */
UENUM(BlueprintType)
enum class ECatShopEntryKind : uint8
{
	/** 未声明类别；运行目录必须拒绝。 */
	Unknown,
	/** 购买后上层把装备型定义交给买家自己的随身库存数组。 */
	EquipmentGrant,
	/** 购买后上层把数量型定义交给买家自己的随身库存数组。 */
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

/** 经济交易类别；账本用它区分玩家购买、售鱼入账和免费自取。 */
UENUM(BlueprintType)
enum class ECatShopTransactionKind : uint8
{
	/** 交易类别未知；只作为默认空值。 */
	Unknown,
	/** 消耗团队公款购买一条商店目录项。 */
	Purchase,
	/** Items 已不可逆移除鱼以后，把售鱼收入记入团队公款。 */
	FishSale,
	/** 领取配置为免费自取的订单；普通饵和 1 级保底竿都走这一类，不会减少公款。 */
	FreeClaim
};

/** 订单交付进度；Shop 只记录下游回执，不直接修改玩家随身库存或 Items。 */
UENUM(BlueprintType)
enum class ECatShopDeliveryState : uint8
{
	/** 该交易不需要交付；售鱼入账属于这种账本。 */
	None,
	/** 公款和商店库存已经提交，等待下游随身库存给出交付回执。 */
	Pending,
	/** 下游领域已经以独立回执确认交付完成；重复确认只读取这条事实。 */
	Delivered
};

/** 一条商店可交易目录项；价格、库存和交付定义都由配置显式给出。 */
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

	/** 校验目录项是否足以进入运行库存；不检查下游定义是否存在，避免 ShopEconomy 偷做 Equipment/Data 的事实判断。 */
	bool IsRuntimeReady() const;
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

	/** 对应的商店目录稳定 ID。 */
	UPROPERTY(BlueprintReadOnly)
	FName EntryId = NAME_None;

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

	/** 下游领域成功交付后的聚合版本；用于审计交付发生在哪个 Equipment/库存版本之后。 */
	UPROPERTY(BlueprintReadOnly)
	int64 DeliveryRevision = 0;

	/** 购买/免费领取时的商店目录项；售鱼可保持 None。 */
	UPROPERTY(BlueprintReadOnly)
	FName EntryId = NAME_None;

	/** 订单要交付的下游定义；售鱼可保持 None。 */
	UPROPERTY(BlueprintReadOnly)
	FName DefinitionId = NAME_None;

	/** 售鱼时被 Items 不可逆移除的鱼实例；购买可保持无效。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid FishInstanceId;

	/** 这条鱼卖出时在谁手里；购买和免费自取保持 Unknown。账本记录它是为了让"钱从哪来"事后可追。 */
	UPROPERTY(BlueprintReadOnly)
	ECatShopFishSaleSource FishSource = ECatShopFishSaleSource::Unknown;

	/** 对团队公款的真实增量；购买为负，售鱼为正，免费领取为 0。 */
	UPROPERTY(BlueprintReadOnly)
	int32 WalletDelta = 0;

	/** 交易提交后的公款版本；免费领取不改余额时保留当前版本。 */
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

	/** 购买、售鱼入账还是免费自取。 */
	UPROPERTY(BlueprintReadOnly)
	ECatShopTransactionKind Kind = ECatShopTransactionKind::Unknown;

	/** 买的是哪一条目录项；售鱼保持 None。 */
	UPROPERTY(BlueprintReadOnly)
	FName EntryId = NAME_None;

	/** 订单指向的下游定义；售鱼保持 None。 */
	UPROPERTY(BlueprintReadOnly)
	FName DefinitionId = NAME_None;

	/** 卖出的鱼来自哪里；购买和免费自取保持 Unknown。 */
	UPROPERTY(BlueprintReadOnly)
	ECatShopFishSaleSource FishSource = ECatShopFishSaleSource::Unknown;

	/** 公款余额的真实增量；购买为负，售鱼为正，免费自取为 0。 */
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

/** 玩家购买或免费领取的经济命令；ExpectedRevision 对应团队公款版本。 */
USTRUCT(BlueprintType)
struct FCatShopPurchaseCommand
{
	GENERATED_BODY()

	/** RequestId、ExpectedRevision 与服务器身份；客户端不能提交价格、库存或公款增量。 */
	UPROPERTY(BlueprintReadWrite)
	FCatDomainCommandContext Context;

	/** 要购买或领取的商店目录项。 */
	UPROPERTY(BlueprintReadWrite)
	FName EntryId = NAME_None;
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

	/** 购买或免费领取涉及的库存快照；售鱼或缺项时保持默认。 */
	UPROPERTY(BlueprintReadOnly)
	FCatShopStockSnapshot Stock;

	/** 首次成功提交的账本记录；拒绝时保持默认，重放返回首次记录。 */
	UPROPERTY(BlueprintReadOnly)
	FCatShopTransactionRecord Transaction;
};
