#include "Equipment/CatEquipmentInventoryItemInstance.h"

#include "Character/CatCharacter.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatRunInventorySlotOperations.h"
#include "GameFramework/Controller.h"
#include "Inventory/CatInventoryComponent.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogCatEquipmentInventoryItem, Log, All);

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

// 装备 Use 预检流程：只在装备物品实例这一层识别 Rod/Bait/Float/ScoopNet，并确认条目、数量、定义和使用目标足够进入正式提交。
bool UCatEquipmentInventoryItemInstance::CanUseFromInventory(
	const FCatInventoryEntry& InventoryEntry, APawn* UserPawn) const
{
	const UCatEquipmentDefinition* EquipmentDefinition = Cast<UCatEquipmentDefinition>(GetItemDefinition());
	const ACatCharacter* Character = Cast<ACatCharacter>(UserPawn);
	const bool bSupportedKind = EquipmentDefinition != nullptr
		&& (EquipmentDefinition->Kind == ECatEquipmentKind::Rod
			|| EquipmentDefinition->Kind == ECatEquipmentKind::Bait
			|| EquipmentDefinition->Kind == ECatEquipmentKind::Float
			|| EquipmentDefinition->Kind == ECatEquipmentKind::ScoopNet);
	return InventoryEntry.Instance == this
		&& InventoryEntry.StackCount > 0
		&& GetItemInstanceId().IsValid()
		&& EquipmentDefinition != nullptr
		&& EquipmentDefinition->IsRuntimeDefinitionReady()
		&& bSupportedKind
		&& Character != nullptr
		&& Character->GetEquipmentComponent() != nullptr;
}

// 旧 bool 使用流程：
// 1. 先把旧接口的 Pawn 和当前库存 owner 重组为结构化 Use 上下文。
// 2. 再走同一个装备库存 Use 提交流程，让右键 UI、蓝图旧入口和直接组件调用不会分叉出第二套钓具规则。
// 3. 钓具选择不会立即扣库存数量，因此成功或同选择 AlreadyResolved 都返回零扣量。
bool UCatEquipmentInventoryItemInstance::TryUseFromInventory(FCatInventoryEntry& InventoryEntry, APawn* UserPawn,
	int32& OutConsumeCount)
{
	OutConsumeCount = 0;
	UCatInventoryComponent* SourceInventory = InventoryEntry.SlotOwnerComponent;
	FCatInventoryItemUseContext UseContext;
	UseContext.RequestId = FGuid::NewGuid();
	UseContext.RequestingController = UserPawn ? UserPawn->GetController() : nullptr;
	UseContext.UserPawn = UserPawn;
	UseContext.SourceInventory = SourceInventory;
	UseContext.ExpectedInventoryRevision = SourceInventory ? SourceInventory->GetInventoryRevision() : 0;
	UseContext.InventorySlotIndex = SourceInventory ? SourceInventory->FindInventorySlotIndexFromInstance(this) : INDEX_NONE;
	const FCatDomainCommandResult Result = UseFromInventorySlotFromAuthority(InventoryEntry, UseContext);
	return Result.Error == ECatDomainCommandError::None
		|| Result.Error == ECatDomainCommandError::AlreadyResolved;
}

