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

/**
 * 玩家把一个正式库存格里的鱼卖给商店的完整意图。
 * 它只声明鱼实例、库存宿主和槽位；重量由服务器从鱼物品实例读取，公款由商店服务裁决。
 */
USTRUCT()
struct FCatShopFishSaleOrderCommand
{
	GENERATED_BODY()

	/** RequestId 与服务器重建的身份；ExpectedRevision 在这条命令里指团队公款版本。 */
	FCatDomainCommandContext Context;

	/** 要卖掉的那条鱼物品实例；Shop 只记录它，删除和来源事实均由正式库存持有。 */
	FGuid FishItemInstanceId;

	/** 这条鱼当前所在的 Actor 宿主；控制器会从该宿主解析正式库存组件。 */
	AActor* SourceInventoryHost = nullptr;

	/** 这条鱼当前所在的库存槽位；服务器会重新读取该槽并核对 FishItemInstanceId。 */
	int32 SourceInventorySlotIndex = INDEX_NONE;
};

/**
 * 一次“买下来并拿到手”或“一条鱼卖出入账”的完整结果。
 * 它刻意分成经济终态和实物终态：付款成功但交付失败、鱼已删但入账重试都是真实中间状态。
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
	 * 交付或库存提交这一段的终态。购物车发货失败时来自营地公共仓库，售鱼时来自来源库存移除鱼。
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

	/** 玩家从可触达正式库存出售一条鱼；本控制器重建服务器身份并把鱼实例提交、钱包入账串成同一条事务链。 */
	FCatShopOrderResult SubmitFishSaleFromPlayer(AController* RequestingController, FGuid FishItemInstanceId,
		AActor* SourceInventoryHost, int32 SourceInventorySlotIndex,
		FGuid RequestId, int64 ExpectedWalletRevision);

	/**
	 * 把玩家可触达库存里的一条鱼卖给商店：读取鱼事实 → 商店预检报价/公款 → 库存不可逆移除 → 公款入账。
	 * 它和购买走同一个控制器，是因为二者都跨越“钱”和“实物”两个领域，必须在一个地方固定提交顺序。
	 */
	FCatShopOrderResult SubmitFishSale(const FCatShopFishSaleOrderCommand& Command);

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

	/** 售鱼命令载荷签名；同一 RequestId 更换库存宿主、槽位、鱼或公款版本会被拒绝。 */
	TMap<FString, FString> FishSaleTerminalPayloadByKey;
};
