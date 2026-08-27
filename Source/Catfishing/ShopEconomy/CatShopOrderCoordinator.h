#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ShopEconomy/CatShopEconomyTypes.h"
#include "CatShopOrderCoordinator.generated.h"

class UCatEquipmentComponent;

/**
 * 玩家把自己容器里的一条鱼卖掉的完整意图。
 * 它刻意带两个 ExpectedRevision：鱼在容器里、钱进公款，这是两个各自独立推进版本的聚合，
 * 只带其中一个就意味着另一边的并发前提没人检查。价格和重量不在这里——两者都由服务器从鱼实例现场取。
 */
USTRUCT()
struct FCatShopFishSaleOrderCommand
{
	GENERATED_BODY()

	/** RequestId 与服务器重建的身份；ExpectedRevision 在这条命令里指团队公款版本。 */
	FCatDomainCommandContext Context;

	/** 要卖掉的那条鱼。 */
	FGuid FishInstanceId;

	/** 这条鱼当前所在的地面鱼护或共用鱼缸。 */
	FGuid ContainerId;

	/** 上述容器的并发前提版本；Items 用它判断调用方看到的容器内容是否已经过时。 */
	int64 ExpectedContainerRevision = 0;

	/** 这条鱼是从哪种容器卖出的；只接受地面鱼护和共用鱼缸两种。 */
	ECatShopFishSaleSource SourceKind = ECatShopFishSaleSource::Unknown;
};

/**
 * 一次"买下来并拿到手"的完整结果。它刻意分成两段终态而不是合成一个：
 * 付款成功但交付失败是真实存在的中间状态，把它压成单个成败会让调用方看不出钱已经扣了。
 */
USTRUCT(BlueprintType)
struct FCatShopOrderResult
{
	GENERATED_BODY()

	/** 订单这一段：公款、商店库存和账本的终态。它的 Transaction 字段带的是账本记录的最新状态。 */
	UPROPERTY(BlueprintReadOnly)
	FCatShopTransactionResult Transaction;

	/**
	 * 交付这一段的终态。链条走到哪一步，它就来自哪一步：授予失败时来自玩家自己的 Equipment，
	 * 走到确认时来自商店的交付确认。因此它的 Revision 指向的聚合也跟着变，读它时要先看 Error 是谁给的。
	 */
	UPROPERTY(BlueprintReadOnly)
	FCatDomainCommandResult Delivery;
};

/**
 * 把商店订单和下游交付串成一条链的服务器协调器：付款 → 交付 → 拿回执 → 确认交付。
 * 交付按目录条目的 Kind 分两条路：EquipmentGrant 与 InventoryQuantityGrant 都进入买家自己角色的随身库存数组；
 * EquipmentComponent 只额外维护当前钓鱼选择，二者都用订单 RequestId 当交付回执。
 *
 * 它存在的理由是《商店订单不拥有玩家随身库存》：商店只能记订单，买家 Equipment 只能改自己的库存数组和当前选择，两边都不该反过来调对方，
 * 所以需要第三个地方按顺序推这条链。它自己不持有公款、库存或实例，任何一步失败都保持两边各自的事实不变。
 *
 * 它是给玩家入口用的唯一写口：真正的玩家意图（哪个 Controller、按了什么）由 Framework 的服务器 RPC 重建成
 * 带服务器身份的命令后交给这里，协调器不接触 Controller，也不做权限判断；买家的 Equipment 也由 RPC 层从它自己的 Pawn 上取好再交进来。
 */
UCLASS()
class CATFISHING_API UCatShopOrderCoordinator : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 只在服务器 Game World 创建；客户端没有这条链，也不能本地推进订单。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/**
	 * 花公款买一条目录项并交付到买家自己的 Equipment。RecipientEquipment 由 RPC 层从买家 Pawn 取得；
	 * 装备和耗材都依赖它，缺少收货角色时订单在扣款前 fail-closed。
	 */
	FCatShopOrderResult SubmitPurchase(const FCatShopPurchaseCommand& Command, UCatEquipmentComponent* RecipientEquipment);

	/** 免费自取普通饵或 1 级保底竿；它和购买只差在不扣钱，交付链（含按 Kind 分流）完全一样。 */
	FCatShopOrderResult SubmitFreeClaim(const FCatShopPurchaseCommand& Command, UCatEquipmentComponent* RecipientEquipment);

	/**
	 * 把玩家容器里的一条鱼卖给商人猫：读取鱼实例 → 商店预检报价/公款 → Items 不可逆移除 → 公款入账。
	 * 它和购买走同一个协调器，是因为二者的本质问题相同——钱和实物分属两个领域，必须有第三方按固定顺序推进，
	 * 而且任何一步失败都要保证不会出现“钱扣了鱼还在”或“鱼没了钱没到”。
	 * 只处理地面鱼护和共享鱼缸；偷来的鱼有独立的 escrow 协议（追回窗口、空间前提、归还分支都在 Social），本模块继续关闭。
	 */
	FCatShopOrderResult SubmitFishSale(const FCatShopFishSaleOrderCommand& Command);

private:
	/**
	 * 声明：按同一条链跑完一笔订单，bFreeClaim 决定走购买还是免费自取写口，目录项的 Kind 只决定用装备型还是数量型入库校验。
	 * 实现：先取本 World 的商店；再按目录项的 Kind 把交付侧的前提问在扣钱之前——数量型物品问收货人在不在、定义是不是
	 *       正式的一局数量物、当前库存格还能不能装下；装备型物品问买家 Equipment 在不在、定义是不是可进入随身库存。任一不成立就直接
	 *       返回，此时公款、商店库存和账本一个字都没动。前提都成立才提交订单，并只在订单确实成立时继续；已经交付过的订单
	 *       直接返回 AlreadyResolved；否则按 Kind 交付：装备型和数量型都写入本人随身库存数组，收货侧再按空选择、缺货旧选择或已断/耐久非法同定义竿
	 *       的规则修正当前选择，二者都以
	 *       订单 RequestId 当回执、Equipment 版本当下游版本；随后确认交付并回填账本最新状态。
	 * 边界：前置 gate 是这条链处理交付失败的唯一手段，因为扣钱那一步不可逆而商店根本没有退款写口——"先扣后发、发不出去
	 *       再补偿"要给唯一的不可逆写口开一条反向路径，商店账本的退款语义产品也还没裁。gate 挡下时订单那一段返回的是交付
	 *       侧的错误码，因为订单压根没提交。目录里没有这条 EntryId 时 gate 不拦，交给购买写口自己拒绝：那条路本来就不扣钱，
	 *       它给的错误码更准确。同 RequestId 的重放也不拦：钱在首次那一趟就扣过了，这次要做的是把既有回执找回来，
	 *       再拿"此刻能不能交付"去挡它，只会让结算夜重放已交付订单、或买家 Pawn 已销毁时重放已交付耗材订单
	 *       拿不到 AlreadyResolved，看起来就像钱扣了东西没了。
	 *       交付回执确认失败仍然不退款、不回滚库存：东西已经到玩家手上，重试同一个 RequestId 会沿着幂等缓存把账本补上，
	 *       而不是重新扣一次钱。
	 */
	FCatShopOrderResult RunOrder(const FCatShopPurchaseCommand& Command, bool bFreeClaim, UCatEquipmentComponent* RecipientEquipment);
};
