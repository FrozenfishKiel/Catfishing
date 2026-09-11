#include "ShopEconomy/Trading/CatShopTradeController.h"

#include "Camp/CatCampHubActor.h"
#include "Camp/CatCampInventoryActor.h"
#include "Camp/CatCampSettings.h"
#include "Character/CatCharacter.h"
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

	// 售鱼订单载荷签名流程：玩家可提交的库存坐标和公款前提都进入签名；重放必须是同一个槽位里的同一条鱼。
	FString MakeFishSaleOrderPayloadSignature(const FCatShopFishSaleOrderCommand& Command)
	{
	return FString::Printf(TEXT("ExpectedWallet=%lld|Host=%s|Slot=%d|Fish=%s"),
			Command.Context.ExpectedRevision,
			*GetPathNameSafe(Command.SourceInventoryHost),
			Command.SourceInventorySlotIndex,
			*Command.FishItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens));
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

FCatShopOrderResult UCatShopTradeController::SubmitFishSaleFromPlayer(AController* RequestingController,
	const FGuid FishItemInstanceId, AActor* SourceInventoryHost,
	const int32 SourceInventorySlotIndex, const FGuid RequestId, const int64 ExpectedWalletRevision)
{
	// 玩家售鱼提交流程：
	// 1. 先重读服务器玩法 gate，关局或 teardown 时不进入鱼实例删除和钱包入账链。
	// 2. 再从 Controller 的 PlayerState 重建稳定身份，并用同一个库存触达规则确认玩家仍靠近来源宿主。
	// 3. 最后把命令交给售鱼交易链，库存提交结果和钱包结果都留在返回结构中供日志或调用方判断。
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
	const ACatCharacter* RequestingCharacter = RequestingController
		? Cast<ACatCharacter>(RequestingController->GetPawn()) : nullptr;
	const UCatCampSettings* CampSettings = GetDefault<UCatCampSettings>();
	if (!CurrentPlayerState || !CurrentPlayerState->GetUniqueId().IsValid()
		|| !SourceInventoryHost || SourceInventoryHost->GetWorld() != World
		|| !CatInventoryAccessRules::IsHostReachable(SourceInventoryHost, RequestingCharacter, CampSettings))
	{
		Result.Transaction.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		Result.Delivery.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}

	FCatShopFishSaleOrderCommand Command;
	Command.Context.RequestId = RequestId;
	Command.Context.ExpectedRevision = ExpectedWalletRevision;
	Command.Context.StableNetId = CurrentPlayerState->GetUniqueId()->ToString();
	Command.FishItemInstanceId = FishItemInstanceId;
	Command.SourceInventoryHost = SourceInventoryHost;
	Command.SourceInventorySlotIndex = SourceInventorySlotIndex;
	Result = SubmitFishSale(Command);
	UE_LOG(LogCatfishing, Log,
		TEXT("Event=shop_fish_sale_submitted RequestId=%s FishItem=%s SourceHost=%s SourceSlot=%d Wallet=%s Inventory=%s"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		*FishItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*GetNameSafe(SourceInventoryHost),
		SourceInventorySlotIndex,
		*UEnum::GetValueAsString(Result.Transaction.Command.Error),
		*UEnum::GetValueAsString(Result.Delivery.Error));
	return Result;
}

// 售鱼链流程：
// 1. 先从来源 Actor 解析正式库存组件并读取指定槽位的鱼物品实例，重量和鱼 ID 只取服务器对象。
// 2. 再让 Shop 用同一份售鱼命令做公款/价格预检；这一步失败时绝不触碰库存，鱼仍留在原槽。
// 3. 预检通过后从同一库存槽真实移除物品实例，再把这次库存扣除作为入账证据交给 Shop::ApplyFishSale。
// 4. Result.Delivery 始终暴露库存提交段，Result.Transaction 暴露公款/账本段，调用方能区分“鱼没删”和“钱没入账”。
FCatShopOrderResult UCatShopTradeController::SubmitFishSale(const FCatShopFishSaleOrderCommand& Command)
{
	FCatShopOrderResult Result;
	Result.Transaction.Command.RequestId = Command.Context.RequestId;
	Result.Delivery.RequestId = Command.Context.RequestId;

	UWorld* World = GetWorld();
	UCatShopEconomyService* Shop = World ? World->GetSubsystem<UCatShopEconomyService>() : nullptr;
	if (!Shop)
	{
		Result.Transaction.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		Result.Delivery.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}

	const auto RejectBeforeInventoryCommit = [&Result, Shop](const ECatDomainCommandError Error)
	{
		// 预检拒绝只回填当前公款，不写任何实物状态；同一个 RequestId 可重新提交。
		Result.Transaction.Wallet = Shop->GetWalletSnapshot();
		Result.Transaction.Command.Error = Error;
		Result.Transaction.Command.Revision = Result.Transaction.Wallet.Revision;
		Result.Delivery.Error = Error;
		return Result;
	};

	if (!Command.Context.RequestId.IsValid() || Command.Context.StableNetId.IsEmpty()
		|| !Command.FishItemInstanceId.IsValid() || !Command.SourceInventoryHost
		|| Command.SourceInventorySlotIndex == INDEX_NONE)
	{
		return RejectBeforeInventoryCommit(ECatDomainCommandError::InvalidPayload);
	}

	const FString TerminalKey = MakeFishSaleTerminalKey(Command.Context.StableNetId, Command.Context.RequestId);
	const FString PayloadSignature = MakeFishSaleOrderPayloadSignature(Command);
	if (const FCatShopOrderResult* Cached = FishSaleTerminalCache.Find(TerminalKey))
	{
		const FString* CachedPayload = FishSaleTerminalPayloadByKey.Find(TerminalKey);
		if (!CachedPayload || *CachedPayload != PayloadSignature)
		{
			return RejectBeforeInventoryCommit(ECatDomainCommandError::InvalidPayload);
		}
		FCatShopOrderResult Replay = *Cached;
		MarkCommandReplayed(Replay.Delivery);
		MarkCommandReplayed(Replay.Transaction.Command);
		return Replay;
	}

	TArray<UCatInventoryComponent*> SourceInventories;
	UCatInventoryStatics::AppendInventoryComponentsFromActor(Command.SourceInventoryHost, SourceInventories);
	UCatInventoryComponent* SourceInventory = nullptr;
	const FCatInventoryEntry* SourceEntry = nullptr;
	UCatFishInventoryItemInstance* SaleFish = nullptr;
	for (UCatInventoryComponent* CandidateInventory : SourceInventories)
	{
		const FCatInventoryEntry* CandidateEntry = CandidateInventory
			? CandidateInventory->GetInventoryEntryAtSlot(Command.SourceInventorySlotIndex) : nullptr;
		UCatFishInventoryItemInstance* CandidateFish = CandidateEntry
			? Cast<UCatFishInventoryItemInstance>(CandidateEntry->Instance.Get()) : nullptr;
		if (CandidateFish && CandidateFish->GetItemInstanceId() == Command.FishItemInstanceId
			&& CandidateEntry->StackCount == 1)
		{
			SourceInventory = CandidateInventory;
			SourceEntry = CandidateEntry;
			SaleFish = CandidateFish;
			break;
		}
	}
	if (!SourceInventory || !SourceEntry || !SaleFish)
	{
		return RejectBeforeInventoryCommit(ECatDomainCommandError::NotFound);
	}

	int32 SaleValue = 0;
	if (!Shop->TryAppraiseFishSale(SaleFish->GetFishWeightKilograms(), SaleValue))
	{
		return RejectBeforeInventoryCommit(ECatDomainCommandError::PolicyUndecided);
	}

	FCatShopFishSaleCommand SaleCommand;
	SaleCommand.Context = Command.Context;
	SaleCommand.FishInstanceId = SaleFish->GetItemInstanceId();
	// InventoryCommitId 采用售鱼 RequestId：库存扣除和公款入账属于同一条协调链，Shop 只需要稳定记录这次实物提交回执。
	SaleCommand.InventoryCommitId = Command.Context.RequestId;
	SaleCommand.WeightKilograms = SaleFish->GetFishWeightKilograms();
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
		return Result;
	}

	FCatInventoryEntry RemovedEntry;
	if (!SourceInventory->RemoveInventoryEntryAtSlotFromAuthority(Command.SourceInventorySlotIndex, RemovedEntry)
		|| RemovedEntry.Instance.Get() != SaleFish)
	{
		return RejectBeforeInventoryCommit(ECatDomainCommandError::NotFound);
	}
	Result.Delivery.RequestId = Command.Context.RequestId;
	Result.Delivery.bCommitted = true;
	Result.Delivery.Error = ECatDomainCommandError::None;

	Result.Transaction = Shop->ApplyFishSale(SaleCommand);
	if (!CatIsAcceptedDomainCommandResult(Result.Transaction.Command))
	{
		FCatInventoryReceiveBatch RollbackBatch;
		FCatInventoryInstanceEntry& RollbackEntry = RollbackBatch.InstanceEntries.AddDefaulted_GetRef();
		RollbackEntry.ItemInstance = RemovedEntry.Instance;
		RollbackEntry.Count = RemovedEntry.StackCount;
		if (!SourceInventory->TryAddInventoryBatch(RollbackBatch))
		{
			UE_LOG(LogCatfishing, Error,
				TEXT("Event=shop_fish_sale_rollback_failed RequestId=%s FishItem=%s SourceHost=%s SourceSlot=%d WalletError=%s"),
				*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
				*Command.FishItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
				*GetNameSafe(Command.SourceInventoryHost),
				Command.SourceInventorySlotIndex,
				*UEnum::GetValueAsString(Result.Transaction.Command.Error));
		}
		Result.Delivery.bCommitted = false;
		Result.Delivery.Error = Result.Transaction.Command.Error;
		return Result;
	}

	FishSaleTerminalCache.Add(TerminalKey, Result);
	FishSaleTerminalPayloadByKey.Add(TerminalKey, PayloadSignature);
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
