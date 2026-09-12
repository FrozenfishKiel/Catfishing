#include "ShopEconomy/Trading/CatShopTradeController.h"

#include "Camp/CatCampHubActor.h"
#include "Camp/CatCampInventoryActor.h"
#include "Camp/CatCampSettings.h"
#include "Character/CatCharacter.h"
#include "Condition/CatConditionComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "FishContainers/CatFishContainerSettings.h"
#include "FishContainers/CatFishGuardActor.h"
#include "FishContainers/CatFishTankActor.h"
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

	/**
	 * 一车货的全部去处。
	 *
	 * 商店册 §3.1.2 把购买物分成三种落法，工程这里一一对应：
	 *   竿、漂这类功能装备 → 公共架（EquipmentRack）
	 *   饵、窝料这类本局消耗品 → 公库（SupplyStore）
	 *   鱼缸容量升级 → 设施类，不进团队装备库，直接作用在营地那口缸上
	 * Fallback 是关卡里没人声明角色时的旧去处（唯一的营地公共仓库），保证这条改动不会让购买链断掉。
	 */
	struct FCatShopDeliveryTargets
	{
		UCatInventoryComponent* EquipmentRack = nullptr;
		UCatInventoryComponent* SupplyStore = nullptr;
		UCatInventoryComponent* Fallback = nullptr;
		ACatFishTankActor* FishTank = nullptr;
		bool bLoggedRoleFallback = false;

		/** 这一车至少要有一个能收货的去处，否则整单在扣钱之前就被拒。 */
		bool HasAnyInventoryTarget() const
		{
			return EquipmentRack != nullptr || SupplyStore != nullptr || Fallback != nullptr;
		}
	};

	/** 判定某个稳定 ID 是不是鱼缸容量升级这类设施商品；返回它对应的档位序号，不是升级商品时返回 INDEX_NONE。 */
	int32 ResolveFishTankUpgradeTier(const FName DefinitionId)
	{
		const UCatFishContainerSettings* ContainerSettings = GetDefault<UCatFishContainerSettings>();
		return ContainerSettings ? ContainerSettings->FindSharedFishTankUpgradeTierByDefinitionId(DefinitionId)
			: INDEX_NONE;
	}

	// 交付去处解析流程：
	// 1. 先按旧路径拿到营地公共仓库作为回退去处（没有它时整条购买链本来就走不通）。
	// 2. 再遍历本 World 的营地容器，按各自库存组件上声明的角色认领公共架与公库；同一角色出现多次只认第一个。
	// 3. 最后找本局共享鱼缸，作为设施类交付的收货方。
	FCatShopDeliveryTargets ResolveDeliveryTargetsForShopOrder(UWorld* World,
		ACatCampInventoryActor* FallbackInventory)
	{
		FCatShopDeliveryTargets Targets;
		Targets.Fallback = FallbackInventory ? FallbackInventory->GetInventoryComponent() : nullptr;
		if (World == nullptr)
		{
			return Targets;
		}
		for (TActorIterator<ACatCampInventoryActor> It(World); It; ++It)
		{
			ACatCampInventoryActor* CampInventory = *It;
			UCatInventoryComponent* Inventory = IsValid(CampInventory)
				? CampInventory->GetInventoryComponent() : nullptr;
			if (Inventory == nullptr || !CampInventory->HasAuthority())
			{
				continue;
			}
			switch (Inventory->GetTeamStorageRole())
			{
			case ECatTeamStorageRole::EquipmentRack:
				Targets.EquipmentRack = Targets.EquipmentRack != nullptr ? Targets.EquipmentRack : Inventory;
				break;
			case ECatTeamStorageRole::SupplyStore:
				Targets.SupplyStore = Targets.SupplyStore != nullptr ? Targets.SupplyStore : Inventory;
				break;
			default:
				break;
			}
		}
		for (TActorIterator<ACatFishTankActor> It(World); It; ++It)
		{
			if (ACatFishTankActor* Tank = *It; IsValid(Tank) && Tank->HasAuthority())
			{
				Targets.FishTank = Tank;
				break;
			}
		}
		return Targets;
	}

	/**
	 * 一行货该进哪个容器：消耗品进公库，其余备装进公共架；对应角色的容器不存在时退回单一公共仓库。
	 * 回退不是失败——公共架／公库是关卡摆位的事，代码不能因为关卡还没摆好就把购买判死。
	 */
	UCatInventoryComponent* ResolveInventoryTargetForDefinition(const UCatInventoryItemDefinition& Definition,
		FCatShopDeliveryTargets& Targets)
	{
		const UCatEquipmentDefinition* Equipment = Cast<UCatEquipmentDefinition>(&Definition);
		const bool bRunConsumable = Equipment != nullptr && Equipment->bRunConsumable;
		UCatInventoryComponent* Preferred = bRunConsumable ? Targets.SupplyStore : Targets.EquipmentRack;
		if (Preferred != nullptr)
		{
			return Preferred;
		}
		if (!Targets.bLoggedRoleFallback)
		{
			Targets.bLoggedRoleFallback = true;
			UE_LOG(LogCatfishing, Warning,
				TEXT("Event=shop_delivery_role_container_missing Role=%s Result=FellBackToSinglePublicInventory"),
				bRunConsumable ? TEXT("SupplyStore") : TEXT("EquipmentRack"));
		}
		return Targets.Fallback;
	}

	// 购物车交付批次构建流程：商店账本只保存稳定定义 ID 和数量，这里把它解析成正式库存定义批次并按去处分组；
	// 容量、实例创建和幂等仍由各自的 InventoryComponent 负责。设施类商品（鱼缸升级）不进任何库存，单独归到 OutFacilityTiers。
	bool AppendShopDeliveryEntry(const FName DefinitionId, const int32 Quantity,
		FCatShopDeliveryTargets& Targets,
		TMap<UCatInventoryComponent*, FCatInventoryReceiveBatch>& OutBatchesByTarget,
		TArray<int32>& OutFacilityTiers)
	{
		if (Quantity <= 0)
		{
			return false;
		}
		if (const int32 UpgradeTier = ResolveFishTankUpgradeTier(DefinitionId); UpgradeTier != INDEX_NONE)
		{
			// 档位不按数量叠加：一行买 N 次就是连买 N 档，逐档追加。
			for (int32 Index = 0; Index < Quantity; ++Index)
			{
				OutFacilityTiers.Add(UpgradeTier + Index);
			}
			return true;
		}
		const UCatInventorySettings* InventorySettings = GetDefault<UCatInventorySettings>();
		UCatInventoryItemDefinition* Definition = InventorySettings
			? InventorySettings->FindRuntimeDefinition(DefinitionId) : nullptr;
		if (Definition == nullptr || !Definition->IsInventoryRuntimeDefinitionReady())
		{
			return false;
		}
		UCatInventoryComponent* Target = ResolveInventoryTargetForDefinition(*Definition, Targets);
		if (Target == nullptr)
		{
			return false;
		}
		FCatInventoryDefinitionEntry& Entry = OutBatchesByTarget.FindOrAdd(Target).DefinitionEntries.AddDefaulted_GetRef();
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
	// 成交行上带着当时的商店天序号，日志跟着写一份，Playtest 不用先把服务器账本导出来就能按天切「每日余额」。
	// 一车里所有行的天序号必然相同，取第一行即可；整车被拒时没有可归日的成交，写 -1 表示不属于任何一天。
	const int32 CommittedShopDayIndex = Result.CartTransaction.Transactions.IsEmpty()
		? -1 : Result.CartTransaction.Transactions[0].ShopDayIndex;
	UE_LOG(LogCatfishing, Log,
		TEXT("Event=shop_cart_submitted RequestId=%s LineCount=%d Order=%s Delivery=%s ShopDay=%d Balance=%d"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens), Lines.Num(),
		*UEnum::GetValueAsString(Result.CartTransaction.Command.Error),
		*UEnum::GetValueAsString(Result.Delivery.Error),
		CommittedShopDayIndex, Result.CartTransaction.Wallet.Balance);
	return Result;
}

