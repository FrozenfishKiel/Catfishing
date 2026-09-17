#include "AbilitySystem/Items/Abilities/CatEquipmentItemAbility.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Equipment/CatEquipmentItemDefinition.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/Fragments/CatItemUseFragment.h"
#include "Misc/ScopeExit.h"

// 装备配置流程：只接受零数量、空效果及空效果参数；这些行为只改变持有、装配或捕获状态，不支持额外自用效果。
bool UCatEquipmentItemAbility::ValidateUseConfiguration(const UCatItemUseFragment& Configuration, FText& OutError) const
{
	if (Configuration.ConsumeCount == 0 && Configuration.Effects.IsEmpty() && Configuration.Magnitudes.IsEmpty()) return true;
	OutError = NSLOCTEXT("CatItem", "EquipmentUseConfig", "拿竿、抄取和装配的使用消耗必须为 0，使用效果与效果参数必须为空；鱼饵在真咬阶段消耗。");
	return false;
}

// 装备使用流程：服务器重查原物品并提交来源成本，再冻结领域上下文与弱引用回调，执行部署、装配或抄取。
// 正式配置使用零数量成本；同步终态立即结束，异步结果保留能力等待回调，库存不再执行行为回调。
void UCatEquipmentItemAbility::CommitUse()
{
	if (!CurrentActorInfo || !CurrentActorInfo->IsNetAuthority() || !IsActive() || bUseCommitted) return;
	IncrementListLock(); ON_SCOPE_EXIT { DecrementListLock(); };
	const auto* Item = ResolveSourceItem();
	const auto* Definition = Item ? Cast<UCatEquipmentItemDefinition>(Item->GetItemDefinition()) : nullptr;
	if (!Definition || !ValidateUse() || !CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo) || !bResourceCommitted)
	{ EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); return; }
	FCatInventoryItemUseContext Context;
	Context.RequestId = UseTarget.RequestId; Context.RequestingController = CurrentActorInfo->PlayerController.Get();
	Context.UserPawn = Cast<APawn>(GetAvatarActorFromActorInfo()); Context.SourceInventory = UseTarget.Inventory;
	Context.InventorySlotIndex = UseTarget.Inventory->FindInventorySlotIndexFromInstanceId(UseTarget.ItemId);
	Context.Target = UseTarget.Aim; Context.bContinuousInput = UseTarget.bContinuousInput;
	const TWeakObjectPtr<UCatEquipmentItemAbility> WeakThis(this);
	Context.OnCompleted = [WeakThis](const FCatDomainCommandResult& Result)
	{
		if (auto* Ability = WeakThis.Get()) Ability->CompleteEquipmentUse(Result);
	};
	const auto Result = ExecuteEquipmentUse(Context, *Definition);
	if (!Result.bPending) CompleteEquipmentUse(Result);
}

// 结束流程：仅接受仍活动且请求身份相同的领域结果，再写入成功状态并结束能力；已结束或下一次激活的回调被忽略，不撤销领域已提交结果。
void UCatEquipmentItemAbility::CompleteEquipmentUse(const FCatDomainCommandResult& Result)
{
	if (!IsActive() || Result.RequestId != UseTarget.RequestId) return;
	bUseCommitted = Result.bCommitted;
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, !Result.bCommitted);
}

