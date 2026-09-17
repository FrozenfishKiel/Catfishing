#include "Equipment/CatEquipmentUseItemInstances.h"

#include "Character/CatCharacter.h"
#include "Fishing/Integration/CatFishingAimLibrary.h"
#include "AbilitySystem/Config/CatAbilitySet.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentItemDefinition.h"
#include "Equipment/CatEquippedDefinition.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Logging/CatLog.h"
#include "Logging/CatLogContext.h"

namespace CatEquipmentUseItemInstances
{
	/** 共同 Use 前置校验；只解析当前角色、装备组件和定义，不解释任何具体装备用途。 */
	bool ResolveUseDependencies(const UCatEquipmentInventoryItemInstance& Instance, const FCatInventoryEntry& Entry,
		const FCatInventoryItemUseContext& Context, ACatCharacter*& OutCharacter, UCatEquipmentComponent*& OutEquipment,
		const UCatEquipmentItemDefinition*& OutDefinition, FCatDomainCommandResult& OutResult)
	{
		OutResult.RequestId = Context.RequestId;
		OutCharacter = Cast<ACatCharacter>(Context.UserPawn);
		if (!OutCharacter && Context.RequestingController) OutCharacter = Cast<ACatCharacter>(Context.RequestingController->GetPawn());
		OutEquipment = OutCharacter ? OutCharacter->GetEquipmentComponent() : nullptr;
		OutDefinition = Cast<UCatEquipmentItemDefinition>(Instance.GetItemDefinition());
		OutResult.Revision = OutEquipment ? OutEquipment->GetSnapshot().Revision : 0;
		if (Entry.Instance != &Instance || Entry.StackCount <= 0 || !Instance.GetItemInstanceId().IsValid()) { OutResult.Error = ECatDomainCommandError::NotFound; return false; }
		if (!OutDefinition || !OutDefinition->IsRuntimeDefinitionReady() || !OutDefinition->GetEquipmentDefinition()) { OutResult.Error = ECatDomainCommandError::InvalidPayload; return false; }
		if (!OutCharacter || !OutEquipment) { OutResult.Error = ECatDomainCommandError::DependencyUnavailable; return false; }
		return true;
	}

	/** 按单一装配槽提交选择；调用方已决定目标槽，函数不根据定义用途分派行为。 */
	FCatDomainCommandResult ConfigureLoadout(UCatEquipmentComponent& Equipment, const UCatEquipmentItemDefinition& Definition,
		const UCatEquipmentInventoryItemInstance& Instance, const FCatInventoryItemUseContext& Context, const bool bBait)
	{
		const FCatEquipmentLoadoutSnapshot& Current = Equipment.GetSnapshot();
		return Equipment.ConfigureLoadoutFromAuthority(Context.RequestId, Current.Revision,
			Current.RodItemId, bBait ? Definition.ItemId : Current.BaitItemId,
			bBait ? Current.FloatItemId : Definition.ItemId, Current.ScoopNetItemId, NAME_None,
			Current.RodItemInstanceId, bBait ? Instance.GetItemInstanceId() : Current.BaitItemInstanceId,
			bBait ? Current.FloatItemInstanceId : Instance.GetItemInstanceId(), Current.ScoopNetItemInstanceId);
	}
}

// 部署流程：核对条目、定义与角色依赖，再把本实例 ID 和装备版本交给既有放置事务；失败不借用其他鱼竿。
FCatDomainCommandResult UCatFishingRodEquipmentItemInstance::UseFromInventorySlotFromAuthority(const FCatInventoryEntry& Entry, const FCatInventoryItemUseContext& Context)
{
	ACatCharacter* Character = nullptr; UCatEquipmentComponent* Equipment = nullptr; const UCatEquipmentItemDefinition* Definition = nullptr; FCatDomainCommandResult Result;
	if (!CatEquipmentUseItemInstances::ResolveUseDependencies(*this, Entry, Context, Character, Equipment, Definition, Result)) return Result;
	if (!Definition->CanServeFishingRod()) { Result.Error = ECatDomainCommandError::InvalidPayload; return Result; }
	ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(Context.RequestingController);
	UCatFishingCommandComponent* Commands = Controller ? Controller->GetFishingCommandComponent() : nullptr;
	if (!Commands) { Result.Error = ECatDomainCommandError::DependencyUnavailable; return Result; }
	FCatPlaceRodCommand Command; Command.RequestId = Context.RequestId; Command.RequestedRodItemInstanceId = GetItemInstanceId(); Command.ExpectedEquipmentRevision = Equipment->GetSnapshot().Revision;
	return Commands->PlaceRodFromInventoryUseOnAuthority(Controller, Command);
}

