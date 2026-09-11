#include "Inventory/CatInventoryItemInstance.h"

#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Net/UnrealNetwork.h"

// 实例构造流程：实例先处于无定义状态，只有被库存组件正式接收后才绑定定义并参与复制。
UCatInventoryItemInstance::UCatInventoryItemInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		ItemInstanceId = FGuid::NewGuid();
	}
}

// 复制声明流程：实例 ID 和定义资产决定客户端如何还原库存格，运行宿主用于调试和后续下游适配，不复制任何 GAS handle。
void UCatInventoryItemInstance::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, ItemInstanceId);
	DOREPLIFETIME(ThisClass, ItemDefinition);
	DOREPLIFETIME(ThisClass, RuntimeOwnerActor);
}

// 网络支持声明：库存组件会把实例登记为子对象，FastArray 里的指针才能在客户端稳定解析。
bool UCatInventoryItemInstance::IsSupportedForNetworking() const
{
	return true;
}

// 定义绑定后才允许片段初始化实例；这样运行状态来源稳定，避免实例自己猜配置。
void UCatInventoryItemInstance::SetItemDefinition(UCatInventoryItemDefinition* InDefinition)
{
	ItemDefinition = InDefinition;

	const UCatInventoryItemDefinition* CurrentDefinition = GetItemDefinition();
	if (CurrentDefinition == nullptr)
	{
		return;
	}

	for (const UCatInventoryItemFragment* Fragment : CurrentDefinition->Fragments)
	{
		if (Fragment != nullptr)
		{
			Fragment->OnInstanceCreated(this);
		}
	}
	HandleItemDefinitionAssigned();
}

// 静态定义资产是实例身份来源；复制这个资产引用，避免从显示名或标签反推物品。
UCatInventoryItemDefinition* UCatInventoryItemInstance::GetItemDefinition() const
{
	return ItemDefinition;
}

// 稳定定义 ID 读取流程：实例自己不缓存第二份 ID，避免定义资产改口径时出现双事实。
FName UCatInventoryItemInstance::GetItemDefinitionId() const
{
	return ItemDefinition != nullptr ? ItemDefinition->GetInventoryDefinitionId() : NAME_None;
}

// 实例 ID 读取流程：返回服务器创建时冻结的 ID；无效 ID 表示实例还没有进入正式库存链。
FGuid UCatInventoryItemInstance::GetItemInstanceId() const
{
	return ItemInstanceId;
}

// 库存实例 Use 裁决流程：
// 1. 先确认运行格确实指向当前实例和当前定义，避免调用方只按同类定义误用另一份物品。
// 2. 再读取实例声明的库存 mutation 语义；普通实例没有声明时返回 AlreadyResolved，不产生格子变化。
// 3. 数量物只能扣不超过当前堆栈的数量；借出物只能一格一件，确保 held entry 保管的是完整实例。
// 4. 基础实例不理解装备、GAS 或 Fishing 规则，具体物品子类在同一实例入口补自己的条件。
ECatDomainCommandError UCatInventoryItemInstance::Use(const FCatInventoryEntry& Item,
	const int32 Quantity) const
{
	const UCatInventoryItemDefinition* Definition = GetItemDefinition();
	const FName RuntimeDefinitionId =
		Definition != nullptr ? Definition->GetInventoryDefinitionId() : NAME_None;
	if (Item.Instance != this || Item.StackCount <= 0 || Quantity <= 0 || Quantity > Item.StackCount
		|| !GetItemInstanceId().IsValid() || RuntimeDefinitionId.IsNone()
		|| GetItemDefinitionId() != RuntimeDefinitionId)
	{
		return ECatDomainCommandError::InvalidPayload;
	}

	const bool bKeepsInstance = KeepsInventoryInstanceWhileUsed();
	const bool bConsumesQuantity = ConsumesInventoryQuantityOnUse();
	if (!bKeepsInstance && !bConsumesQuantity)
	{
		return ECatDomainCommandError::AlreadyResolved;
	}
	if (bKeepsInstance && bConsumesQuantity)
	{
		return ECatDomainCommandError::InvalidPhase;
	}
	if (bKeepsInstance && (Definition->GetMaxStackCount() > 1 || Item.StackCount != 1 || Quantity != 1))
	{
		return ECatDomainCommandError::InvalidPhase;
	}
	return ECatDomainCommandError::None;
}

// 库存实例 UnUse 裁决流程：只确认活动记录仍然指向当前实例和定义；子类可在同一入口补热配置或损坏状态检查。
ECatDomainCommandError UCatInventoryItemInstance::UnUse(const FCatInventoryEntry& Item) const
{
	const FName RuntimeDefinitionId = GetItemDefinitionId();
	if (Item.Instance != this || Item.StackCount <= 0 || !GetItemInstanceId().IsValid()
		|| RuntimeDefinitionId.IsNone() || GetItemDefinitionId() != RuntimeDefinitionId)
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	return ECatDomainCommandError::None;
}

// 库存实例持有策略读取流程：普通实例没有部署语义；只有专属实例明确覆盖后才能离开可见背包进入 held entry。
bool UCatInventoryItemInstance::KeepsInventoryInstanceWhileUsed() const
{
	return false;
}

// 库存实例扣量策略读取流程：普通实例没有消耗语义；数量耗材必须通过专属实例覆盖后才由库存组件扣减。
bool UCatInventoryItemInstance::ConsumesInventoryQuantityOnUse() const
{
	return false;
}

// 实例身份恢复流程：只有服务器拥有的实例能接收外部 ID，非法 ID 保持现有身份，避免客户端或坏存档制造无身份物品。
void UCatInventoryItemInstance::SetItemInstanceIdFromAuthority(const FGuid InItemInstanceId)
{
	AActor* RuntimeOwner = GetRuntimeOwnerActor();
	if ((RuntimeOwner != nullptr && !RuntimeOwner->HasAuthority()) || !InItemInstanceId.IsValid())
	{
		return;
	}

	ItemInstanceId = InItemInstanceId;
}

// 宿主设置流程：跨库存移动只更新运行归属，不改变定义资产、数量或物品自身状态。
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

// 通用使用预检流程：这里只读条目和使用者，基础实例没有真实物品效果所以默认拒绝；能使用的物品必须在结构化提交入口声明自己的效果。
bool UCatInventoryItemInstance::CanUseFromInventory(const FCatInventoryEntry& InventoryEntry, APawn* UserPawn) const
{
	(void)InventoryEntry;
	(void)UserPawn;
	return false;
}

// 结构化使用流程：基础实例仍然拒绝，因为它没有声明任何真实物品效果；返回请求 ID 和错误码，让 UI 只按正式命令回包诊断和刷新。
FCatDomainCommandResult UCatInventoryItemInstance::UseFromInventorySlotFromAuthority(
	const FCatInventoryEntry& InventoryEntry, const FCatInventoryItemUseContext& UseContext)
{
	(void)InventoryEntry;
	FCatDomainCommandResult Result;
	Result.RequestId = UseContext.RequestId;

	Result.Error = ECatDomainCommandError::InvalidPayload;
	return Result;
}

// 定义绑定扩展流程：基础库存实例没有额外状态要派生；子类可以读取当前定义补齐自己的运行字段。
void UCatInventoryItemInstance::HandleItemDefinitionAssigned()
{
}
