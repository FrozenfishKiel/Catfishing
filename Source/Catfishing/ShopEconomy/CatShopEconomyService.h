#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ShopEconomy/CatShopEconomyTypes.h"
#include "CatShopEconomyService.generated.h"

/**
 * 一笔公开经济流水提交完成的服务器本机通知；复制挂载点订阅它来做"每笔购买全队广播"和公款余额刷新。
 * 它只在首次真实提交时发一次，重放和拒绝都不发，因此订阅方不需要自己去重。
 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatShopPublicTransactionCommitted, const FCatShopPublicTransaction&);

/** 一局团队商店经济服务；只写钱包、库存、订单和账本，不直接改 Equipment 或 Items。 */
UCLASS()
class CATFISHING_API UCatShopEconomyService : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 仅在 authority Game World 创建；客户端 UI 以后只能读复制/查询结果，不持有第二份钱包。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** WorldSubsystem 初始化时从显式 Settings 建立本局钱包、库存和目录可用性。 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** World 退出时关闭新交易并清空本局库存、账本和幂等缓存。 */
	virtual void Deinitialize() override;

	/** 团队钱包快照代表本局唯一余额和版本事实；UI、拒绝结果和交易前提都读它的副本，不能拿到可写引用绕过交易入口。 */
	FCatShopWalletSnapshot GetWalletSnapshot() const;

	/** 查询某个商店目录项的库存快照；不存在或目录不可用时返回 false。 */
	bool TryGetStockSnapshot(FName EntryId, FCatShopStockSnapshot& OutSnapshot) const;

	/**
	 * 取回某个目录项在开局被冻结下来的配置原文，主要是"这笔订单最后要交给哪个领域、交哪个定义"这两件事。
	 * 订单协调器用它在下单之前定位交付去向，好把交付侧的前提问在扣钱之前；不存在或目录不可用时返回 false 并清空输出。
	 * 返回 true 不代表这一项现在买得成——价格、库存、钱包版本和命令门仍然只由购买写口判定。
	 */
	bool TryGetCatalogEntry(FName EntryId, FCatShopCatalogEntry& OutEntry) const;

	/** 本局经济账本是钱包、库存和订单状态的审计事实；外层展示只能读副本，金额不可回写，交付状态只由确认回执推进。 */
	TArray<FCatShopTransactionRecord> GetTransactionLedgerSnapshot() const;

	/** 购买一条显式目录项；成功只扣钱包、扣库存并写账本，具体交付定义由下游领域接续。 */
	FCatShopTransactionResult PurchaseCatalogEntry(const FCatShopPurchaseCommand& Command);

	/**
	 * 领取显式配置为免费自取的目录项；当前只有普通饵和 1 级保底竿两条。
	 * 条目必须价格为 0 且库存无限——飞书对这两项写的都是"免费、不限量"，有限库存的免费品会让保底竿在某天领光，
	 * 那就不再是保底了，所以这里把无限库存当成免费自取的准入条件而不是可选项。
	 */
	FCatShopTransactionResult ClaimFreeCatalogEntry(const FCatShopPurchaseCommand& Command);

	/**
	 * 声明：按体重轴给出一条鱼的收购价；返回 false 表示这条鱼现在卖不掉，调用方必须整笔拒绝而不是自己补一个价。
	 * 实现：先要求本局经济 runtime 可用且收鱼价已被显式裁定，再把开局冻结的档位表和重量交给 Settings 的纯函数求值。
	 * 用途：售鱼写口自己会再查一次同一个价来核对调用方报价；这个公开入口是给界面报价和 Items 预检用的。
	 */
	bool TryAppraiseFishSale(double WeightKilograms, int32& OutSaleValue) const;

	/**
	 * 在 Items 不可逆删除鱼之前预检这笔售鱼能不能入账；它只读钱包、命令 gate、幂等缓存和收鱼价，不写账本。
	 * 预检和入账用完全相同的判据，包括那次估价核对，所以预检说能卖，入账就不会再因为价格被拒。
	 */
	bool ValidateFishSale(const FCatShopFishSaleCommand& Command, ECatDomainCommandError& OutError,
		int64& OutCurrentWalletRevision) const;

	/**
	 * Items 已经不可逆移除这条鱼之后，把卖鱼的钱记进团队钱包。本服务不删除鱼，只认调用方带来的 Items 提交证据。
	 * 价格由服务器按体重轴自己算，调用方带来的报价只是用来核对；两者不一致就整笔拒绝，不会按其中任何一个入账。
	 */
	FCatShopTransactionResult ApplyFishSale(const FCatShopFishSaleCommand& Command);
	/** 用 Equipment 或团队装备库的成功回执确认订单已交付；它不重新扣钱包、库存或生成第二条账本。 */
	FCatShopTransactionResult ConfirmTransactionDelivery(const FCatShopDeliveryConfirmationCommand& Command);

	/**
	 * 声明：把商店推进到新的一天，并按飞书 §3.2 的进货制把"每日进货"条目的剩余库存重置回当日进货量。
	 * 实现：先要求经济 runtime、目录和写口都可用，且新天序号确实比商店当前所在的天大——同一天重复调用不补第二次货；
	 *       随后逐条只重置标了 bDailyRestock 的库存并推进它们各自的库存 Revision，无限库存和一局一次的限量品原样不动。
	 * 边界：它不换货架、不改价格。飞书"商店每日刷新的装备不一样"那句还没有任何轮换名单，本函数不替它挑今天卖什么。
	 * 返回值表示这次调用是否真的推进了一天并补了货，供调用方决定要不要广播货架已刷新。
	 */
	bool AdvanceShopDay(int32 NewDayIndex);

	/**
	 * 声明：构造团队经济的对外只读形态，包含公款余额、商店天序号和本局全部公开流水。
	 * 实现：直接从当前钱包和账本逐条转换，不做过滤或裁剪；每条流水的操作者由传入的解析器现场解析。
	 * 用途：给复制挂载点做全量刷新用；OnPublicTransactionCommitted 只负责通知“变了”，重建仍走这里。
	 * 参数 ResolveActorPlayerState：服务手上只有服务器私有 StableNetId，按项目约定它不能进复制 DTO，
	 *      所以“这笔流水是谁做的”必须由持有身份映射的一方现场解析。不传时全部条目的操作者留空，
	 *      这对服务器内部查询和自动化是正确的，对客户端则表现为未知操作者。
	 */
	FCatShopPublicEconomySnapshot BuildPublicSnapshot(
		const TFunction<APlayerState*(const FString&)>& ResolveActorPlayerState = nullptr) const;

	/** 首次提交一笔经济流水后的本机广播；不携带可写指针，也不携带服务器私有身份。 */
	FCatShopPublicTransactionCommitted OnPublicTransactionCommitted;

	/**
	 * 商人猫收摊：购买、免费自取、售鱼入账和交付确认四个写口从此不再受理新命令，每日进货也一并停下；
	 * 钱包、库存和账本查询照常可读，既有 RequestId 重放仍返回首次终态。
	 * 新命令拿到的错误码不一定是 CommandsClosed：四个写口都把配置/策略未裁的 PolicyUndecided 排在命令门之前，
	 * 所以配置缺失时收摊后返回的是 PolicyUndecided。两者都是拒绝，判断"商店关没关"不要只认 CommandsClosed。
	 * 它同时是最后一个夜晚"买卖冻结、只剩吃鱼与篝火回看"的表达和 World teardown 的收口；调用点是 Run 进入两种
	 * 结算夜时的 GameMode 相位切换和 World teardown 的 Deinitialize，两者重复调用不产生第二次副作用。
	 */
	void CloseCommands();