// 部署保管策略：鱼竿离开可见背包后仍由 held entry 保存同一实例，收竿必须归还该对象。
bool UCatFishingRodEquipmentItemInstance::KeepsInventoryInstanceWhileUsed() const { return true; }

FCatInventoryUseTarget UCatScoopNetEquipmentItemInstance::CaptureUseTarget(APlayerController* Controller) const
{
	FCatInventoryUseTarget Target;
	Target.bHasViewRay = UCatFishingAimLibrary::TryGetLocalCastViewRay(Controller, Target.ViewOrigin, Target.ViewDirection);
	Target.Actor = Target.bHasViewRay ? UCatFishingAimLibrary::ResolveFishingViewTarget(Controller, Target.ViewOrigin, Target.ViewDirection) : nullptr;
	return Target;
}

// 抄网流程：验证定义确有抄网能力并解析本人命令组件；只提交这件实例，捕获与 GE 冷却仍由原事务裁决。
FCatDomainCommandResult UCatScoopNetEquipmentItemInstance::UseFromInventorySlotFromAuthority(const FCatInventoryEntry& Entry, const FCatInventoryItemUseContext& Context)
{
	ACatCharacter* Character = nullptr; UCatEquipmentComponent* Equipment = nullptr; const UCatEquipmentItemDefinition* Definition = nullptr; FCatDomainCommandResult Result;
	if (!CatEquipmentUseItemInstances::ResolveUseDependencies(*this, Entry, Context, Character, Equipment, Definition, Result)) return Result;
	if (!Definition->CanServeScoopNet()) { Result.Error = ECatDomainCommandError::InvalidPayload; return Result; }
	ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(Context.RequestingController);
	if (UCatFishingCommandComponent* Commands = Controller ? Controller->GetFishingCommandComponent() : nullptr) return Commands->ScoopFromInventoryUseOnAuthority(Controller, Context, GetItemInstanceId());
	Result.Error = ECatDomainCommandError::DependencyUnavailable; return Result;
}

// 安装流程：先校验当前实例，再读取定义目标槽并保留其余装配；目标未配置或片段不匹配时拒绝。
FCatDomainCommandResult UCatLoadoutEquipmentItemInstance::UseFromInventorySlotFromAuthority(const FCatInventoryEntry& Entry, const FCatInventoryItemUseContext& Context)
{
	ACatCharacter* Character = nullptr; UCatEquipmentComponent* Equipment = nullptr; const UCatEquipmentItemDefinition* Definition = nullptr; FCatDomainCommandResult Result;
	if (!CatEquipmentUseItemInstances::ResolveUseDependencies(*this, Entry, Context, Character, Equipment, Definition, Result)) return Result;
	if (Definition->TargetSlot == ECatEquipmentLoadoutTargetSlot::Bait && Definition->CanServeFishingBait()) return CatEquipmentUseItemInstances::ConfigureLoadout(*Equipment, *Definition, *this, Context, true);
	if (Definition->TargetSlot == ECatEquipmentLoadoutTargetSlot::Float && Definition->CanServeFishingFloat()) return CatEquipmentUseItemInstances::ConfigureLoadout(*Equipment, *Definition, *this, Context, false);
	Result.Error = ECatDomainCommandError::InvalidPayload; return Result;
}

