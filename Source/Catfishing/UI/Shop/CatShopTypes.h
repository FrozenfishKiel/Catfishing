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
	/** 点击商品卡片后把该目录项加入本地购物车；不会立刻扣钱或发货。 */
	AddEntryToCart,
	/** 点击购物车垃圾桶后移除该目录项的一次选购；数量大于 1 时只减少一份。 */
	RemoveCartEntry,
	/** 点击支付按钮后把当前购物车一次提交给服务器结算。 */
	PayCart
};

/** 商店目录中一行商品的展示投影；它由摊位目录和公开经济快照拼出，不能写回商店后端。 */
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

	/** 商品所属展示分类；WBP 本地分类按钮只用它过滤 DisplayedEntries，不修改 Model 的真实商品数组。 */
	UPROPERTY(BlueprintReadOnly)
	FName DisplayCategoryId = NAME_None;

	/** 商品所属展示分类的显示名；为空时 WBP 可以回退显示 DisplayCategoryId。 */
	UPROPERTY(BlueprintReadOnly)
	FText DisplayCategoryNameText;

	/** 单次选购会发到营地公共仓库的数量；UI 只展示，不能把它作为客户端提交参数。 */
	UPROPERTY(BlueprintReadOnly)
	int32 PurchaseQuantity = 1;

	/** 配置里的单价；Widget 只展示该数值，实际扣款仍以服务器冻结目录为准。 */
	UPROPERTY(BlueprintReadOnly)
	int32 UnitPrice = 0;

	/** 配置冻结时的本轮起始库存；UI 用它显示“剩余/初始”的分母，服务器库存仍是最终裁决。 */
	UPROPERTY(BlueprintReadOnly)
	int32 InitialStock = 0;

	/** GameState 公开货架里复制出的剩余库存；无限库存展示不依赖该数值，只看 bUnlimitedStock。 */
	UPROPERTY(BlueprintReadOnly)
	int32 RemainingStock = 0;

	/** 当前公开经济快照是否包含这条商品的库存事实；缺失时 UI 禁用点击，避免按配置猜库存。 */
	UPROPERTY(BlueprintReadOnly)
	bool bStockAvailable = false;

	/** 商品是否无限库存；UI 用它判断是否需要展示剩余量和限制本地加购次数。 */
	UPROPERTY(BlueprintReadOnly)
	bool bUnlimitedStock = false;

	/** 当前公开经济快照下该商品是否已售罄；服务器仍是最终裁决，UI 只用它禁用明显无效点击。 */
	UPROPERTY(BlueprintReadOnly)
	bool bSoldOut = false;

	/** 当前团队公款是否足够支付单个条目；它只作为展示提示，购物车最终按总价裁决。 */
	UPROPERTY(BlueprintReadOnly)
	bool bAffordable = true;

	/** 当前 UI 是否允许发起该条目的点击；缺少公款快照或已有 pending 时保持 false。 */
	UPROPERTY(BlueprintReadOnly)
	bool bActionEnabled = false;

	/** 当前购物车里这个商品已选购几次；商品按钮可用它显示叠加数量，不作为服务器提交的唯一事实。 */
	UPROPERTY(BlueprintReadOnly)
	int32 CartCount = 0;

	/** 商品行当前可展示的中文摘要；Model 写入价格、库存和余额提示，WBP 只负责显示。 */
	UPROPERTY(BlueprintReadOnly)
	FText DisplayText;

	/** 商品行按钮当前应显示的动作名；当前正式语义是加入本地购物车。 */
	UPROPERTY(BlueprintReadOnly)
	FText ActionText;

	/** 商品行当前显示名；优先来自商店表展示覆盖，未配置时回退到 DefinitionId 或 EntryId。 */
	UPROPERTY(BlueprintReadOnly)
	FText DisplayNameText;

	/** 商品行当前说明；只作为 WBP 详情展示，不参与购买裁决。 */
	UPROPERTY(BlueprintReadOnly)
	FText DescriptionText;

	/** 商品行图标覆盖；WBP 可以直接加载它，也可以在为空时回退到装备定义图标。 */
	UPROPERTY(BlueprintReadOnly)
	TSoftObjectPtr<UTexture2D> IconOverride;
};

/** 商店顶部分类按钮的一行展示投影；它从真实商品数组归纳出来，玩家当前选择只保存在本地 Widget。 */
USTRUCT(BlueprintType)
struct FCatShopCategoryView
{
	GENERATED_BODY()

	/** 分类稳定 ID；NAME_None 表示“全部”，其他值必须来自商品表的 DisplayCategoryId。 */
	UPROPERTY(BlueprintReadOnly)
	FName CategoryId = NAME_None;

	/** 分类按钮显示名；“全部”由程序兜底，其他分类优先来自 DataTable 的分类显示名覆盖。 */
	UPROPERTY(BlueprintReadOnly)
	FText DisplayNameText;

	/** 当前分类下可展示的商品数量；WBP 可用它隐藏空分类或显示数量角标。 */
	UPROPERTY(BlueprintReadOnly)
	int32 EntryCount = 0;

	/** 这个分类是否正被本地玩家选中；Model 默认 false，Widget 会在本地刷新时写入它。 */
	UPROPERTY(BlueprintReadOnly)
	bool bSelected = false;
};

