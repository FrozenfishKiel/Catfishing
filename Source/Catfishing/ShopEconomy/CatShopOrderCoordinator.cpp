#include "ShopEconomy/CatShopOrderCoordinator.h"

#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatTeamEquipmentLibrary.h"
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

// 售鱼链流程：先按来源和请求合法性挡住不该进这条链的意图，再冻结鱼、服务器估价、钱包入账，最后不可逆移除鱼。
// 三步共用同一个 SaleRequestId；失败回退的边界只有一条：钱包入账成功之前失败一律解冻，之后一律不解冻。
// 入账成功而移除失败时故意什么都不回退——钱已经进了公款，这时候把鱼放回去等于凭空多出一条鱼，
// 正确的收口是拿同一个 RequestId 重试移除，Items 那一步本身是幂等的。
FCatShopOrderResult UCatShopOrderCoordinator::SubmitFishSale(const FCatShopFishSaleOrderCommand& Command)
{
	FCatShopOrderResult Result;
	Result.Transaction.Command.RequestId = Command.Context.RequestId;
	// 售鱼没有实物交付这一段，Delivery 在这条链上承载的是 Items 那一步（冻结与移除）的终态。
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
	// 只放行玩家自己容器里的鱼。赃物从这条通用链卖掉会绕过 Social 的追回窗口、到店距离和归还分支，
	// 所以这里直接拒绝，而不是“顺便也支持一下”。
	if (Command.SourceKind != ECatShopFishSaleSource::PersonalGuard
		&& Command.SourceKind != ECatShopFishSaleSource::SharedFishTank)
	{
		Result.Transaction.Command.Error = ECatDomainCommandError::InvalidPayload;
		Result.Delivery.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}

	FCatFishSaleHoldCommand HoldCommand;
	HoldCommand.Context.RequestId = Command.Context.RequestId;
	HoldCommand.Context.StableNetId = Command.Context.StableNetId;
	HoldCommand.Context.ExpectedRevision = Command.ExpectedContainerRevision;
	HoldCommand.FishInstanceId = Command.FishInstanceId;
	HoldCommand.ContainerId = Command.ContainerId;
	const FCatFishSaleHoldResult Held = Items->PrepareFishSale(HoldCommand);
	// 冻结成立同样包含“这次冻的”和“同一 RequestId 之前就冻过”两种；后者必须继续往下走，
	// 否则一次网络重试会把一条已经冻住、还没卖掉的鱼永远锁死在容器里。
	const bool bHoldStanding = Held.Command.bCommitted
		|| Held.Command.Error == ECatDomainCommandError::AlreadyResolved;
	if (!bHoldStanding)
	{
		Result.Transaction.Command.Error = Held.Command.Error;
		Result.Delivery = Held.Command;
		return Result;
	}

	FCatShopFishSaleCommand SaleCommand;
	SaleCommand.Context = Command.Context;
	SaleCommand.FishInstanceId = Command.FishInstanceId;
	// 账本要的是“凭哪条 Items 记录入的账”；这条链上冻结与移除共用 SaleRequestId，它就是那条记录的引用。
	SaleCommand.ItemsCommitId = Command.Context.RequestId;
	SaleCommand.SourceKind = Command.SourceKind;
	// 重量只能取服务器上这条鱼被捕获时冻结下来的值；Items 在冻结结果里把整条鱼交回来，正是为了这一步。
	SaleCommand.WeightKilograms = Held.Fish.WeightKilograms;
	int32 AppraisedValue = 0;
	if (!Shop->TryAppraiseFishSale(SaleCommand.WeightKilograms, AppraisedValue))
	{
		// 收鱼价体重轴未裁时整条售鱼不可达。这里必须解冻：估不出价是配置事实，不是这条鱼的问题，
		// 让它一直冻着等于因为一次点击就把玩家的鱼永久锁死。
		Items->CancelPreparedFishSale(Command.Context.StableNetId, Command.ContainerId, Command.Context.RequestId);
		Result.Transaction.Command.Error = ECatDomainCommandError::PolicyUndecided;
		Result.Delivery.Error = ECatDomainCommandError::PolicyUndecided;
		return Result;
	}
	SaleCommand.SaleValue = AppraisedValue;

	Result.Transaction = Shop->ApplyFishSale(SaleCommand);
	const bool bWalletAccepted = Result.Transaction.Command.bCommitted
		|| (Result.Transaction.Command.Error == ECatDomainCommandError::AlreadyResolved
			&& Result.Transaction.Transaction.TransactionId.IsValid());
	if (!bWalletAccepted)
	{
		Items->CancelPreparedFishSale(Command.Context.StableNetId, Command.ContainerId, Command.Context.RequestId);
		Result.Delivery.Error = Result.Transaction.Command.Error;
		Result.Delivery.Revision = Result.Transaction.Command.Revision;
		return Result;
	}

	// 过了这一行钱已经进公款，从这里开始不存在任何解冻分支。
	const FCatFishSaleHoldResult Drained = Items->CommitPreparedFishSale(
		Command.Context.StableNetId, Command.ContainerId, Command.Context.RequestId);
	Result.Delivery = Drained.Command;
	if (!Drained.Command.bCommitted && Drained.Command.Error != ECatDomainCommandError::AlreadyResolved)
	{
		UE_LOG(LogCatfishing, Error,
			TEXT("Event=shop_fish_sale_drain_failed RequestId=%s ContainerId=%s Error=%s"),
			*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			*Command.ContainerId.ToString(EGuidFormats::DigitsWithHyphens),
			*UEnum::GetValueAsString(Drained.Command.Error));
	}
	return Result;
}

