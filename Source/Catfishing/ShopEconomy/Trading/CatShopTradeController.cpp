#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Tags/CatStateTags.h"
#include "ShopEconomy/Trading/CatShopTradeController.h"

#include "Inventory/CatInventorySettings.h"
#include "Camp/CatCampHubActor.h"
#include "Camp/CatCampInventoryActor.h"
#include "Camp/CatCampSettings.h"
#include "Character/CatCharacter.h"
#include "Condition/CatConditionComponent.h"
#include "Equipment/CatEquipmentItemDefinition.h"
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
#include "Logging/CatLog.h"
#include "ShopEconomy/CatShopCartCommandUtils.h"
#include "ShopEconomy/CatShopEconomyService.h"
#include "ShopEconomy/CatShopEconomySettings.h"
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
		/** 本车装备的公共架收货方；世界解析时按库存角色填写，商品分流读取，空值表示该角色未配置。 */
		UCatInventoryComponent* EquipmentRack = nullptr;
		/** 本车消耗品的公库收货方；世界解析时按库存角色填写，消耗品交付读取，空值表示缺少公库。 */
		UCatInventoryComponent* SupplyStore = nullptr;
		/** 未分角色关卡的唯一公共仓库候选；解析时填写并在多仓或已有角色时清空，分流只在专属目标缺失时读取。 */
		UCatInventoryComponent* Fallback = nullptr;
		/** 本车设施升级对应的共享鱼缸；世界解析填写，升级预检与提交读取，空值使设施商品无法交付。 */
		ACatFishTankActor* FishTank = nullptr;
		/** 本车是否已输出缺收货角色日志；商品分流首次遇到缺角色时置真，抑制后续行重复日志，不影响交付判据。 */
		bool bLoggedRoleFallback = false;
		/** 本车收货对象是否存在角色或共享鱼缸重复；世界解析发现重复时置真，订单协调器据此在交付前拒绝。 */
		bool bAmbiguous = false;

	};

	/** 判定某个稳定 ID 是不是鱼缸容量升级这类设施商品；返回它对应的档位序号，不是升级商品时返回 INDEX_NONE。 */
	int32 ResolveFishTankUpgradeTier(const int32  ItemId)
	{
		const UCatFishContainerSettings* ContainerSettings = GetDefault<UCatFishContainerSettings>();
		return ContainerSettings ? ContainerSettings->FindSharedFishTankUpgradeTierByItemId(ItemId)
			: INDEX_NONE;
	}

	// 交付去处解析流程：
	// 1. 保留旧单仓的显式 Hub 绑定；仅世界仍为一个未分角色仓库时允许兼容回退。
	// 2. 再遍历本 World 的营地容器，按各自库存组件上声明的角色认领公共架与公库；同一角色出现多次拒绝交付，不能按遍历次序选仓。
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
		int32 CampCount = 0;
		for (TActorIterator<ACatCampInventoryActor> It(World); It; ++It)
		{
			ACatCampInventoryActor* CampInventory = *It;
			UCatInventoryComponent* Inventory = IsValid(CampInventory)
				? CampInventory->GetInventoryComponent() : nullptr;
			if (Inventory == nullptr || !CampInventory->HasAuthority())
			{
				continue;
			}
			++CampCount;
			switch (Inventory->GetTeamStorageRole())
			{
			case ECatTeamStorageRole::EquipmentRack:
				Targets.bAmbiguous |= Targets.EquipmentRack != nullptr;
				Targets.EquipmentRack = Inventory;
				break;
			case ECatTeamStorageRole::SupplyStore:
				Targets.bAmbiguous |= Targets.SupplyStore != nullptr;
				Targets.SupplyStore = Inventory;
				break;
			default:
				break;
			}
		}
		if (CampCount != 1 || Targets.EquipmentRack || Targets.SupplyStore) Targets.Fallback = nullptr;
		for (TActorIterator<ACatFishTankActor> It(World); It; ++It)
		{
			if (ACatFishTankActor* Tank = *It; IsValid(Tank) && Tank->HasAuthority())
			{
				Targets.bAmbiguous |= Targets.FishTank != nullptr;
				Targets.FishTank = Tank;
			}
		}
		return Targets;
	}

	/**
	 * 一行货该进哪个容器：消耗品进公库，其余备装进公共架；对应角色的容器不存在时退回单一公共仓库。
	 * 多仓或已声明角色后缺收货方会拒绝；不能在迁移后把消耗品悄悄送到公共架。
	 */
	UCatInventoryComponent* ResolveInventoryTargetForDefinition(const UCatInventoryItemDefinition& Definition,
		FCatShopDeliveryTargets& Targets)
	{
		const UCatEquipmentItemDefinition* Equipment = Cast<UCatEquipmentItemDefinition>(&Definition);
		const bool bRunConsumable = (Equipment != nullptr && Equipment->bRunConsumable)
			|| Definition.GetItemId() == GetDefault<UCatShopEconomySettings>()->SettlementDriedItemId;
		UCatInventoryComponent* Preferred = bRunConsumable ? Targets.SupplyStore : Targets.EquipmentRack;
		if (Preferred != nullptr)
		{
			return Preferred;
		}
		if (!Targets.bLoggedRoleFallback)
		{
			Targets.bLoggedRoleFallback = true;
			UE_LOG(LogCatfishing, Warning,
				TEXT("Event=shop_delivery_role_container_missing Role=%s Result=%s"),
				bRunConsumable ? TEXT("SupplyStore") : TEXT("EquipmentRack"),
				Targets.Fallback ? TEXT("FellBackToSinglePublicInventory") : TEXT("DeliveryTargetMissing"));
		}
		return Targets.Fallback;
	}

	// 购物车交付批次构建流程：商店账本只保存稳定定义 ID 和数量，这里把它解析成正式库存定义批次并按去处分组；
	// 容量、实例创建和幂等仍由各自的 InventoryComponent 负责。设施类商品（鱼缸升级）不进任何库存，单独归到 OutFacilityTiers。
	bool AppendShopDeliveryEntry(const int32  ItemId, const int32 Quantity,
		FCatShopDeliveryTargets& Targets,
		TMap<UCatInventoryComponent*, FCatInventoryReceiveBatch>& OutBatchesByTarget,
		TArray<int32>& OutFacilityTiers)
	{
		if (Quantity <= 0)
		{
			return false;
		}
		if (const int32 UpgradeTier = ResolveFishTankUpgradeTier(ItemId); UpgradeTier != INDEX_NONE)
		{
			// 一件商品对应一个定价档位；不能用第一档的价格连升两档。
			if (Quantity != 1) return false;
			OutFacilityTiers.Add(UpgradeTier);
			return true;
		}
		const UCatInventorySettings* InventorySettings = GetDefault<UCatInventorySettings>();
		UCatInventoryItemDefinition* Definition = InventorySettings
			? InventorySettings->FindRuntimeDefinition(ItemId) : nullptr;
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
	ACatShopKioskActor* ShopKiosk, const TArray<FCatShopCartLineCommand>& Lines, const FGuid RequestId)
{
	// 摊位购物车提交流程：
	// 1. 先重读服务器玩法 gate 和原始 RPC 载荷大小，拒绝无效局状态或异常购物车。
	// 2. 再从请求 Controller 重建稳定玩家身份，并要求摊位与玩家处于同一 World 且摊位仍启用；已打开页面不再限制下单距离。
	// 3. 摊位只给来源货架库存，营地收货仓库由 ShopEconomy 在 World 中解析，Controller 只保留交易意图。
	// 4. 将来源与收货库存交经济服务统一成交；实物回执直接采用成交终态，所有前置失败也带结果回到 UI。
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
	// 墓碑（2026-09-13）：下单不再二次检查摊位距离，货架仍从本 World 的来源摊位解析。
	if (World && IsValid(ShopKiosk) && ShopKiosk->GetWorld() == World
		&& ShopKiosk->CanServeOrderFromAuthority(RequestingController))
	{
		ShopInventory = ShopKiosk->GetShopInventory();
	}
	ACatCampInventoryActor* DeliveryInventory = ResolveDeliveryInventoryForShopOrder(World);
	if (!CurrentPlayerState || !CurrentPlayerState->GetUniqueId().IsValid()
		|| !ShopInventory || !ShopInventory->GetShopInventoryId().IsValid())
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
	Command.Context.StableNetId = CurrentPlayerState->GetUniqueId()->ToString();
	Command.ShopInventoryId = ShopInventory->GetShopInventoryId();
	Command.Lines = Lines;
	UCatShopEconomyService* Shop = World->GetSubsystem<UCatShopEconomyService>();
	if (Shop)
	{
		Result = RunCartOrder(Command, ShopInventory, DeliveryInventory);
	}
	else
	{
		Result.CartTransaction.Command.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	Result.Delivery = Result.CartTransaction.Command;
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
	if (!Character->GetConditionComponent() || Character->GetCatAbilitySystemComponent()->HasMatchingGameplayTag(CatStateTags::Downed))
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
			Line.ItemId = Fish->GetItemId();
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
		Line.ItemId = Fish.ItemId;
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

// 整车事务：先静默交付，再扣公款；任何交付或扣款拒绝都恢复所有收货方，经济服务同时恢复货架。
// 成功账本和一车一通知由经济服务在完整提交后发布，库存观察者随后才收到变化。
FCatShopOrderResult UCatShopTradeController::RunCartOrder(const FCatShopCartCommand& Command,
	UCatShopInventoryComponent* ShopInventory, ACatCampInventoryActor* DeliveryInventory)
{
	FCatShopOrderResult Result;
	Result.CartTransaction.Command.RequestId = Command.Context.RequestId;
	Result.Delivery.RequestId = Command.Context.RequestId;
	UWorld* World = GetWorld();
	auto* Shop = World ? World->GetSubsystem<UCatShopEconomyService>() : nullptr;
	if (!Shop || !ShopInventory)
	{
		Result.CartTransaction.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		Result.Delivery = Result.CartTransaction.Command;
		return Result;
	}
	// 已有终态只重放完整订单，不读取可能已被玩家取走的交付物，也不会补发。
	if (Shop->HasCatalogCartTerminal(Command))
	{
		Result.CartTransaction = Shop->PurchaseCatalogCart(Command, ShopInventory, [](TFunctionRef<bool()> Pay) { return false; });
		Result.Delivery = Result.CartTransaction.Command;
		return Result;
	}
	FCatShopResolvedCart Quote;
	ECatDomainCommandError Error;
	if (!Shop->ResolveCatalogCartForAuthority(Command, ShopInventory, Quote, Error))
	{
		Result.CartTransaction.Command.Error = Error;
		Result.CartTransaction.Command.FailureReason = Quote.FailureReason;
		Result.CartTransaction.Wallet = Shop->GetWalletSnapshot();
		Result.Delivery = Result.CartTransaction.Command;
		return Result;
	}
	auto Targets = ResolveDeliveryTargetsForShopOrder(World, DeliveryInventory);
	if (Targets.bAmbiguous)
	{
		Result.CartTransaction.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		Result.CartTransaction.Command.FailureReason = TEXT("DeliveryAmbiguous");
		Result.Delivery = Result.CartTransaction.Command;
		return Result;
	}
	TMap<UCatInventoryComponent*, FCatInventoryReceiveBatch> Batches;
	TArray<int32> Tiers;
	for (const auto& Line : Quote.Lines)
	{
		if (!AppendShopDeliveryEntry(Line.Entry.ItemId, Line.DeliveryQuantity, Targets, Batches, Tiers))
		{
			Result.CartTransaction.Command.Error = ECatDomainCommandError::DependencyUnavailable;
			Result.CartTransaction.Command.FailureReason = TEXT("DeliveryUnavailable");
			Result.Delivery = Result.CartTransaction.Command;
			return Result;
		}
	}
	Tiers.Sort();
	if (!Tiers.IsEmpty() && (!Targets.FishTank || !Targets.FishTank->CanApplyCapacityUpgradeSequenceFromAuthority(Tiers, Command.Context.RequestId)))
	{
		Result.CartTransaction.Command.Error = ECatDomainCommandError::InvalidPhase;
		Result.CartTransaction.Command.FailureReason = TEXT("FacilityUpgradeInvalid");
		Result.Delivery = Result.CartTransaction.Command;
		return Result;
	}
	TMap<UCatInventoryComponent*, TArray<FCatInventoryEntry>> Before;
	for (const auto& Pair : Batches)
	{
		if (!Pair.Key->CanFullyAcceptInventoryBatch(Pair.Value))
		{
			Result.CartTransaction.Command.Error = ECatDomainCommandError::CapacityExceeded;
			Result.CartTransaction.Command.FailureReason = TEXT("DeliveryCapacity");
			Result.Delivery = Result.CartTransaction.Command;
			return Result;
		}
		Before.Add(Pair.Key, Pair.Key->GetInventoryEntries());
	}
	const int32 PreviousTier = Targets.FishTank ? Targets.FishTank->GetCapacityTier() : 0;
	TArray<FCatInventoryEntry> TankBefore;
	TSet<FGuid> UpgradeIdsBefore;
	if (Targets.FishTank && !Tiers.IsEmpty())
	{
		TankBefore = Targets.FishTank->FishInventory->GetInventoryEntries();
		UpgradeIdsBefore = Targets.FishTank->CommittedUpgradeRequestIds;
	}
	const auto Rollback = [&]()
	{
		for (const auto& Pair : Before)
			verify(Pair.Key->ReplaceInventoryEntriesFromAuthority(Pair.Value, Pair.Value.Num(), false));
		if (Targets.FishTank && !Tiers.IsEmpty())
		{
			Targets.FishTank->CapacityTier = PreviousTier;
			Targets.FishTank->CommittedUpgradeRequestIds = UpgradeIdsBefore;
			auto* Inventory = Targets.FishTank->FishInventory.Get();
			verify(Inventory->ReplaceInventoryEntriesFromAuthority(TankBefore, TankBefore.Num(), false));
			Inventory->SetInventorySlotCountFromAuthority(TankBefore.Num(), false);
		}
		UE_LOG(LogCatfishing, Warning, TEXT("Event=shop_cart_rolled_back RequestId=%s World=%s NetMode=%d Authority=1 Targets=%d Wallet=Unchanged Stock=RestoredByEconomy"),
			*Command.Context.RequestId.ToString(), *GetNameSafe(World), World->GetNetMode(), Before.Num());
	};
	Result.CartTransaction = Shop->PurchaseCatalogCart(Command, ShopInventory, [&](TFunctionRef<bool()> Pay)
	{
		int32 Step = 0;
		for (const auto& Pair : Batches)
		{
#if WITH_DEV_AUTOMATION_TESTS
			if (FailDeliveryStepForTest == Step++) { Rollback(); return false; }
#endif
			if (!Pair.Key->TryAddInventoryBatch(Pair.Value, false)) { Rollback(); return false; }
		}
		for (const int32 Tier : Tiers)
		{
#if WITH_DEV_AUTOMATION_TESTS
			if (FailDeliveryStepForTest == Step++) { Rollback(); return false; }
#endif
			const FGuid ItemId = FGuid::NewDeterministicGuid(FString::Printf(TEXT("CartUpgrade|%s|%d"),
				*Command.Context.RequestId.ToString(), Tier));
			if (!Targets.FishTank->ApplyCapacityUpgradeFromAuthority(Tier, ItemId, false)) { Rollback(); return false; }
		}
		if (!Pay()) { Rollback(); return false; }
		return true;
	});
	Result.Delivery = Result.CartTransaction.Command;
	if (Result.CartTransaction.Command.bCommitted)
	{
		for (const auto& Pair : Batches) Pair.Key->BroadcastInventoryChange();
		if (Targets.FishTank && !Tiers.IsEmpty())
		{
			Targets.FishTank->FishInventory->BroadcastInventoryChange();
			Targets.FishTank->ForceNetUpdate();
		}
		UE_LOG(LogCatfishing, Log, TEXT("Event=shop_cart_delivered RequestId=%s World=%s NetMode=%d Authority=1 Targets=%d FacilityLines=%d"),
			*Command.Context.RequestId.ToString(), *GetNameSafe(World), World->GetNetMode(), Batches.Num(), Tiers.Num());
	}
	return Result;
}
