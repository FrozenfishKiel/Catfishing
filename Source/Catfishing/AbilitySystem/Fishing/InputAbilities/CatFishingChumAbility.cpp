#include "AbilitySystem/Fishing/InputAbilities/CatFishingChumAbility.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "AbilitySystemComponent.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "Inventory/Fragments/CatItemUseFragment.h"

// 窝料配置流程：使用数量进入精确来源成本，必须大于零；窝点规则由领域片段提供，拒绝不会执行的自用效果。
bool UCatGA_FishingChum::ValidateUseConfiguration(const UCatItemUseFragment& Configuration, FText& OutError) const
{
	if (Configuration.ConsumeCount > 0 && Configuration.Effects.IsEmpty() && Configuration.Magnitudes.IsEmpty()) return true;
	OutError = NSLOCTEXT("CatItem", "ChumUseConfig", "窝料消耗必须大于 0，使用效果与效果参数必须为空；窝点作用请配置窝料影响片段。");
	return false;
}

// 蓄力流程：目标已由共同入口验证；菜单直接走零蓄力，连续输入让两端建立同一激活键的释放任务，服务器不采信客户端持续时间。
void UCatGA_FishingChum::CommitUse()
{
	if (!IsActive() || bWaitingForInputRelease) return;
	if (!UseTarget.bContinuousInput) { HandleInputReleased(0.0f); return; }
	bWaitingForInputRelease = true;
	PreviewStartSeconds = GetWorld()->GetTimeSeconds();
	if (CurrentActorInfo->IsNetAuthority() && !CurrentActorInfo->IsLocallyControlled())
		if (auto* Spec = CurrentActorInfo->AbilitySystemComponent->FindAbilitySpecFromHandle(CurrentSpecHandle)) Spec->InputPressed = true;
	auto* Release = UAbilityTask_WaitInputRelease::WaitInputRelease(this, true);
	Release->OnRelease.AddDynamic(this, &ThisClass::HandleInputReleased);
	Release->ReadyForActivation();
}

// 释放流程：客户端停止预览并等待权威结束；服务器重查原实例和当前槽，再让领域服务计算弹道、校验水域并准备窝点。
// 服务同步调用支付回调时才 CommitAbility，物品成本延迟通知；提交锁延后取消，领域终态返回后记录成功并结束能力。
void UCatGA_FishingChum::HandleInputReleased(float HeldSeconds)
{
	bWaitingForInputRelease = false;
	if (!CurrentActorInfo || !CurrentActorInfo->IsNetAuthority()) return;
	IncrementListLock(); ON_SCOPE_EXIT { DecrementListLock(); };
	auto* Item = ResolveSourceItem();
	auto* Controller = Cast<ACatfishingPlayerController>(CurrentActorInfo->PlayerController.Get());
	auto* Commands = Controller ? Controller->GetFishingCommandComponent() : nullptr;
	if (!Item || !Commands || !ValidateUse()) { EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); return; }
	FCatInventoryItemUseContext Context;
	Context.RequestId = UseTarget.RequestId; Context.RequestingController = Controller;
	Context.UserPawn = Controller->GetPawn(); Context.SourceInventory = UseTarget.Inventory;
	Context.InventorySlotIndex = UseTarget.Inventory->FindInventorySlotIndexFromInstanceId(UseTarget.ItemId);
	const auto Result = Commands->CommitChumUseFromAbilityOnAuthority(Controller, Context, UseTarget.ItemId, Item->GetItemId(), FMath::Max(0.f, HeldSeconds), [this]()
	{
		return CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo) && bResourceCommitted;
	});
	bUseCommitted = Result.bCommitted;
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, !bUseCommitted);
}

// 预览读取流程：只允许当前本地控制者读取仍等待的能力；换 Pawn 或结束后不沿物品实例残留显示弹道。
bool UCatGA_FishingChum::TryGetLocalChargePreview(APlayerController* Controller, float& OutHeldSeconds) const
{
	OutHeldSeconds = 0.f;
	if (!Controller || !Controller->IsLocalController() || !IsActive() || !bWaitingForInputRelease
		|| Controller->GetPawn() != GetAvatarActorFromActorInfo()) return false;
	OutHeldSeconds = FMath::Max(0.0, GetWorld()->GetTimeSeconds() - PreviewStartSeconds);
	return true;
}

// 收尾流程：没有提交锁时释放本地预览状态，然后由共同入口销毁任务和发送唯一物品回执；锁内保持冻结来源到提交结束。
void UCatGA_FishingChum::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (ScopeLockCount == 0) { bWaitingForInputRelease = false; PreviewStartSeconds = 0.0; }
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
