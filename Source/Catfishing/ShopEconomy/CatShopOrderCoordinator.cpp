#include "ShopEconomy/CatShopOrderCoordinator.h"

#include "Camp/CatCampInventoryActor.h"
#include "Items/CatItemsService.h"
#include "Logging/CatLog.h"
#include "ShopEconomy/CatShopEconomyService.h"
#include "ShopEconomy/CatShopInventoryComponent.h"
#include "UObject/Class.h"

// 创建条件流程：只在服务器 Game World 建立这条链；客户端不能本地推进订单。
bool UCatShopOrderCoordinator::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

// 购物车入口流程：只声明这是整车支付订单，其余交给共用链条。
FCatShopOrderResult UCatShopOrderCoordinator::SubmitCart(const FCatShopCartCommand& Command,
	UCatShopInventoryComponent* ShopInventory, ACatCampInventoryActor* DeliveryInventory)
{
	return RunCartOrder(Command, ShopInventory, DeliveryInventory);
}

// 售鱼链流程：
// 1. 先读取 Items 容器快照并确认来源种类，价格只从鱼实例重量现场估出。
// 2. 再让 Shop 用同一份售鱼命令做公款/价格预检；这一步失败时绝不触碰 Items，鱼仍留在原容器。
// 3. 预检通过后用同一个 RequestId 调 Items::ConsumeFish 完成实物提交，成功或合法重放才进入 Shop::ApplyFishSale。
// 4. Result.Delivery 始终暴露 Items 提交段，Result.Transaction 暴露公款/账本段，调用方能区分“鱼没删”和“钱没入账”。
// 边界：Social escrow 售鱼有追回窗口和归还分支，本模块没有那些协议事实，因此继续 fail-closed。
FCatShopOrderResult UCatShopOrderCoordinator::SubmitFishSale(const FCatShopFishSaleOrderCommand& Command)
{
	FCatShopOrderResult Result;
	Result.Transaction.Command.RequestId = Command.Context.RequestId;
	Result.Delivery.RequestId = Command.Context.RequestId;

	UWorld* World = GetWorld();
	UCatShopEconomyService* Shop = World ? World->GetSubsystem<UCatShopEconomyService>() : nullptr;
	UCatItemsService* Items = World ? World->GetSubsystem<UCatItemsService>() : nullptr;
	if (!Shop || !Items)
	{
		Result.Transaction.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		Result.Delivery.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}

	const auto RejectBeforeItemsCommit = [&Result, Shop](const ECatDomainCommandError Error, const int64 DeliveryRevision = 0)
	{
		// 预检拒绝只回填当前公款和可选容器版本，不写任何终态缓存；同一个 RequestId 以后仍可在玩家重读快照后重新提交。
		Result.Transaction.Wallet = Shop->GetWalletSnapshot();
		Result.Transaction.Command.Error = Error;
		Result.Transaction.Command.Revision = Result.Transaction.Wallet.Revision;
		Result.Delivery.Error = Error;
		Result.Delivery.Revision = DeliveryRevision;
		return Result;
	};

	if (!Command.Context.RequestId.IsValid() || Command.Context.StableNetId.IsEmpty()
		|| !Command.FishInstanceId.IsValid() || !Command.ContainerId.IsValid())
	{
		return RejectBeforeItemsCommit(ECatDomainCommandError::InvalidPayload);
	}

	ECatContainerKind ExpectedContainerKind = ECatContainerKind::Unknown;
	if (Command.SourceKind == ECatShopFishSaleSource::FishGuard)
	{
		ExpectedContainerKind = ECatContainerKind::FishGuard;
	}
	else if (Command.SourceKind == ECatShopFishSaleSource::SharedFishTank)
	{
		ExpectedContainerKind = ECatContainerKind::SharedFishTank;
	}
	else if (Command.SourceKind == ECatShopFishSaleSource::StolenEscrow)
	{
		return RejectBeforeItemsCommit(ECatDomainCommandError::PolicyUndecided);
	}
	else
	{
		return RejectBeforeItemsCommit(ECatDomainCommandError::InvalidPayload);
	}

	FCatContainerSnapshot Snapshot;
	if (!Items->TryGetContainerSnapshot(Command.ContainerId, Snapshot))
	{
		return RejectBeforeItemsCommit(ECatDomainCommandError::NotFound);
	}
	if (Snapshot.Kind != ExpectedContainerKind)
	{
		return RejectBeforeItemsCommit(ECatDomainCommandError::InvalidPayload, Snapshot.Revision);
	}

	FCatFishConsumeCommand ConsumeCommand;
	ConsumeCommand.Context.RequestId = Command.Context.RequestId;
	ConsumeCommand.Context.ExpectedRevision = Command.ExpectedContainerRevision;
	ConsumeCommand.Context.StableNetId = Command.Context.StableNetId;
	ConsumeCommand.FishInstanceId = Command.FishInstanceId;
	ConsumeCommand.SourceContainerId = Command.ContainerId;

	FCatFishInstance SaleFish;
	const FCatFishInstance* FishInContainer = Snapshot.Fish.FindByPredicate([&Command](const FCatFishInstance& Fish)
	{
		return Fish.FishInstanceId == Command.FishInstanceId;
	});
	bool bItemsAlreadyCommitted = false;
	if (FishInContainer)
	{
		SaleFish = *FishInContainer;
	}
	else
	{
		// 鱼不在快照里时只允许同一 ConsumeFish 终态重放补回鱼事实；没有终态就保持容器原样并拒绝。
		// 这条分支服务的是“鱼已删、Shop 入账回执需要重试”的恢复，不会创建新的删除动作。
		const FCatFishConsumeResult ConsumeReplay = Items->ConsumeFish(ConsumeCommand);
		Result.Delivery = ConsumeReplay.Command;
		if (ConsumeReplay.Command.Error != ECatDomainCommandError::AlreadyResolved
			|| ConsumeReplay.Fish.FishInstanceId != Command.FishInstanceId)
		{
			return RejectBeforeItemsCommit(ConsumeReplay.Command.Error == ECatDomainCommandError::AlreadyResolved
				? ECatDomainCommandError::InvalidPayload : ConsumeReplay.Command.Error, Snapshot.Revision);
		}
		SaleFish = ConsumeReplay.Fish;
		bItemsAlreadyCommitted = true;
	}

	if (Command.SourceKind == ECatShopFishSaleSource::FishGuard
		&& SaleFish.OwnerStableNetId != Command.Context.StableNetId)
	{
		return RejectBeforeItemsCommit(ECatDomainCommandError::PermissionDenied, Snapshot.Revision);
	}

	int32 SaleValue = 0;
	if (!Shop->TryAppraiseFishSale(SaleFish.WeightKilograms, SaleValue))
	{
		return RejectBeforeItemsCommit(ECatDomainCommandError::PolicyUndecided, Snapshot.Revision);
	}

	FCatShopFishSaleCommand SaleCommand;
	SaleCommand.Context = Command.Context;
	SaleCommand.FishInstanceId = SaleFish.FishInstanceId;
	// ItemsCommitId 采用售鱼 RequestId：Items::ConsumeFish 的幂等终态同样由 RequestId+容器作用域证明，
	// Shop 账本只需要记录这条协调链对应的实物提交回执，而不是另造一套提交 ID。
	SaleCommand.ItemsCommitId = Command.Context.RequestId;
	SaleCommand.SourceKind = Command.SourceKind;
	SaleCommand.WeightKilograms = SaleFish.WeightKilograms;
	SaleCommand.SaleValue = SaleValue;

	ECatDomainCommandError ShopValidationError = ECatDomainCommandError::None;
	int64 CurrentWalletRevision = Shop->GetWalletSnapshot().Revision;
	if (!Shop->ValidateFishSale(SaleCommand, ShopValidationError, CurrentWalletRevision)
		&& ShopValidationError != ECatDomainCommandError::AlreadyResolved)
	{
		Result.Transaction.Wallet = Shop->GetWalletSnapshot();
		Result.Transaction.Command.Error = ShopValidationError;
		Result.Transaction.Command.Revision = CurrentWalletRevision;
		Result.Delivery.Error = ShopValidationError;
		Result.Delivery.Revision = Snapshot.Revision;
		return Result;
	}

	if (!bItemsAlreadyCommitted)
	{
		const FCatFishConsumeResult Consume = Items->ConsumeFish(ConsumeCommand);
		Result.Delivery = Consume.Command;
		const bool bItemsCommitStanding = Consume.Command.bCommitted
			|| Consume.Command.Error == ECatDomainCommandError::AlreadyResolved;
		if (!bItemsCommitStanding)
		{
			Result.Transaction.Wallet = Shop->GetWalletSnapshot();
			Result.Transaction.Command.Error = Consume.Command.Error;
			Result.Transaction.Command.Revision = Result.Transaction.Wallet.Revision;
			return Result;
		}
		if (Consume.Fish.FishInstanceId != SaleCommand.FishInstanceId)
		{
			Result.Transaction.Wallet = Shop->GetWalletSnapshot();
			Result.Transaction.Command.Error = ECatDomainCommandError::InvalidPayload;
			Result.Transaction.Command.Revision = Result.Transaction.Wallet.Revision;
			Result.Delivery.Error = ECatDomainCommandError::InvalidPayload;
			return Result;
		}
	}

	Result.Transaction = Shop->ApplyFishSale(SaleCommand);
	return Result;
}