private:
	/** 一条运行期库存记录；目录项不可变，剩余库存和版本只由交易提交修改。 */
	struct FStockRecord
	{
		/** 原始配置目录项；价格、定义和无限库存语义都从这里读取。 */
		FCatShopCatalogEntry Entry;

		/** 当前剩余库存；无限库存不会被交易修改。 */
		int32 RemainingStock = 0;

		/** 库存版本；有限库存扣减时递增。 */
		int64 Revision = 0;
	};

	/** 从 Settings 重建本局钱包与库存；重复目录或非法目录项会让购买路径 fail-closed。 */
	void LoadRuntimeEconomyFromSettings();

	/** 购买/免费领取共用提交流程；调用前已经选定交易类别并完成基本命令校验。 */
	FCatShopTransactionResult CommitCatalogTransaction(const FCatShopPurchaseCommand& Command,
		ECatShopTransactionKind TransactionKind);

	/** 按当前库存记录构造只读快照；空指针返回默认快照。 */
	static FCatShopStockSnapshot MakeStockSnapshot(const FStockRecord* StockRecord);

	/** 把一条账本记录转成对外公开流水；操作者身份留空，服务不持有可复制的公开身份。 */
	static FCatShopPublicTransaction MakePublicTransaction(const FCatShopTransactionRecord& Record);

	/** 判断某个目录项是否属于本局配置的免费自取条目；只认普通饵和保底竿两个显式配置，不按价格为 0 反推。 */
	bool IsFreeClaimEntry(FName EntryId) const;

	/** 构造身份、操作与 RequestId 的幂等键；业务字段进入 PayloadSignature，避免同 RequestId 换条目或回执时静默重放。 */
	static FString MakeTerminalKey(const FString& StableNetId, const TCHAR* Operation, FGuid RequestId);

	/** 购买/免费领取的业务载荷签名；缓存重放前必须完全匹配，不能靠换 EntryId 生成第二笔订单。 */
	static FString MakeCatalogPayloadSignature(const FCatShopPurchaseCommand& Command,
		ECatShopTransactionKind TransactionKind);

	/** 售鱼入账的业务载荷签名；鱼实例、Items 提交证据、估值和钱包前提都必须保持稳定。 */
	static FString MakeFishSalePayloadSignature(const FCatShopFishSaleCommand& Command);

	/** 交付确认的业务载荷签名；同 RequestId 不能替换 Transaction、Receipt 或下游版本。 */
	static FString MakeDeliveryPayloadSignature(const FCatShopDeliveryConfirmationCommand& Command);

	/** 检查终态缓存的业务载荷是否仍是同一意图；缺失签名按漂移处理，避免半升级缓存被误放行。 */
	bool DoesTerminalPayloadMatch(const FString& CacheKey, const FString& PayloadSignature) const;

	/** 同时写入终态和载荷签名；后续重放先比对签名，再决定返回缓存还是拒绝漂移。 */
	void CacheTerminalResult(const FString& CacheKey, const FString& PayloadSignature,
		const FCatShopTransactionResult& Result);

	/** 当前团队钱包事实；所有经济命令只改这一份余额。 */
	FCatShopWalletSnapshot Wallet;

	/** 运行期商店库存，以目录 EntryId 为稳定主键。 */
	TMap<FName, FStockRecord> StockByEntryId;

	/** 本局交易账本；价格/钱包/库存事实不可重算，交付状态只允许 Pending 到 Delivered。 */
	TArray<FCatShopTransactionRecord> TransactionLedger;

	/** RequestId 幂等终态缓存；重放返回首次账本记录但不重复扣款或入账。 */
	TMap<FString, FCatShopTransactionResult> TerminalCache;

	/** 终态缓存对应的业务载荷签名；同 key 载荷漂移会被拒绝，避免旧 RequestId 被挪作另一笔交易。 */
	TMap<FString, FString> TerminalPayloadByKey;

	/** 免费普通饵目录项；服务初始化时从 Settings 冻结。 */
	FName FreeOrdinaryBaitEntryId = NAME_None;

	/** 1 级保底竿的免费自取目录项；服务初始化时从 Settings 冻结。 */
	FName FreeStarterRodEntryId = NAME_None;

	/** 开局冻结的收鱼价体重轴档位表；中途改配置不影响本局已经在跑的报价。 */
	TArray<FCatShopFishWeightPrice> FishPurchasePriceAnchors;

	/** 收鱼价是否已被产品显式裁定；false 时售鱼整体 fail-closed，不退回任何工程默认价。 */
	bool bFishPurchasePriceDecided = false;

	/** 商店当前所处的天序号；AdvanceShopDay 只接受比它更大的天，避免同一天被补两次货。 */
	int32 CurrentShopDayIndex = 0;

	/** 售鱼入账最小金额；服务初始化时从 Settings 冻结。 */
	int32 MinimumFishSaleValue = 1;

	/** Settings 与目录项是否足以支持运行命令；关闭时所有新命令 fail-closed。 */
	bool bRuntimeReady = false;

	/** 目录是否没有重复或非法条目；购买和免费领取需要它为 true。 */
	bool bCatalogReady = false;

	/** Ending 或 World teardown 后关闭新交易；缓存重放仍允许读首次终态。 */
	bool bCommandsOpen = true;
};



