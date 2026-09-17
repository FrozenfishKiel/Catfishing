#include "AbilitySystem/Items/CatEquipmentItemAbilities.h"
#include "Inventory/CatBackPackComponent.h"
#include "Character/CatCharacter.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentItemDefinition.h"
#include "Inventory/CatInventoryComponent.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Fishing/Integration/CatFishingAimLibrary.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Misc/ScopeExit.h"
#include "Inventory/Fragments/CatItemUseFragment.h"

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

// 拿竿流程：复核物品的鱼竿配置，命令携带原实例及当前权威装备版本，领域服务继续拥有放置、借出与物理初始化。
FCatDomainCommandResult UCatGA_DeployFishingRod::ExecuteEquipmentUse(const FCatInventoryItemUseContext& Context, const UCatEquipmentItemDefinition& Definition)
{
	FCatDomainCommandResult Result; Result.RequestId = Context.RequestId; Result.Error = ECatDomainCommandError::InvalidPayload;
	auto* Character = Cast<ACatCharacter>(Context.UserPawn);
	auto* Controller = Cast<ACatfishingPlayerController>(Context.RequestingController);
	auto* Commands = Controller ? Controller->GetFishingCommandComponent() : nullptr;
	if (!Definition.CanServeFishingRod() || !Character || !Commands) return Result;
	// 借出前保留本次来源格，防止库存通知中的收货占掉原位；拿竿失败释放同一预留，仍由领域服务归还原实例。
	auto* BackPack = Cast<UCatBackPackComponent>(Context.SourceInventory);
	if (!BackPack || !BackPack->ReserveQuickbarHeldSlotFromAuthority(Context.InventorySlotIndex, UseTarget.ItemId)) return Result;
	FCatPlaceRodCommand Command; Command.RequestId = Context.RequestId; Command.RequestedRodItemInstanceId = UseTarget.ItemId;
	Command.ExpectedEquipmentRevision = Character->GetEquipmentComponent()->GetSnapshot().Revision;
	Result = Commands->PlaceRodFromInventoryUseOnAuthority(Controller, Command);
	if (!Result.bCommitted && BackPack->GetQuickbarHeldSlot().ItemInstanceId == UseTarget.ItemId)
		BackPack->ClearQuickbarHeldSlotFromAuthority();
	return Result;
}

// 本地抄网采样流程：读取输入当刻视线并解析可观察目标；这里只产生意图，网络端不信任该 Actor 已经命中。
void UCatGA_UseScoopNet::CaptureTarget(APlayerController* Controller, FCatItemAbilityTargetData& Target) const
{
	Target.Aim.bHasViewRay = UCatFishingAimLibrary::TryGetLocalCastViewRay(Controller, Target.Aim.ViewOrigin, Target.Aim.ViewDirection);
	Target.Aim.Actor = Target.Aim.bHasViewRay ? UCatFishingAimLibrary::ResolveFishingViewTarget(Controller, Target.Aim.ViewOrigin, Target.Aim.ViewDirection) : nullptr;
}

// 抄取流程：把同一来源和目标意图交给既有裁决；同步结果立即结束，异步捕获通过上下文回调结束。
FCatDomainCommandResult UCatGA_UseScoopNet::ExecuteEquipmentUse(const FCatInventoryItemUseContext& Context, const UCatEquipmentItemDefinition& Definition)
{
	FCatDomainCommandResult Result; Result.RequestId = Context.RequestId; Result.Error = ECatDomainCommandError::InvalidPayload;
	auto* Controller = Cast<ACatfishingPlayerController>(Context.RequestingController);
	auto* Commands = Controller ? Controller->GetFishingCommandComponent() : nullptr;
	return Definition.CanServeScoopNet() && Commands ? Commands->ScoopFromInventoryUseOnAuthority(Controller, Context, UseTarget.ItemId) : Result;
}

// 抄取收尾流程：服务器的前提交取消先移除排队请求；已确认捕获和提交锁内的收尾仍由原流程完成，避免取消推翻权威结果。
void UCatGA_UseScoopNet::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (ScopeLockCount == 0 && IsActive() && bWasCancelled && !bUseCommitted && ActorInfo && ActorInfo->IsNetAuthority())
		if (auto* Controller = Cast<ACatfishingPlayerController>(ActorInfo->PlayerController.Get()))
			if (auto* Commands = Controller->GetFishingCommandComponent()) Commands->CancelScoopUseFromAuthority(UseTarget.RequestId);
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

// 装配流程：只接受定义明确声明的饵或漂目标槽，替换该槽实例并保留其他选择；装配不消耗鱼饵数量。
FCatDomainCommandResult UCatGA_SelectFishingLoadout::ExecuteEquipmentUse(const FCatInventoryItemUseContext& Context, const UCatEquipmentItemDefinition& Definition)
{
	FCatDomainCommandResult Result; Result.RequestId = Context.RequestId; Result.Error = ECatDomainCommandError::InvalidPayload;
	auto* Character = Cast<ACatCharacter>(Context.UserPawn);
	auto* Equipment = Character ? Character->GetEquipmentComponent() : nullptr;
	const bool bBait = Definition.TargetSlot == ECatEquipmentLoadoutTargetSlot::Bait && Definition.CanServeFishingBait();
	const bool bFloat = Definition.TargetSlot == ECatEquipmentLoadoutTargetSlot::Float && Definition.CanServeFishingFloat();
	if (!Equipment || (!bBait && !bFloat)) return Result;
	const auto& Current = Equipment->GetSnapshot();
	return Equipment->ConfigureLoadoutFromAuthority(Context.RequestId, Current.Revision,
		Current.RodItemId, bBait ? Definition.ItemId : Current.BaitItemId,
		bFloat ? Definition.ItemId : Current.FloatItemId, Current.ScoopNetItemId, NAME_None,
		Current.RodItemInstanceId, bBait ? UseTarget.ItemId : Current.BaitItemInstanceId,
		bFloat ? UseTarget.ItemId : Current.FloatItemInstanceId, Current.ScoopNetItemInstanceId);
}
