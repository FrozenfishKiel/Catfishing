#include "Inventory/Fragments/CatConsumableEffectFragment.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Character/CatCharacter.h"
#include "Inventory/CatInventoryStatics.h"
#include "Condition/CatConditionComponent.h"

#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Net/UnrealNetwork.h"

// 操作查询只读定义清单与当前角色；菜单和服务器共用此入口，但它不替代服务器的库存宿主与身份校验。
bool UCatInventoryItemInstance::CanExecuteInventoryAction(const FGameplayTag& Action,
	const FCatInventoryEntry& Entry, APawn* UserPawn, FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	const ACatCharacter* Character = Cast<ACatCharacter>(UserPawn);
	if (!ItemDefinition || !Entry.Instance || Entry.Instance != this || Entry.StackCount <= 0
		|| !ItemDefinition->InventoryActions.ContainsByPredicate([&](const FCatInventoryActionDefinition& Row) { return Row.Action == Action; }))
	{
		OutReason = NSLOCTEXT("CatInventory", "ActionUnsupported", "此物品不支持这项操作");
		return false;
	}
	if (!Character || !Character->GetConditionComponent() || Character->GetConditionComponent()->GetSnapshot().bDowned)
	{
		OutReason = NSLOCTEXT("CatInventory", "ActionDowned", "当前身体状态无法操作");
		return false;
	}
	if (Action == CatInventoryActionTags::Use)
	{
		if (CanUseFromInventory(Entry, UserPawn)) return true;
		OutReason = NSLOCTEXT("CatInventory", "UseUnavailable", "当前无法使用此物品");
		return false;
	}
	if (Action == CatInventoryActionTags::Drop || Action == CatInventoryActionTags::Place)
	{
		if (IsValid(GetWorldActor()) || !ItemDefinition->WorldActorClass.IsNull()) return true;
		OutReason = NSLOCTEXT("CatInventory", "WorldActorUnavailable", "此物品尚无可用的地面载体");
		return false;
	}
	OutReason = NSLOCTEXT("CatInventory", "ActionUnsupported", "此物品不支持这项操作");
	return false;
}

// 通用分发先核对服务器与来源实例；特殊标识由子类重写，已声明的基础动作只进入各自虚函数，不在UI或RPC按物品类型分支。
FCatDomainCommandResult UCatInventoryItemInstance::ExecuteInventoryActionFromAuthority(const FGameplayTag& Action,
	const FCatInventoryEntry& Entry, const FCatInventoryItemUseContext& Context, const int32 Quantity)
{
	FCatDomainCommandResult Result;
	Result.RequestId = Context.RequestId;
	if (!Context.UserPawn || !Context.UserPawn->HasAuthority() || !Context.SourceInventory || Entry.Instance != this)
	{ Result.Error = ECatDomainCommandError::PermissionDenied; return Result; }
	if (Action == CatInventoryActionTags::Use) return UseFromInventorySlotFromAuthority(Entry, Context);
	if (Action == CatInventoryActionTags::Drop) return DropFromInventoryFromAuthority(Entry, Context, Quantity);
	if (Action == CatInventoryActionTags::Place) return PlaceFromInventoryFromAuthority(Entry, Context, Quantity);
	if (Action == CatInventoryActionTags::Carry) return CarryFromInventoryFromAuthority(Entry, Context);
	Result.Error = ECatDomainCommandError::InvalidPayload;
	return Result;
}

// 丢弃复用同一库存事务；这里只选择语义，数量扣减、载体准备和重试幂等仍由库存负责。
FCatDomainCommandResult UCatInventoryItemInstance::DropFromInventoryFromAuthority(const FCatInventoryEntry& Entry,
	const FCatInventoryItemUseContext& Context, const int32 Quantity)
{
	return Context.SourceInventory->ReleaseItemToWorldFromAuthority(Cast<ACatCharacter>(Context.UserPawn),
		Context.RequestId, Context.InventorySlotIndex, GetItemInstanceId(), Quantity, ECatInventoryWorldAction::Drop);
}

// 放置复用既有空间求解和库存提交；保持所选数量的原有放置语义。
FCatDomainCommandResult UCatInventoryItemInstance::PlaceFromInventoryFromAuthority(const FCatInventoryEntry& Entry,
	const FCatInventoryItemUseContext& Context, const int32 Quantity)
{
	return Context.SourceInventory->ReleaseItemToWorldFromAuthority(Cast<ACatCharacter>(Context.UserPawn),
		Context.RequestId, Context.InventorySlotIndex, GetItemInstanceId(), Quantity, ECatInventoryWorldAction::Place);
}

// 基类没有嘴部携带能力；拒绝而不创建世界物，鱼等具体实例按已有领域合同覆盖。
FCatDomainCommandResult UCatInventoryItemInstance::CarryFromInventoryFromAuthority(const FCatInventoryEntry& Entry,
	const FCatInventoryItemUseContext& Context)
{
	FCatDomainCommandResult Result; Result.RequestId = Context.RequestId;
	Result.Error = ECatDomainCommandError::InvalidPayload;
	return Result;
}

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

