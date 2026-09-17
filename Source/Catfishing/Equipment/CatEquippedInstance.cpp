#include "Equipment/CatEquippedInstance.h"
#include "Equipment/CatEquippedDefinition.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Net/UnrealNetwork.h"

// 复制注册流程：来源实例与装备配置构成客户端可读的来源关系，实际授予状态由 ASC 自己复制。
void UCatEquippedInstance::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, SourceItem);
	DOREPLIFETIME(ThisClass, EquipmentDefinition);
}

// 装备流程：先冻结原 ASC 和来源，再逐个授予以本装备为 SourceObject 的集合；失败即撤销本批，不遗留部分输入能力。
bool UCatEquippedInstance::Equip(UCatAbilitySystemComponent* AbilitySystem, UCatInventoryItemInstance* Item, const UCatEquippedDefinition* Definition)
{
	if (!AbilitySystem || !AbilitySystem->IsOwnerActorAuthoritative() || !Item || !Definition || GrantedAbilitySystem.IsValid()) return false;
	GrantedAbilitySystem = AbilitySystem; SourceItem = Item; EquipmentDefinition = Definition;
	for (const auto& SetRef : Definition->AbilitySetsToGrant)
	{
		const auto* Set = SetRef.LoadSynchronous();
		FCatGrantedAbilitySetHandles Handles;
		if (!Set || !Set->GiveToAbilitySystem(AbilitySystem, Handles, this))
		{
			Handles.TakeFromAbilitySystem(AbilitySystem); Unequip(); return false;
		}
		GrantedHandles.Append(MoveTemp(Handles));
	}
	return true;
}

// 卸下流程：先移出句柄和 ASC 引用，结束回调即使重入也不会再次撤销；来源关系保留到宿主释放对象，便于结束中的能力读取。
void UCatEquippedInstance::Unequip()
{
	auto* AbilitySystem = GrantedAbilitySystem.Get();
	FCatGrantedAbilitySetHandles Handles = MoveTemp(GrantedHandles);
	GrantedAbilitySystem.Reset();
	if (AbilitySystem) Handles.TakeFromAbilitySystem(AbilitySystem);
}
