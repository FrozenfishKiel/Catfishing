#include "ShopEconomy/CatShopOrderCoordinator.h"

#include "Equipment/CatEquipmentComponent.h"
#include "Items/CatItemsService.h"
#include "Logging/CatLog.h"
#include "ShopEconomy/CatShopEconomyService.h"
#include "UObject/Class.h"

// 创建条件流程：只在服务器 Game World 建立这条链；客户端不能本地推进订单。
bool UCatShopOrderCoordinator::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

// 购买入口流程：只声明这是付费订单，其余交给共用链条。
FCatShopOrderResult UCatShopOrderCoordinator::SubmitPurchase(const FCatShopPurchaseCommand& Command,
	UCatEquipmentComponent* RecipientEquipment)
{
	return RunOrder(Command, false, RecipientEquipment);
}

// 免费自取入口流程：只声明这是免费订单，其余交给共用链条。
FCatShopOrderResult UCatShopOrderCoordinator::SubmitFreeClaim(const FCatShopPurchaseCommand& Command,
	UCatEquipmentComponent* RecipientEquipment)
{
	return RunOrder(Command, true, RecipientEquipment);
}

// 售鱼链流程：
// 1. 先读取 Items 容器快照，确认来源种类和个人鱼护主人，价格只从鱼实例重量现场估出。
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
	if (Command.SourceKind == ECatShopFishSaleSource::PersonalGuard)
	{
		ExpectedContainerKind = ECatContainerKind::PersonalGuard;
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

	if (Command.SourceKind == ECatShopFishSaleSource::PersonalGuard
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

// 订单链流程：取商店依赖、在扣钱之前问完买家 Equipment 交付前提、下订单、判断订单是否成立、按 EntryKind 分流到本人装备槽或耗材栈、确认交付、回填账本状态。
FCatShopOrderResult UCatShopOrderCoordinator::RunOrder(const FCatShopPurchaseCommand& Command, const bool bFreeClaim,
	UCatEquipmentComponent* RecipientEquipment)
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

	// 交付侧的前提必须问在扣钱之前。PurchaseCatalogEntry 一提交就把钱从公款划走、把限量条目的库存也扣掉，
	// 而整个商店服务没有任何反方向的写口。本项目对这种跨聚合链的既定做法是前置 gate，不是事后补偿：
	// 补一条退款路径等于给唯一的不可逆写口开一个反向入口，而"退款算不算一笔交易记录、账本怎么记"产品还没裁，
	// 记待重投则要再引入一份重投队列。所以这里宁可把每条交付前提都提前问一遍，也不让钱先出去再发现东西送不出。
	// 目录里根本没有这条 EntryId 时不在这里拦：那条路购买写口自己就会拒绝且一分钱不动，它给的错误码（NotFound、
	// PolicyUndecided、CommandsClosed）比这里编一个更准确。
	// 前置校验只对"第一次下这一单"有意义。同 RequestId 的重放，钱在首次那一趟就已经扣掉了，这次要做的是把既有回执
	// 找回来（交付过就回 AlreadyResolved，没交付完就把交付补上），而不是重新问一遍"现在还能不能交付"。
	// 少了这道判断就会出现：结算夜或买家 Pawn 已经销毁时重放一笔已交付订单，被前置校验判成没有收货人；
	// 这种情况下调用方拿不到本该拿到的
	// AlreadyResolved 和实物回执，看起来就像"钱扣了、东西不知去向"。
	FCatShopCatalogEntry Entry;
	if (!Shop->HasCatalogTransactionTerminal(Command, bFreeClaim) && Shop->TryGetCatalogEntry(Command.EntryId, Entry))
	{
		ECatDomainCommandError DeliveryRejection = ECatDomainCommandError::None;
		if (Entry.Kind == ECatShopEntryKind::RunConsumableGrant)
		{
			// 耗材要落到买家自己身上：没有身体就没有收货人，栈已经满了也塞不进去，两件事都在扣钱之前问清楚。
			DeliveryRejection = RecipientEquipment
				? RecipientEquipment->ValidateRunConsumableGrant(Command.Context.RequestId, Entry.DefinitionId, 1)
				: ECatDomainCommandError::DependencyUnavailable;
		}
		else if (Entry.Kind == ECatShopEntryKind::EquipmentGrant)
		{
			// 装备直接落到买家本人 Equipment：没有收货组件、或定义不是可装配装备，都必须在扣款前拦下。
			DeliveryRejection = RecipientEquipment
				? RecipientEquipment->ValidateEquipmentGrantFromAuthority(Command.Context.RequestId, Entry.DefinitionId)
				: ECatDomainCommandError::DependencyUnavailable;
		}
		else
		{
			DeliveryRejection = ECatDomainCommandError::InvalidPayload;
		}
		if (DeliveryRejection != ECatDomainCommandError::None)
		{
			// 订单这一段报的是交付侧的错误码，因为订单压根没提交：公款、商店库存和账本一个字都没动。
			// Revision 仍给当前公款版本，调用方据此重读并决定要不要换个条件重试。
			Result.Transaction.Command.Error = DeliveryRejection;
			Result.Transaction.Command.Revision = Shop->GetWalletSnapshot().Revision;
			Result.Delivery.Error = DeliveryRejection;
			UE_LOG(LogCatfishing, Warning,
				TEXT("Event=shop_order_delivery_precheck_rejected RequestId=%s EntryId=%s DefinitionId=%s Error=%s"),
				*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens), *Command.EntryId.ToString(),
				*Entry.DefinitionId.ToString(), *UEnum::GetValueAsString(DeliveryRejection));
			return Result;
		}
	}

	Result.Transaction = bFreeClaim ? Shop->ClaimFreeCatalogEntry(Command) : Shop->PurchaseCatalogEntry(Command);
	// 订单成立包含两种情况：这次真的下单了，或者同一个 RequestId 之前已经下过单。
	// 后者必须继续往下走，否则一次网络重试就会把一笔已经付过钱、还没入库的订单永远丢在待交付。
	const FCatShopTransactionRecord& Record = Result.Transaction.Transaction;
	const bool bOrderStanding = Record.TransactionId.IsValid()
		&& (Result.Transaction.Command.bCommitted
			|| Result.Transaction.Command.Error == ECatDomainCommandError::AlreadyResolved);
	if (!bOrderStanding)
	{
		Result.Delivery.Error = Result.Transaction.Command.Error;
		return Result;
	}
	const bool bRunConsumableOrder = Record.EntryKind == ECatShopEntryKind::RunConsumableGrant;
	const bool bEquipmentOrder = Record.EntryKind == ECatShopEntryKind::EquipmentGrant;
	if (!bRunConsumableOrder && !bEquipmentOrder)
	{
		// 账本记录的交付类别必须能映射到一个明确的下游领域；未知类别不默认当成装备，避免错配目录污染本人装备槽。
		Result.Delivery.Error = ECatDomainCommandError::InvalidPayload;
		Result.Delivery.Revision = Record.DeliveryRevision;
		return Result;
	}
	if (Record.DeliveryState == ECatShopDeliveryState::Delivered)
	{
		// 已经交付过的订单不再交付第二次。装备和耗材都落在买家自己的 Equipment 快照里，
		// 账本说交付过就只返回幂等终态，避免为了恢复回执再写一次装备槽或数量栈。
		Result.Delivery.Revision = Record.DeliveryRevision;
		Result.Delivery.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}

	FCatShopDeliveryConfirmationCommand Confirmation;
	Confirmation.Context.RequestId = Command.Context.RequestId;
	Confirmation.Context.ExpectedRevision = Record.WalletRevision;
	Confirmation.Context.StableNetId = Command.Context.StableNetId;
	Confirmation.TransactionId = Record.TransactionId;
	if (bRunConsumableOrder)
	{
		// 耗材订单落到买家自己身上：一份订单授予 1 份。Equipment 那一侧按 RequestId 幂等，同一订单重试拿回首次结果而不是再给一份；
		// 回执没有实例 ID 可用，就用订单 RequestId——它正是 Equipment 授予记录的幂等键，账本据此能指回那条授予。
		if (!RecipientEquipment)
		{
			Result.Delivery.Error = ECatDomainCommandError::DependencyUnavailable;
			UE_LOG(LogCatfishing, Warning,
				TEXT("Event=shop_order_consumable_grant_failed TransactionId=%s DefinitionId=%s Error=NoRecipientEquipment"),
				*Record.TransactionId.ToString(EGuidFormats::DigitsWithHyphens), *Record.DefinitionId.ToString());
			return Result;
		}
		const FCatDomainCommandResult Grant = RecipientEquipment->GrantRunConsumableFromAuthority(
			Command.Context.RequestId, RecipientEquipment->GetSnapshot().Revision, Record.DefinitionId, 1);
		if (!Grant.bCommitted && Grant.Error != ECatDomainCommandError::AlreadyResolved)
		{
			Result.Delivery = Grant;
			UE_LOG(LogCatfishing, Warning,
				TEXT("Event=shop_order_consumable_grant_failed TransactionId=%s DefinitionId=%s Error=%s"),
				*Record.TransactionId.ToString(EGuidFormats::DigitsWithHyphens), *Record.DefinitionId.ToString(),
				*UEnum::GetValueAsString(Grant.Error));
			return Result;
		}
		Confirmation.DeliveryReceiptId = Command.Context.RequestId;
		Confirmation.DeliveryRevision = Grant.Revision;
		const FCatShopTransactionResult Confirmed = Shop->ConfirmTransactionDelivery(Confirmation);
		Result.Delivery = Confirmed.Command;
		if (Confirmed.Transaction.TransactionId == Record.TransactionId)
		{
			Result.Transaction.Transaction = Confirmed.Transaction;
		}
		return Result;
	}

	if (!RecipientEquipment)
	{
		Result.Delivery.Error = ECatDomainCommandError::DependencyUnavailable;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=shop_order_equipment_grant_failed TransactionId=%s DefinitionId=%s Error=NoRecipientEquipment"),
			*Record.TransactionId.ToString(EGuidFormats::DigitsWithHyphens), *Record.DefinitionId.ToString());
		return Result;
	}
	const FCatDomainCommandResult Grant = RecipientEquipment->GrantEquipmentFromAuthority(
		Command.Context.RequestId, RecipientEquipment->GetSnapshot().Revision, Record.DefinitionId);
	const bool bEquipmentReady = Grant.bCommitted || Grant.Error == ECatDomainCommandError::AlreadyResolved;
	if (!bEquipmentReady)
	{
		Result.Delivery = Grant;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=shop_order_equipment_grant_failed TransactionId=%s DefinitionId=%s Error=%s"),
			*Record.TransactionId.ToString(EGuidFormats::DigitsWithHyphens), *Record.DefinitionId.ToString(),
			*UEnum::GetValueAsString(Grant.Error));
		return Result;
	}
	// 订单 RequestId 当 Equipment 回执，Equipment Revision 当下游版本；账本能证明这笔订单已落到买家自己的装备快照。
	Confirmation.DeliveryReceiptId = Command.Context.RequestId;
	Confirmation.DeliveryRevision = Grant.Revision;
	const FCatShopTransactionResult Confirmed = Shop->ConfirmTransactionDelivery(Confirmation);
	Result.Delivery = Confirmed.Command;
	if (Confirmed.Transaction.TransactionId == Record.TransactionId)
	{
		// 只回填账本记录，不覆盖订单那一段的终态：订单是不是这次才下的，和交付有没有确认成功，是两件事。
		Result.Transaction.Transaction = Confirmed.Transaction;
	}
	return Result;
}
