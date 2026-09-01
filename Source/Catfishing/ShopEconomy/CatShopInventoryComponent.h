#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShopEconomy/CatShopEconomyTypes.h"
#include "CatShopInventoryComponent.generated.h"

class UDataTable;

/** 商店货架库存变化通知；经济复制层订阅它后重建公开快照，UI 不直接改库存。 */
DECLARE_MULTICAST_DELEGATE(FCatShopInventoryComponentChanged);

/**
 * 挂在商店摊位上的库存组件，代表“这个摊位当前卖什么”和“每个货架项还剩多少”。
 * 它从策划 DataTable 构建固定货架和随机候选；团队公款、交易账本和公共仓库发货仍由 ShopEconomy 服务与订单协调器处理。
 */
UCLASS(ClassGroup = (Catfishing), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class CATFISHING_API UCatShopInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 构造商店货架组件；组件只建立复制和刷新规则默认值，具体商品必须由策划 DataTable 提供。 */
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

	/** authority 整车订单提交时一次性扣减多条货架库存；任一有限库存不足时整批保持原状。 */
	bool ConsumeCatalogEntriesFromAuthority(const TArray<FCatShopCartLineCommand>& Lines,
		TArray<FCatShopStockSnapshot>& OutSnapshots);

	/** 收集本摊位所有可展示商品候选；UI 用公开货架快照过滤出本轮随机抽中的条目。 */
	void CollectDisplayCatalogEntries(TArray<FCatShopCatalogEntry>& OutEntries) const;

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

	/** 按本组件的正式 DataTable 生成一次运行货架目录；失败时说明具体配置问题。 */
	bool BuildRuntimeCatalogEntries(FRandomStream& RandomStream, TArray<FCatShopCatalogEntry>& OutEntries,
		FString& OutError) const;

	/** 从策划 DataTable 构建一次运行货架；固定行直接进入货架，随机行按权重不放回抽取。 */
	bool BuildCatalogEntriesFromTable(const UDataTable& CatalogTable, FRandomStream& RandomStream,
		TArray<FCatShopCatalogEntry>& OutEntries, FString& OutError) const;

	/** 解析本摊位实际使用的出售表；实例配置优先，未配置时读取项目默认表入口。 */
	TSoftObjectPtr<UDataTable> ResolveShopCatalogTable() const;

	/** 从策划 DataTable 收集所有可能展示的启用商品候选；当前未抽中的随机项会在 UI 侧被公开库存过滤掉。 */
	void CollectDisplayCatalogEntriesFromTable(const UDataTable& CatalogTable,
		TArray<FCatShopCatalogEntry>& OutEntries) const;

	/** 把一组已校验目录项写成当前货架库存；重复 EntryId 或非法项会让整轮重建失败并清空临时结果。 */
	bool RebuildStockFromCatalogEntries(const TArray<FCatShopCatalogEntry>& CatalogEntries, FString& OutError);

	/** 把一条内部库存记录复制成公开库存快照；空记录返回默认快照，拒绝结果可安全携带。 */
	FCatShopStockSnapshot MakeStockSnapshot(const FStockRecord* StockRecord) const;

	/** 本摊位库存的稳定身份；服务器生成后复制给客户端，用来把公开货架行和具体摊位对齐。 */
	UPROPERTY(ReplicatedUsing = OnRep_ShopInventoryId, VisibleInstanceOnly, BlueprintReadOnly, Category = "Catfishing|Shop",
		meta = (AllowPrivateAccess = "true"))
	FGuid ShopInventoryId;

	/** 正式商店出售表资产；策划在这里维护商品、分类、价格、固定上架和随机候选，未配置时商店 fail-closed。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|Shop|Catalog",
		meta = (AllowPrivateAccess = "true"))
	TSoftObjectPtr<UDataTable> ShopCatalogTable;

	/** 本摊位的刷新规则；只描述每次从随机候选里抽几条，不决定刷新触发时机。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Catfishing|Shop|Catalog",
		meta = (AllowPrivateAccess = "true"))
	FCatShopCatalogRefreshRule RefreshRule;

	/** 当前 authority 货架库存；客户端不直接复制这份 Map，只从 GameState 读取公开快照。 */
	TMap<FName, FStockRecord> StockByEntryId;

	/** 已成功执行过的刷新 RequestId；同一刷新命令重放不会再次随机抽取。 */
	TSet<FGuid> RefreshTerminalRequests;

	/** 本摊位当前处在第几天；每日进货只接受更大的天序号，避免同一天重复补货。 */
	int32 CurrentShopDayIndex = 0;

	/** 本摊位目录是否足够支撑运行；为 false 时展示和购买都应按未就绪处理。 */
	bool bCatalogReady = false;
};
