#include "Inventory/CatBackPackComponent.h"

#include "Inventory/CatInventorySettings.h"
#include "Growth/CatGrowthComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Net/UnrealNetwork.h"

// 构造流程：保留父类全部库存事实和复制行为，只声明背包是角色默认整批收货目标。
UCatBackPackComponent::UCatBackPackComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UnifiedInventoryIntakePriority = 100;
}

// 个人背包容量兼容流程：
// 1. 蓝图 CDO 可能仍带有旧的正数 NumSlots；若直接交给父类初始化会先生成旧格子，之后配置缩小又不能安全裁掉。
// 2. 因此只在库存数组尚为空且蓝图明确给过正数时，先把该默认值收束到项目基础容量与角色成长容量之和。
// 3. 随后完全复用父类创建槽位；动态测试或运行时显式容量从零开始的组件不在这里改写，保存恢复中的已有条目也不会受影响。
void UCatBackPackComponent::InitializeComponent()
{
	if (InventoryList.Entries.IsEmpty() && NumSlots > 0)
	{
		NumSlots = GetConfiguredPlayerSlotCapacity();
	}
	Super::InitializeComponent();
}

// 容量初始化流程：仅 authority 合并项目基础容量与角色成长加成，再交给父类建立或扩展槽位；已有物品的缩容边界仍由父类裁决。
void UCatBackPackComponent::InitializePlayerInventorySlotCapacityFromAuthority()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	SetInventorySlotCountFromAuthority(GetConfiguredPlayerSlotCapacity());
}

// 容量读取流程：读取项目基础格数与角色成长加成，缺失的一项按零处理；相加后限制到非负 int32 范围，供初始化和成长扩容共用。
int32 UCatBackPackComponent::GetConfiguredPlayerSlotCapacity() const
{
	const UCatInventorySettings* InventorySettings = GetDefault<UCatInventorySettings>();
	const auto* Growth = GetOwner() ? GetOwner()->FindComponentByClass<UCatGrowthComponent>() : nullptr;
	const double Bonus = Growth ? Growth->GetTotalMagnitude(ECatGrowthOptionId::InventorySlots) : 0.0;
	return static_cast<int32>(FMath::Clamp(
		double(InventorySettings ? InventorySettings->GetPlayerInventorySlotCapacity() : 0) + Bonus,
		0.0, double(MAX_int32)));
}

// 预留流程：权威端核对原格中的单件实例，已有预留只接受同一身份；写入归还格后触发复制，库存通知仍由实际移出操作发出。
bool UCatBackPackComponent::ReserveQuickbarHeldSlotFromAuthority(const int32 SlotIndex, const FGuid ItemId)
{
	const auto* Entry = GetInventoryEntryAtSlot(SlotIndex);
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Entry || !Entry->Instance
		|| Entry->Instance->GetItemInstanceId() != ItemId || Entry->StackCount != 1) return false;
	if (QuickbarHeldSlot.ItemInstanceId.IsValid()) return QuickbarHeldSlot.ItemInstanceId == ItemId;
	QuickbarHeldSlot.SlotIndex = SlotIndex;
	QuickbarHeldSlot.ItemInstanceId = ItemId;
	GetOwner()->ForceNetUpdate();
	return true;
}
// 接回预留流程：确认服务器持有的实例和空格，再校验已有预留是否一致；成功只写入格位与实例 GUID 并刷新复制。
// 参数 ItemId 是实例 GUID；已有同实例同格预留时幂等返回，库存 UI 不观察这份操作预留。
bool UCatBackPackComponent::ReserveExistingHeldQuickbarSlotFromAuthority(const int32 SlotIndex, const FGuid ItemId)
{
	const auto* Entry = FindHeldInventoryEntryFromAuthority(ItemId);
	if (!GetOwner() || !GetOwner()->HasAuthority() || !IsValidInventorySlotIndex(SlotIndex) || HasItemAtSlot(SlotIndex)
		|| !Entry || !Entry->Instance || Entry->StackCount != 1) return false;
	if (QuickbarHeldSlot.ItemInstanceId.IsValid())
		return QuickbarHeldSlot.ItemInstanceId == ItemId && QuickbarHeldSlot.SlotIndex == SlotIndex;
	QuickbarHeldSlot.SlotIndex = SlotIndex;
	QuickbarHeldSlot.ItemInstanceId = ItemId;
	GetOwner()->ForceNetUpdate();
	return true;
}

// 解除流程：权威端清空已有预留并触发复制；库存的实际归还或移出另有统一通知，不在这里重复广播。
void UCatBackPackComponent::ClearQuickbarHeldSlotFromAuthority()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !QuickbarHeldSlot.ItemInstanceId.IsValid()) return;
	QuickbarHeldSlot = FCatQuickbarHeldSlot{};
	GetOwner()->ForceNetUpdate();
}
// 使用锁更新流程：只接受权威端对有效预留的值变化，再触发拥有者复制；不会因锁定或解锁而重建库存界面。
void UCatBackPackComponent::SetQuickbarHeldSlotInUseFromAuthority(const bool bInUse)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !QuickbarHeldSlot.ItemInstanceId.IsValid() || QuickbarHeldSlot.bInUse == bInUse) return;
	QuickbarHeldSlot.bInUse = bInUse;
	GetOwner()->ForceNetUpdate();
}
void UCatBackPackComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UCatBackPackComponent, QuickbarHeldSlot, COND_OwnerOnly);
}
bool UCatBackPackComponent::CanAcceptInventoryEntryAtSlot(const FCatInventoryEntry& Entry, const int32 TargetSlotIndex) const
{
	if (QuickbarHeldSlot.ItemInstanceId.IsValid())
	{
		const bool bReturning = Entry.Instance && Entry.Instance->GetItemInstanceId() == QuickbarHeldSlot.ItemInstanceId;
		if (bReturning) return TargetSlotIndex == QuickbarHeldSlot.SlotIndex && Super::CanAcceptInventoryEntryAtSlot(Entry, TargetSlotIndex);
		if (TargetSlotIndex == QuickbarHeldSlot.SlotIndex) return false;
	}
	return Super::CanAcceptInventoryEntryAtSlot(Entry, TargetSlotIndex);
}
bool UCatBackPackComponent::CanAcceptInventoryDefinitionAtSlot(const UCatInventoryItemDefinition& Definition, const int32 TargetSlotIndex) const
{
	if (QuickbarHeldSlot.ItemInstanceId.IsValid() && TargetSlotIndex == QuickbarHeldSlot.SlotIndex)
		return false; // 定义批次没有原实例身份，不能占用手持保留格。
	return Super::CanAcceptInventoryDefinitionAtSlot(Definition, TargetSlotIndex);
}