// 订单链流程：取依赖、在扣钱之前问完交付侧前提、下订单、判断订单是否成立、按 EntryKind 分流交付
// （装备库入库 / 角色耗材栈授予）、确认交付、回填账本状态。
FCatShopOrderResult UCatShopOrderCoordinator::RunOrder(const FCatShopPurchaseCommand& Command, const bool bFreeClaim,
	UCatEquipmentComponent* RecipientEquipment)
{
	FCatShopOrderResult Result;
	Result.Transaction.Command.RequestId = Command.Context.RequestId;
	Result.Delivery.RequestId = Command.Context.RequestId;
	UWorld* World = GetWorld();
	UCatShopEconomyService* Shop = World ? World->GetSubsystem<UCatShopEconomyService>() : nullptr;
	UCatTeamEquipmentLibrary* Library = World ? World->GetSubsystem<UCatTeamEquipmentLibrary>() : nullptr;
	if (!Shop || !Library)
	{
		Result.Transaction.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		Result.Delivery.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}

	// 交付侧的前提必须问在扣钱之前。PurchaseCatalogEntry 一提交就把钱从公款划走、把限量条目的库存也扣掉，
	// 而整个商店服务没有任何反方向的写口。本项目对这种跨聚合链的既定做法是前置 gate，不是事后补偿：
	// 补一条退款路径等于给唯一的不可逆写口开一个反向入口，而"退款算不算一笔流水、账本怎么记"产品还没裁，
	// 记待重投则要再引入一份重投队列。所以这里宁可把每条交付前提都提前问一遍，也不让钱先出去再发现东西送不出。
	// 目录里根本没有这条 EntryId 时不在这里拦：那条路购买写口自己就会拒绝且一分钱不动，它给的错误码（NotFound、
	// PolicyUndecided、CommandsClosed）比这里编一个更准确。
	FCatShopCatalogEntry Entry;
	if (Shop->TryGetCatalogEntry(Command.EntryId, Entry))
	{
		ECatDomainCommandError DeliveryRejection = ECatDomainCommandError::None;
		if (Entry.Kind == ECatShopEntryKind::RunConsumableGrant)
		{
			// 耗材要落到买家自己身上：没有身体就没有收货人，栈已经满了也塞不进去，两件事都在扣钱之前问清楚。
			DeliveryRejection = RecipientEquipment
				? RecipientEquipment->ValidateRunConsumableGrant(Command.Context.RequestId, Entry.DefinitionId, 1)
				: ECatDomainCommandError::DependencyUnavailable;
		}
		else
		{
			// 装备要进团队装备库：写口关了、或者这条 DefinitionId 在装备运行目录里查不到，都交付不出去。
			DeliveryRejection = Library->ValidateShopOrderGrant(Entry.DefinitionId);
		}
		if (DeliveryRejection != ECatDomainCommandError::None)
		{
			// 订单这一段报的是交付侧的错误码，因为订单压根没提交：钱包、商店库存和账本一个字都没动。
			// Revision 仍给当前钱包版本，调用方据此重读并决定要不要换个条件重试。
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
	if (Record.DeliveryState == ECatShopDeliveryState::Delivered)
	{
		// 已经交付过的订单不再交付第二次。装备类把当初那件实物找回来，找不到说明装备库和账本已经不同源，
		// 这时候宁可报依赖不可用，也不要凭订单再造一件实物出来；耗材类没有实物可找，账本说交付过就是交付过。
		Result.Delivery.Revision = Record.DeliveryRevision;
		Result.Delivery.Error = bRunConsumableOrder
			|| Library->TryFindInstanceBySourceTransaction(Record.TransactionId, Result.Instance)
			? ECatDomainCommandError::AlreadyResolved : ECatDomainCommandError::DependencyUnavailable;
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

	FCatTeamEquipmentGrantCommand GrantCommand;
	GrantCommand.Context.RequestId = Command.Context.RequestId;
	GrantCommand.Context.ExpectedRevision = Library->GetSnapshot().Revision;
	GrantCommand.Context.StableNetId = Command.Context.StableNetId;
	GrantCommand.SourceTransactionId = Record.TransactionId;
	GrantCommand.DefinitionId = Record.DefinitionId;
	const FCatTeamEquipmentGrantResult Grant = Library->GrantFromShopOrder(GrantCommand);
	const bool bInstanceReady = Grant.Instance.InstanceId.IsValid()
		&& (Grant.Command.bCommitted || Grant.Command.Error == ECatDomainCommandError::AlreadyResolved);
	if (!bInstanceReady)
	{
		Result.Delivery = Grant.Command;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=shop_order_grant_failed TransactionId=%s DefinitionId=%s Error=%s"),
			*Record.TransactionId.ToString(EGuidFormats::DigitsWithHyphens), *Record.DefinitionId.ToString(),
			*UEnum::GetValueAsString(Grant.Command.Error));
		return Result;
	}
	Result.Instance = Grant.Instance;
	// 实例 ID 当回执、装备库版本当下游版本：账本因此能指回具体是哪一件东西、在装备库的哪个版本之后交付的。
	Confirmation.DeliveryReceiptId = Grant.Instance.InstanceId;
	Confirmation.DeliveryRevision = Grant.Command.Revision;
	const FCatShopTransactionResult Confirmed = Shop->ConfirmTransactionDelivery(Confirmation);
	Result.Delivery = Confirmed.Command;
	if (Confirmed.Transaction.TransactionId == Record.TransactionId)
	{
		// 只回填账本记录，不覆盖订单那一段的终态：订单是不是这次才下的，和交付有没有确认成功，是两件事。
		Result.Transaction.Transaction = Confirmed.Transaction;
	}
	return Result;
}