/** 购物车中一行商品的展示投影；它来自本地购物车和当前真实商品数组的交叉结果，不会同步给其他玩家。 */
USTRUCT(BlueprintType)
struct FCatShopCartLineView
{
	GENERATED_BODY()

	/** 购物车行对应的商店目录 ID；垃圾桶按钮只回传它，Model 再删除一份本地选购。 */
	UPROPERTY(BlueprintReadOnly)
	FName EntryId = NAME_None;

	/** 购物车行指向的装备或耗材定义；WBP 可用它回退展示图标或名字。 */
	UPROPERTY(BlueprintReadOnly)
	FName DefinitionId = NAME_None;

	/** 商品所属展示分类；购物车本身不按分类过滤，但可用它做视觉分组或调试显示。 */
	UPROPERTY(BlueprintReadOnly)
	FName DisplayCategoryId = NAME_None;

	/** 该商品在购物车里被选购了几次；垃圾桶每点一次只减少这个计数一份。 */
	UPROPERTY(BlueprintReadOnly)
	int32 CartCount = 0;

	/** 单次选购会发放的数量；交付总数等于它乘以 CartCount。 */
	UPROPERTY(BlueprintReadOnly)
	int32 PurchaseQuantity = 1;

	/** 支付成功后本行会进入营地公共仓库的总数量。 */
	UPROPERTY(BlueprintReadOnly)
	int32 DeliveryQuantity = 0;

	/** 商品单次选购价格；实际扣款仍由服务器当前目录重新计算。 */
	UPROPERTY(BlueprintReadOnly)
	int32 UnitPrice = 0;

	/** 本行小计；等于 UnitPrice 乘以 CartCount。 */
	UPROPERTY(BlueprintReadOnly)
	int32 LineTotalPrice = 0;

	/** 当前真实货架下这行是否仍可结算；任一行不可结算时支付按钮整体禁用。 */
	UPROPERTY(BlueprintReadOnly)
	bool bLineAvailable = false;

	/** 商品行当前显示名；优先沿用商品投影的展示名。 */
	UPROPERTY(BlueprintReadOnly)
	FText DisplayNameText;

	/** 购物车行当前可展示的中文摘要；简单 WBP 可直接绑定这段文本。 */
	UPROPERTY(BlueprintReadOnly)
	FText DisplayText;

	/** 购物车行图标覆盖；WBP 可用它显示右侧已选购列表的小图。 */
	UPROPERTY(BlueprintReadOnly)
	TSoftObjectPtr<UTexture2D> IconOverride;
};

/** 商店界面的完整展示投影；它只包含商品、公款、购物车和最近提交提示，不包含背包或 HUD 状态。 */
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

	/** 当前来源摊位的完整真实商品目录；Widget 会在本地按分类过滤出 DisplayedEntries。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatShopEntryView> Entries;

	/** 从 Entries 归纳出的分类按钮数据；玩家当前选择不在这里同步，只由本地 Widget 标记。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatShopCategoryView> Categories;

	/** 当前本地购物车行；它由玩家本机点击生成，不同步给其他客户端，支付时才发送 EntryId 和次数。 */
	UPROPERTY(BlueprintReadOnly)
	TArray<FCatShopCartLineView> CartLines;

	/** 当前购物车总价；Model 只用于展示和按钮可用性，服务器支付时会重新计算。 */
	UPROPERTY(BlueprintReadOnly)
	int32 CartTotalPrice = 0;

	/** 当前购物车是否包含行数超限、已下架、售罄、数量超过库存或小计溢出的行；为 true 时支付整体禁用。 */
	UPROPERTY(BlueprintReadOnly)
	bool bCartHasInvalidLines = false;

	/** 当前支付按钮是否可点击；空车、pending、数据未同步、购物车失效或资金不足都会置 false。 */
	UPROPERTY(BlueprintReadOnly)
	bool bCanPayCart = false;

	/** 最近一次 UI 提交的动作；用于展示“已提交/被本地拒绝”的结果提示。 */
	UPROPERTY(BlueprintReadOnly)
	ECatShopUIAction LastAction = ECatShopUIAction::None;

	/** 最近一次提交或拒绝的条目 ID；用于把结果提示和商品行关联起来。 */
	UPROPERTY(BlueprintReadOnly)
	FName LastEntryId = NAME_None;

	/** 当前是否已有支付请求提交后等待服务器回包；Model 和 WBP 用它禁用加购、删除和支付，避免本地购物车与已提交订单脱节。 */
	UPROPERTY(BlueprintReadOnly)
	bool bActionPending = false;

	/** 给 WBP 顶部文本绑定的团队公款摘要。 */
	UPROPERTY(BlueprintReadOnly)
	FText WalletText;

	/** 给 WBP 结果区域绑定的加购、删除、支付或拒绝反馈。 */
	UPROPERTY(BlueprintReadOnly)
	FText ResultText;

	/** 给 WBP 购物车总计文本绑定的金额摘要。 */
	UPROPERTY(BlueprintReadOnly)
	FText CartTotalText;

	/** 给 WBP 支付按钮绑定的固定按钮文案。 */
	UPROPERTY(BlueprintReadOnly)
	FText PayButtonText;

	/** 支付按钮禁用时的原因；资金不足时必须显示“资金不足，无法购买！”。 */
	UPROPERTY(BlueprintReadOnly)
	FText PayDisabledReasonText;
};
