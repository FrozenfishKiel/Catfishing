#include "Inventory/CatFishInventoryItemInstance.h"

#include "Character/CatCharacter.h"
#include "Collection/CatFishCollectionLayers.h"
#include "Collection/CatRunImprintService.h"
#include "Condition/CatConditionComponent.h"
#include "Data/CatFishDefinition.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"
#include "Inventory/CatInventoryComponent.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "Net/UnrealNetwork.h"
#include "Inventory/CatInventoryStatics.h"
#include "FishContainers/CatFishGuardActor.h"
#include "FishContainers/CatFishTankActor.h"
#include "Inventory/CatFishOnlyInventoryComponent.h"
#include "ShopEconomy/CatFishBuyerActor.h"
#include "ShopEconomy/Trading/CatShopTradeController.h"
#include "Interaction/CatInteractable.h"
#include "Engine/World.h"

// 鱼专属查询复核定义和真实容器；只读嘴部占用或买家资格，客户端展示不能替代服务器交易验证。
bool UCatFishInventoryItemInstance::CanExecuteInventoryAction(const FGameplayTag& Action,
	const FCatInventoryEntry& Entry, APawn* UserPawn, FText& OutReason) const
{
	if (Action != CatInventoryActionTags::Carry && Action != CatInventoryActionTags::Sell)
		return Super::CanExecuteInventoryAction(Action, Entry, UserPawn, OutReason);
	OutReason = NSLOCTEXT("CatInventory", "FishActionUnavailable", "当前容器或身体状态不允许此操作");
	ACatCharacter* Character = Cast<ACatCharacter>(UserPawn);
	if (!Character || !Character->GetConditionComponent() || Character->GetConditionComponent()->GetSnapshot().bDowned
		|| Entry.Instance != this || Entry.StackCount != 1 || !GetItemDefinition()
		|| !GetItemDefinition()->InventoryActions.ContainsByPredicate([&](const FCatInventoryActionDefinition& Row) { return Row.Action == Action; })) return false;
	UCatInventoryComponent* Inventory = Entry.SlotOwnerComponent;
	AActor* Host = Inventory ? Inventory->GetOwner() : nullptr;
	ACatFishGuardActor* Guard = Cast<ACatFishGuardActor>(Host);
	ACatFishTankActor* Tank = Cast<ACatFishTankActor>(Host);
	if (Action == CatInventoryActionTags::Carry)
	{
		if (Character->GetMouthCarriedActor())
		{ OutReason = NSLOCTEXT("CatInventory", "MouthOccupied", "嘴里已经有物品"); return false; }
		if ((!Guard || Guard->GetFishInventoryComponent() != Inventory) && (!Tank || Tank->GetFishInventoryComponent() != Inventory)) return false;
		if (!Host->Implements<UCatInteractable>() || !ICatInteractable::Execute_CanInteract(Host, Character->GetController())) return false;
	}
	else
	{
		ACatFishBuyerActor* Buyer = Guard ? ACatFishBuyerActor::FindAvailableBuyer(Character->GetController(), Guard) : nullptr;
		int32 Price = 0;
		if (!Buyer || !Buyer->TryAppraiseFish(const_cast<UCatFishInventoryItemInstance*>(this), Price))
		{ OutReason = NSLOCTEXT("CatInventory", "NoFishBuyer", "附近没有可以收购这条鱼的买家"); return false; }
	}
	OutReason = FText::GetEmpty(); return true;
}

// 只扩展鱼的出售标识，公共动作仍由基类调用虚函数；不在RPC添加类型分支。
FCatDomainCommandResult UCatFishInventoryItemInstance::ExecuteInventoryActionFromAuthority(const FGameplayTag& Action,
	const FCatInventoryEntry& Entry, const FCatInventoryItemUseContext& Context, const int32 Quantity)
{
	if (Action == CatInventoryActionTags::Sell) return SellFromInventoryFromAuthority(Entry, Context);
	return Super::ExecuteInventoryActionFromAuthority(Action, Entry, Context, Quantity);
}

// 叼鱼沿用已验证的库存世界事务；其内部继续验证容器、原载体和嘴部占用，失败时保留库存鱼。
FCatDomainCommandResult UCatFishInventoryItemInstance::CarryFromInventoryFromAuthority(const FCatInventoryEntry& Entry,
	const FCatInventoryItemUseContext& Context)
{
	return Context.SourceInventory->ReleaseItemToWorldFromAuthority(Cast<ACatCharacter>(Context.UserPawn),
		Context.RequestId, Context.InventorySlotIndex, GetItemInstanceId(), 1, ECatInventoryWorldAction::Carry);
}

