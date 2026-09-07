#include "Inventory/CatInventoryItemInstance.h"

#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Inventory/Fragments/CatInventoryConsumableFragment.h"
#include "Net/UnrealNetwork.h"

// 实例构造流程：实例先处于无定义状态，只有被库存组件正式接收后才绑定定义并参与复制。
UCatInventoryItemInstance::UCatInventoryItemInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

// 复制声明流程：定义类决定客户端如何读静态配置，运行宿主用于调试和后续下游适配，不复制任何 GAS handle。
void UCatInventoryItemInstance::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, ItemDefinitionClass);
	DOREPLIFETIME(ThisClass, RuntimeOwnerActor);
}

// 网络支持声明：库存组件会把实例登记为子对象，FastArray 里的指针才能在客户端稳定解析。
bool UCatInventoryItemInstance::IsSupportedForNetworking() const
{
	return true;
}

// 定义绑定后才允许片段初始化实例；这样运行状态来源稳定，避免实例自己猜配置。
void UCatInventoryItemInstance::SetItemDefinitionClass(
	const TSubclassOf<UCatInventoryItemDefinition> InDefinitionClass)
{
	ItemDefinitionClass = InDefinitionClass;

	const UCatInventoryItemDefinition* ItemDefinition = GetItemDefinition();
	if (ItemDefinition == nullptr)
	{
		return;
	}

	for (const UCatInventoryItemFragment* Fragment : ItemDefinition->Fragments)
	{
		if (Fragment != nullptr)
		{
			Fragment->OnInstanceCreated(this);
		}
	}
}

// 静态定义类是实例身份来源；复制这个类引用，避免从显示名或标签反推物品。
TSubclassOf<UCatInventoryItemDefinition> UCatInventoryItemInstance::GetItemDefinitionClass() const
{
	return ItemDefinitionClass;
}

// 定义读取流程：通过类默认对象读取静态配置，避免把定义对象复制成另一份可变状态。
const UCatInventoryItemDefinition* UCatInventoryItemInstance::GetItemDefinition() const
{
	return ItemDefinitionClass != nullptr ? GetDefault<UCatInventoryItemDefinition>(ItemDefinitionClass) : nullptr;
}

// 宿主设置流程：跨库存移动只更新运行归属，不改变定义类、数量或物品自身状态。
void UCatInventoryItemInstance::SetRuntimeOwnerActor(AActor* InRuntimeOwnerActor)
{
	RuntimeOwnerActor = InRuntimeOwnerActor;
}

// 宿主读取流程：优先使用显式运行宿主；新建实例尚未同步时回退到 Outer Actor。
AActor* UCatInventoryItemInstance::GetRuntimeOwnerActor() const
{
	if (RuntimeOwnerActor != nullptr)
	{
		return RuntimeOwnerActor;
	}

	return Cast<AActor>(GetOuter());
}

// 通用使用预检流程：基础实例没有物品效果，默认拒绝库存 Use，避免误把任意物品当消耗品扣掉。
bool UCatInventoryItemInstance::CanUseFromInventory(const FCatInventoryEntry& InventoryEntry, APawn* UserPawn) const
{
	(void)InventoryEntry;
	(void)UserPawn;
	return false;
}

// 通用使用流程：基础实例不提交任何库存变化；需要 Use 的物品必须选择具名实例子类。
bool UCatInventoryItemInstance::TryUseFromInventory(FCatInventoryEntry& InventoryEntry, APawn* UserPawn,
	int32& OutConsumeCount)
{
	(void)InventoryEntry;
	(void)UserPawn;
	OutConsumeCount = 0;
	return false;
}

// 消耗品预检流程：读取定义上的纯库存消耗片段，并确认当前格子数量足够本次扣减。
bool UCatInventoryConsumableItemInstance::CanUseFromInventory(const FCatInventoryEntry& InventoryEntry,
	APawn* UserPawn) const
{
	(void)UserPawn;

	const UCatInventoryItemDefinition* ItemDefinition = GetItemDefinition();
	if (ItemDefinition == nullptr)
	{
		return false;
	}

	const UCatInventoryConsumableFragment* ConsumableFragment =
		Cast<UCatInventoryConsumableFragment>(
			ItemDefinition->FindFragmentByClass(UCatInventoryConsumableFragment::StaticClass()));
	return ConsumableFragment != nullptr
		&& ConsumableFragment->HasUsableInventoryUse()
		&& InventoryEntry.StackCount >= ConsumableFragment->GetConsumeCount();
}

// 消耗品使用流程：只把应扣数量交给库存组件；治疗、窝料、钓鱼扣饵等真实效果必须由上层在提交后执行。
bool UCatInventoryConsumableItemInstance::TryUseFromInventory(FCatInventoryEntry& InventoryEntry,
	APawn* UserPawn, int32& OutConsumeCount)
{
	OutConsumeCount = 0;
	if (!CanUseFromInventory(InventoryEntry, UserPawn))
	{
		return false;
	}

	const UCatInventoryItemDefinition* ItemDefinition = GetItemDefinition();
	const UCatInventoryConsumableFragment* ConsumableFragment =
		ItemDefinition != nullptr
			? Cast<UCatInventoryConsumableFragment>(
				ItemDefinition->FindFragmentByClass(UCatInventoryConsumableFragment::StaticClass()))
			: nullptr;
	if (ConsumableFragment == nullptr)
	{
		return false;
	}

	OutConsumeCount = ConsumableFragment->GetConsumeCount();
	return true;
}
