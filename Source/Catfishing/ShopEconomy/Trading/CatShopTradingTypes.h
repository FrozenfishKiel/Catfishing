#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "GameFramework/PlayerState.h"
#include "ShopEconomy/Catalog/CatShopCatalogTypes.h"
#include "CatShopTradingTypes.generated.h"

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

	/** 这条记录是否来自购物车购买；交付确认只接受购买记录，由交易来源判断业务分支。 */
	UPROPERTY(BlueprintReadOnly)
	bool bPurchase = false;

	/** 这条记录是否来自售鱼入账；它用于公开流水展示，不授权任何库存或 Social 后续操作。 */
	UPROPERTY(BlueprintReadOnly)
	bool bFishSale = false;

	/** 这条购买记录是否仍等待下游库存回执；售鱼记录和已确认购买都保持 false。 */
	UPROPERTY(BlueprintReadOnly)
	bool bDeliveryPending = false;

	/** 下游领域是否已经确认交付完成；重复确认只读取这条事实，支付和发货只发生一次。 */
	UPROPERTY(BlueprintReadOnly)
	bool bDeliveryConfirmed = false;

	/** 下游领域成功交付时返回的回执 ID；未交付或售鱼记录保持无效。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid DeliveryReceiptId;

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

	/** 售鱼批次中首条鱼的身份摘要；服务写入供流水定位，不证明实物已移除，购买时保持无效。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid FishInstanceId;

	/** 对团队公款的真实增量；购物车购买为负或 0，售鱼为非负整数，逐鱼舍入可产生零收入。 */
	UPROPERTY(BlueprintReadOnly)
	int32 WalletDelta = 0;

	/** 交易提交后的公款版本；0 元购物车不改余额时保留当前版本。 */
	UPROPERTY(BlueprintReadOnly)
	int64 WalletRevision = 0;

	/** 交易提交后的摊位货架版本；售鱼不涉及货架扣减时为 0。 */
	UPROPERTY(BlueprintReadOnly)
	int64 StockRevision = 0;
};

/**
 * 一条对全队公开的经济交易记录。它只留下“谁、做了什么、钱怎么动了”的展示事实，
 * 不带服务器私有身份键，也不暴露商店内部或库存写口决策。
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

	/** 这条公开流水是否来自购物车购买；表现层用它选择文案，不回写订单状态。 */
	UPROPERTY(BlueprintReadOnly)
	bool bPurchase = false;

	/** 这条公开流水是否来自售鱼入账；表现层用它选择文案，不回写库存状态。 */
	UPROPERTY(BlueprintReadOnly)
	bool bFishSale = false;

	/** 购买流水是否仍在等待下游回执；售鱼流水保持 false。 */
	UPROPERTY(BlueprintReadOnly)
	bool bDeliveryPending = false;

	/** 购买流水是否已经收到下游回执；售鱼流水保持 false。 */
	UPROPERTY(BlueprintReadOnly)
	bool bDeliveryConfirmed = false;

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

	/** 售鱼批次首条鱼的公开身份摘要；购买保持无效，客户端不能用它补删库存或当作整批鱼清单。 */
	UPROPERTY(BlueprintReadOnly)
	FGuid FishInstanceId;

	/** 公款余额的真实增量；购物车购买为负或 0，售鱼可为正或零，客户端只展示已提交的结果。 */
	UPROPERTY(BlueprintReadOnly)
	int32 WalletDelta = 0;
};

/**
 * 团队经济对外的完整只读形态。客户端拿到它就能同时渲染余额、库存和交易反馈，
 * 不需要再发第二次查询，也不能通过它推进任何购买或售鱼写口。
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

/** 服务器对购物车的只读报价结果；交易控制器用它在扣钱前先询问营地公共仓库能否整批接收。 */
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

/** 一条待收购的服务器确认鱼事实；价格只由 FishDefinitionId 和实际千克重量计算，不接受客户端金额。 */
USTRUCT(BlueprintType)
struct FCatShopFishSaleLine
{
	GENERATED_BODY()

	/** 正式鱼实例 ID；交易账本可保留首条摘要，库存提交和重放以整组实例事实为准。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid FishInstanceId;

	/** 正式鱼种 ID；服务以它查本地收购 DataTable 的金币系数。 */
	UPROPERTY(BlueprintReadWrite)
	FName FishDefinitionId = NAME_None;

	/** 服务器冻结的实际重量，单位千克；逐条乘系数并四舍五入后才汇总整单收入。 */
	UPROPERTY(BlueprintReadWrite)
	double WeightKilograms = 0.0;
};

/** 售鱼协调器保护实物后提交的整批入账命令；经济成功才完成实物消费，失败仍可恢复原来源。 */
USTRUCT(BlueprintType)
struct FCatShopFishSaleCommand
{
	GENERATED_BODY()

	/** RequestId 与服务器身份；ExpectedRevision 不参与售鱼库存或乐观并发校验。 */
	UPROPERTY(BlueprintReadWrite)
	FCatDomainCommandContext Context;

	/** 实物与经济提交的关联号；协调器在占用前写入并纳入重放签名，不代表库存已经不可逆扣除。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid InventoryCommitId;

	/** 本次协调器冻结的实物鱼行；服务完整重算一笔收入，任一行非法即整单拒绝，不自行访问鱼护或世界鱼。 */
	UPROPERTY(BlueprintReadWrite)
	TArray<FCatShopFishSaleLine> Fish;
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
