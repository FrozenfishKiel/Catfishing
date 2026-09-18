#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ShopEconomy/Trading/CatShopTradingTypes.h"
#include "CatShopEconomyService.generated.h"

class UCatShopInventoryComponent;
class UCatInventoryComponent;
class UDataTable;

/**
 * 一笔公开经济交易提交完成的服务器本机通知；复制挂载点订阅它来做"每笔购买全队广播"和公款余额刷新。
 * 它只在首次真实提交时发一次，重放和拒绝都不发，因此订阅方不需要自己去重。
 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatShopPublicTransactionCommitted, const FCatShopPublicTransaction&);

/**
 * 商店货架刷新后的服务器本机通知；GameMode 订阅它后重建公开快照。
 * 它不代表一笔交易，所以不携带公开交易记录，避免刷新被 UI 当成收入支出广播。
 */
DECLARE_MULTICAST_DELEGATE(FCatShopInventoryRefreshed);

/** 一局团队商店经济服务；只写团队公款、订单和账本，摊位货架库存由来源组件自己持有。 */
UCLASS()
class CATFISHING_API UCatShopEconomyService : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 汇总读档前与当前已提交购买件数；跨摊位限购和存档共用，不把货架补货当作额度恢复。 */
	TMap<int32, int32> GetRunPurchaseCounts() const;
	/** 世界断点在开放交易前恢复已购件数；拒绝无效编号和负数，不重放历史购买或扣款。 */
	bool RestoreRunPurchaseCountsFromAuthority(const TMap<int32, int32>& Counts);
	/** 世界断点启动时恢复唯一 ASC 公款；已发生交易后不能覆盖。 */
	bool RestoreWalletFromAuthority(int32 Balance);
	bool AreCommandsOpen() const { return bCommandsOpen; }
	bool ExportWalletFromAuthority(int32& OutBalance) const { return TryGetTeamWalletBalance(OutBalance); }
	/** 仅在 authority Game World 创建；客户端 UI 以后只能读复制/查询结果，不持有第二份公款。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** WorldSubsystem 初始化时从显式 Settings 建立本局交易版本、命令 gate 与收购表引用；余额由 GameState ASC 在就绪后初始化。 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** World 退出时关闭新交易并清空本局注册摊位、账本和幂等缓存。 */
	virtual void Deinitialize() override;

	/** 注册一个关卡里的商店货架库存；公开经济快照会把所有已注册摊位的当前库存一起发给客户端。 */
	bool RegisterShopInventory(UCatShopInventoryComponent* ShopInventory);

	/** 注销一个即将离开 World 的商店货架库存；注销后公开快照和购买来源都排除它。 */
	void UnregisterShopInventory(UCatShopInventoryComponent* ShopInventory);

	/** 团队公款快照代表本局唯一余额和版本事实；UI、拒绝结果和交易前提都读它的副本，不能拿到可写引用绕过交易入口。 */
	FCatShopWalletSnapshot GetWalletSnapshot() const;

	/** 查询某个来源摊位上的商店目录项库存快照；不存在或目录不可用时返回 false。 */
	bool TryGetStockSnapshot(const UCatShopInventoryComponent* ShopInventory, FName EntryId,
		FCatShopStockSnapshot& OutSnapshot) const;

	/**
	 * 取回某个目录项在指定摊位当前货架里的配置原文，主要是"这笔订单最后要交给哪个领域、交哪个定义"这两件事。
	 * 商店交易入口用它在下单之前定位交付去向，好把交付侧的前提问在扣钱之前；未上架或目录不可用时返回 false 并清空输出。
	 * 返回 true 不代表这一项现在买得成——价格、库存、当前余额和命令门仍然只由购买写口判定。
	 */
	bool TryGetCatalogEntry(const UCatShopInventoryComponent* ShopInventory, FName EntryId,
		FCatShopCatalogEntry& OutEntry) const;

	/**
	 * 声明：这条购物车支付命令是不是同一 RequestId 的重放，也就是整车购买写口那边已经存过终态了。
	 * 实现：按整车购买写口完全相同的规则拼出幂等键（身份 + CartPurchase + RequestId），只查终态表在不在，不比对载荷、
	 *       不读账本、不碰任何状态。
	 * 边界：它只回答"这个号以前来过没有"，不回答"这一笔当时成没成功"，也不回答"现在还能不能买"。
	 *       商店交易入口用它决定要不要跑交付前置校验——重放的整车订单钱在首次那一趟就已经扣了，再拿"此刻能不能交付"
	 *       去挡它，只会把一次本该返回既有回执的重试变成拒绝，反而制造出"钱扣了、回执拿不到"的假象。
	 */
	bool HasCatalogCartTerminal(const FCatShopCartCommand& Command) const;

	/**
	 * 本局经济账本是公款、库存和订单状态的审计事实；外层展示只能读副本，金额不可回写。
	 * 每条记录都带提交 UTC 时刻和当时的商店天序号，Playtest 回看据此按天切片，不必再从广播顺序倒推。
	 * 它只活在本局：World 退出时随 Deinitialize 一起清空，和公款「跟局走」是同一条口径。
	 */
	TArray<FCatShopTransactionRecord> GetTransactionLedgerSnapshot() const;

	/**
	 * 声明：只读解析整车商品，供购买写口在入库前取得服务器总价、每行物品数量和货架前提；本方法不检查收货仓库。
	 * 实现：合并重复 EntryId，重新读取来源摊位当前目录和库存，再按服务器当前余额、库存数量、价格和溢出边界整体验证，不要求客户端钱包版本匹配。
	 * 边界：它不写幂等缓存、不扣钱、不扣库存；同一购物车真正提交时 PurchaseCatalogCart 会再走同一套判据。
	 */
	bool ResolveCatalogCartForAuthority(const FCatShopCartCommand& Command,
		const UCatShopInventoryComponent* ShopInventory, FCatShopResolvedCart& OutResolved,
		ECatDomainCommandError& OutError) const;

	/** 玩家支付购物车时提交整单指定摊位商品；协调回调先准备交付再调用付款闭包，失败须回滚交付。经济服务返回公款终态、库存快照和逐项账本。 */
	FCatShopCartTransactionResult PurchaseCatalogCart(const FCatShopCartCommand& Command,
		UCatShopInventoryComponent* ShopInventory,
		TFunctionRef<bool(TFunctionRef<bool()>)> CommitDeliveryAndPayment);

	/** 按鱼种收购表和实际千克重量估一条鱼的收入；UI 与服务器预检复用同一纯算式，缺表或缺行返回 false。 */
	bool TryAppraiseFishSale(int32  ItemId, double WeightKilograms, int32& OutSaleValue) const;

	/** 在售鱼协调器占用实物前预检整单；只读余额、命令门、重放缓存与价格表，不使用 ExpectedRevision 裁决库存并发。 */
	bool ValidateFishSale(const FCatShopFishSaleCommand& Command, ECatDomainCommandError& OutError,
		int64& OutCurrentWalletRevision) const;

	/** 在售鱼协调器保护实物期间，通过一次 GAS GE 逐鱼算钱并入账；服务不删除鱼，失败由协调器释放世界鱼或恢复鱼护原格。 */
	FCatShopTransactionResult ApplyFishSale(const FCatShopFishSaleCommand& Command);

	/**
	 * 局级商店天序号是清晨那一拍的共享边界；调用方跨到新一天时调用本函数，让所有已注册摊位库存换一轮货架并补货。
	 * 实现：先要求经济 runtime 和写口可用，且新天序号确实比当前天序号大——同一天重复调用不换第二次货架；
	 *       随后对每个摊位库存组件先按出售表重抽当前货架（商店册 §3.1.2「装备每日刷新」），再让组件只重置
	 *       标了 bDailyRestock 的有限库存（「特殊饵与特殊道具每日限量进货」）。
	 * 边界：开局第一天不换货架——那一轮货架是摊位 BeginPlay 时抽的，换掉等于玩家还没看见就被重抽；
	 *       从第二天起每天清晨换一次。价格仍只来自出售表，本函数不改价。
	 * 返回值契约：只有首次跨到更新的天序号且货架或库存确实变过时才返回 true；同一天重放或写口关闭时返回 false，
	 * 调用方据此保持客户端现有货架展示，不广播一次没有事实变化的刷新。
	 */
	bool AdvanceShopDay(int32 NewDayIndex);

	/**
	 * 声明：按指定摊位自己的出售表显式刷新当前货架库存；调用方决定何时触发，本服务只守经济 gate 和注册关系。
	 * 实现：要求服务器 runtime、命令门、来源摊位库存、注册关系和 RequestId 成立；再转给该摊位库存组件按自身配置重新抽货架。
	 * 结果：成功后只改变当前货架库存、目录可用标记和刷新缓存，不清空公款或交易账本；组件变化订阅会推动公开快照刷新。
	 * 正式节拍走 AdvanceShopDay 的清晨换货架；本入口留给调试、验收脚本和将来需要在一天之内额外换一轮的场合。
	 */
	bool RefreshShopInventoryFromCatalog(UCatShopInventoryComponent* ShopInventory, const FGuid& RequestId,
		const FCatShopRefreshRequest& Request);

	/**
	 * 声明：构造团队经济的对外只读形态，包含公款余额、商店天序号、当前货架库存和本局全部公开交易记录。
	 * 实现：直接从当前公款、库存和账本逐条转换，不做过滤或裁剪；每条交易记录的操作者由传入的解析器现场解析。
	 * 用途：给复制挂载点做全量刷新用；OnPublicTransactionCommitted 只负责通知“变了”，重建仍走这里。
	 * 参数 ResolveActorPlayerState：服务手上只有服务器私有 StableNetId，按项目约定它不能进复制 DTO，
	 *      所以“这笔交易是谁做的”必须由持有身份映射的一方现场解析。不传时全部条目的操作者留空，
	 *      这对服务器内部查询和自动化是正确的，对客户端则表现为未知操作者。
	 */
	FCatShopPublicEconomySnapshot BuildPublicSnapshot(
		const TFunction<APlayerState*(const FString&)>& ResolveActorPlayerState = nullptr) const;

	/** 首次提交一笔经济交易后的本机广播；不携带可写指针，也不携带服务器私有身份。 */
	FCatShopPublicTransactionCommitted OnPublicTransactionCommitted;

	/** 当前货架由 Catalog 刷新成功后的本机广播；订阅方据此重建 ShopEconomySnapshot。 */
	FCatShopInventoryRefreshed OnShopInventoryRefreshed;

	/** 仅冻结营业门；重放仍读终态，不执行毕业兑换。 */
	void CloseCommands();
	/** 毕业专属：按完整商品配置原价折算装备与公款，整除小鱼干售价，余数丢弃；一局只提交一次。 */
	int32 ConvertSettlementLeftoversToDriedFish();
	/** 失败专属：清世界资源和公款，不产出小鱼干。 */
	void ClearFailedRunResourcesFromAuthority();
	bool TryGetOriginalItemPrice(int32  ItemId, int32& OutPrice) const;

