#include "ShopEconomy/Trading/CatShopTradeController.h"

#include "Camp/CatCampHubActor.h"
#include "Camp/CatCampInventoryActor.h"
#include "EngineUtils.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"
#include "Items/CatItemsService.h"
#include "Logging/CatLog.h"
#include "ShopEconomy/CatShopCartCommandUtils.h"
#include "ShopEconomy/CatShopEconomyService.h"
#include "ShopEconomy/CatShopInventoryComponent.h"
#include "ShopEconomy/CatShopKioskActor.h"
#include "UObject/Class.h"

namespace
{
	// 营地收货仓库解析流程：在当前 World 中寻找第一个能回答商店订单 PublicInventory 的营地，不把营地引用缓存到商店或 Controller。
	ACatCampInventoryActor* ResolveDeliveryInventoryForShopOrder(UWorld* World)
	{
		if (!World)
		{
			return nullptr;
		}
		for (TActorIterator<ACatCampHubActor> It(World); It; ++It)
		{
			ACatCampHubActor* Camp = *It;
			if (ACatCampInventoryActor* DeliveryInventory = IsValid(Camp)
				? Camp->ResolvePublicInventoryForShopOrder() : nullptr)
			{
				return DeliveryInventory;
			}
		}
		return nullptr;
	}
}

// 创建条件流程：只在服务器 Game World 建立这条链；客户端不能本地推进订单。
bool UCatShopTradeController::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

FCatShopOrderResult UCatShopTradeController::SubmitCartFromKiosk(AController* RequestingController,
	ACatShopKioskActor* ShopKiosk, const TArray<FCatShopCartLineCommand>& Lines, const FGuid RequestId,
	const int64 ExpectedWalletRevision)
{
	// 摊位购物车提交流程：
	// 1. 先重读服务器玩法 gate 和原始 RPC 载荷大小，拒绝无效局状态或异常购物车。
	// 2. 再从请求 Controller 重建稳定玩家身份，并要求摊位在当前 World 内证明玩家仍在服务半径。
	// 3. 摊位只给来源货架库存，营地收货仓库由 ShopEconomy 在 World 中解析，Controller 不再拼接业务依赖。
	// 4. 所有前提成立后才构造购物车命令并进入订单链；任一早期失败都会带 Delivery 结果回到 UI。
	FCatShopOrderResult Result;
	Result.CartTransaction.Command.RequestId = RequestId;
	Result.Delivery.RequestId = RequestId;
	UWorld* World = GetWorld();
	const ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	if (!GameMode || !GameMode->CanAcceptGameplayCommand(RequestingController))
	{
		Result.CartTransaction.Command.Error = ECatDomainCommandError::CommandsClosed;
		Result.Delivery.Error = ECatDomainCommandError::CommandsClosed;
		return Result;
	}
	if (!RequestId.IsValid() || !CatShopCartCommands::IsPayloadWithinLimits(Lines))
	{
		Result.CartTransaction.Command.Error = ECatDomainCommandError::InvalidPayload;
		Result.Delivery.Error = ECatDomainCommandError::InvalidPayload;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=shop_cart_invalid_rpc_payload RequestId=%s LineCount=%d MaxLines=%d MaxCountPerEntry=%d"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens), Lines.Num(),
			CatShopCartLimits::MaxCartLines, CatShopCartLimits::MaxCartCountPerEntry);
		return Result;
	}

	const APlayerState* CurrentPlayerState = RequestingController ? RequestingController->PlayerState : nullptr;
	UCatShopInventoryComponent* ShopInventory = nullptr;
	if (World && ShopKiosk && ShopKiosk->GetWorld() == World
		&& ShopKiosk->CanServeOrderFromAuthority(RequestingController))
	{
		ShopInventory = ShopKiosk->GetShopInventory();
	}
	ACatCampInventoryActor* DeliveryInventory = ResolveDeliveryInventoryForShopOrder(World);
	if (!CurrentPlayerState || !CurrentPlayerState->GetUniqueId().IsValid()
		|| !ShopInventory || !ShopInventory->GetShopInventoryId().IsValid() || !DeliveryInventory)
	{
		Result.CartTransaction.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		Result.Delivery.Error = ECatDomainCommandError::DependencyUnavailable;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=shop_cart_dependency_missing RequestId=%s LineCount=%d Shop=%s HasShopInventory=%s HasDeliveryInventory=%s Result=RejectedBeforePayment"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens), Lines.Num(), *GetNameSafe(ShopKiosk),
			ShopInventory ? TEXT("true") : TEXT("false"), DeliveryInventory ? TEXT("true") : TEXT("false"));
		return Result;
	}

	FCatShopCartCommand Command;
	Command.Context.RequestId = RequestId;
	Command.Context.ExpectedRevision = ExpectedWalletRevision;
	Command.Context.StableNetId = CurrentPlayerState->GetUniqueId()->ToString();
	Command.ShopInventoryId = ShopInventory->GetShopInventoryId();
	Command.Lines = Lines;
	Result = RunCartOrder(Command, ShopInventory, DeliveryInventory);
	Result.Delivery.RequestId = RequestId;
	UE_LOG(LogCatfishing, Log,
		TEXT("Event=shop_cart_submitted RequestId=%s LineCount=%d Order=%s Delivery=%s"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens), Lines.Num(),
		*UEnum::GetValueAsString(Result.CartTransaction.Command.Error),
		*UEnum::GetValueAsString(Result.Delivery.Error));
	return Result;
}

