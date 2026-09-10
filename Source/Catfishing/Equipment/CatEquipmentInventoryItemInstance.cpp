#include "Equipment/CatEquipmentInventoryItemInstance.h"

#include "Equipment/Fragments/CatEquipmentFragment_Rod.h"

#include "Character/CatCharacter.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "GameFramework/Controller.h"
#include "Inventory/CatInventoryComponent.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogCatEquipmentInventoryItem, Log, All);

// 构造流程：装备实例先保持无专属状态；定义绑定后再按鱼竿能力补齐耐久，避免 CDO 或错误定义提前写运行值。
UCatEquipmentInventoryItemInstance::UCatEquipmentInventoryItemInstance(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

// 复制声明流程：只复制装备物品实例自己的专属运行状态；定义、实例 ID 和运行宿主仍由父类库存实例复制。
void UCatEquipmentInventoryItemInstance::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, RodDurability);
	DOREPLIFETIME(ThisClass, bRodBroken);
}

// 耐久读取流程：返回当前实例保存的状态；普通装备不会写入耐久，因此自然保持 0。
double UCatEquipmentInventoryItemInstance::GetRodDurability() const
{
	return RodDurability;
}

// 断竿读取流程：返回服务器复制来的状态；普通装备固定保持 false，不参与鱼竿使用判断。
bool UCatEquipmentInventoryItemInstance::IsRodBroken() const
{
	return bRodBroken;
}

// 鱼竿状态写入流程：
// 1. 只有服务器或尚未拥有 Actor 的构造/恢复路径可以写，客户端本地拖放不能伪造耐久。
// 2. 再按装备定义的耐久上限收束非法数值，断裂状态和 0 耐久保持一致。
// 3. 不具备鱼竿能力的定义直接清零，避免读模型把普通道具误判成鱼竿。
void UCatEquipmentInventoryItemInstance::SetRodRuntimeStateFromAuthority(
	const double NewRodDurability, const bool bNewRodBroken)
{
	AActor* RuntimeOwner = GetRuntimeOwnerActor();
	if (RuntimeOwner != nullptr && !RuntimeOwner->HasAuthority())
	{
		return;
	}

	const UCatEquipmentDefinition* EquipmentDefinition = Cast<UCatEquipmentDefinition>(GetItemDefinition());
	if (EquipmentDefinition == nullptr || !EquipmentDefinition->CanServeFishingRod())
	{
		RodDurability = 0.0;
		bRodBroken = false;
		return;
	}

	const double MaximumDurability = FMath::IsFinite(EquipmentDefinition->FindFragment<UCatEquipmentFragment_Rod>()->MaximumRodDurability)
		? FMath::Max(0.0, EquipmentDefinition->FindFragment<UCatEquipmentFragment_Rod>()->MaximumRodDurability) : 0.0;
	RodDurability = bNewRodBroken || !FMath::IsFinite(NewRodDurability)
		? 0.0 : FMath::Clamp(NewRodDurability, 0.0, MaximumDurability);
	bRodBroken = bNewRodBroken || RodDurability <= 0.0;
}

// 装备实例 Use 裁决流程：
// 1. 先复用通用实例身份校验，确保调用方处理的是这份 UObject 实例而不是同定义物品。
// 2. 再读取装备定义声明的库存影响；部署物要求 Actor 类和单实例，数量物要求可堆叠消耗身份。
// 3. 鱼竿的断裂和耐久是实例运行状态，必须在这里拒绝，不能回到 Equipment 读模型或 Definition。
ECatDomainCommandError UCatEquipmentInventoryItemInstance::Use(const FCatInventoryEntry& Item,
	const int32 Quantity) const
{
	const UCatEquipmentDefinition* EquipmentDefinition = Cast<UCatEquipmentDefinition>(GetItemDefinition());
	if (EquipmentDefinition == nullptr || Item.Instance != this || Item.StackCount <= 0)
	{
		return ECatDomainCommandError::InvalidPayload;
	}

	const ECatDomainCommandError BaseError = Super::Use(Item, Quantity);
	if (BaseError != ECatDomainCommandError::None)
	{
		return BaseError;
	}

	const bool bKeepsInstance = KeepsInventoryInstanceWhileUsed();
	const bool bConsumesQuantity = ConsumesInventoryQuantityOnUse();
	if (bKeepsInstance && (EquipmentDefinition->UseActorClass.IsNull()
		|| EquipmentDefinition->bRunConsumable || Item.StackCount != 1 || Quantity != 1))
	{
		return ECatDomainCommandError::InvalidPhase;
	}
	if (bConsumesQuantity && !EquipmentDefinition->bRunConsumable)
	{
		return ECatDomainCommandError::InvalidPhase;
	}
	if (EquipmentDefinition->CanServeFishingRod()
		&& (bRodBroken || !FMath::IsFinite(RodDurability) || RodDurability <= 0.0))
	{
		return ECatDomainCommandError::InvalidPhase;
	}
	return ECatDomainCommandError::None;
}

