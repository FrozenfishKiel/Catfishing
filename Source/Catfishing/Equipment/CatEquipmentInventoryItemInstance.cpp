#include "Equipment/CatEquipmentInventoryItemInstance.h"

#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatRunInventorySlotOperations.h"
#include "Net/UnrealNetwork.h"

// 构造流程：装备实例先保持无专属状态；定义绑定后再按装备类型补齐鱼竿耐久，避免 CDO 或错误定义提前写运行值。
UCatEquipmentInventoryItemInstance::UCatEquipmentInventoryItemInstance(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

// 复制声明流程：只复制装备实例真正拥有的运行状态；定义、实例 ID 和运行宿主仍由父类库存实例复制。
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
// 3. 非 Rod 定义直接清零，避免旧快照把普通道具误投影成鱼竿。
void UCatEquipmentInventoryItemInstance::SetRodRuntimeStateFromAuthority(
	const double NewRodDurability, const bool bNewRodBroken)
{
	AActor* RuntimeOwner = GetRuntimeOwnerActor();
	if (RuntimeOwner != nullptr && !RuntimeOwner->HasAuthority())
	{
		return;
	}

	const UCatEquipmentDefinition* EquipmentDefinition = Cast<UCatEquipmentDefinition>(GetItemDefinition());
	if (EquipmentDefinition == nullptr || EquipmentDefinition->Kind != ECatEquipmentKind::Rod)
	{
		RodDurability = 0.0;
		bRodBroken = false;
		return;
	}

	const double MaximumDurability = FMath::IsFinite(EquipmentDefinition->MaximumRodDurability)
		? FMath::Max(0.0, EquipmentDefinition->MaximumRodDurability) : 0.0;
	RodDurability = bNewRodBroken || !FMath::IsFinite(NewRodDurability)
		? 0.0 : FMath::Clamp(NewRodDurability, 0.0, MaximumDurability);
	bRodBroken = bNewRodBroken || RodDurability <= 0.0;
}

// 旧槽位投影流程：
// 1. 先确认实例绑定的是运行就绪的装备定义，并且实例 ID 与数量能表达一个有效库存格。
// 2. 再把库存定义 ID、实例 ID 和数量写入旧结构；Rod 额外带出耐久，非 Rod 清空工具字段。
// 3. 最后复用旧结构归一化规则，保证迁移期 UI/存档读到的旧槽位仍满足原有约束。
bool UCatEquipmentInventoryItemInstance::BuildLegacyRunInventorySlot(
	const int32 StackCount, FCatRunInventorySlot& OutSlot) const
{
	OutSlot = FCatRunInventorySlot();
	const UCatEquipmentDefinition* EquipmentDefinition = Cast<UCatEquipmentDefinition>(GetItemDefinition());
	if (EquipmentDefinition == nullptr || !EquipmentDefinition->IsRuntimeDefinitionReady()
		|| StackCount <= 0 || !GetItemInstanceId().IsValid())
	{
		return false;
	}

	OutSlot.DefinitionId = EquipmentDefinition->EquipmentDefinitionId;
	OutSlot.ItemInstanceId = GetItemInstanceId();
	OutSlot.Quantity = StackCount;
	if (EquipmentDefinition->Kind == ECatEquipmentKind::Rod)
	{
		OutSlot.RodDurability = RodDurability;
		OutSlot.bRodBroken = bRodBroken;
	}
	CatRunInventorySlotOperations::NormalizeStoredItemSlot(OutSlot, *EquipmentDefinition);
	return true;
}

// 定义绑定扩展流程：父类先执行通用片段初始化；装备层随后只为 Rod 初始化耐久，其他装备保持无专属实例状态。
void UCatEquipmentInventoryItemInstance::HandleItemDefinitionAssigned()
{
	Super::HandleItemDefinitionAssigned();
	const UCatEquipmentDefinition* EquipmentDefinition = Cast<UCatEquipmentDefinition>(GetItemDefinition());
	if (EquipmentDefinition != nullptr && EquipmentDefinition->Kind == ECatEquipmentKind::Rod)
	{
		SetRodRuntimeStateFromAuthority(EquipmentDefinition->MaximumRodDurability, false);
		return;
	}

	RodDurability = 0.0;
	bRodBroken = false;
}
