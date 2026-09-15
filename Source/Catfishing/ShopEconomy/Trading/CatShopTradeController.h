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
 * 购买成功即已入库，经济与实物字段呈现同一成交终态；售鱼在独占实物期间统一入账，失败保留实物。
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
	 * 实物操作的最终结果。购物车与成交结果一致；售鱼来自鱼护批量移除或嘴叼鱼消费。
	 * 购买的 Revision 为公款版本；该字段供既有 RPC/UI 判断整单结果，不代表独立交付阶段。
	 */
	UPROPERTY(BlueprintReadOnly)
	FCatDomainCommandResult Delivery;
};

/**
 * 商店交易控制器负责串起付款、交付、售鱼和入账的服务器链路。
 * PlayerController 只把 owning client 的请求送进来；购买的钱货提交由经济服务统一完成，售鱼实物与入账由本控制器协调。
 */
UCLASS()
class CATFISHING_API UCatShopTradeController : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 只在服务器 Game World 创建；客户端没有这条链，也不能本地推进订单。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** 服务器接收玩家摊位购物车请求，复核玩法门与服务距离并重建身份、货架和营地收货仓库；交经济服务成交后返回统一钱货终态，前置失败直接返回拒绝。 */
	FCatShopOrderResult SubmitCartFromKiosk(AController* RequestingController, ACatShopKioskActor* ShopKiosk,
		const TArray<FCatShopCartLineCommand>& Lines, FGuid RequestId);

	/** 向明确买家出售当前鱼护内指定鱼实例；Guard为空时只售嘴叼鱼，整单复核后统一处理实物与GAS入账。 */
	FCatShopOrderResult SubmitFishSaleFromPlayer(AController* RequestingController, ACatFishBuyerActor* Buyer,
		ACatFishGuardActor* Guard, const TArray<FGuid>& FishInstanceIds, FGuid RequestId);

private:
	friend class FCatShopCartAtomicTest;
	friend class FCatShopInventoryDeliveryReplayTest;
#if WITH_DEV_AUTOMATION_TESTS
	int32 FailDeliveryStepForTest = INDEX_NONE;
#endif
	/** 静默准备全部交付后扣款；任一失败同步恢复收货方，服务恢复货架；终态重放不补货。 */
	FCatShopOrderResult RunCartOrder(const FCatShopCartCommand& Command,
		UCatShopInventoryComponent* ShopInventory, ACatCampInventoryActor* DeliveryInventory);

	/** 售鱼命令终态缓存；跨库存扣除和公款入账的重放必须返回首次结果，不能再读已被扣除的鱼槽。 */
	TMap<FString, FCatShopOrderResult> FishSaleTerminalCache;

	/** 售鱼请求绑定的买家、鱼护与鱼身份集合；同一 RequestId 换载荷会被拒绝，不依赖库存或钱包版本。 */
	TMap<FString, FString> FishSaleTerminalPayloadByKey;
};