FCatShopOrderResult UCatShopTradeController::SubmitFishSaleFromPlayer(AController* RequestingController,
	const FGuid FishInstanceId, const FGuid ContainerId, const int64 ExpectedContainerRevision,
	const FGuid RequestId, const int64 ExpectedWalletRevision)
{
	// 玩家售鱼提交流程：
	// 1. 先重读服务器玩法 gate，关局或 teardown 时不进入鱼实例删除和钱包入账链。
	// 2. 再从 Controller 的 PlayerState 重建稳定身份；客户端提交的只是鱼、容器和预期版本。
	// 3. 最后把命令交给售鱼交易链，Items 提交结果和钱包结果都留在返回结构中供日志或调用方判断。
	FCatShopOrderResult Result;
	Result.Transaction.Command.RequestId = RequestId;
	Result.Delivery.RequestId = RequestId;
	UWorld* World = GetWorld();
	const ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	if (!GameMode || !GameMode->CanAcceptGameplayCommand(RequestingController))
	{
		Result.Transaction.Command.Error = ECatDomainCommandError::CommandsClosed;
		Result.Delivery.Error = ECatDomainCommandError::CommandsClosed;
		return Result;
	}
	const APlayerState* CurrentPlayerState = RequestingController ? RequestingController->PlayerState : nullptr;
	if (!CurrentPlayerState || !CurrentPlayerState->GetUniqueId().IsValid())
	{
		Result.Transaction.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		Result.Delivery.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}

	FCatShopFishSaleOrderCommand Command;
	Command.Context.RequestId = RequestId;
	Command.Context.ExpectedRevision = ExpectedWalletRevision;
	Command.Context.StableNetId = CurrentPlayerState->GetUniqueId()->ToString();
	Command.FishInstanceId = FishInstanceId;
	Command.ContainerId = ContainerId;
	Command.ExpectedContainerRevision = ExpectedContainerRevision;
	Result = SubmitFishSale(Command);
	UE_LOG(LogCatfishing, Log,
		TEXT("Event=shop_fish_sale_submitted RequestId=%s FishInstanceId=%s Wallet=%s Items=%s"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		*FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*UEnum::GetValueAsString(Result.Transaction.Command.Error),
		*UEnum::GetValueAsString(Result.Delivery.Error));
	return Result;
}

// 售鱼链流程：
// 1. 先让 Items 准备鱼事实，容器种类、版本、预留锁和鱼护归属都在 Items 域内完成。
// 2. 再让 Shop 用同一份售鱼命令做公款/价格预检；这一步失败时绝不触碰 Items，鱼仍留在原容器。
// 3. 预检通过后用同一个 RequestId 调 Items::ConsumeFish 完成实物提交，成功或合法重放才进入 Shop::ApplyFishSale。
// 4. Result.Delivery 始终暴露 Items 提交段，Result.Transaction 暴露公款/账本段，调用方能区分“鱼没删”和“钱没入账”。
FCatShopOrderResult UCatShopTradeController::SubmitFishSale(const FCatShopFishSaleOrderCommand& Command)
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

	const auto RejectBeforeItemsCommit = [&Result, Shop](const ECatDomainCommandError Error,
		const int64 DeliveryRevision = 0)
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

	FCatFishConsumeCommand ConsumeCommand;
	ConsumeCommand.Context.RequestId = Command.Context.RequestId;
	ConsumeCommand.Context.ExpectedRevision = Command.ExpectedContainerRevision;
	ConsumeCommand.Context.StableNetId = Command.Context.StableNetId;
	ConsumeCommand.FishInstanceId = Command.FishInstanceId;
	ConsumeCommand.SourceContainerId = Command.ContainerId;

	FCatFishInstance SaleFish;
	int64 CurrentContainerRevision = 0;
	ECatDomainCommandError PrepareError = ECatDomainCommandError::None;
	bool bItemsAlreadyCommitted = false;
	if (!Items->TryPrepareFishForSaleFromContainer(Command.FishInstanceId, Command.ContainerId,
		Command.ExpectedContainerRevision, Command.Context.StableNetId, SaleFish, CurrentContainerRevision,
		PrepareError))
	{
		FCatFishConsumeResult ConsumeReplay;
		if (PrepareError == ECatDomainCommandError::NotFound
			&& Items->TryReplayFishConsumeTerminal(ConsumeCommand, ConsumeReplay)
			&& CatIsAcceptedDomainCommandResult(ConsumeReplay.Command)
			&& ConsumeReplay.Fish.FishInstanceId == Command.FishInstanceId)
		{
			SaleFish = ConsumeReplay.Fish;
			Result.Delivery = ConsumeReplay.Command;
			bItemsAlreadyCommitted = true;
		}
		else
		{
			return RejectBeforeItemsCommit(PrepareError, CurrentContainerRevision);
		}
	}

	int32 SaleValue = 0;
	if (!Shop->TryAppraiseFishSale(SaleFish.WeightKilograms, SaleValue))
	{
		return RejectBeforeItemsCommit(ECatDomainCommandError::PolicyUndecided, CurrentContainerRevision);
	}

	FCatShopFishSaleCommand SaleCommand;
	SaleCommand.Context = Command.Context;
	SaleCommand.FishInstanceId = SaleFish.FishInstanceId;
	// ItemsCommitId 采用售鱼 RequestId：Items::ConsumeFish 的幂等终态同样由 RequestId+容器作用域证明，
	// Shop 账本只需要记录这条协调链对应的实物提交回执，而不是另造一套提交 ID。
	SaleCommand.ItemsCommitId = Command.Context.RequestId;
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
		Result.Delivery.Revision = CurrentContainerRevision;
		return Result;
	}

	if (!bItemsAlreadyCommitted)
	{
		const FCatFishConsumeResult Consume = Items->ConsumeFish(ConsumeCommand);
		Result.Delivery = Consume.Command;
		if (!CatIsAcceptedDomainCommandResult(Consume.Command))
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
FCatShopOrderResult UCatShopTradeController::RunCartOrder(const FCatShopCartCommand& Command,
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
		if (!Record.bPurchase || Record.DefinitionId.IsNone() || Record.PurchaseQuantity <= 0
			|| (!Record.bDeliveryPending && !Record.bDeliveryConfirmed))
		{
			Result.Delivery.Error = ECatDomainCommandError::InvalidPhase;
			return Result;
		}
		FCatCampInventoryAddItemRequest& Item = DeliveryItems.AddDefaulted_GetRef();
		Item.DefinitionId = Record.DefinitionId;
		Item.Quantity = Record.PurchaseQuantity;
		if (!Record.bDeliveryConfirmed)
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
		if (Record.bDeliveryConfirmed)
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
