#include "ShopEconomy/Trading/CatShopTradeController.h"

#include "Camp/CatCampHubActor.h"
#include "Camp/CatCampInventoryActor.h"
#include "Camp/CatCampSettings.h"
#include "Character/CatCharacter.h"
#include "Condition/CatConditionComponent.h"
#include "FishContainers/CatFishGuardActor.h"
#include "Inventory/CatFishOnlyInventoryComponent.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "ShopEconomy/CatFishBuyerActor.h"
#include "EngineUtils.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Inventory/CatInventoryAccessRules.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventorySettings.h"
#include "Inventory/CatInventoryStatics.h"
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

	// 购物车交付批次构建流程：商店账本只保存稳定定义 ID 和数量，这里把它解析成正式库存定义批次；容量、实例创建和幂等仍由 InventoryComponent 负责。
	bool AppendShopDeliveryEntryToInventoryBatch(const FName DefinitionId, const int32 Quantity,
		FCatInventoryReceiveBatch& OutReceiveBatch)
	{
		const UCatInventorySettings* InventorySettings = GetDefault<UCatInventorySettings>();
		UCatInventoryItemDefinition* Definition = InventorySettings
			? InventorySettings->FindRuntimeDefinition(DefinitionId) : nullptr;
		if (Definition == nullptr || Quantity <= 0 || !Definition->IsInventoryRuntimeDefinitionReady())
		{
			return false;
		}
		FCatInventoryDefinitionEntry& Entry = OutReceiveBatch.DefinitionEntries.AddDefaulted_GetRef();
		Entry.ItemDefinition = Definition;
		Entry.Count = Quantity;
		return true;
	}

	// 售鱼终态键流程：同一玩家同一 RequestId 只允许形成一笔售鱼协调链，避免可靠 RPC 重放时再次扣库存或再次入账。
	FString MakeFishSaleTerminalKey(const FString& StableNetId, const FGuid RequestId)
	{
		return FString::Printf(TEXT("%s|FishSaleOrder|%s"), *StableNetId,
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
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
	// 3. 摊位只给来源货架库存，营地收货仓库由 ShopEconomy 在 World 中解析，Controller 只保留交易意图。
	// 4. 所有前提成立后才构造购物车命令并进入订单链；任一前置失败都会带 Delivery 结果回到 UI。
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

// 售鱼协调流程：先重放请求，再核对买家、来源与整批鱼身份；估价和余额预检全部通过后才进入实物与GAS提交。
// 地面鱼护一次移除所有选中格且暂不广播，入账失败恢复原格；嘴叼鱼在消费保护内执行入账回调，失败继续叼着。
// 成功结果先进入终态缓存，再通知库存观察者；既不卖附近其他鱼护，也不信任客户端重量或价格。
FCatShopOrderResult UCatShopTradeController::SubmitFishSaleFromPlayer(AController* RequestingController,
	ACatFishBuyerActor* Buyer, ACatFishGuardActor* Guard, const TArray<FGuid>& FishInstanceIds, const FGuid RequestId)
{
	FCatShopOrderResult Result;
	Result.Transaction.Command.RequestId = RequestId;
	Result.Delivery.RequestId = RequestId;
	UCatShopEconomyService* Shop = GetWorld()->GetSubsystem<UCatShopEconomyService>();
	const APlayerState* PlayerState = RequestingController ? RequestingController->PlayerState : nullptr;
	const FString StableId = PlayerState && PlayerState->GetUniqueId().IsValid() ? PlayerState->GetUniqueId()->ToString() : FString();
	const FString Key = MakeFishSaleTerminalKey(StableId, RequestId);
	FString Payload = FString::Printf(TEXT("Buyer=%s|Guard=%s"), *GetPathNameSafe(Buyer), *GetPathNameSafe(Guard));
	for (const FGuid Id : FishInstanceIds) Payload += TEXT("|") + Id.ToString();
	const auto Finish = [&](const ECatDomainCommandError Error)
	{
		Result.Delivery.Error = Error;
		Result.Delivery.bCommitted = Error == ECatDomainCommandError::None;
		if (Error != ECatDomainCommandError::None) Result.Transaction.Command.Error = Error;
		if (Shop) Result.Transaction.Wallet = Shop->GetWalletSnapshot();
		if (RequestId.IsValid() && !StableId.IsEmpty())
		{
			FishSaleTerminalCache.Add(Key, Result);
			FishSaleTerminalPayloadByKey.Add(Key, Payload);
		}
		UE_LOG(LogCatfishing, Log, TEXT("Event=fish_sale_order Request=%s Buyer=%s Guard=%s FishCount=%d Result=%s Balance=%d World=%s NetMode=%d Authority=1"),
			*RequestId.ToString(), *GetNameSafe(Buyer), *GetNameSafe(Guard), Guard ? FishInstanceIds.Num() : 1,
			*UEnum::GetValueAsString(Error), Result.Transaction.Wallet.Balance, *GetNameSafe(GetWorld()), GetWorld()->GetNetMode());
		return Result;
	};
	if (const FCatShopOrderResult* Cached = FishSaleTerminalCache.Find(Key))
	{
		if (FishSaleTerminalPayloadByKey.FindRef(Key) != Payload)
		{
			Result.Delivery.Error = ECatDomainCommandError::InvalidPayload;
			Result.Transaction.Command.Error = Result.Delivery.Error;
			return Result;
		}
		Result = *Cached;
		MarkCommandReplayed(Result.Delivery);
		MarkCommandReplayed(Result.Transaction.Command);
		return Result;
	}
	ACatCharacter* Character = RequestingController ? Cast<ACatCharacter>(RequestingController->GetPawn()) : nullptr;
	const ACatfishingGameModeBase* GameMode = GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>();
	if (!Shop || !Character || StableId.IsEmpty() || !RequestId.IsValid()) return Finish(ECatDomainCommandError::InvalidPayload);
	if (!GameMode || !GameMode->CanAcceptGameplayCommand(RequestingController)) return Finish(ECatDomainCommandError::CommandsClosed);
	if (!Character->GetConditionComponent() || Character->GetConditionComponent()->GetSnapshot().bDowned)
		return Finish(ECatDomainCommandError::PermissionDenied);
	AActor* Source = Guard ? static_cast<AActor*>(Guard) : Character;
	if (!IsValid(Buyer) || Buyer->GetWorld() != GetWorld() || !Buyer->CanServeSource(RequestingController, Source)
		|| (Guard && !CatInventoryAccessRules::IsHostReachable(Guard, Character, GetDefault<UCatCampSettings>())))
		return Finish(ECatDomainCommandError::PermissionDenied);
	FCatShopFishSaleCommand Command;
	Command.Context.RequestId = RequestId;
	Command.Context.StableNetId = StableId;
	Command.InventoryCommitId = RequestId;
	UCatInventoryComponent* Inventory = Guard ? Guard->GetFishInventoryComponent() : nullptr;
	TArray<FCatInventoryEntry> OriginalEntries;
	TArray<FCatInventoryEntry> RemainingEntries;
	ACatFishPickupActor* MouthFish = nullptr;
	if (Guard)
	{
		if (!Inventory || FishInstanceIds.IsEmpty() || FishInstanceIds.Num() > Inventory->GetInventorySlotCount())
			return Finish(ECatDomainCommandError::InvalidPayload);
		OriginalEntries = Inventory->GetInventoryEntries();
		RemainingEntries = OriginalEntries;
		TSet<FGuid> Seen;
		for (const FGuid Id : FishInstanceIds)
		{
			const int32 Slot = Inventory->FindInventorySlotIndexFromInstanceId(Id);
			const FCatInventoryEntry* Entry = Inventory->GetInventoryEntryAtSlot(Slot);
			UCatFishInventoryItemInstance* Fish = Entry ? Cast<UCatFishInventoryItemInstance>(Entry->Instance) : nullptr;
			if (!Id.IsValid() || Seen.Contains(Id) || !Fish || Entry->StackCount != 1)
				return Finish(ECatDomainCommandError::NotFound);
			Seen.Add(Id);
			FCatShopFishSaleLine& Line = Command.Fish.AddDefaulted_GetRef();
			Line.FishInstanceId = Id;
			Line.FishDefinitionId = Fish->GetItemDefinitionId();
			Line.WeightKilograms = Fish->GetFishWeightKilograms();
			RemainingEntries[Slot] = FCatInventoryEntry(Inventory);
		}
	}
	else
	{
		MouthFish = ACatFishPickupActor::FindCarriedFish(Character);
		if (!FishInstanceIds.IsEmpty() || !MouthFish || !MouthFish->CanConsumeFromAuthority(RequestingController))
			return Finish(ECatDomainCommandError::NotFound);
		const FCatFishPickupPresentationState& Fish = MouthFish->GetPresentationState();
		FCatShopFishSaleLine& Line = Command.Fish.AddDefaulted_GetRef();
		Line.FishInstanceId = Fish.FishInstanceId;
		Line.FishDefinitionId = Fish.FishDefinitionId;
		Line.WeightKilograms = Fish.WeightKilograms;
	}
	ECatDomainCommandError Error = ECatDomainCommandError::None;
	int64 WalletRevision = 0;
	if (!Shop->ValidateFishSale(Command, Error, WalletRevision))
	{
		if (Error == ECatDomainCommandError::AlreadyResolved)
		{
			Result.Transaction = Shop->ApplyFishSale(Command);
			Result.Delivery = Result.Transaction.Command;
			return Result;
		}
		return Finish(Error);
	}
	// 提交期间的同步回调不能用同一 RequestId 再次进入；真正结果在本函数结束时替换，未新增事务阶段或备用库存。
	Result.Delivery.Error = ECatDomainCommandError::AlreadyResolved;
	FishSaleTerminalCache.Add(Key, Result);
	FishSaleTerminalPayloadByKey.Add(Key, Payload);
	if (MouthFish)
	{
		const bool bConsumed = MouthFish->ConsumeFromAuthority(RequestingController, RequestId, [&]()
		{
			Result.Transaction = Shop->ApplyFishSale(Command);
			return CatIsAcceptedDomainCommandResult(Result.Transaction.Command);
		});
		return Finish(bConsumed ? ECatDomainCommandError::None
			: (Result.Transaction.Command.Error == ECatDomainCommandError::None
				? ECatDomainCommandError::DependencyUnavailable : Result.Transaction.Command.Error));
	}
	if (!Inventory->ReplaceInventoryEntriesFromAuthority(RemainingEntries, OriginalEntries.Num(), false))
		return Finish(ECatDomainCommandError::NotFound);
	Result.Transaction = Shop->ApplyFishSale(Command);
	if (!CatIsAcceptedDomainCommandResult(Result.Transaction.Command))
	{
		Inventory->ReplaceInventoryEntriesFromAuthority(OriginalEntries, OriginalEntries.Num(), false);
		return Finish(Result.Transaction.Command.Error);
	}
	Finish(ECatDomainCommandError::None);
	Inventory->BroadcastInventoryChange();
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
	UCatInventoryComponent* DeliveryInventoryComponent = DeliveryInventory
		? DeliveryInventory->GetInventoryComponent() : nullptr;
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
			return Result;
		}
		FCatInventoryReceiveBatch DeliveryBatch;
		DeliveryBatch.DefinitionEntries.Reserve(ResolvedCart.Lines.Num());
		bool bDeliveryBatchReady = true;
		for (const FCatShopResolvedCartLine& Line : ResolvedCart.Lines)
		{
			bDeliveryBatchReady &= AppendShopDeliveryEntryToInventoryBatch(
				Line.Entry.DefinitionId, Line.DeliveryQuantity, DeliveryBatch);
		}
		const ECatDomainCommandError DeliveryRejection = !DeliveryInventoryComponent
			? ECatDomainCommandError::DependencyUnavailable
			: (!bDeliveryBatchReady
				? ECatDomainCommandError::InvalidPayload
				: DeliveryInventoryComponent->ValidateInventoryDefinitionBatchGrantFromAuthority(
					Command.Context.RequestId, Command.Context.StableNetId, DeliveryBatch));
		if (DeliveryRejection != ECatDomainCommandError::None)
		{
			// 订单这一段报的是交付侧的错误码，因为订单压根没提交：公款、商店库存和账本一个字都没动。
			// Revision 仍给当前公款版本，调用方据此重读并决定要不要换个条件重试。
			Result.CartTransaction.Wallet = Shop->GetWalletSnapshot();
			Result.CartTransaction.Command.Error = DeliveryRejection;
			Result.CartTransaction.Command.Revision = Result.CartTransaction.Wallet.Revision;
			Result.Delivery.Error = DeliveryRejection;
			UE_LOG(LogCatfishing, Warning,
				TEXT("Event=shop_cart_delivery_precheck_rejected RequestId=%s LineCount=%d Error=%s"),
				*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
				DeliveryBatch.DefinitionEntries.Num(),
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
	if (!DeliveryInventoryComponent)
	{
		Result.Delivery.Error = ECatDomainCommandError::DependencyUnavailable;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=shop_cart_camp_inventory_grant_failed RequestId=%s Error=NoCampInventory"),
			*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens));
		return Result;
	}

	FCatInventoryReceiveBatch DeliveryBatch;
	DeliveryBatch.DefinitionEntries.Reserve(Result.CartTransaction.Transactions.Num());
	bool bAllDelivered = true;
	for (const FCatShopTransactionRecord& Record : Result.CartTransaction.Transactions)
	{
		if (!Record.bPurchase || Record.DefinitionId.IsNone() || Record.PurchaseQuantity <= 0
			|| (!Record.bDeliveryPending && !Record.bDeliveryConfirmed))
		{
			Result.Delivery.Error = ECatDomainCommandError::InvalidPhase;
			return Result;
		}
		if (!AppendShopDeliveryEntryToInventoryBatch(Record.DefinitionId, Record.PurchaseQuantity, DeliveryBatch))
		{
			Result.Delivery.Error = ECatDomainCommandError::InvalidPayload;
			return Result;
		}
		if (!Record.bDeliveryConfirmed)
		{
			bAllDelivered = false;
		}
	}
	if (bAllDelivered)
	{
		Result.Delivery.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}

	const FCatDomainCommandResult Grant = DeliveryInventoryComponent->GrantInventoryDefinitionBatchFromAuthority(
		Command.Context.RequestId, Command.Context.StableNetId, DeliveryBatch);
	const bool bDeliveryReady = Grant.bCommitted || Grant.Error == ECatDomainCommandError::AlreadyResolved;
	if (!bDeliveryReady)
	{
		Result.Delivery = Grant;
		Result.Delivery.RequestId = Command.Context.RequestId;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=shop_cart_camp_inventory_grant_failed RequestId=%s LineCount=%d Error=%s"),
			*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			DeliveryBatch.DefinitionEntries.Num(),
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
	Result.Delivery.RequestId = Command.Context.RequestId;
	return Result;
}
