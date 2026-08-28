#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ShopEconomy/CatShopEconomyTypes.h"
#include "CatShopOrderCoordinator.generated.h"

class UCatShopInventoryComponent;
class ACatCampInventoryActor;

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
	 * 交付这一段的终态。链条走到哪一步，它就来自哪一步：发货失败时来自营地公共仓库，
	 * 走到确认时来自商店的交付确认。因此它的 Revision 指向的聚合也跟着变，读它时要先看 Error 是谁给的。
	 */
	UPROPERTY(BlueprintReadOnly)
	FCatDomainCommandResult Delivery;
};

/**
 * 把商店订单和下游交付串成一条链的服务器协调器：付款 → 交付 → 拿回执 → 确认交付。
 * 购买物统一进入营地公共仓库；玩家要使用时再从公共仓库取到自己的随身装备库存。
 *
 * 它存在的理由是《商店订单不拥有公共仓库库存》：商店只能记订单，营地仓库只保存物品格子，两边都不该反过来调对方，
 * 所以需要第三个地方按顺序推这条链。它自己不持有公款、库存或实例，任何一步失败都保持两边各自的事实不变。
 *
 * 它是给玩家入口用的唯一写口：真正的玩家意图（哪个 Controller、按了什么）由 Framework 的服务器 RPC 重建成
 * 带服务器身份的命令后交给这里，协调器不接触 Controller，也不做权限判断；目标公共仓库也由 RPC 层按当前 World 解析好再交进来。
 */
UCLASS()
class CATFISHING_API UCatShopOrderCoordinator : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 只在服务器 Game World 创建；客户端没有这条链，也不能本地推进订单。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** 花公款买一条来源摊位目录项并交付到营地公共仓库；缺少摊位库存或公共仓库时订单在扣款前 fail-closed。 */
	FCatShopOrderResult SubmitPurchase(const FCatShopPurchaseCommand& Command,
		UCatShopInventoryComponent* ShopInventory, ACatCampInventoryActor* DeliveryInventory);

	/** 免费自取普通饵、保底竿或保底漂；白名单来自来源摊位库存，交付目标同样是营地公共仓库。 */
	FCatShopOrderResult SubmitFreeClaim(const FCatShopPurchaseCommand& Command,
		UCatShopInventoryComponent* ShopInventory, ACatCampInventoryActor* DeliveryInventory);

	/**
	 * 把玩家容器里的一条鱼卖给商人猫：读取鱼实例 → 商店预检报价/公款 → Items 不可逆移除 → 公款入账。
	 * 它和购买走同一个协调器，是因为二者的本质问题相同——钱和实物分属两个领域，必须有第三方按固定顺序推进，
	 * 而且任何一步失败都要保证不会出现“钱扣了鱼还在”或“鱼没了钱没到”。
	 * 只处理地面鱼护和共享鱼缸；偷来的鱼有独立的 escrow 协议（追回窗口、空间前提、归还分支都在 Social），本模块继续关闭。
	 */
	FCatShopOrderResult SubmitFishSale(const FCatShopFishSaleOrderCommand& Command);

private:
	/**
	 * 声明：按同一条链跑完一笔订单，bFreeClaim 决定走购买还是免费自取写口，实际卖货对象是来源摊位库存，收货对象是营地公共仓库。
	 * 实现：先取本 World 的经济服务、来源摊位库存和公共仓库版本；再把公共仓库能否接收 DefinitionId + PurchaseQuantity 问在扣钱之前。任一不成立就直接
	 *       返回，此时公款、商店库存和账本一个字都没动。前提都成立才提交订单，并只在订单确实成立时继续；已经交付过的订单
	 *       直接返回 AlreadyResolved；否则用同一个公共仓库版本和订单 RequestId 把物品加入公共仓库，仓库自己按装备定义决定堆叠或单件入格；
	 *       随后确认交付并回填账本最新状态。
	 * 边界：前置 gate 是这条链处理交付失败的唯一手段，因为扣钱那一步不可逆而商店根本没有退款写口——"先扣后发、发不出去
	 *       再补偿"要给唯一的不可逆写口开一条反向路径，商店账本的退款语义产品也还没裁。gate 挡下时订单那一段返回的是交付
	 *       侧的错误码，因为订单压根没提交。目录里没有这条 EntryId 时 gate 不拦，交给购买写口自己拒绝：那条路本来就不扣钱，
	 *       它给的错误码更准确。同 RequestId 的重放也不拦：钱在首次那一趟就扣过了，这次要做的是把既有回执找回来，
	 *       再拿"此刻公共仓库还能不能收货"去挡它，只会让结算夜或营地仓库状态变化后的已交付订单
	 *       拿不到 AlreadyResolved，看起来就像钱扣了东西没进公共仓库。
	 *       交付回执确认失败仍然不退款、不回滚库存：东西已经进入营地公共仓库，重试同一个 RequestId 会沿着幂等缓存把账本补上，
	 *       而不是重新扣一次钱。
	 */
	FCatShopOrderResult RunOrder(const FCatShopPurchaseCommand& Command, bool bFreeClaim,
		UCatShopInventoryComponent* ShopInventory, ACatCampInventoryActor* DeliveryInventory);
};