// 开始流程：验证此实例没有未结束请求，冻结上下文后逐套授予来源能力；任一失败回收本批，成功启动等待而不扣量。
FCatDomainCommandResult UCatChumEquipmentItemInstance::UseFromInventorySlotFromAuthority(const FCatInventoryEntry& Entry, const FCatInventoryItemUseContext& Context)
{
	ACatCharacter* Character = nullptr; UCatEquipmentComponent* Equipment = nullptr; const UCatEquipmentItemDefinition* Definition = nullptr; FCatDomainCommandResult Result;
	if (!CatEquipmentUseItemInstances::ResolveUseDependencies(*this, Entry, Context, Character, Equipment, Definition, Result)) return Result;
	if (!Definition->CanServeChumPlacement() || bHasActiveUseContext) { Result.Error = ECatDomainCommandError::InvalidPhase; return Result; }
	ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(Context.RequestingController);
	UCatFishingCommandComponent* Commands = Controller ? Controller->GetFishingCommandComponent() : nullptr;
	if (!Commands) { Result.Error = ECatDomainCommandError::DependencyUnavailable; return Result; }
	ActiveUseContext = Context; bHasActiveUseContext = true;
	UCatAbilitySystemComponent* ASC = Character->GetCatAbilitySystemComponent();
	for (const TSoftObjectPtr<UCatAbilitySet>& SetRef : Definition->GetEquipmentDefinition()->AbilitySetsToGrant)
	{
		const UCatAbilitySet* Set = SetRef.LoadSynchronous();
		FCatGrantedAbilitySetHandles SetHandles;
		if (!ASC || !Set || !Set->GiveToAbilitySystem(ASC, SetHandles, this))
		{
			SetHandles.TakeFromAbilitySystem(ASC); ActiveUseAbilityHandles.TakeFromAbilitySystem(ASC); bHasActiveUseContext = false;
			Result.Error = ECatDomainCommandError::DependencyUnavailable; return Result;
		}
		ActiveUseAbilityHandles.Append(MoveTemp(SetHandles));
	}
	Result = Commands->BeginChumUseFromInventoryOnAuthority(Controller, Context, GetItemInstanceId(), GetItemId());
	if (!Result.bCommitted) { ActiveUseAbilityHandles.TakeFromAbilitySystem(ASC); bHasActiveUseContext = false; }
	// 右键菜单没有后续松开输入，沿同一能力立即提交零蓄力投放；左键才等待原请求的 Release。
	if (Result.bCommitted && !Context.bContinuousInput) return EndUseFromInventorySlotFromAuthority(Context, false);
	return Result;
}

// 输入生命周期声明：左键的松开仍回到启动实例；选择其他格不会结束或改写这次使用。
bool UCatChumEquipmentItemInstance::UsesContinuousInput() const { return true; }

void UCatChumEquipmentItemInstance::SetUseInputActiveLocally(APlayerController* Controller, const bool bActive)
{
	if (bActive)
	{
		if (!Controller || !Controller->IsLocalController() || !Controller->GetPawn() || !Controller->GetWorld()) return;
		if (LocalPreviewController == Controller && LocalPreviewPawn == Controller->GetPawn() && LocalPreviewStartSeconds >= 0.0) return;
		LocalPreviewController = Controller;
		LocalPreviewPawn = Controller->GetPawn();
		LocalPreviewStartSeconds = Controller->GetWorld()->GetTimeSeconds();
	}
	else
	{
		if (LocalPreviewStartSeconds < 0.0 || LocalPreviewController != Controller) return;
		LocalPreviewController.Reset();
		LocalPreviewPawn.Reset();
		LocalPreviewStartSeconds = -1.0;
	}
	UE_LOG(LogCatFishing, Log, TEXT("Event=chum_local_preview_changed InstanceId=%s World=%s Authority=%d Active=%d %s"),
		*GetItemInstanceId().ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(Controller ? Controller->GetWorld() : nullptr),
		Controller && Controller->HasAuthority() ? 1 : 0, bActive ? 1 : 0, *CatLogContext::BuildControllerFields(Controller));
}

