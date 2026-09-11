#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ShopEconomy/Trading/CatShopTradingTypes.h"
#include "CatShopTradeController.generated.h"

class ACatCampInventoryActor;
class ACatShopKioskActor;
class AController;
class AActor;
class UCatShopInventoryComponent;
class ACatFishBuyerActor;
class ACatFishGuardActor;


/**
 * 一次购买交付或整批售鱼的完整结果，分别呈现经济记录与实物提交回执。
 * 购买仍可能等待发货；售鱼在独占实物期间统一入账，失败保留实物，不存在先删鱼再补款的中间状态。
 */
USTRUCT(BlueprintType)
struct FCatShopOrderResult
{
	GENERATED_BODY()

	/** 单交易订单这一段：售鱼仍只生成一条经济记录；购物车购买请读取 CartTransaction。 */
	UPROPERTY(BlueprintReadOnly)
	FCatShopTransactionResult Transaction;

	/** 购物车订单这一段：公款、商店库存和多条购买账本的终态。 */
	UPROPERTY(BlueprintReadOnly)
	FCatShopCartTransactionResult CartTransaction;

	/**
	 * 交付或实物提交的终态。购物车发货来自营地公共仓库；售鱼来自鱼护批量移除或嘴叼鱼消费。
	 * 它的 Revision 指向的聚合随来源不同而变化，读它时要先看调用链和 Error。
	 */
	UPROPERTY(BlueprintReadOnly)
	FCatDomainCommandResult Delivery;
};

/**
 * 商店交易控制器负责串起付款、交付、售鱼和入账的服务器链路。
 * PlayerController 只把 owning client 的请求送进来；公款、摊位库存、公共仓库和玩家库存都在这里按顺序协调。
 */
UCLASS()
class CATFISHING_API UCatShopTradeController : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 只在服务器 Game World 创建；客户端没有这条链，也不能本地推进订单。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** 玩家在指定摊位提交购物车；本控制器重建玩家身份、摊位库存和营地收货仓库，PlayerController 不参与订单业务。 */
	FCatShopOrderResult SubmitCartFromKiosk(AController* RequestingController, ACatShopKioskActor* ShopKiosk,
		const TArray<FCatShopCartLineCommand>& Lines, FGuid RequestId, int64 ExpectedWalletRevision);

	/** 向明确买家出售当前鱼护内指定鱼实例；Guard为空时只售嘴叼鱼，整单复核后统一处理实物与GAS入账。 */
	FCatShopOrderResult SubmitFishSaleFromPlayer(AController* RequestingController, ACatFishBuyerActor* Buyer,
		ACatFishGuardActor* Guard, const TArray<FGuid>& FishInstanceIds, FGuid RequestId);

private:
	/**
	 * 声明：按同一条链跑完整个购物车，实际卖货对象是来源摊位库存，收货对象是营地公共仓库。
	 * 实现：先取本 World 的经济服务、来源摊位库存和营地收货库存；再用整车报价得到每行 DefinitionId + DeliveryQuantity，
	 *       并把公共仓库能否整批接收问在扣钱之前。任一不成立就直接返回，此时公款、商店库存和账本一个字都没动。
	 *       前提都成立才提交整车购买，并只在订单确实成立时继续；已经全部交付过的购物车直接返回 AlreadyResolved。
	 *       否则用同一个购物车 RequestId 把整批物品加入营地收货库存，随后逐条确认购买账本并回填最新状态。
	 * 边界：前置 gate 是这条链处理交付失败的主要手段，因为扣钱那一步不可逆而商店根本没有退款写口。
	 */
	FCatShopOrderResult RunCartOrder(const FCatShopCartCommand& Command,
		UCatShopInventoryComponent* ShopInventory, ACatCampInventoryActor* DeliveryInventory);

	/** 售鱼命令终态缓存；跨库存扣除和公款入账的重放必须返回首次结果，不能再读已被扣除的鱼槽。 */
	TMap<FString, FCatShopOrderResult> FishSaleTerminalCache;

	/** 售鱼请求绑定的买家、鱼护与鱼身份集合；同一 RequestId 换载荷会被拒绝，不依赖库存或钱包版本。 */
	TMap<FString, FString> FishSaleTerminalPayloadByKey;
};
