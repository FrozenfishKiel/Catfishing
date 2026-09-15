#include "AbilitySystem/Fishing/InputAbilities/CatFishingChumAbility.h"

#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "Equipment/CatEquipmentUseItemInstances.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Logging/CatLog.h"

UCatGA_FishingChum::UCatGA_FishingChum()
{
	// 构造流程：保留能力身份标签供来源 AbilitySet 精确授予；ServerOnly 使服务器任务成为蓄力时长的唯一权威。
	SetAssetTags(FGameplayTagContainer(CatFishingAbilityTags::Ability_Fishing_Chum));
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
}

void UCatGA_FishingChum::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// 激活流程：
	// 1. 只接受权威 ActorInfo，来源实例由 CommandComponent 按 SourceObject 精确激活，客户端不创建第二条计时路径。
	// 2. 从当前 AbilitySpec 的 SourceObject 读取物品实例已保存的 UseContext，并把它冻结在 Ability 内，后续换槽不影响本次扣量身份。
	// 3. 创建 UE 原生 WaitInputRelease；引擎在服务器端并行计时，收到同一 Spec 的 Release 后才回调最终提交。
	(void)TriggerEventData;
	if (!ActorInfo || !ActorInfo->IsNetAuthority())
	{
		return;
	}
	const FGameplayAbilitySpec* Spec = GetCurrentAbilitySpec();
	UCatChumEquipmentItemInstance* SourceItem = Spec ? Cast<UCatChumEquipmentItemInstance>(Spec->SourceObject.Get()) : nullptr;
	if (!SourceItem || !SourceItem->TryGetActiveUseContext(ActiveUseContext)
		|| !ActiveUseContext.RequestId.IsValid() || ActiveUseContext.RequestingController != ActorInfo->PlayerController.Get())
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
		return;
	}
	ActiveSourceItem = SourceItem;
	UAbilityTask_WaitInputRelease* WaitForRelease = UAbilityTask_WaitInputRelease::WaitInputRelease(this, true);
	if (!WaitForRelease)
	{
		CancelAbility(Handle, ActorInfo, ActivationInfo, true);
		return;
	}
	WaitForRelease->OnRelease.AddDynamic(this, &ThisClass::HandleInputReleased);
	bWaitingForInputRelease = true;
	WaitForRelease->ReadyForActivation();
}

bool UCatGA_FishingChum::MatchesActiveUseRequest(const FGuid RequestId) const
{
	// 匹配流程：只比较激活时冻结的请求身份；实例和槽位由提交口再复核，防止迟到 End 影响后一次 G Use。
	return RequestId.IsValid() && RequestId == ActiveUseContext.RequestId && ActiveSourceItem.IsValid();
}

bool UCatGA_FishingChum::IsWaitingForInputRelease() const
{
	// 查询流程：只返回 Task 已成功创建且尚未经过任意 End 路径的本地生命周期标记，不把它当作服务器计时或库存真相。
	return bWaitingForInputRelease;
}

void UCatGA_FishingChum::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const bool bReplicateEndAbility, const bool bWasCancelled)
{
	// 收尾流程：先清除 Task 等待观察值和冻结来源弱引用，再交给 GAS 终止任务与复制状态；取消和正常释放共用该路径。
	bWaitingForInputRelease = false;
	if (UCatChumEquipmentItemInstance* SourceItem = ActiveSourceItem.Get())
	{
		// 正常 Release 与取消共用 GAS 收尾；只有 Release Task 才会消费，Abort 只清上下文并用当前 ASC 安全回收来源句柄。
		SourceItem->AbortActiveUseFromAbility(ActiveUseContext.RequestId,
			Cast<UCatAbilitySystemComponent>(ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr));
	}
	ActiveSourceItem.Reset();
	ActiveUseContext = FCatInventoryItemUseContext();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UCatGA_FishingChum::HandleInputReleased(const float ServerHeldSeconds)
{
	// 松开流程：
	// 1. Task 已在服务器时钟上计算保持时长；先检查冻结来源仍有效，失效时直接取消而不猜测替代物。
	// 2. 再把冻结 Context、实例和定义交给命令提交口，复用原有弹道、环境与精确库存消费事务。
	// 3. 最后无论提交成功与否结束 Ability，确保下一次 Use 不会复用旧 RequestId 或 Task。
	bWaitingForInputRelease = false;
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	UCatChumEquipmentItemInstance* SourceItem = ActiveSourceItem.Get();
	UCatFishingCommandComponent* Commands = ResolveCommandComponent(ActorInfo);
	if (!ActorInfo || !ActorInfo->IsNetAuthority() || !SourceItem || !Commands)
	{
		CancelAbility(GetCurrentAbilitySpecHandle(), ActorInfo, GetCurrentActivationInfo(), true);
		return;
	}
	const FCatDomainCommandResult Result = Commands->CommitChumUseFromAbilityOnAuthority(
		Cast<APlayerController>(ActorInfo->PlayerController.Get()), ActiveUseContext, SourceItem->GetItemInstanceId(),
		SourceItem->GetItemDefinitionId(), FMath::Max(0.0f, ServerHeldSeconds));
	if (Result.bCommitted)
	{
		UE_LOG(LogCatFishing, Log, TEXT("Event=chum_source_ability_released RequestId=%s InstanceId=%s HeldSeconds=%.3f Committed=1 Error=%s"),
			*ActiveUseContext.RequestId.ToString(EGuidFormats::DigitsWithHyphens), *SourceItem->GetItemInstanceId().ToString(EGuidFormats::DigitsWithHyphens),
			ServerHeldSeconds, *UEnum::GetValueAsString(Result.Error));
	}
	else
	{
		UE_LOG(LogCatFishing, Warning, TEXT("Event=chum_source_ability_released RequestId=%s InstanceId=%s HeldSeconds=%.3f Committed=0 Error=%s"),
			*ActiveUseContext.RequestId.ToString(EGuidFormats::DigitsWithHyphens), *SourceItem->GetItemInstanceId().ToString(EGuidFormats::DigitsWithHyphens),
			ServerHeldSeconds, *UEnum::GetValueAsString(Result.Error));
	}
	EndAbility(GetCurrentAbilitySpecHandle(), ActorInfo, GetCurrentActivationInfo(), true, false);
}