// 售鱼协调流程：先重放请求，再核对买家、来源与整批鱼身份；估价和余额预检全部通过后才进入实物与GAS提交。
// 地面鱼护一次移除所有选中格且暂不广播，入账失败恢复原格；嘴叼鱼在消费保护内执行入账回调，失败继续叼着。
// 成功结果先进入终态缓存，再通知库存观察者；拒绝按同一请求落盘，既不卖附近其他鱼护，也不信任客户端重量或价格。
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
		// 与整车购买同一口径：入账行带着当时的商店天序号就照写，没成交则 -1，好让「每日余额」两侧都能按天切。
		const int32 CommittedShopDayIndex = Result.Transaction.Transaction.TransactionId.IsValid()
			? Result.Transaction.Transaction.ShopDayIndex : -1;
		const FString Event = FString::Printf(TEXT("Event=fish_sale_order RequestId=%s Buyer=%s Guard=%s FishCount=%d Result=%s ShopDay=%d Balance=%d World=%s NetMode=%d Authority=1 LocalRole=%d Player=%s"),
			*RequestId.ToString(), *GetNameSafe(Buyer), *GetNameSafe(Guard), Guard ? FishInstanceIds.Num() : 1,
			*UEnum::GetValueAsString(Error), CommittedShopDayIndex, Result.Transaction.Wallet.Balance,
			*GetNameSafe(GetWorld()), GetWorld()->GetNetMode(),
			RequestingController ? static_cast<int32>(RequestingController->GetLocalRole()) : -1, *GetNameSafe(RequestingController));
		if (Error == ECatDomainCommandError::None || Error == ECatDomainCommandError::AlreadyResolved)
		{ UE_LOG(LogCatfishing, Log, TEXT("%s"), *Event); }
		else { UE_LOG(LogCatfishing, Warning, TEXT("%s"), *Event); }
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
	TArray<TWeakObjectPtr<ACatFishPickupActor>> SoldRetainedFishActors;
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
			if (ACatFishPickupActor* RetainedFishActor = Cast<ACatFishPickupActor>(Fish->GetWorldActor()))
			{
				SoldRetainedFishActors.AddUnique(RetainedFishActor);
			}
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
	// 经济提交已不可回滚，才销毁本次售出实例保留的容器 Actor；通用整表替换也服务于转移和回滚，不能在其中做这项清理。
	for (const TWeakObjectPtr<ACatFishPickupActor>& RetainedFishActor : SoldRetainedFishActors)
	{
		if (ACatFishPickupActor* Actor = RetainedFishActor.Get())
		{
			Actor->Destroy();
		}
	}
	Finish(ECatDomainCommandError::None);
	Inventory->BroadcastInventoryChange();
	return Result;
}

