#include "Equipment/CatEquipmentInventoryItemInstance.h"
#include "Equipment/CatEquippedDefinition.h"
#include "Inventory/Fragments/CatEquippableItemFragment.h"

#include "Equipment/Fragments/CatEquipmentFragment_Rod.h"

#include "Character/CatCharacter.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentItemDefinition.h"
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

	const UCatEquipmentItemDefinition* EquipmentDefinition = Cast<UCatEquipmentItemDefinition>(GetItemDefinition());
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
	const UCatEquipmentItemDefinition* EquipmentDefinition = Cast<UCatEquipmentItemDefinition>(GetItemDefinition());
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
	if (bKeepsInstance && (EquipmentDefinition->GetEquipmentDefinition()->ActorClass.IsNull()
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
	const UCatEquipmentItemDefinition* EquipmentDefinition = Cast<UCatEquipmentItemDefinition>(GetItemDefinition());
	if (EquipmentDefinition == nullptr || Item.Instance != this || Item.StackCount != 1)
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	return Super::UnUse(Item);
}

// 基础装备没有部署行为；只有鱼竿行为子类声明 held 保管，鱼护等普通装备不因定义类别被自动使用。
bool UCatEquipmentInventoryItemInstance::KeepsInventoryInstanceWhileUsed() const
{
	return false;
}

// 基础装备不消费库存；窝料子类在实际投放事务中单独声明扣量。
bool UCatEquipmentInventoryItemInstance::ConsumesInventoryQuantityOnUse() const
{
	return false;
}

// 装备 Use 预检流程：只确认有效条目、具体行为实例与角色装备组件；具体用途由子类裁决，此处不选择或改变装备。
bool UCatEquipmentInventoryItemInstance::CanUseFromInventory(
	const FCatInventoryEntry& InventoryEntry, APawn* UserPawn) const
{
	const UCatEquipmentItemDefinition* EquipmentDefinition = Cast<UCatEquipmentItemDefinition>(GetItemDefinition());
	const ACatCharacter* Character = Cast<ACatCharacter>(UserPawn);
	const bool bHasConcreteBehavior = GetClass() != UCatEquipmentInventoryItemInstance::StaticClass();
	return InventoryEntry.Instance == this
		&& InventoryEntry.StackCount > 0
		&& GetItemInstanceId().IsValid()
		&& EquipmentDefinition != nullptr
		&& EquipmentDefinition->IsRuntimeDefinitionReady()
		&& (bHasConcreteBehavior || Super::CanUseFromInventory(InventoryEntry, UserPawn))
		&& Character != nullptr
		&& Character->GetEquipmentComponent() != nullptr;
}

// 定义绑定扩展流程：父类先执行通用片段初始化；装备层随后只为具备鱼竿能力的定义初始化耐久，其他装备保持无专属实例状态。
void UCatEquipmentInventoryItemInstance::HandleItemDefinitionAssigned()
{
	Super::HandleItemDefinitionAssigned();
	const UCatEquipmentItemDefinition* EquipmentDefinition = Cast<UCatEquipmentItemDefinition>(GetItemDefinition());
	if (EquipmentDefinition != nullptr && EquipmentDefinition->CanServeFishingRod())
	{
		SetRodRuntimeStateFromAuthority(EquipmentDefinition->FindFragment<UCatEquipmentFragment_Rod>()->MaximumRodDurability, false);
		return;
	}

	RodDurability = 0.0;
	bRodBroken = false;
}