// 扣量策略读取流程：基础实例只在定义声明有效消耗片段时扣量；无 Use 配置继续不可用。
bool UCatInventoryItemInstance::ConsumesInventoryQuantityOnUse() const
{
	return GetInventoryUseQuantity() > 0;
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

// 世界载体读取流程：返回拾取保留的原物；已被销毁的 Actor 视为空，让落地沿用已有生成路径。
AActor* UCatInventoryItemInstance::GetWorldActor() const
{
	return IsValid(WorldActor) && !WorldActor->IsActorBeingDestroyed() ? WorldActor.Get() : nullptr;
}

// 世界载体关联流程：只替换当前世界引用；不复制 Actor，也不改实例身份和数量。
void UCatInventoryItemInstance::SetWorldActor(AActor* InWorldActor)
{
	WorldActor = InWorldActor;
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

// 通用使用预检流程：同一真实条目必须有足够数量且定义效果可用；没有片段的物品不会因此获得默认行为。
bool UCatInventoryItemInstance::CanUseFromInventory(const FCatInventoryEntry& InventoryEntry, APawn* UserPawn) const
{
 const UCatConsumableEffectFragment* Effect = ItemDefinition ? ItemDefinition->FindFragment<UCatConsumableEffectFragment>() : nullptr;
 return InventoryEntry.Instance == this && GetInventoryUseQuantity() > 0
  && InventoryEntry.StackCount >= GetInventoryUseQuantity() && Effect && Effect->ValidateForUser(UserPawn);
}

// 通用消费流程：核对上下文，复用库存唯一事务暂扣精确实例；实例效果成功才发布扣量，失败与重放沿用事务结果。
FCatDomainCommandResult UCatInventoryItemInstance::UseFromInventorySlotFromAuthority(
 const FCatInventoryEntry& InventoryEntry, const FCatInventoryItemUseContext& UseContext)
{
 FCatDomainCommandResult Result;
 Result.RequestId = UseContext.RequestId;
 if (!UseContext.SourceInventory || !UseContext.UserPawn || !UseContext.UserPawn->HasAuthority()
  || !UseContext.RequestId.IsValid() || InventoryEntry.Instance != this || GetInventoryUseQuantity() <= 0)
 {
  Result.Error = ECatDomainCommandError::InvalidPayload;
  return Result;
 }
 const FString Payload = FString::Printf(TEXT("UseSlot=%d|Definition=%s"), UseContext.InventorySlotIndex, *GetPathNameSafe(ItemDefinition));
 const FCatInventoryItemUseResult Used = UseContext.SourceInventory->UseItemInstanceFromAuthority(
  UseContext.RequestId, GetItemInstanceId(), GetInventoryUseQuantity(), Payload,
  [&](FCatInventoryItemUseResult& Mutable)
  {
   return CanUseFromInventory(InventoryEntry, UseContext.UserPawn)
    ? ECatDomainCommandError::None : ECatDomainCommandError::DependencyUnavailable;
  },
  [&](FCatInventoryItemUseResult& Mutable)
  {
   return CatIsAcceptedDomainCommandResult(ApplyUseEffectsFromAuthority(UseContext));
  });
 Result.RequestId = Used.RequestId;
 Result.bCommitted = Used.bCommitted;
 Result.bTerminalReplay = Used.bTerminalReplay;
 Result.bReplayedTerminalCommitted = Used.bReplayedTerminalCommitted;
 Result.Error = Used.Error;
 Result.ReplayedTerminalError = Used.ReplayedTerminalError;
 return Result;
}

// 扣量读取流程：只从当前定义消费片段读取，返回值不持久化也不另存于快捷栏。
int32 UCatInventoryItemInstance::GetInventoryUseQuantity() const
{
 const UCatConsumableEffectFragment* Effect = ItemDefinition ? ItemDefinition->FindFragment<UCatConsumableEffectFragment>() : nullptr;
 return Effect && Effect->IsRuntimeReady() ? Effect->ConsumeCount : 0;
}

// 效果提交流程：基础实例不生成玩法参数；需要实例数值的物品覆盖此口，数量始终由调用方事务处理。
FCatDomainCommandResult UCatInventoryItemInstance::ApplyUseEffectsFromAuthority(const FCatInventoryItemUseContext& UseContext)
{
 const UCatConsumableEffectFragment* Effect = ItemDefinition ? ItemDefinition->FindFragment<UCatConsumableEffectFragment>() : nullptr;
 if (Effect) return Effect->ApplyFromAuthority(UseContext.UserPawn, UseContext.RequestId, this, {});
 FCatDomainCommandResult Result;
 Result.RequestId = UseContext.RequestId;
 Result.Error = ECatDomainCommandError::InvalidPayload;
 return Result;
}

// 持续输入声明流程：基础实例没有按住后的第二阶段效果，返回 false 让 Controller 不保留无意义的输入会话。
bool UCatInventoryItemInstance::UsesContinuousInput() const
{
	return false;
}

// 本地连续表现流程：基础实例没有按住表现，保留空实现让 Controller 不认识具体物品类型也能对称通知子类。
void UCatInventoryItemInstance::SetUseInputActiveLocally(APlayerController* RequestingController, const bool bActive)
{
	(void)RequestingController;
	(void)bActive;
}

// 持续使用结束流程：基础实例没有 Begin 阶段状态可结束；返回明确失败，防止输入层把 Release 解释为另一种默认物品行为。
FCatDomainCommandResult UCatInventoryItemInstance::EndUseFromInventorySlotFromAuthority(
	const FCatInventoryItemUseContext& UseContext, const bool bCancelled)
{
	(void)bCancelled;
	FCatDomainCommandResult Result;
	Result.RequestId = UseContext.RequestId;
	Result.Error = ECatDomainCommandError::InvalidPayload;
	return Result;
}

// 定义绑定扩展流程：基础库存实例没有额外状态要派生；子类可以读取当前定义补齐自己的运行字段。
void UCatInventoryItemInstance::HandleItemDefinitionAssigned()
{
}