bool UCatChumEquipmentItemInstance::TryGetLocalChargePreview(APlayerController* Controller, float& OutHeldSeconds) const
{
	OutHeldSeconds = 0.0f;
	if (!Controller || !Controller->IsLocalController() || LocalPreviewController != Controller
		|| !LocalPreviewPawn.IsValid() || LocalPreviewPawn != Controller->GetPawn()
		|| !Controller->GetWorld() || LocalPreviewStartSeconds < 0.0) return false;
	OutHeldSeconds = FMath::Max(0.0, Controller->GetWorld()->GetTimeSeconds() - LocalPreviewStartSeconds);
	return true;
}

// 库存影响声明：只有能力真正提交投放时按已有规则扣量，开始等待和取消都不进入扣量事务。
bool UCatChumEquipmentItemInstance::ConsumesInventoryQuantityOnUse() const { return true; }

// 结束流程：核对原请求，将释放或取消交给同来源能力；能力结束会移出本次句柄，随后清除实例剩余上下文。
FCatDomainCommandResult UCatChumEquipmentItemInstance::EndUseFromInventorySlotFromAuthority(const FCatInventoryItemUseContext& Context, const bool bCancelled)
{
	FCatDomainCommandResult Result; Result.RequestId = Context.RequestId;
	ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(Context.RequestingController);
	UCatFishingCommandComponent* Commands = Controller ? Controller->GetFishingCommandComponent() : nullptr;
	if (!bHasActiveUseContext || ActiveUseContext.RequestId != Context.RequestId || !Commands) { Result.Error = ECatDomainCommandError::InvalidPayload; return Result; }
	// Release 可同步结束能力并清空实例上下文；传值副本保证后续提交回执仍使用原 RequestId。
	const FCatInventoryItemUseContext EndingContext = ActiveUseContext;
	Result = Commands->EndChumUseFromInventoryOnAuthority(Controller, EndingContext, bCancelled);
	if (ACatCharacter* Character = Cast<ACatCharacter>(Controller->GetPawn())) ActiveUseAbilityHandles.TakeFromAbilitySystem(Character->GetCatAbilitySystemComponent());
	bHasActiveUseContext = false; ActiveUseContext = FCatInventoryItemUseContext();
	return Result;
}

// 激活上下文读取：只复制尚未结束的服务器请求；无有效请求时保持拒绝，不从当前选中格推导来源。
bool UCatChumEquipmentItemInstance::TryGetActiveUseContext(FCatInventoryItemUseContext& OutUseContext) const
{
	if (!bHasActiveUseContext || !ActiveUseContext.RequestId.IsValid()) return false;
	OutUseContext = ActiveUseContext; return true;
}

// 能力收尾流程：仅匹配当前请求，移出本次句柄并立即清空实例；使用传入的原 ASC 延后撤销，旧回调不触及下一次使用。
void UCatChumEquipmentItemInstance::AbortActiveUseFromAbility(const FGuid RequestId, UCatAbilitySystemComponent* SourceAbilitySystem)
{
	// 正常释放和外部取消都到达此处；先离开实例再延后一帧清当前 Spec，避免 GAS 结束栈内重入。
	if (bHasActiveUseContext && ActiveUseContext.RequestId == RequestId)
	{
		// 把本次句柄移出实例，再用原 ASC 的世界调度；后续 GC、换 Owner 或新 Use 都不会让旧回调碰到新状态。
		FCatGrantedAbilitySetHandles Handles = MoveTemp(ActiveUseAbilityHandles);
		TWeakObjectPtr<UCatAbilitySystemComponent> WeakASC = SourceAbilitySystem;
		bHasActiveUseContext = false;
		ActiveUseContext = FCatInventoryItemUseContext();
		if (UWorld* World = WeakASC.IsValid() ? WeakASC->GetWorld() : nullptr)
		{
			World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([WeakASC, Handles = MoveTemp(Handles)]() mutable
			{
				if (UCatAbilitySystemComponent* ASC = WeakASC.Get()) Handles.TakeFromAbilitySystem(ASC);
			}));
		}
		else if (SourceAbilitySystem)
		{
			Handles.TakeFromAbilitySystem(SourceAbilitySystem);
		}
	}
}
