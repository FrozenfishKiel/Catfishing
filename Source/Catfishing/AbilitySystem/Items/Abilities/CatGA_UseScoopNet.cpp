#include "AbilitySystem/Items/Abilities/CatGA_UseScoopNet.h"
#include "Equipment/CatEquipmentItemDefinition.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Fishing/Integration/CatFishingAimLibrary.h"
#include "Framework/Game/CatfishingPlayerController.h"

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