// 装备实例 UnUse 裁决流程：先确认实例和定义身份没有错位；热配置失去部署 Actor 时仍允许收回，避免 held entry 卡死。
ECatDomainCommandError UCatEquipmentInventoryItemInstance::UnUse(const FCatInventoryEntry& Item) const
{
	const UCatEquipmentDefinition* EquipmentDefinition = Cast<UCatEquipmentDefinition>(GetItemDefinition());
	if (EquipmentDefinition == nullptr || Item.Instance != this || Item.StackCount != 1)
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	return Super::UnUse(Item);
}

// 装备实例持有策略读取流程：鱼竿部署需要同一 UObject 离开可见背包，库存活动区只按实例身份保管它。
bool UCatEquipmentInventoryItemInstance::KeepsInventoryInstanceWhileUsed() const
{
	const UCatEquipmentDefinition* EquipmentDefinition = Cast<UCatEquipmentDefinition>(GetItemDefinition());
	return EquipmentDefinition != nullptr && EquipmentDefinition->CanServeFishingRod();
}

// 装备实例扣量策略读取流程：窝料投放由专门玩法入口完成前置裁决后交给正式库存扣同一槽位数量。
bool UCatEquipmentInventoryItemInstance::ConsumesInventoryQuantityOnUse() const
{
	const UCatEquipmentDefinition* EquipmentDefinition = Cast<UCatEquipmentDefinition>(GetItemDefinition());
	return EquipmentDefinition != nullptr && EquipmentDefinition->CanServeChumPlacement();
}

// 装备 Use 预检流程：这里只读条目、定义和使用目标，确认这份定义是否具备现有钓具槽需要的能力，不在预检阶段提交选择。
bool UCatEquipmentInventoryItemInstance::CanUseFromInventory(
	const FCatInventoryEntry& InventoryEntry, APawn* UserPawn) const
{
	const UCatEquipmentDefinition* EquipmentDefinition = Cast<UCatEquipmentDefinition>(GetItemDefinition());
	const ACatCharacter* Character = Cast<ACatCharacter>(UserPawn);
	const bool bSupportedLoadoutSlot = EquipmentDefinition != nullptr
		&& (EquipmentDefinition->CanServeFishingRod()
			|| EquipmentDefinition->CanServeFishingBait()
			|| EquipmentDefinition->CanServeFishingFloat()
			|| EquipmentDefinition->CanServeScoopNet());
	return InventoryEntry.Instance == this
		&& InventoryEntry.StackCount > 0
		&& GetItemInstanceId().IsValid()
		&& EquipmentDefinition != nullptr
		&& EquipmentDefinition->IsRuntimeDefinitionReady()
		&& bSupportedLoadoutSlot
		&& Character != nullptr
		&& Character->GetEquipmentComponent() != nullptr;
}