// 单鱼出售只固定本实例身份；服务器交易控制器重新校验地面鱼护、买家和价格后扣鱼入账。
FCatDomainCommandResult UCatFishInventoryItemInstance::SellFromInventoryFromAuthority(const FCatInventoryEntry& Entry,
	const FCatInventoryItemUseContext& Context)
{
	FCatDomainCommandResult Result; Result.RequestId = Context.RequestId;
	ACatFishGuardActor* Guard = Context.SourceInventory ? Cast<ACatFishGuardActor>(Context.SourceInventory->GetOwner()) : nullptr;
	ACatFishBuyerActor* Buyer = Guard ? ACatFishBuyerActor::FindAvailableBuyer(Context.RequestingController, Guard) : nullptr;
	UCatShopTradeController* Trading = Context.UserPawn && Context.UserPawn->HasAuthority()
		? Context.UserPawn->GetWorld()->GetSubsystem<UCatShopTradeController>() : nullptr;
	if (!Trading || !Buyer) { Result.Error = ECatDomainCommandError::PermissionDenied; return Result; }
	return Trading->SubmitFishSaleFromPlayer(Context.RequestingController, Buyer, Guard, {GetItemInstanceId()}, Context.RequestId).Delivery;
}

// 鱼实例构造流程：基础实例已分配 ItemInstanceId；鱼专属字段等捕获提交时再由服务器写入。
UCatFishInventoryItemInstance::UCatFishInventoryItemInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

// 复制声明流程：来源会话和重量是公开鱼物品状态，捕获者 StableNetId 不注册复制以避免身份泄漏到客户端。
void UCatFishInventoryItemInstance::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, SourceFishingSessionId);
	DOREPLIFETIME(ThisClass, WeightKilograms);
}

// 鱼状态初始化流程：
// 1. 只允许服务器或尚未绑定运行宿主的构造期写入，客户端不能伪造鱼身份。
// 2. 逐项验证来源会话、鱼实例、捕获者身份和重量，任一缺失都保持实例未初始化。
// 3. 成功后复用基础实例的 authority 身份写口，再写鱼专属字段。
bool UCatFishInventoryItemInstance::InitializeFishFromAuthority(const FGuid InFishingSessionId,
	const FGuid InFishInstanceId, const FString& InOwnerStableNetId, const double InWeightKilograms)
{
	AActor* RuntimeOwner = GetRuntimeOwnerActor();
	if ((RuntimeOwner != nullptr && !RuntimeOwner->HasAuthority())
		|| !InFishingSessionId.IsValid()
		|| !InFishInstanceId.IsValid()
		|| InFishingSessionId == InFishInstanceId
		|| InOwnerStableNetId.IsEmpty()
		|| !FMath::IsFinite(InWeightKilograms)
		|| InWeightKilograms <= 0.0)
	{
		return false;
	}

	SetItemInstanceIdFromAuthority(InFishInstanceId);
	SourceFishingSessionId = InFishingSessionId;
	OwnerStableNetId = InOwnerStableNetId;
	WeightKilograms = InWeightKilograms;
	return GetItemInstanceId() == InFishInstanceId;
}

// 鱼定义读取流程：运行实例只承认 UCatFishDefinition；普通库存定义不能伪装成可吃鱼。
UCatFishDefinition* UCatFishInventoryItemInstance::GetFishDefinition() const
{
	return Cast<UCatFishDefinition>(GetItemDefinition());
}

// 来源会话读取流程：返回捕获时冻结的 FishingSession ID，不尝试查找已经结束的会话对象。
FGuid UCatFishInventoryItemInstance::GetSourceFishingSessionId() const
{
	return SourceFishingSessionId;
}

// 捕获者身份读取流程：只返回服务器内存字段；客户端收到的实例自然为空字符串。
const FString& UCatFishInventoryItemInstance::GetFishOwnerStableNetId() const
{
	return OwnerStableNetId;
}

// 重量读取流程：返回单条鱼冻结重量；调用方必须自行处理无效或未初始化实例。
double UCatFishInventoryItemInstance::GetFishWeightKilograms() const
{
	return WeightKilograms;
}

