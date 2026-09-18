#include "Inventory/Fragments/CatItemDropFragment.h"
#include "AbilitySystem/Attributes/CatItemBonusAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Inventory/CatInventoryStatics.h"
#include "Logging/CatLog.h"

// 静态检查只确认表的可解释性；资产完整加载后的库存契约由收货入口复核，避免循环引用递归校验。
bool UCatItemDropFragment::IsRuntimeReady() const
{
	for (const auto& Drop : ExtraDrops)
		if (Drop.Item.IsNull() || !FMath::IsFinite(Drop.Probability) || Drop.Probability < 0.f || Drop.Probability > 1.f
			|| Drop.MinimumCount < 1 || Drop.MaximumCount < Drop.MinimumCount) return false;
	return true;
}
// 结算流程：捕获入口保证事件只发生一次；这里冻结执行者加成，逐项归一为概率，命中后构造一批收货。
// 不根据鱼或奖励的名字分支，也不将开背包或拾取误认成再次捕获；失败记录事件 ID 供定位。
bool UCatItemDropFragment::AwardCaptureDropsFromAuthority(AActor* Executor, const FGuid& CaptureId) const
{
	if (!Executor || !Executor->HasAuthority() || !CaptureId.IsValid() || !IsRuntimeReady()) return false;
	const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Executor);
	const double Bonus = ASC && ASC->HasAttributeSetForAttribute(UCatItemBonusAttributeSet::GetDropChanceBonusAttribute())
		? ASC->GetNumericAttribute(UCatItemBonusAttributeSet::GetDropChanceBonusAttribute()) : 0.0;
	if (!FMath::IsFinite(Bonus)) return false;
	FRandomStream Random(GetTypeHash(CaptureId));
	FCatInventoryReceiveBatch Batch;
	for (const auto& Drop : ExtraDrops)
	{
		const double Probability = FMath::Clamp(Drop.Probability + Bonus, 0.0, 1.0);
		if (Random.GetFraction() >= Probability) continue;
		auto* Definition = Drop.Item.LoadSynchronous();
		if (!Definition) return false;
		Batch.DefinitionEntries.Add({Random.RandRange(Drop.MinimumCount, Drop.MaximumCount), Definition, nullptr});
	}
	const bool bDelivered = UCatInventoryStatics::ReceiveInventoryWithOverflowFromAuthority(Executor, Batch);
	const FString Record = FString::Printf(TEXT("Event=capture_extra_drops CaptureId=%s Executor=%s Entries=%d Bonus=%.4f Result=%s World=%s NetMode=%d Authority=1"),
		*CaptureId.ToString(), *GetNameSafe(Executor), Batch.DefinitionEntries.Num(), Bonus, bDelivered ? TEXT("Delivered") : TEXT("Failed"), *GetNameSafe(Executor->GetWorld()), Executor->GetNetMode());
	if (bDelivered) { UE_LOG(LogCatfishing, Log, TEXT("%s"), *Record); }
	else { UE_LOG(LogCatfishing, Error, TEXT("%s"), *Record); }
	return bDelivered;
}