#if !UE_BUILD_SHIPPING
	/** 开发期救援入口：只在人工 ForceNextDay 需要从失败结算夜回到白天前重新打开商店写口；它不清公款、账本、货架或幂等缓存，后续日进货仍由 AdvanceShopDay 按正式天数处理。 */
	bool ReopenCommandsForDebugForceNextDay();
#endif

private:
	friend class FCatShopCartAtomicTest;
#if WITH_DEV_AUTOMATION_TESTS
	bool FailPaymentForTest = false;
#endif
	int32 FinalizeRunResourcesFromAuthority(bool bGraduation);
	bool bSettlementResourcesFinalized = false;
	/** 一个摊位库存组件和服务订阅它货架变化时拿到的委托句柄；注销或 World 退出时用它成对解绑。 */
	struct FRegisteredShopInventorySubscription
	{
		/** 被订阅的摊位库存组件；弱引用保证摊位销毁后服务不会延长它的生命周期。 */
		TWeakObjectPtr<UCatShopInventoryComponent> Inventory;

		/** 订阅该摊位库存变化时得到的句柄；只有组件仍有效时才拿它去 Remove。 */
		FDelegateHandle Handle;
	};

	/** 从 Settings 重建事务版本、收购表引用和交易 gate；余额本身由 GameState ASC 在就绪后初始化。 */
	void LoadRuntimeEconomyFromSettings();

	/** 从已绑定 GameState 的 ASC 读取整数团队余额；基础值与当前值不一致或依赖未就绪时返回 false 和零输出，不创建缓存余额。 */
	bool TryGetTeamWalletBalance(int32& OutBalance) const;

	/** 通过一次 GE 提交购买扣款或冻结售鱼行；成功才回传执行器实际金额，缺依赖、未执行或余额不符时返回 false。 */
	bool TryApplyTeamWalletTransaction(int32& InOutDelta, const FGuid& RequestId,
		const FCatShopFishSaleCommand* FishSale = nullptr);

	/** 解析当前默认收购表；软引用尚未加载或资产不存在时返回空，让售鱼按策略缺失拒绝。 */
	UDataTable* GetFishSalePriceTable() const;

	/** 重放购物车终态时刷新当前货架与公款快照；已成交记录直接使用首次缓存，不再改变。 */
	void RefreshCartReplaySnapshots(FCatShopCartTransactionResult& Result) const;

	/**
	 * 为「某摊位在第 N 天清晨换货架」拼一个确定性刷新 RequestId；同一摊位同一天算出来的是同一个号，
	 * 组件侧的刷新幂等集合据此保证一天只抽一轮货架，不靠调用方自己记有没有换过。
	 */
	static FGuid MakeShopDayRefreshRequestId(const FGuid& ShopInventoryId, int32 DayIndex);

	/** 把一条账本记录转成对外公开交易记录；操作者身份留空，服务不持有可复制的公开身份。 */
	static FCatShopPublicTransaction MakePublicTransaction(const FCatShopTransactionRecord& Record);

	/** 按稳定 ShopInventoryId 找回已注册摊位库存；重放和账本查询用它避免全局 EntryId 串货。 */
	UCatShopInventoryComponent* FindRegisteredShopInventoryById(FGuid ShopInventoryId) const;

	/** 商店库存组件变化入口；任一注册摊位刷新或扣库存后，服务发布一次公开货架快照刷新通知。 */
	void HandleRegisteredShopInventoryChanged();

	/** 构造身份、操作与 RequestId 的幂等键；业务字段进入 PayloadSignature，避免同 RequestId 换条目或回执时静默重放。 */
	static FString MakeTerminalKey(const FString& StableNetId, const TCHAR* Operation, FGuid RequestId);

	/** 购物车支付的业务载荷签名；缓存重放前必须完全匹配，不能靠换商品、数量或来源摊位生成第二笔订单。 */
	static FString MakeCartPayloadSignature(const FCatShopCartCommand& Command);

	/** 售鱼入账的业务载荷签名；库存提交证据与每条鱼的身份、种类和重量必须保持稳定。 */
	static FString MakeFishSalePayloadSignature(const FCatShopFishSaleCommand& Command);

	/** 检查终态缓存的业务载荷是否仍是同一意图；缺失签名按漂移处理，避免半升级缓存被误放行。 */
	bool DoesTerminalPayloadMatch(const FString& CacheKey, const FString& PayloadSignature) const;

	/** 同时写入终态和载荷签名；后续重放先比对签名，再决定返回缓存还是拒绝漂移。 */
	void CacheTerminalResult(const FString& CacheKey, const FString& PayloadSignature,
		const FCatShopTransactionResult& Result);

	/** 同时写入整车终态和载荷签名；购物车重放走独立结果表，但和其他命令共享漂移防护。 */
	void CacheCartTerminalResult(const FString& CacheKey, const FString& PayloadSignature,
		const FCatShopCartTransactionResult& Result);

	/** 团队余额的事务版本；本服务初始化并在成交改变余额后递增，快照、命令回执和账本读取它记录余额版本，购买裁决与重放签名不依赖客户端版本。 */
	int64 WalletRevision = 0;

	/** 本局已完成交易的审计记录；购买仅在实物入库与扣款成功后写入，服务查询和公开流水读取它。 */
	TArray<FCatShopTransactionRecord> TransactionLedger;
	/** 读档时恢复的历史购买汇总；只代表加载前的成交，加载后的购买仍由 TransactionLedger 唯一记录。 */
	TMap<int32, int32> RestoredPurchaseCounts;

	/** RequestId 幂等终态缓存；重放返回首次账本记录但不重复扣款或入账。 */
	TMap<FString, FCatShopTransactionResult> TerminalCache;

	/** 购物车 RequestId 幂等终态缓存；一车可包含多条账本记录，所以不能塞进单交易结果表。 */
	TMap<FString, FCatShopCartTransactionResult> CartTerminalCache;

	/** 终态缓存对应的业务载荷签名；同 key 载荷漂移会被拒绝，避免失效 RequestId 被挪作另一笔交易。 */
	TMap<FString, FString> TerminalPayloadByKey;

	/** 本局使用的本地收购表引用；行按鱼种 ID 定义每千克金币系数，服务端与 UI 估价都读取它。 */
	TSoftObjectPtr<UDataTable> FishSalePriceTable;

	/** 当前商店经济快照展示的局级天序号；实际补货由每个摊位库存组件按这个值各自推进。 */
	int32 CurrentShopDayIndex = 0;

	/** Settings 是否足以支持本局团队经济；关闭时购物车支付和售鱼这些新命令 fail-closed。 */
	bool bRuntimeReady = false;

	/** Ending 或 World teardown 后关闭新交易；缓存重放仍允许读首次终态。 */
	bool bCommandsOpen = true;

	/**
	 * World 正在拆除；CloseCommands 里那些会写世界状态的收尾（把剩余公款换成小鱼干）在这种情况下一律不做。
	 * 它区分的是两种「关门」：结算夜收摊是玩法事件，要兑换；World teardown 只是释放资源，不该再往营地发货。
	 */
	bool bTearingDown = false;

	/** 当前是否处于经济提交的同步调用栈；购买与售鱼用作用域守卫写入，预检和嵌套写口读取，防止回调在终态缓存落定前重复交易。 */
	bool bTransactionInProgress = false;

	/** 当前 World 中已注册的商店摊位库存；购买必须显式带来源组件，公开快照会聚合这里的库存。 */
	TArray<TWeakObjectPtr<UCatShopInventoryComponent>> RegisteredShopInventories;

	/** 每个已注册摊位库存的变化订阅；注销时按组件对象解除，避免服务持有销毁后的委托。 */
	TArray<FRegisteredShopInventorySubscription> RegisteredInventoryChangedHandles;
};