// 鱼 Use 裁决流程：复用通用扣量语义后，再确认鱼专属字段和当前实例完全一致。
ECatDomainCommandError UCatFishInventoryItemInstance::Use(const FCatInventoryEntry& Item,
	const int32 Quantity) const
{
	const ECatDomainCommandError BaseError = Super::Use(Item, Quantity);
	if (BaseError != ECatDomainCommandError::None)
	{
		return BaseError;
	}

	const UCatFishDefinition* Definition = GetFishDefinition();
	if (Definition == nullptr
		|| Item.Instance != this || Item.StackCount != 1
		|| Quantity != 1
		|| !SourceFishingSessionId.IsValid() || OwnerStableNetId.IsEmpty()
		|| !FMath::IsFinite(WeightKilograms) || WeightKilograms <= 0.0)
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	return ECatDomainCommandError::None;
}

// 鱼扣量策略读取流程：吃鱼会真实移除这一条库存鱼，不借出、不创建世界 Actor。
bool UCatFishInventoryItemInstance::ConsumesInventoryQuantityOnUse() const
{
	return true;
}

// 鱼使用预检核对公开实例数据；服务器额外检查私有捕获身份与 Condition 的权威成长预检。
// 客户端只展示公开数据能确定的可用性，不能调用仅允许 authority 的 Condition 入口，否则所有客户端的食用都会置灰。
bool UCatFishInventoryItemInstance::CanUseFromInventory(
	const FCatInventoryEntry& InventoryEntry, APawn* UserPawn) const
{
	const UCatFishDefinition* Definition = GetFishDefinition();
	const ACatCharacter* Character = Cast<ACatCharacter>(UserPawn);
	const UCatConditionComponent* Condition = Character ? Character->GetConditionComponent() : nullptr;
	return InventoryEntry.Instance == this
		&& InventoryEntry.StackCount == 1
		&& GetItemInstanceId().IsValid()
		&& SourceFishingSessionId.IsValid()
		&& (Character != nullptr && (!Character->HasAuthority() || !OwnerStableNetId.IsEmpty()))
		&& FMath::IsFinite(WeightKilograms)
		&& WeightKilograms > 0.0
		&& Definition != nullptr
		&& Definition->IsRuntimeDefinitionReady()
		// 不可食用的鱼（咸鱼、湖心巨影）在这里直接拒绝：理由是「它不能吃」，
		// 不是绕道去看经验系数是不是 0——那是两件事，只是在鱼表里恰好同时成立。
		&& Definition->IsEdible()
		&& Condition != nullptr
		&& (!Character->HasAuthority() || Condition->ValidateFishConsumption(Definition, WeightKilograms) == ECatDomainCommandError::None);
}

namespace CatFishInventoryConsumePrivate
{
	// 知识层授予流程：从吃鱼的猫解析服务器私有身份，交给一局图鉴服务生成 FishKnowledge Grant。
	// 授予按「接收者+鱼种」去重，吃第二条同种鱼不会再生成待 ACK 的 Grant；本函数不改身体状态也不碰库存。
	void GrantFishKnowledgeFromAuthority(const ACatCharacter* EatingCharacter, const UCatFishDefinition* Definition)
	{
		if (!CatFishCollectionLayers::HasKnowledgeLayer(Definition) || !EatingCharacter)
		{
			return;
		}
		const AController* Controller = EatingCharacter->GetController();
		const APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
		UCatRunImprintService* Imprint = EatingCharacter->GetWorld()
			? EatingCharacter->GetWorld()->GetSubsystem<UCatRunImprintService>() : nullptr;
		if (!Imprint || !PlayerState || !PlayerState->GetUniqueId().IsValid())
		{
			return;
		}
		Imprint->RecordFishKnowledge(Definition->FishDefinitionId, PlayerState->GetUniqueId()->ToString());
	}
}