// 购物车订单链流程：
// 1. 取商店依赖和来源摊位库存，首次请求先解析整车报价，再在扣钱之前问完公共仓库整批接收前提。
// 2. 前提成立后提交整车购买，随后用购物车 RequestId 和服务器身份把完整购物车批次一次放入营地公共仓库。
// 3. 重放时发货 payload 仍按整车账本重建，只给仍 Pending 的账本补确认，避免部分确认失败后拿缩水批次撞仓库幂等签名。
// 4. 每条购买账本用自己的 TransactionId 作为交付确认 RequestId，避免一车多行互相撞同一个确认幂等键。
FCatShopOrderResult UCatShopOrderCoordinator::RunCartOrder(const FCatShopCartCommand& Command,
	UCatShopInventoryComponent* ShopInventory, ACatCampInventoryActor* DeliveryInventory)
{
	FCatShopOrderResult Result;
	Result.CartTransaction.Command.RequestId = Command.Context.RequestId;
	Result.Delivery.RequestId = Command.Context.RequestId;
	UWorld* World = GetWorld();
	UCatShopEconomyService* Shop = World ? World->GetSubsystem<UCatShopEconomyService>() : nullptr;
	if (!Shop || !ShopInventory || ShopInventory->GetShopInventoryId() != Command.ShopInventoryId)
	{
		Result.CartTransaction.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		Result.Delivery.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}

	// 交付侧的前提必须问在扣钱之前。整车购买一提交就会把总价从公款划走、把限量条目的库存也扣掉，
	// 而商店服务没有退款写口，所以首次请求必须先让公共仓库按整批物品模拟一次容量和堆叠。
	// 同 RequestId 重放不跑这道前置 gate：钱和货架库存可能已经在首次提交里改变了，重试要拿回既有回执或补交付确认。
	const int64 DeliveryExpectedRevision = DeliveryInventory ? DeliveryInventory->GetSnapshot().Revision : 0;
	if (!Shop->HasCatalogCartTerminal(Command))
	{
		FCatShopResolvedCart ResolvedCart;
		ECatDomainCommandError QuoteRejection = ECatDomainCommandError::None;
		if (!Shop->ResolveCatalogCartForAuthority(Command, ShopInventory, ResolvedCart, QuoteRejection))
		{
			Result.CartTransaction.Wallet = Shop->GetWalletSnapshot();
			Result.CartTransaction.Command.Error = QuoteRejection;
			Result.CartTransaction.Command.Revision = Result.CartTransaction.Wallet.Revision;
			Result.Delivery.Error = QuoteRejection;
			Result.Delivery.Revision = DeliveryExpectedRevision;
			return Result;
		}
		TArray<FCatCampInventoryAddItemRequest> DeliveryItems;
		DeliveryItems.Reserve(ResolvedCart.Lines.Num());
		for (const FCatShopResolvedCartLine& Line : ResolvedCart.Lines)
		{
			FCatCampInventoryAddItemRequest& Item = DeliveryItems.AddDefaulted_GetRef();
			Item.DefinitionId = Line.Entry.DefinitionId;
			Item.Quantity = Line.DeliveryQuantity;
		}
		const ECatDomainCommandError DeliveryRejection = DeliveryInventory
			? DeliveryInventory->ValidateAddItemsFromAuthority(
				Command.Context.RequestId, DeliveryExpectedRevision, Command.Context.StableNetId, DeliveryItems)
			: ECatDomainCommandError::DependencyUnavailable;
		if (DeliveryRejection != ECatDomainCommandError::None)
		{
			// 订单这一段报的是交付侧的错误码，因为订单压根没提交：公款、商店库存和账本一个字都没动。
			// Revision 仍给当前公款版本，调用方据此重读并决定要不要换个条件重试。
			Result.CartTransaction.Wallet = Shop->GetWalletSnapshot();
			Result.CartTransaction.Command.Error = DeliveryRejection;
			Result.CartTransaction.Command.Revision = Result.CartTransaction.Wallet.Revision;
			Result.Delivery.Error = DeliveryRejection;
			Result.Delivery.Revision = DeliveryExpectedRevision;
			UE_LOG(LogCatfishing, Warning,
				TEXT("Event=shop_cart_delivery_precheck_rejected RequestId=%s LineCount=%d Error=%s"),
				*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens), DeliveryItems.Num(),
				*UEnum::GetValueAsString(DeliveryRejection));
			return Result;
		}
	}

	Result.CartTransaction = Shop->PurchaseCatalogCart(Command, ShopInventory);
	// 订单成立包含两种情况：这次真的下单了，或者同一个 RequestId 之前已经下过单。
	// 后者必须继续往下走，否则一次网络重试就会把已经付过钱、还没完全确认交付的购物车留在 Pending。
	const bool bOrderStanding = !Result.CartTransaction.Transactions.IsEmpty()
		&& (Result.CartTransaction.Command.bCommitted
			|| Result.CartTransaction.Command.Error == ECatDomainCommandError::AlreadyResolved);
	if (!bOrderStanding)
	{
		Result.Delivery.Error = Result.CartTransaction.Command.Error;
		return Result;
	}
	if (!DeliveryInventory)
	{
		Result.Delivery.Error = ECatDomainCommandError::DependencyUnavailable;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=shop_cart_camp_inventory_grant_failed RequestId=%s Error=NoCampInventory"),
			*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens));
		return Result;
	}

	TArray<FCatCampInventoryAddItemRequest> DeliveryItems;
	bool bAllDelivered = true;
	for (const FCatShopTransactionRecord& Record : Result.CartTransaction.Transactions)
	{
		if (Record.EntryKind == ECatShopEntryKind::Unknown || Record.DefinitionId.IsNone()
			|| Record.PurchaseQuantity <= 0)
		{
			Result.Delivery.Error = ECatDomainCommandError::InvalidPhase;
			return Result;
		}
		FCatCampInventoryAddItemRequest& Item = DeliveryItems.AddDefaulted_GetRef();
		Item.DefinitionId = Record.DefinitionId;
		Item.Quantity = Record.PurchaseQuantity;
		if (Record.DeliveryState != ECatShopDeliveryState::Delivered)
		{
			bAllDelivered = false;
		}
	}
	if (bAllDelivered)
	{
		Result.Delivery.Error = ECatDomainCommandError::AlreadyResolved;
		Result.Delivery.Revision = DeliveryInventory->GetSnapshot().Revision;
		return Result;
	}

	const FCatDomainCommandResult Grant = DeliveryInventory->AddItemsFromAuthority(
		Command.Context.RequestId, DeliveryExpectedRevision, Command.Context.StableNetId, DeliveryItems);
	const bool bDeliveryReady = Grant.bCommitted || Grant.Error == ECatDomainCommandError::AlreadyResolved;
	if (!bDeliveryReady)
	{
		Result.Delivery = Grant;
		Result.Delivery.RequestId = Command.Context.RequestId;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=shop_cart_camp_inventory_grant_failed RequestId=%s LineCount=%d Error=%s"),
			*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens), DeliveryItems.Num(),
			*UEnum::GetValueAsString(Grant.Error));
		return Result;
	}
	Result.Delivery = Grant;
	for (FCatShopTransactionRecord& Record : Result.CartTransaction.Transactions)
	{
		if (Record.DeliveryState == ECatShopDeliveryState::Delivered)
		{
			continue;
		}
		FCatShopDeliveryConfirmationCommand Confirmation;
		// 每条购买账本各用自己的 TransactionId 做确认请求号；公共仓库入库回执仍然是整车 RequestId。
		Confirmation.Context.RequestId = Record.TransactionId;
		Confirmation.Context.ExpectedRevision = Record.WalletRevision;
		Confirmation.Context.StableNetId = Command.Context.StableNetId;
		Confirmation.TransactionId = Record.TransactionId;
		Confirmation.DeliveryReceiptId = Command.Context.RequestId;
		Confirmation.DeliveryRevision = Grant.Revision;
		const FCatShopTransactionResult Confirmed = Shop->ConfirmTransactionDelivery(Confirmation);
		if (Confirmed.Transaction.TransactionId == Record.TransactionId)
		{
			Record = Confirmed.Transaction;
		}
		if (Confirmed.Command.Error != ECatDomainCommandError::None
			&& Confirmed.Command.Error != ECatDomainCommandError::AlreadyResolved)
		{
			Result.Delivery.Error = Confirmed.Command.Error;
			Result.Delivery.Revision = Confirmed.Command.Revision;
			Result.Delivery.RequestId = Command.Context.RequestId;
			UE_LOG(LogCatfishing, Warning,
				TEXT("Event=shop_cart_delivery_confirmation_failed RequestId=%s TransactionId=%s Error=%s"),
				*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
				*Record.TransactionId.ToString(EGuidFormats::DigitsWithHyphens),
				*UEnum::GetValueAsString(Confirmed.Command.Error));
			return Result;
		}
	}
	Result.Delivery.Error = ECatDomainCommandError::None;
	Result.Delivery.Revision = Grant.Revision;
	Result.Delivery.RequestId = Command.Context.RequestId;
	return Result;
}