// 装备库存 Use 提交流程：
// 1. 先复核库存 entry、使用 Pawn 和 Equipment 组件，避免装备实例被其他宿主或空格冒用。
// 2. 再按服务器当前 Equipment 快照补齐未点击的 Rod/Bait/Float/ScoopNet 选择；客户端不提交完整 loadout。
// 3. 旧 SelectFishingItem 兼容路径需要严格 EquipmentRevision 时在这里拒绝并发冲突；新通用 Use 直接以服务器快照为基线。
// 4. 最后调用 Equipment 的正式选择提交入口，让解锁、消耗属性、断竿和同选择 AlreadyResolved 仍由原权威路径裁决。
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
	ECatEquipmentKind SelectedKind = ECatEquipmentKind::Unknown;
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
	else if (UseContext.bRequireEquipmentRevision
		&& ObservedEquipmentRevision != UseContext.ExpectedEquipmentRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
	}
	else
	{
		SelectedDefinitionId = Definition->EquipmentDefinitionId;
		SelectedItemInstanceId = GetItemInstanceId();
		SelectedKind = Definition->Kind;

		const FCatEquipmentLoadoutSnapshot& Snapshot = Equipment->GetSnapshot();
		FName RodDefinitionId = Snapshot.RodDefinitionId;
		FName BaitDefinitionId = Snapshot.BaitDefinitionId;
		FName FloatDefinitionId = Snapshot.FloatDefinitionId;
		FName ScoopNetDefinitionId = Snapshot.ScoopNetDefinitionId;
		FGuid RodItemInstanceId = Snapshot.RodItemInstanceId;
		FGuid BaitItemInstanceId = Snapshot.BaitItemInstanceId;
		FGuid FloatItemInstanceId = Snapshot.FloatItemInstanceId;
		FGuid ScoopNetItemInstanceId = Snapshot.ScoopNetItemInstanceId;
		bool bSelectedKindSupported = true;

		switch (SelectedKind)
		{
		case ECatEquipmentKind::Rod:
			RodDefinitionId = SelectedDefinitionId;
			RodItemInstanceId = SelectedItemInstanceId;
			break;
		case ECatEquipmentKind::Bait:
			BaitDefinitionId = SelectedDefinitionId;
			BaitItemInstanceId = SelectedItemInstanceId;
			break;
		case ECatEquipmentKind::Float:
			FloatDefinitionId = SelectedDefinitionId;
			FloatItemInstanceId = SelectedItemInstanceId;
			break;
		case ECatEquipmentKind::ScoopNet:
			ScoopNetDefinitionId = SelectedDefinitionId;
			ScoopNetItemInstanceId = SelectedItemInstanceId;
			break;
		default:
			bSelectedKindSupported = false;
			break;
		}

		if (!bSelectedKindSupported)
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
		}
		else if (RodDefinitionId.IsNone() || BaitDefinitionId.IsNone() || FloatDefinitionId.IsNone())
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
		}
		else
		{
			const int64 EffectiveExpectedEquipmentRevision = UseContext.bRequireEquipmentRevision
				? UseContext.ExpectedEquipmentRevision : ObservedEquipmentRevision;
			Result = Equipment->ConfigureLoadoutFromAuthority(UseContext.RequestId, EffectiveExpectedEquipmentRevision,
				RodDefinitionId, BaitDefinitionId, FloatDefinitionId, ScoopNetDefinitionId, NAME_None,
				RodItemInstanceId, BaitItemInstanceId, FloatItemInstanceId, ScoopNetItemInstanceId);
		}
	}

	const int64 FinalEquipmentRevision = Equipment ? Equipment->GetSnapshot().Revision : ObservedEquipmentRevision;
	UE_LOG(LogCatEquipmentInventoryItem, Log,
		TEXT("Event=equipment_inventory_item_use Request=%s Character=%s Slot=%d Definition=%s Item=%s Kind=%s StrictEquipmentRevision=%s ExpectedEquipmentRevision=%lld EquipmentRevision=%lld FinalEquipmentRevision=%lld Committed=%s Error=%s ResultRevision=%lld"),
		*UseContext.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		*GetNameSafe(Character),
		UseContext.InventorySlotIndex,
		*SelectedDefinitionId.ToString(),
		*SelectedItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*UEnum::GetValueAsString(SelectedKind),
		UseContext.bRequireEquipmentRevision ? TEXT("true") : TEXT("false"),
		UseContext.ExpectedEquipmentRevision,
		ObservedEquipmentRevision,
		FinalEquipmentRevision,
		Result.bCommitted ? TEXT("true") : TEXT("false"),
		*UEnum::GetValueAsString(Result.Error),
		Result.Revision);
	return Result;
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