// 购物车订单链流程：
// 1. 取商店依赖和来源摊位库存，首次请求先解析整车报价，再在扣钱之前问完公共仓库整批接收前提。
// 2. 前提成立后提交整车购买，随后用购物车 RequestId 和服务器身份把完整购物车批次一次放入营地公共仓库。
//    成交与入库在同一次调用里走完，账本不留「待交付」中间态（2026-09-09 按 bug 定性，购买即入库）。
// 3. 重放时发货 payload 仍按整车账本整份重建，再撞一次公共仓库的幂等键；缩水批次会算成另一笔而被拒绝。
// 4. 入库失败时钱和货架库存已经动过，订单以拒绝返回，靠同一 RequestId 重试补发货——商店没有退款写口。
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
	// 而商店服务没有退款写口，所以首次请求必须先让每个收货去处按各自那份物品模拟一次容量和堆叠。
	// 同 RequestId 重放不跑这道前置 gate：钱和货架库存可能已经在首次提交里改变了，重试要拿回既有回执或补上入库。
	FCatShopDeliveryTargets DeliveryTargets = ResolveDeliveryTargetsForShopOrder(World, DeliveryInventory);
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
		TMap<UCatInventoryComponent*, FCatInventoryReceiveBatch> DeliveryBatches;
		TArray<int32> FacilityTiers;
		bool bDeliveryBatchReady = true;
		for (const FCatShopResolvedCartLine& Line : ResolvedCart.Lines)
		{
			bDeliveryBatchReady &= AppendShopDeliveryEntry(Line.Entry.DefinitionId, Line.DeliveryQuantity,
				DeliveryTargets, DeliveryBatches, FacilityTiers);
		}
		ECatDomainCommandError DeliveryRejection = ECatDomainCommandError::None;
		if (!DeliveryTargets.HasAnyInventoryTarget())
		{
			DeliveryRejection = ECatDomainCommandError::DependencyUnavailable;
		}
		else if (!bDeliveryBatchReady)
		{
			DeliveryRejection = ECatDomainCommandError::InvalidPayload;
		}
		else
		{
			for (const TPair<UCatInventoryComponent*, FCatInventoryReceiveBatch>& Pair : DeliveryBatches)
			{
				DeliveryRejection = Pair.Key->ValidateInventoryDefinitionBatchGrantFromAuthority(
					Command.Context.RequestId, Command.Context.StableNetId, Pair.Value);
				if (DeliveryRejection != ECatDomainCommandError::None)
				{
					break;
				}
			}
			// 设施类同样要问在扣钱之前：没有鱼缸、或这一档不是当前档的下一档，都不该先把 300／700 划走。
			if (DeliveryRejection == ECatDomainCommandError::None && FacilityTiers.Num() > 0)
			{
				if (DeliveryTargets.FishTank == nullptr)
				{
					DeliveryRejection = ECatDomainCommandError::DependencyUnavailable;
				}
				else if (!DeliveryTargets.FishTank->CanApplyCapacityUpgradeSequenceFromAuthority(
					FacilityTiers, Command.Context.RequestId))
				{
					// 一车里可以连买两档，所以这里按整串核，不是逐档各问一次当前档。
					DeliveryRejection = ECatDomainCommandError::InvalidPhase;
				}
			}
		}
		if (DeliveryRejection != ECatDomainCommandError::None)
		{
			// 订单这一段报的是交付侧的错误码，因为订单压根没提交：公款、商店库存和账本一个字都没动。
			// Revision 仍给当前公款版本，调用方据此重读并决定要不要换个条件重试。
			Result.CartTransaction.Wallet = Shop->GetWalletSnapshot();
			Result.CartTransaction.Command.Error = DeliveryRejection;
			Result.CartTransaction.Command.Revision = Result.CartTransaction.Wallet.Revision;
			Result.Delivery.Error = DeliveryRejection;
			UE_LOG(LogCatfishing, Warning,
				TEXT("Event=shop_cart_delivery_precheck_rejected RequestId=%s Targets=%d FacilityLines=%d Error=%s"),
				*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
				DeliveryBatches.Num(), FacilityTiers.Num(),
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
	if (!DeliveryTargets.HasAnyInventoryTarget())
	{
		Result.Delivery.Error = ECatDomainCommandError::DependencyUnavailable;
		UE_LOG(LogCatfishing, Warning,
			TEXT("Event=shop_cart_camp_inventory_grant_failed RequestId=%s Error=NoCampInventory"),
			*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens));
		return Result;
	}

	TMap<UCatInventoryComponent*, FCatInventoryReceiveBatch> DeliveryBatches;
	TArray<int32> FacilityTiers;
	for (const FCatShopTransactionRecord& Record : Result.CartTransaction.Transactions)
	{
		if (!Record.bPurchase || Record.DefinitionId.IsNone() || Record.PurchaseQuantity <= 0)
		{
			Result.Delivery.Error = ECatDomainCommandError::InvalidPhase;
			return Result;
		}
		if (!AppendShopDeliveryEntry(Record.DefinitionId, Record.PurchaseQuantity,
			DeliveryTargets, DeliveryBatches, FacilityTiers))
		{
			Result.Delivery.Error = ECatDomainCommandError::InvalidPayload;
			return Result;
		}
	}

	// 入库和重放走同一次调用：整车 RequestId 就是每个收货容器各自的幂等键，首次写入返回 committed，重试返回 AlreadyResolved。
	// 账本这边没有第二个阶段要推进，所以这里拿到什么就是整单交付的终态。
	// 分成两个去处不改变幂等语义：同一个号在公共架和公库各撞各的键，互不影响。
	FCatDomainCommandResult Delivery;
	Delivery.RequestId = Command.Context.RequestId;
	Delivery.bCommitted = true;
	int32 DeliveredLineCount = 0;
	for (const TPair<UCatInventoryComponent*, FCatInventoryReceiveBatch>& Pair : DeliveryBatches)
	{
		const FCatDomainCommandResult Grant = Pair.Key->GrantInventoryDefinitionBatchFromAuthority(
			Command.Context.RequestId, Command.Context.StableNetId, Pair.Value);
		DeliveredLineCount += Pair.Value.DefinitionEntries.Num();
		if (!CatIsAcceptedDomainCommandResult(Grant))
		{
			Result.Delivery = Grant;
			Result.Delivery.RequestId = Command.Context.RequestId;
			UE_LOG(LogCatfishing, Warning,
				TEXT("Event=shop_cart_camp_inventory_grant_failed RequestId=%s Target=%s LineCount=%d Error=%s"),
				*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
				*GetNameSafe(Pair.Key->GetOwner()), Pair.Value.DefinitionEntries.Num(),
				*UEnum::GetValueAsString(Grant.Error));
			return Result;
		}
		Delivery.Revision = FMath::Max(Delivery.Revision, Grant.Revision);
	}

	// 设施类交付：鱼缸容量升级不进团队装备库，直接作用在营地那口缸上（商店册 §3.1.2）。
	// 一车里连买两档时必须从低到高提交，否则第二档会因为「不是当前档的下一档」被拒。
	FacilityTiers.Sort();
	for (const int32 Tier : FacilityTiers)
	{
		if (DeliveryTargets.FishTank == nullptr
			|| !DeliveryTargets.FishTank->ApplyCapacityUpgradeFromAuthority(Tier, Command.Context.RequestId))
		{
			Result.Delivery.Error = ECatDomainCommandError::DependencyUnavailable;
			Result.Delivery.RequestId = Command.Context.RequestId;
			UE_LOG(LogCatfishing, Warning,
				TEXT("Event=shop_cart_facility_delivery_failed RequestId=%s Tier=%d Tank=%s"),
				*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens), Tier,
				*GetNameSafe(DeliveryTargets.FishTank));
			return Result;
		}
	}

	Result.Delivery = Delivery;
	UE_LOG(LogCatfishing, Log,
		TEXT("Event=shop_cart_delivered RequestId=%s Targets=%d Lines=%d FacilityLines=%d"),
		*Command.Context.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		DeliveryBatches.Num(), DeliveredLineCount, FacilityTiers.Num());
	return Result;
}
