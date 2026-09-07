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

// 通用使用预检流程：基础实例没有真实物品效果，默认拒绝库存 Use；需要右键使用的物品必须用具名实例把效果和扣量一起声明清楚。
bool UCatInventoryItemInstance::CanUseFromInventory(const FCatInventoryEntry& InventoryEntry, APawn* UserPawn) const
{
	(void)InventoryEntry;
	(void)UserPawn;
	return false;
}

// 通用使用流程：基础实例不提交任何库存变化；库存层不能只扣数量就宣称使用成功，避免草药、窝料这类有目标动作绕过自己的领域系统。
bool UCatInventoryItemInstance::TryUseFromInventory(FCatInventoryEntry& InventoryEntry, APawn* UserPawn,
	int32& OutConsumeCount)
{
	(void)InventoryEntry;
	(void)UserPawn;
	OutConsumeCount = 0;
	return false;
}

// 定义绑定扩展流程：基础库存实例没有额外状态要派生；子类可以读取当前定义补齐自己的运行字段。
void UCatInventoryItemInstance::HandleItemDefinitionAssigned()
{
}
