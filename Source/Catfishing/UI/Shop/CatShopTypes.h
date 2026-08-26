#pragma once

#include "CoreMinimal.h"
#include "ShopEconomy/CatShopEconomyTypes.h"
#include "CatShopTypes.generated.h"

/** 商店 UI 发给控制器的意图类型；Widget 只描述点击，不携带价格、库存或交付结果。 */
UENUM(BlueprintType)
enum class ECatShopUIAction : uint8
{
	/** 没有有效动作；用于默认空值和拒绝旧点击。 */
	None,
	/** 购买一条服务器目录项；价格和库存仍由 ShopEconomy 裁决。 */
	PurchaseEntry,
	/** 领取一条服务器明确配置为免费自取的目录项。 */
	ClaimFreeEntry
};

/** 商店目录中一行商品的展示投影；它由配置目录和公开经济快照拼出，不能写回商店后端。 */
USTRUCT(BlueprintType)
struct FCatShopEntryView
{
	GENERATED_BODY()

	/** 商品目录稳定 ID；UI 点击只回传它，不回传价格或库存。 */
	UPROPERTY(BlueprintReadOnly)
	FName EntryId = NAME_None;

	/** 商品最终交付类别；只用于展示“装备/耗材”，真正交付仍在服务器订单协调器里发生。 */
	UPROPERTY(BlueprintReadOnly)
	ECatShopEntryKind Kind = ECatShopEntryKind::Unknown;

	/** 商品指向的装备或消耗品定义；UI 用它显示名字，不能据此直接发放物品。 */
	UPROPERTY(BlueprintReadOnly)
	FName DefinitionId = NAME_None;

	/** 配置里的单价；Widget 只展示该数值，实际扣款仍以服务器冻结目录为准。 */
	UPROPERTY(BlueprintReadOnly)
	int32 UnitPrice = 0;

	/** 配置冻结时的本轮起始库存；UI 用它显示“剩余/初始”的分母，服务器库存仍是最终裁决。 */
	UPROPERTY(BlueprintReadOnly)
	int32 InitialStock = 0;

	/** GameState 公开货架里复制出的剩余库存；无限库存时保持 0，只看 bUnlimitedStock。 */
	UPROPERTY(BlueprintReadOnly)
	int32 RemainingStock = 0;

	/** 当前公开经济快照是否包含这条商品的库存事实；缺失时 UI 禁用点击，避免按配置猜库存。 */
	UPROPERTY(BlueprintReadOnly)
	bool bStockAvailable = false;

	/** 商品是否无限库存；免费饵和保底竿通常依赖它保证永远能领。 */
	UPROPERTY(BlueprintReadOnly)
	bool bUnlimitedStock = false;

	/** 当前公开经济快照下该商品是否已售罄；服务器仍是最终裁决，UI 只用它禁用明显无效点击。 */
	UPROPERTY(BlueprintReadOnly)
	bool bSoldOut = false;

	/** 当前团队公款是否足够支付该条目；免费领取始终为 true，付费项随公款变化刷新。 */
	UPROPERTY(BlueprintReadOnly)
	bool bAffordable = true;

	/** 该条目是否应该走免费领取 RPC；它来自 ShopEconomy Settings 的免费条目白名单。 */
	UPROPERTY(BlueprintReadOnly)
	bool bFreeClaim = false;

	/** 当前 UI 是否允许发起该条目的点击；缺少公款快照或已有 pending 时保持 false。 */
	UPROPERTY(BlueprintReadOnly)
	bool bActionEnabled = false;

	/** 商品行当前可展示的中文摘要；Model 写入价格、库存和余额提示，WBP 只负责显示。 */
	UPROPERTY(BlueprintReadOnly)
	FText DisplayText;

	/** 商品行按钮当前应显示的动作名；Model 按免费领取白名单写入“领取”或“购买”。 */
	UPROPERTY(BlueprintReadOnly)
	FText ActionText;
};

/** 商店界面的完整展示投影；它只包含商品、公款和最近提交提示，不包含背包或 HUD 状态。 */
USTRUCT(BlueprintType)
struct FCatShopViewState
{
	GENERATED_BODY()

	/** 商店窗口是否由交互对象打开；Model 只把这个状态投给 View，不拥有打开来源。 */
	UPROPERTY(BlueprintReadOnly)
	bool bOpen = false;

	/** 当前是否有 GameState 复制过来的公开经济快照。 */
	UPROPERTY(BlueprintReadOnly)
	bool bEconomyAvailable = false;

	/** 团队公款和公开经济记录快照；客户端只读它，不能通过 UI 修改公款。 */
	UPROPERTY(BlueprintReadOnly)
	FCatShopPublicEconomySnapshot Economy;

	/** 当前可展示商品目录；条目来自配置冻结后的展示副本，不包含服务器写口。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatShopEntryView> Entries;

	/** 最近一次 UI 提交的动作；用于展示“已提交/被本地拒绝”的结果提示。 */
	UPROPERTY(BlueprintReadOnly)
	ECatShopUIAction LastAction = ECatShopUIAction::None;

	/** 最近一次提交或拒绝的条目 ID；用于把结果提示和商品行关联起来。 */
	UPROPERTY(BlueprintReadOnly)
	FName LastEntryId = NAME_None;

	/** 当前是否已有购买或领取请求提交后等待商店快照同步。 */
	UPROPERTY(BlueprintReadOnly)
	bool bActionPending = false;

	/** 给 WBP 顶部文本绑定的团队公款摘要。 */
	UPROPERTY(BlueprintReadOnly)
	FText WalletText;

	/** 给 WBP 结果区域绑定的购买/领取反馈。 */
	UPROPERTY(BlueprintReadOnly)
	FText ResultText;
};
