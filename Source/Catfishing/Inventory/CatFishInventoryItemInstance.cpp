#include "Inventory/CatFishInventoryItemInstance.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Tags/CatStateTags.h"
#include "Inventory/Fragments/CatItemUseFragment.h"
#include "AbilitySystem/Effects/CatFishExperienceEffect.h"
#include "Growth/CatGrowthComponent.h"

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

// 鱼专属查询复核定义和真实容器；只读嘴部占用或买家资格，客户端展示不能替代服务器交易验证。
bool UCatFishInventoryItemInstance::CanExecuteInventoryAction(const FGameplayTag& Action,
	const FCatInventoryEntry& Entry, APawn* UserPawn, FText& OutReason) const
{
	if (Action != CatInventoryActionTags::Carry && Action != CatInventoryActionTags::Sell)
		return Super::CanExecuteInventoryAction(Action, Entry, UserPawn, OutReason);
	OutReason = NSLOCTEXT("CatInventory", "FishActionUnavailable", "当前容器或身体状态不允许此操作");
	ACatCharacter* Character = Cast<ACatCharacter>(UserPawn);
	if (!Character || !Character->GetConditionComponent() || Character->GetCatAbilitySystemComponent()->HasMatchingGameplayTag(CatStateTags::Downed)
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

// 鱼使用预检先核对公开身份、重量、可食用标记与动作配置；服务器再检查捕获身份和成长系统。
// 客户端不能调用仅允许 authority 的成长预检；此处只决定菜单可用性，实际消费仍由进食能力复核并提交。
bool UCatFishInventoryItemInstance::CanUseFromInventory(
	const FCatInventoryEntry& InventoryEntry, APawn* UserPawn) const
{
	const UCatFishDefinition* Definition = GetFishDefinition();
	const ACatCharacter* Character = Cast<ACatCharacter>(UserPawn);
	const UCatGrowthComponent* Growth = Character ? Character->GetGrowthComponent() : nullptr;
	const UCatItemUseFragment* Effect = Definition ? Definition->FindFragment<UCatItemUseFragment>() : nullptr;
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
		&& Growth && Effect && Super::CanUseFromInventory(InventoryEntry, UserPawn)
		&& (!Character->HasAuthority() || Growth->ValidateFishGrowth(Definition, WeightKilograms) == ECatDomainCommandError::None);
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
