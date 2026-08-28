#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShopEconomy/CatShopCatalogDefinition.h"
#include "ShopEconomy/CatShopEconomyTypes.h"
#include "CatShopInventoryComponent.generated.h"

/** 商店货架库存变化通知；经济复制层订阅它后重建公开快照，UI 不直接改库存。 */
DECLARE_MULTICAST_DELEGATE(FCatShopInventoryComponentChanged);

/**
 * 挂在商店摊位上的库存组件，代表“这个摊位当前卖什么”和“每个货架项还剩多少”。
 * 它持有固定商品、随机池和刷新规则；团队公款、交易账本和公共仓库发货仍由 ShopEconomy 服务与订单协调器处理。
 */
UCLASS(ClassGroup = (Catfishing), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class CATFISHING_API UCatShopInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 构造默认商店货架表；默认表满足初级竿、基础饵、初级漂必出，其余商品进入随机池。 */
	UCatShopInventoryComponent();

	/** 复制商店库存组件的稳定身份；货架数量仍通过 GameState 的公开经济快照同步。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 进入 World 时在 authority 上生成本摊位库存并注册给 ShopEconomy 服务；客户端只等待稳定 ID 和公开快照。 */
	virtual void BeginPlay() override;

	/** 离开 World 时从 ShopEconomy 服务注销本摊位库存，避免旧摊位继续出现在公开货架快照中。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 返回本摊位库存的稳定 ID；购买命令和公开库存快照用它保证 EntryId 只在同一个摊位内解释。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Shop")
	FGuid GetShopInventoryId() const;

	/** 查询本摊位目录是否已经在 authority 上成功生成；未就绪时购买会 fail-closed。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Shop")
	bool IsRuntimeCatalogReady() const;

	/** 按本组件持有的出售表重新生成开局货架；成功后保留新库存，失败时清空并记录目录不可用。 */
	bool RebuildInitialInventoryFromCatalog();

	/** 按本组件持有的出售表刷新当前货架；调用方决定刷新时机，本函数只处理同一 RequestId 的幂等和随机抽取。 */
	bool RefreshShopInventoryFromCatalog(const FGuid& RequestId, const FCatShopRefreshRequest& Request);

	/** 把本摊位所有当前货架库存追加到公开快照；调用方负责决定是否清空外层数组。 */
	void AppendStockSnapshots(TArray<FCatShopStockSnapshot>& OutStocks) const;

	/** 查询本摊位当前某个 EntryId 的库存；EntryId 只在本组件的 ShopInventoryId 范围内有意义。 */
	bool TryGetStockSnapshot(FName EntryId, FCatShopStockSnapshot& OutSnapshot) const;

	/** 查询本摊位当前某个 EntryId 对应的目录原文；订单协调器用它在扣款前先问公共仓库能否接收。 */
	bool TryGetCatalogEntry(FName EntryId, FCatShopCatalogEntry& OutEntry) const;

	/** authority 订单提交时扣减一条货架库存；无限库存保持版本不变，有限库存成功扣减后推进库存版本。 */
	bool ConsumeCatalogEntryFromAuthority(FName EntryId, FCatShopStockSnapshot& OutSnapshot);

	/** 收集本摊位所有可展示商品候选；UI 用公开货架快照过滤出本轮随机抽中的条目。 */
	void CollectDisplayCatalogEntries(TArray<FCatShopCatalogEntry>& OutEntries) const;

	/** 判断某个 EntryId 是否是本摊位允许免费领取的保底项；只认组件上显式配置的三条白名单。 */
	bool IsFreeClaimEntry(FName EntryId) const;

	/** 推进本摊位货架天序号，并只重置目录项上标记为每日进货的有限库存。 */
	bool AdvanceShopDay(int32 NewDayIndex);

	/** 本组件货架变化通知；经济服务收到后发布新的全队公开货架快照。 */
	FCatShopInventoryComponentChanged OnInventoryChanged;

	/** 本组件稳定身份复制完成后的通知；本地商店页面用它把已打开的货架行重新对齐公开快照。 */
	FCatShopInventoryComponentChanged OnInventoryIdentityChanged;

private:
	/** 客户端收到摊位库存稳定身份后广播本地刷新通知；不修改任何货架库存。 */
	UFUNCTION()
	void OnRep_ShopInventoryId();

	/** 本摊位的一条运行期库存记录；目录项不变，剩余库存和版本只由刷新、每日进货和购买提交修改。 */
	struct FStockRecord
	{
		/** 本货架项的目录原文；价格、发放定义、购买数量和展示覆盖都从这里读取。 */
		FCatShopCatalogEntry Entry;

		/** 本货架项当前剩余订单次数；无限库存条目不会因为购买修改它。 */
		int32 RemainingStock = 0;

		/** 本货架项的库存版本；有限库存扣减或每日补货时递增，方便 UI 识别快照变化。 */
		int64 Revision = 0;
	};

	/** 一条必须出现在固定货架中的保底免费项；它来自本组件的免费白名单配置。 */
	struct FRequiredStarterShopEntry
	{
		/** 需要稳定存在于固定货架的目录 ID；随机池抽中不能替代“每次都有”。 */
		FName EntryId = NAME_None;

		/** 对应配置字段的人类可读名称；报错时用它指出缺的是哪条白名单。 */
		const TCHAR* ConfigName = TEXT("");
	};

	/** 根据本组件的三条免费白名单组装保底检查列表；任一缺失或重复都会关闭本摊位目录。 */
	bool CollectRequiredStarterShopEntries(TArray<FRequiredStarterShopEntry>& OutRequiredEntries,
		FString& OutError) const;

	/** 校验三条保底项确实来自固定表，且每条都是启用、免费、无限库存的可运行商品。 */
	bool ValidateRequiredFixedSaleEntries(const TArray<FCatShopSaleEntry>& Entries, FString& OutError) const;

	/** 按本组件的正式资产或内联固定/随机数组生成一次运行货架目录；失败时说明具体配置问题。 */
	bool BuildRuntimeCatalogEntries(FRandomStream& RandomStream, TArray<FCatShopCatalogEntry>& OutEntries,
		FString& OutError) const;

	/** 把一组已校验目录项写成当前货架库存；重复 EntryId 或非法项会让整轮重建失败并清空临时结果。 */
	bool RebuildStockFromCatalogEntries(const TArray<FCatShopCatalogEntry>& CatalogEntries, FString& OutError);

	/** 把一条内部库存记录复制成公开库存快照；空记录返回默认快照，拒绝结果可安全携带。 */
	FCatShopStockSnapshot MakeStockSnapshot(const FStockRecord* StockRecord) const;

	/** 本摊位库存的稳定身份；服务器生成后复制给客户端，用来把公开货架行和具体摊位对齐。 */
	UPROPERTY(ReplicatedUsing = OnRep_ShopInventoryId, VisibleInstanceOnly, BlueprintReadOnly, Category = "Catfishing|Shop",
		meta = (AllowPrivateAccess = "true"))
	FGuid ShopInventoryId;

	/** 正式商店出售表资产；配置后它是本摊位固定货架、随机池和刷新规则的首选数据源。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|Shop|Catalog",
		meta = (AllowPrivateAccess = "true"))
	TSoftObjectPtr<UCatShopCatalogDefinition> ShopCatalog;

	/** 本摊位每次刷新都会保留的固定商品；初级竿、基础饵和初级漂默认在这里。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|Shop|Catalog",
		meta = (AllowPrivateAccess = "true"))
	TArray<FCatShopSaleEntry> FixedSaleEntries;

	/** 本摊位随机刷新候选池；刷新时按权重抽出若干条进入当前货架库存。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|Shop|Catalog",
		meta = (AllowPrivateAccess = "true"))
	TArray<FCatShopRandomSaleEntry> RandomSaleEntries;

	/** 本摊位的刷新规则；只描述抽几条和是否保留固定项，不决定何时刷新。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|Shop|Catalog",
		meta = (AllowPrivateAccess = "true"))
	FCatShopRefreshRule RefreshRule;

	/** 基础鱼饵免费领取条目；它必须指向固定表里免费且无限库存的商品。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|Shop|Catalog",
		meta = (AllowPrivateAccess = "true"))
	FName FreeOrdinaryBaitEntryId = NAME_None;

	/** 初级鱼竿免费领取条目；它必须始终在固定表中，保证玩家没钱时仍能开钓。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|Shop|Catalog",
		meta = (AllowPrivateAccess = "true"))
	FName FreeStarterRodEntryId = NAME_None;

	/** 初级鱼漂免费领取条目；它和初级鱼竿、基础鱼饵一起构成开钓保底。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|Shop|Catalog",
		meta = (AllowPrivateAccess = "true"))
	FName FreeStarterFloatEntryId = NAME_None;

	/** 当前 authority 货架库存；客户端不直接复制这份 Map，只从 GameState 读取公开快照。 */
	TMap<FName, FStockRecord> StockByEntryId;

	/** 已成功执行过的刷新 RequestId；同一刷新命令重放不会再次随机抽取。 */
	TSet<FGuid> RefreshTerminalRequests;

	/** 本摊位当前处在第几天；每日进货只接受更大的天序号，避免同一天重复补货。 */
	int32 CurrentShopDayIndex = 0;

	/** 本摊位目录是否足够支撑运行；为 false 时展示和购买都应按未就绪处理。 */
	bool bCatalogReady = false;
};
