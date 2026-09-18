#include "Inventory/Fragments/CatItemContainerOpenFragment.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "Logging/CatLog.h"

// 静态校验流程：只允许独立实例承载一次开箱效果，不给普通鱼追加隐式行为。
bool UCatItemContainerOpenFragment::IsRuntimeReady() const
{
	const auto* Definition = GetTypedOuter<UCatInventoryItemDefinition>();
	return Effect && Definition && Definition->GetMaxStackCount() == 1;
}
// 开箱流程：服务器取得开箱者 ASC，按格子寻找首件配置，先构造效果，再以请求 ID 扣除一件。
// 扣量失败或重放均不放效果；成功施加后才广播，后面的假鱼保留到下一次独立开箱。
void UCatItemContainerOpenFragment::TriggerFirstFromAuthority(UCatInventoryComponent* Inventory, AActor* Opener, FGuid RequestId)
{
	if (!Inventory || !Inventory->GetOwner() || !Inventory->GetOwner()->HasAuthority() || !Opener || !RequestId.IsValid()) return;
	auto* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Opener);
	if (!ASC) return;
	for (const auto& Entry : Inventory->GetInventoryEntries())
	{
		const auto* Definition = Entry.Instance ? Entry.Instance->GetItemDefinition() : nullptr;
		const auto* Trigger = Definition ? Definition->FindFragment<UCatItemContainerOpenFragment>() : nullptr;
		if (!Trigger || !Trigger->IsRuntimeReady()) continue;
		auto Context = ASC->MakeEffectContext(); Context.AddSourceObject(Entry.Instance);
		const auto Spec = ASC->MakeOutgoingSpec(Trigger->Effect, 1.f, Context);
		if (!Spec.IsValid()) return;
		const FGuid ItemId = Entry.Instance->GetItemInstanceId();
		const auto Result = Inventory->ConsumeAbilityItemFromAuthority(RequestId, ItemId, 1, false);
		if (!Result.bCommitted || Result.bTerminalReplay) return;
		ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		Inventory->BroadcastInventoryChange();
		UE_LOG(LogCatfishing, Log, TEXT("Event=container_open_item_trigger RequestId=%s Container=%s Opener=%s Item=%s World=%s NetMode=%d Authority=1"),
			*RequestId.ToString(), *GetNameSafe(Inventory->GetOwner()), *GetNameSafe(Opener), *ItemId.ToString(),
			*GetNameSafe(Opener->GetWorld()), Opener->GetNetMode());
		return;
	}
}