// 装备物品从库存使用的正式提交流程：
// 1. 先复核库存 entry、使用 Pawn 和 Equipment 组件，避免装备实例被其他宿主或空格冒用。
// 2. 再按服务器当前 Equipment 快照补齐未点击的 Rod/Bait/Float/ScoopNet 选择；客户端不提交完整 loadout。
// 3. 库存槽位由正式库存原子裁决；钓鱼选择读模型版本由 Equipment 入口在调用后读取。
// 4. 最后用当前 Equipment 版本调用正式选择提交入口，让解锁、消耗属性、断竿和同选择 AlreadyResolved 仍由原权威路径裁决。
FCatDomainCommandResult UCatEquipmentInventoryItemInstance::UseFromInventorySlotFromAuthority(
	const FCatInventoryEntry& InventoryEntry, const FCatInventoryItemUseContext& UseContext)
{
	FCatDomainCommandResult Result;
	Result.RequestId = UseContext.RequestId;

	ACatCharacter* Character = Cast<ACatCharacter>(UseContext.UserPawn);
	if (Character == nullptr && UseContext.RequestingController != nullptr)
	{
		Character = Cast<ACatCharacter>(UseContext.RequestingController->GetPawn());
	}
	UCatEquipmentComponent* Equipment = Character ? Character->GetEquipmentComponent() : nullptr;
	const UCatEquipmentDefinition* Definition = Cast<UCatEquipmentDefinition>(GetItemDefinition());
	const int64 ObservedEquipmentRevision = Equipment ? Equipment->GetSnapshot().Revision : 0;
	Result.Revision = ObservedEquipmentRevision;

	FName SelectedDefinitionId = NAME_None;
	FGuid SelectedItemInstanceId;
	FString SelectedLoadoutSlot = TEXT("None");
	if (InventoryEntry.Instance != this || InventoryEntry.StackCount <= 0 || !GetItemInstanceId().IsValid())
	{
		Result.Error = ECatDomainCommandError::NotFound;
	}
	else if (Definition == nullptr || !Definition->IsRuntimeDefinitionReady())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (Character == nullptr || Equipment == nullptr)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else
	{
		SelectedDefinitionId = Definition->EquipmentDefinitionId;
		SelectedItemInstanceId = GetItemInstanceId();

		const FCatEquipmentLoadoutSnapshot& Snapshot = Equipment->GetSnapshot();
		FName RodDefinitionId = Snapshot.RodDefinitionId;
		FName BaitDefinitionId = Snapshot.BaitDefinitionId;
		FName FloatDefinitionId = Snapshot.FloatDefinitionId;
		FName ScoopNetDefinitionId = Snapshot.ScoopNetDefinitionId;
		FGuid RodItemInstanceId = Snapshot.RodItemInstanceId;
		FGuid BaitItemInstanceId = Snapshot.BaitItemInstanceId;
		FGuid FloatItemInstanceId = Snapshot.FloatItemInstanceId;
		FGuid ScoopNetItemInstanceId = Snapshot.ScoopNetItemInstanceId;
		bool bSelectedLoadoutSlotSupported = true;

		if (Definition->CanServeFishingRod())
		{
			RodDefinitionId = SelectedDefinitionId;
			RodItemInstanceId = SelectedItemInstanceId;
			SelectedLoadoutSlot = TEXT("Rod");
		}
		else if (Definition->CanServeFishingBait())
		{
			BaitDefinitionId = SelectedDefinitionId;
			BaitItemInstanceId = SelectedItemInstanceId;
			SelectedLoadoutSlot = TEXT("Bait");
		}
		else if (Definition->CanServeFishingFloat())
		{
			FloatDefinitionId = SelectedDefinitionId;
			FloatItemInstanceId = SelectedItemInstanceId;
			SelectedLoadoutSlot = TEXT("Float");
		}
		else if (Definition->CanServeScoopNet())
		{
			ScoopNetDefinitionId = SelectedDefinitionId;
			ScoopNetItemInstanceId = SelectedItemInstanceId;
			SelectedLoadoutSlot = TEXT("ScoopNet");
		}
		else
		{
			bSelectedLoadoutSlotSupported = false;
		}

		if (!bSelectedLoadoutSlotSupported)
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
		}
		else if (RodDefinitionId.IsNone() || BaitDefinitionId.IsNone() || FloatDefinitionId.IsNone())
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
		}
		else
		{
			Result = Equipment->ConfigureLoadoutFromAuthority(UseContext.RequestId, ObservedEquipmentRevision,
				RodDefinitionId, BaitDefinitionId, FloatDefinitionId, ScoopNetDefinitionId, NAME_None,
				RodItemInstanceId, BaitItemInstanceId, FloatItemInstanceId, ScoopNetItemInstanceId);
		}
	}

	const int64 FinalEquipmentRevision = Equipment ? Equipment->GetSnapshot().Revision : ObservedEquipmentRevision;
	UE_LOG(LogCatEquipmentInventoryItem, Log,
		TEXT("Event=equipment_inventory_item_use Request=%s Character=%s Slot=%d Definition=%s Item=%s LoadoutSlot=%s EquipmentRevision=%lld FinalEquipmentRevision=%lld Committed=%s Error=%s ResultRevision=%lld"),
		*UseContext.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		*GetNameSafe(Character),
		UseContext.InventorySlotIndex,
		*SelectedDefinitionId.ToString(),
		*SelectedItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*SelectedLoadoutSlot,
		ObservedEquipmentRevision,
		FinalEquipmentRevision,
		Result.bCommitted ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(Result.Error),
		Result.Revision);
	return Result;
}

// 定义绑定扩展流程：父类先执行通用片段初始化；装备层随后只为具备鱼竿能力的定义初始化耐久，其他装备保持无专属实例状态。
void UCatEquipmentInventoryItemInstance::HandleItemDefinitionAssigned()
{
	Super::HandleItemDefinitionAssigned();
	const UCatEquipmentDefinition* EquipmentDefinition = Cast<UCatEquipmentDefinition>(GetItemDefinition());
	if (EquipmentDefinition != nullptr && EquipmentDefinition->CanServeFishingRod())
	{
		SetRodRuntimeStateFromAuthority(EquipmentDefinition->FindFragment<UCatEquipmentFragment_Rod>()->MaximumRodDurability, false);
		return;
	}

	RodDurability = 0.0;
	bRodBroken = false;
}