// 鱼库存 Use 提交流程：
// 1. 复核来源库存、使用者和鱼定义，失败时不进入库存扣量。
// 2. 让正式库存按当前槽位和实例 ID 扣除这一条鱼。
// 3. 库存扣除成功后才提交 Character Condition 的吃鱼效果；身体失败时库存入口会回滚刚才的扣量。
// 4. 返回公共领域结果给 UI，结果只描述本次吃鱼命令的提交或失败。
FCatDomainCommandResult UCatFishInventoryItemInstance::UseFromInventorySlotFromAuthority(
	const FCatInventoryEntry& InventoryEntry, const FCatInventoryItemUseContext& UseContext)
{
	FCatDomainCommandResult Result;
	Result.RequestId = UseContext.RequestId;


	ACatCharacter* Character = Cast<ACatCharacter>(UseContext.UserPawn);
	if (Character == nullptr && UseContext.RequestingController != nullptr)
	{
		Character = Cast<ACatCharacter>(UseContext.RequestingController->GetPawn());
	}
	UCatConditionComponent* Condition = Character ? Character->GetConditionComponent() : nullptr;
	UCatFishDefinition* Definition = GetFishDefinition();
	if (InventoryEntry.Instance != this || InventoryEntry.StackCount != 1
		|| UseContext.SourceInventory == nullptr || Character == nullptr || Condition == nullptr
		|| Definition == nullptr || !UseContext.RequestId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}

	const FString PayloadContext = FString::Printf(TEXT("Fish=%s|Definition=%s|SourceSession=%s"),
		*GetItemInstanceId().ToString(EGuidFormats::DigitsWithHyphens),
		*Definition->FishDefinitionId.ToString(),
		*SourceFishingSessionId.ToString(EGuidFormats::DigitsWithHyphens));
	const FCatInventoryItemUseResult InventoryResult =
		UseContext.SourceInventory->UseItemInstanceFromAuthority(UseContext.RequestId,
			GetItemInstanceId(), 1, PayloadContext,
			[&](FCatInventoryItemUseResult& MutableResult)
			{
				(void)MutableResult;
				return CanUseFromInventory(InventoryEntry, Character)
					? ECatDomainCommandError::None : ECatDomainCommandError::DependencyUnavailable;
			},
			[&](FCatInventoryItemUseResult& MutableResult)
			{
				(void)MutableResult;
				// 吃鱼经验 ＝ 经验系数 × 实际重量，重量只有鱼实例知道，所以必须由这里一路带到成长槽。
				const FCatDomainCommandResult BodyResult =
					Condition->ConsumeCommittedFish(UseContext.RequestId, Definition, WeightKilograms);
				if (!CatIsAcceptedDomainCommandResult(BodyResult))
				{
					return false;
				}
				// 知识层：自己吃过才解锁食用效果，谁吃谁记（图鉴 §3.1.4:124）。
				// 收件人是这次真的把鱼吃下去的人，不是钓到它的人——别人钓的鱼被自己吃掉，效果记进自己的图鉴；
				// 被拿走吃掉就记进拿鱼那个人的。不可食用的鱼没有这一层，这里也不会走到（吃鱼链本身 fail-closed）。
				CatFishInventoryConsumePrivate::GrantFishKnowledgeFromAuthority(Character, Definition);
				// 身体效果已经接受，库存不会再回滚这条鱼；此时才释放容器保管的隐藏 Actor，避免失败回滚留下失配的库存条目。
				if (ACatFishPickupActor* RetainedFishActor = Cast<ACatFishPickupActor>(GetWorldActor()))
				{
					RetainedFishActor->Destroy();
				}
				return true;
			});

	Result.RequestId = InventoryResult.RequestId;

	Result.bCommitted = InventoryResult.bCommitted;
	Result.bTerminalReplay = InventoryResult.bTerminalReplay;
	Result.bReplayedTerminalCommitted = InventoryResult.bReplayedTerminalCommitted;
	Result.Error = InventoryResult.Error;
	Result.ReplayedTerminalError = InventoryResult.ReplayedTerminalError;
	return Result;
}

// 投掷效果查询流程：只沿鱼定义读那一份逐鱼数据；定义缺失、数据不完整或本鱼没有投掷效果时返回 false。
bool UCatFishInventoryItemInstance::HasThrowEffect() const
{
	const UCatFishDefinition* Definition = GetFishDefinition();
	return Definition && Definition->ThrowEffect.Kind != ECatFishThrowEffectKind::None
		&& Definition->ThrowEffect.IsRuntimeEffectReady();
}

// 投掷效果读取流程：数据不完整时返回空效果而不是半份数据，调用方拿到 Kind=None 就该当作「这条鱼扔出去只是掉地上」。
FCatFishThrowEffect UCatFishInventoryItemInstance::GetThrowEffect() const
{
	const UCatFishDefinition* Definition = GetFishDefinition();
	return Definition && Definition->ThrowEffect.IsRuntimeEffectReady()
		? Definition->ThrowEffect : FCatFishThrowEffect();
}
