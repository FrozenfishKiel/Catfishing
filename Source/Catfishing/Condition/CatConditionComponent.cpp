#include "Condition/CatConditionComponent.h"

#include "AbilitySystem/Config/CatAbilitySettings.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Character/CatCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Condition/CatHerbRecoveryItemFragment.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Logging/CatLog.h"
#include "Logging/CatLogContext.h"
#include "Condition/CatConditionSettings.h"
#include "Data/CatFishDefinition.h"
#include "Fishing/CatFishingService.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Growth/CatGrowthComponent.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Net/UnrealNetwork.h"

namespace CatConditionComponentPrivate
{
// 草药库存终态键只按 RequestId 分组；载荷差异交给签名检查，使网络重试和冲突请求能被明确区分。
FString MakeHerbInventoryTerminalKey(const FGuid RequestId)
{
	return FString::Printf(TEXT("HerbInventory|%s"), *RequestId.ToString(EGuidFormats::DigitsWithHyphens));
}

// 草药库存载荷签名记录施药者、目标和实例身份；同一个 RequestId 如果换目标或换药，会被视为非法重放。
FString MakeHerbInventoryPayloadSignature(const AController* HelpingController, const ACatCharacter* TargetCharacter,
	const FGuid HerbItemInstanceId)
{
	return FString::Printf(TEXT("Helper=%s|Target=%s|Herb=%s"),
		*GetPathNameSafe(HelpingController), *GetPathNameSafe(TargetCharacter),
		*HerbItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens));
}
}

// 构造流程：开启默认复制并关闭 Tick；Snapshot 初始 Revision=0 表示尚未提交身体离散事实。
UCatConditionComponent::UCatConditionComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
}

// 复制声明流程：保留父类字段并注册单一 Snapshot；缓存与原始调用者信息只留 authority。
void UCatConditionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, Snapshot);
}

// Snapshot 读取流程：返回本机服务器真相或客户端最近复制值，不把 ASC 数值复制进第二个 DTO。
const FCatConditionSnapshot& UCatConditionComponent::GetSnapshot() const
{
	return Snapshot;
}

// Wet 写入流程：只接受落水、天气等 authority 反馈和真实变化；提交后增加 Revision/强制更新，明确不触碰 Poison、成长、搏斗体力、移动能力或 BodyAction。
void UCatConditionComponent::SetWetFromAuthority(const bool bNewWet)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || Snapshot.bWet == bNewWet)
	{
		return;
	}
	Snapshot.bWet = bNewWet;
	++Snapshot.Revision;
	PublishSnapshot();
	UE_LOG(LogCatCharacter, Log, TEXT("Event=character_wet_changed Character=%s Wet=%s Revision=%lld"),
		*Owner->GetName(), Snapshot.bWet ? TEXT("true") : TEXT("false"), Snapshot.Revision);
}

ECatWaterExposureUpdate UCatConditionComponent::UpdateWaterExposureFromAuthority(
	const FCatWaterRegionHandle& WaterRegion, const double DeltaSeconds,
	double& OutImmersionDepthCentimeters)
{
	// 水域暴露更新流程：
	// 1. 先确认 Character、authority、阈值配置、水域子系统和固定步时长齐全，缺任一项都不猜湿身结果。
	// 2. 再用脚点查询指定 WaterRegion 的浸没深度，并按湿润阈值、危险进入阈值和退出滞回维护离散状态。
	// 3. 状态没有变化时只返回 Unchanged；危险首次进入用 Warning 记录并交给 Fishing 终局入口处理。
	OutImmersionDepthCentimeters = 0.0;
	ACatCharacter* Character = Cast<ACatCharacter>(GetOwner());
	const UCatConditionSettings* Settings = GetDefault<UCatConditionSettings>();
	UCatWaterQuerySubsystem* Water = GetWorld() ? GetWorld()->GetSubsystem<UCatWaterQuerySubsystem>() : nullptr;
	if (!Character || !Character->HasAuthority() || !Settings || !Settings->HasWaterExposureThresholds()
		|| !Water || !WaterRegion.IsValid() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0)
	{
		return ECatWaterExposureUpdate::Unavailable;
	}
	// 身体组件提供真实支撑脚点；停用的 Character 胶囊不再代表物理猫的身高。
	const FVector FootPoint = Character->GetBodyFootPointWorld();
	const FCatWaterImmersionResult Immersion = Water->QueryImmersionAtWorldPoint(FootPoint, WaterRegion);
	if (!Immersion.bSucceeded)
	{
		return ECatWaterExposureUpdate::Unavailable;
	}
	OutImmersionDepthCentimeters = Immersion.ImmersionDepthCentimeters;
	const bool bWet = Immersion.Containment != ECatWaterContainment::Outside
		&& OutImmersionDepthCentimeters >= Settings->WetWaterDepthCentimeters;
	ECatWaterExposureState NewExposure = bWet ? ECatWaterExposureState::Shallow : ECatWaterExposureState::Dry;
	if (Snapshot.WaterExposure == ECatWaterExposureState::Dangerous
		&& bWet && OutImmersionDepthCentimeters > Settings->DangerousWaterExitDepthCentimeters)
	{
		NewExposure = ECatWaterExposureState::Dangerous;
	}
	else if (bWet && OutImmersionDepthCentimeters >= Settings->DangerousWaterDepthCentimeters)
	{
		DangerousWaterBuildUpSeconds += DeltaSeconds;
		// 危险水域按 World 秒累计；极小容差只吸收浮点边界，避免正好到确认阈值的那帧被漏判。
		if (DangerousWaterBuildUpSeconds + UE_DOUBLE_KINDA_SMALL_NUMBER
			>= Settings->DangerousWaterConfirmationSeconds)
		{
			NewExposure = ECatWaterExposureState::Dangerous;
		}
	}
	else
	{
		DangerousWaterBuildUpSeconds = 0.0;
	}

	const bool bDangerousEntered = Snapshot.WaterExposure != ECatWaterExposureState::Dangerous
		&& NewExposure == ECatWaterExposureState::Dangerous;
	if (Snapshot.bWet == bWet && Snapshot.WaterExposure == NewExposure)
	{
		return ECatWaterExposureUpdate::Unchanged;
	}
	Snapshot.bWet = bWet;
	Snapshot.WaterExposure = NewExposure;
	++Snapshot.Revision;
	PublishSnapshot();
	const FString ControllerFields = CatLogContext::BuildControllerFields(Character->GetController());
	if (bDangerousEntered)
	{
		UE_LOG(LogCatCharacter, Warning,
			TEXT("Event=character_water_exposure_changed Character=%s Region=%s Exposure=%s DepthCm=%.2f Revision=%lld Authority=true %s"),
			*Character->GetName(), *WaterRegion.RegionId.ToString(),
			*UEnum::GetValueAsString(NewExposure), OutImmersionDepthCentimeters, Snapshot.Revision,
			*ControllerFields);
	}
	else
	{
		UE_LOG(LogCatCharacter, Log,
			TEXT("Event=character_water_exposure_changed Character=%s Region=%s Exposure=%s DepthCm=%.2f Revision=%lld Authority=true %s"),
			*Character->GetName(), *WaterRegion.RegionId.ToString(),
			*UEnum::GetValueAsString(NewExposure), OutImmersionDepthCentimeters, Snapshot.Revision,
			*ControllerFields);
	}
	return bDangerousEntered ? ECatWaterExposureUpdate::DangerousEntered
		: ECatWaterExposureUpdate::Changed;
}

// 食用预检流程：只读核对 authority、正式身体 runtime、鱼定义、ASC、倒地阈值与 Growth 入口；不修改实物鱼、Attribute、Snapshot 或终态缓存。
ECatDomainCommandError UCatConditionComponent::ValidateFishConsumption(const UCatFishDefinition* FishDefinition) const
{
	const UCatConditionSettings* Settings = GetDefault<UCatConditionSettings>();
	const ACatCharacter* Character = Cast<ACatCharacter>(GetOwner());
	const UCatGrowthComponent* Growth = Character ? Character->GetGrowthComponent() : nullptr;
	return GetOwner() && GetOwner()->HasAuthority() && GetDefault<UCatAbilitySettings>()->IsRuntimeEnabled()
		&& FishDefinition && FishDefinition->IsRuntimeDefinitionReady() && ResolveAbilitySystem()
		&& Settings && Settings->HasDownedThresholds()
		&& Growth && Growth->ValidateFishGrowth(FishDefinition) == ECatDomainCommandError::None
		? ECatDomainCommandError::None : ECatDomainCommandError::DependencyUnavailable;
}

// 草药预检流程：只读核对 authority、施药者 Pawn、正式身体 runtime、ASC、倒地阈值、恢复量和服务器距离；不扣库存、不写 Attribute，也不制造临时占用状态。
ECatDomainCommandError UCatConditionComponent::ValidateHerbRecovery(AController* HelpingController) const
{
	const UCatConditionSettings* Settings = GetDefault<UCatConditionSettings>();
	const APawn* HelpingPawn = HelpingController ? HelpingController->GetPawn() : nullptr;
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority() && GetDefault<UCatAbilitySettings>()->IsRuntimeEnabled()
		&& ResolveAbilitySystem() && Settings && Settings->HasDownedThresholds()
		&& FMath::IsFinite(Settings->HerbPoisonRelief) && Settings->HerbPoisonRelief > 0.0
		&& FMath::IsFinite(Settings->HerbUseRangeCentimeters) && Settings->HerbUseRangeCentimeters > 0.0
		&& HelpingPawn && HelpingPawn->GetWorld() == Owner->GetWorld()
		&& FVector::DistSquared(HelpingPawn->GetActorLocation(), Owner->GetActorLocation())
			<= FMath::Square(Settings->HerbUseRangeCentimeters)
		? ECatDomainCommandError::None : ECatDomainCommandError::PolicyUndecided;
}

FCatDomainCommandResult UCatConditionComponent::UseHerbOnCharacterFromAuthority(AController* HelpingController,
	const FGuid RequestId, const FGuid HerbItemInstanceId)
{
	// 草药库存恢复流程：
	// 1. 先确认请求键、施药者当前 Pawn、双方组件、正式库存和目标 World；草药消耗没有正式库存时直接失败。
	// 2. 重放命中时直接返回首次库存和身体提交终态，避免网络重试再次扣草药或重新恢复目标。
	// 3. 不是重放时才检查玩法 gate、正式库存里的草药实例、施药者状态、范围和目标恢复预检，随后在库存组件扣草药并提交身体恢复。
	// 4. Equipment 只在正式扣药成功后刷新钓具选择读模型；草药数量和幂等终态都由正式库存链路裁决。
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	UWorld* World = GetWorld();
	ACatCharacter* TargetCharacter = Cast<ACatCharacter>(GetOwner());
	ACatCharacter* ControlledCharacter = HelpingController ? Cast<ACatCharacter>(HelpingController->GetPawn()) : nullptr;
	UCatEquipmentComponent* Equipment = ControlledCharacter ? ControlledCharacter->GetEquipmentComponent() : nullptr;
	UCatInventoryComponent* OwnerInventory = ControlledCharacter ? ControlledCharacter->GetInventoryComponent() : nullptr;
	UCatConditionComponent* SourceConditions = ControlledCharacter ? ControlledCharacter->GetConditionComponent() : nullptr;
	if (!RequestId.IsValid() || !HerbItemInstanceId.IsValid() || !ControlledCharacter || !Equipment
		|| !SourceConditions || !TargetCharacter || TargetCharacter->GetWorld() != World)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	if (!OwnerInventory)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}

	const FString HerbTerminalKey = CatConditionComponentPrivate::MakeHerbInventoryTerminalKey(RequestId);
	const FString HerbPayloadSignature = CatConditionComponentPrivate::MakeHerbInventoryPayloadSignature(
		HelpingController, TargetCharacter, HerbItemInstanceId);
	FCatDomainCommandResult CachedResult;
	const ECatTerminalReplayOutcome ReplayOutcome = CatQueryTerminalReplay(TerminalCache,
		TerminalPayloadByKey, HerbTerminalKey, HerbPayloadSignature, CachedResult,
		[](FCatDomainCommandResult& Cached)
		{
			MarkCommandReplayed(Cached);
		});
	if (ReplayOutcome == ECatTerminalReplayOutcome::Replayed)
	{
		return CachedResult;
	}
	if (ReplayOutcome == ECatTerminalReplayOutcome::PayloadMismatch)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}

	const auto StoreHerbTerminal = [&](const FCatDomainCommandResult& TerminalResult)
	{
		// 草药库存分支把失败和成功都缓存为同一个终态；后续同 RequestId 重试只回放结果，避免读取已经变化的库存格。
		TerminalCache.Add(HerbTerminalKey, TerminalResult);
		TerminalPayloadByKey.Add(HerbTerminalKey, HerbPayloadSignature);
		return TerminalResult;
	};
	const auto LogBodyFailure = [&]
	{
		// 身体提交失败时保持 Equipment 终态；日志只记录本次草药与身体结果。
		UE_LOG(LogCatCharacter, Error,
			TEXT("Event=herb_recovery_body_commit_failed RequestId=%s Helper=%s Target=%s HerbItem=%s BodyError=%s BodyReplay=%s BodyReplayError=%s BodyRevision=%lld"),
			*RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			*GetNameSafe(ControlledCharacter), *GetNameSafe(TargetCharacter),
			*HerbItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
			*UEnum::GetValueAsString(Result.Error),
			Result.bTerminalReplay ? TEXT("true") : TEXT("false"),
			*UEnum::GetValueAsString(Result.ReplayedTerminalError), Result.Revision);
	};

	const ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	const UCatConditionSettings* ConditionSettings = GetDefault<UCatConditionSettings>();
	if (!GameMode || !GameMode->CanAcceptGameplayCommand(HelpingController))
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		return Result;
	}
	if (SourceConditions->GetSnapshot().bDowned || !ConditionSettings
		|| !FMath::IsFinite(ConditionSettings->HerbUseRangeCentimeters)
		|| ConditionSettings->HerbUseRangeCentimeters <= 0.0
		|| FVector::DistSquared(ControlledCharacter->GetActorLocation(), TargetCharacter->GetActorLocation())
			> FMath::Square(ConditionSettings->HerbUseRangeCentimeters))
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}

	const int32 HerbSlotIndex = OwnerInventory->FindInventorySlotIndexFromInstanceId(HerbItemInstanceId);
	const FCatInventoryEntry* HerbEntry = OwnerInventory->GetInventoryEntryAtSlot(HerbSlotIndex);
	const UCatInventoryItemInstance* HerbInstance = HerbEntry != nullptr ? HerbEntry->Instance.Get() : nullptr;
	const UCatInventoryItemDefinition* Definition = HerbInstance != nullptr
		? HerbInstance->GetItemDefinition() : nullptr;
	const bool bHasHerbRecoveryFragment = Definition != nullptr
		&& Definition->FindFragmentByClass(UCatHerbRecoveryItemFragment::StaticClass()) != nullptr;
	const bool bHasCurrentHerb = HerbEntry != nullptr
		&& HerbEntry->StackCount > 0
		&& HerbInstance != nullptr
		&& HerbInstance->GetItemInstanceId() == HerbItemInstanceId
		&& Definition != nullptr
		&& Definition->IsInventoryRuntimeDefinitionReady()
		&& bHasHerbRecoveryFragment;
	if (!bHasCurrentHerb)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}
	Result.Error = ValidateHerbRecovery(HelpingController);
	if (Result.Error != ECatDomainCommandError::None)
	{
		return Result;
	}
	const TArray<FCatInventoryEntry> SavedEntries = OwnerInventory->GetInventoryEntries();
	if (!OwnerInventory->ConsumeItemAtSlot(HerbSlotIndex, 1))
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return StoreHerbTerminal(Result);
	}

	if (!Equipment->RefreshLoadoutFromInventoryComponentFromAuthority())
	{
		OwnerInventory->ReplaceInventoryEntriesFromAuthority(SavedEntries, SavedEntries.Num());
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return StoreHerbTerminal(Result);
	}

	UE_LOG(LogCatCharacter, Log,
		TEXT("Event=herb_inventory_consumed RequestId=%s Helper=%s Target=%s HerbItem=%s"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens),
		*GetNameSafe(ControlledCharacter), *GetNameSafe(TargetCharacter),
		*HerbItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens));

	Result = ApplyCommittedHerbRecovery(HelpingController, RequestId);
	if (!CatIsAcceptedDomainCommandResult(Result))
	{
		LogBodyFailure();
	}
	return StoreHerbTerminal(Result);
}

// 进食流程：先按 RequestId 重放，再验证 authority/定义/项目 ASC/Growth；Toxic 鱼只通过 ApplyPoisonDelta/GE 增加 Poison。
// Poison 或 Growth 任一提交失败都不裁决 Downed；全部身体后置事实成立后才推进 Snapshot 并缓存完整终态。
FCatDomainCommandResult UCatConditionComponent::ConsumeCommittedFish(const FGuid RequestId,
	const UCatFishDefinition* FishDefinition)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString Key = MakeTerminalKey(TEXT("EatFish"), RequestId);
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		Result = *Cached;
		MarkCommandReplayed(Result);
		return Result;
	}
	UCatAbilitySystemComponent* ASC = ResolveAbilitySystem();
	const ACatCharacter* Character = Cast<ACatCharacter>(GetOwner());
	UCatGrowthComponent* Growth = Character ? Character->GetGrowthComponent() : nullptr;
	if (!RequestId.IsValid() || ValidateFishConsumption(FishDefinition) != ECatDomainCommandError::None)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else
	{
		if (FishDefinition->FoodSafety == ECatFishFoodSafety::Toxic
			&& !ASC->ApplyPoisonDelta(static_cast<float>(FishDefinition->PoisonIncrease)))
		{
			Result.Error = ECatDomainCommandError::DependencyUnavailable;
		}
		else
		{
			const FCatDomainCommandResult GrowthResult = Growth->ApplyCommittedFish(RequestId, FishDefinition);
			if (!CatIsAcceptedDomainCommandResult(GrowthResult))
			{
				Result.Error = GrowthResult.bTerminalReplay ? GrowthResult.ReplayedTerminalError : GrowthResult.Error;
				Result.Revision = Snapshot.Revision;
			}
			else
			{
				EvaluateDownedFromAttributes(ECatRecoveryMode::None);
				Result.bCommitted = true;
				Result.Error = ECatDomainCommandError::None;
				Result.Revision = Snapshot.Revision;
			}
		}
	}
	TerminalCache.Add(Key, Result);
	return Result;
}

// 野外自救流程：要求请求者正拥有本 Character，再读取显式较慢清毒值；0/非法配置返回 PolicyUndecided，成功交统一恢复路径。
FCatDomainCommandResult UCatConditionComponent::RequestFieldSelfRecovery(AController* RequestingController,
	const FGuid RequestId)
{
	const ACatCharacter* Character = Cast<ACatCharacter>(GetOwner());
	const UCatConditionSettings* Settings = GetDefault<UCatConditionSettings>();
	if (!Character || Character->GetController() != RequestingController || !Settings
		|| !FMath::IsFinite(Settings->FieldRestPoisonRelief) || Settings->FieldRestPoisonRelief <= 0.0)
	{
		FCatDomainCommandResult Result;
		Result.RequestId = RequestId;
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Result;
	}
	return ApplyRecovery(RequestId, ECatRecoveryMode::FieldSelfRecovery, Settings->FieldRestPoisonRelief);
}

// 营地休息流程：要求调用者拥有本 Character 且上层已验证固定营地范围，再用显式营地清毒值交统一恢复；不会强制等待或启动计时任务。
FCatDomainCommandResult UCatConditionComponent::RequestCampRest(AController* RequestingController,
	const FGuid RequestId, const bool bAtCamp)
{
	const ACatCharacter* Character = Cast<ACatCharacter>(GetOwner());
	const UCatConditionSettings* Settings = GetDefault<UCatConditionSettings>();
	if (!bAtCamp || !Character || Character->GetController() != RequestingController || !Settings
		|| !FMath::IsFinite(Settings->CampRestPoisonRelief) || Settings->CampRestPoisonRelief <= 0.0)
	{
		FCatDomainCommandResult Result;
		Result.RequestId = RequestId;
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Result;
	}
	return ApplyRecovery(RequestId, ECatRecoveryMode::CampRest, Settings->CampRestPoisonRelief);
}

// 草药恢复流程：上层完成库存事务后调用；先按 Herb+RequestId 重放首次终态，再验证 authority/Controller/显式恢复值和距离，失败也缓存以避免同一库存提交请求搬近后变成成功。
FCatDomainCommandResult UCatConditionComponent::ApplyCommittedHerbRecovery(AController* HelpingController,
	const FGuid RequestId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString Key = MakeTerminalKey(*UEnum::GetValueAsString(ECatRecoveryMode::Herb), RequestId);
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		Result = *Cached;
		MarkCommandReplayed(Result);
		return Result;
	}
	const UCatConditionSettings* Settings = GetDefault<UCatConditionSettings>();
	if (!HelpingController || ValidateHerbRecovery(HelpingController) != ECatDomainCommandError::None)
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		TerminalCache.Add(Key, Result);
		return Result;
	}
	return ApplyRecovery(RequestId, ECatRecoveryMode::Herb, Settings->HerbPoisonRelief);
}

// 搬运完成流程：先重放已完成 RequestId，再要求真实救援者、固定营地落点和目标仍处于 Downed；不修改 Attribute，只把恢复方式标为 CarriedToCamp，后续休息/草药继续处理阈值。
FCatDomainCommandResult UCatConditionComponent::CompleteCarryToCamp(AController* HelpingController,
	const FGuid RequestId, const bool bAtCampRescuePoint)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString Key = MakeTerminalKey(TEXT("CarryToCamp"), RequestId);
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		Result = *Cached;
		MarkCommandReplayed(Result);
		return Result;
	}
	if (!GetOwner() || !GetOwner()->HasAuthority() || !HelpingController || !RequestId.IsValid() || !bAtCampRescuePoint)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		TerminalCache.Add(Key, Result);
		return Result;
	}
	if (!Snapshot.bDowned)
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
		TerminalCache.Add(Key, Result);
		return Result;
	}
	Snapshot.RecoveryMode = ECatRecoveryMode::CarriedToCamp;
	++Snapshot.Revision;
	PublishSnapshot();
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	Result.Revision = Snapshot.Revision;
	TerminalCache.Add(Key, Result);
	return Result;
}

// Snapshot 复制回调流程：客户端只消费完整离散事实；表现系统可查询它，但这里不写 ASC、不请求救援也不推导死亡。
void UCatConditionComponent::OnRep_Snapshot()
{
	OnSnapshotChanged.Broadcast();
}

// 统一恢复流程：按 Mode+RequestId 幂等重放，验证 authority/项目 ASC/阈值 gate 后通过 ApplyPoisonDelta 走 GE 减少 Poison。
// ASC 拒绝恢复时返回 PolicyUndecided 且不重新裁决 Downed；成功才发布恢复后的唯一 Snapshot 并缓存首次终态。
FCatDomainCommandResult UCatConditionComponent::ApplyRecovery(const FGuid RequestId, const ECatRecoveryMode Mode,
	const double PoisonRelief)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString Key = MakeTerminalKey(*UEnum::GetValueAsString(Mode), RequestId);
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		Result = *Cached;
		MarkCommandReplayed(Result);
		return Result;
	}
	UCatAbilitySystemComponent* ASC = ResolveAbilitySystem();
	const UCatConditionSettings* Settings = GetDefault<UCatConditionSettings>();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid() || !ASC || !Settings
		|| !Settings->HasDownedThresholds()
		|| !FMath::IsFinite(PoisonRelief) || PoisonRelief < 0.0)
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
	}
	else
	{
		if (!ASC->ApplyPoisonDelta(-static_cast<float>(PoisonRelief)))
		{
			Result.Error = ECatDomainCommandError::PolicyUndecided;
		}
		else
		{
			EvaluateDownedFromAttributes(Mode);
			Result.bCommitted = true;
			Result.Error = ECatDomainCommandError::None;
			Result.Revision = Snapshot.Revision;
		}
	}
	TerminalCache.Add(Key, Result);
	return Result;
}

// 倒地裁决流程：读取 ASC Poison 与显式阈值，更新唯一 Downed/RecoveryMode；首次进入倒地时终止相关 FishingSession，始终没有死亡分支。
void UCatConditionComponent::EvaluateDownedFromAttributes(const ECatRecoveryMode RecoveryMode)
{
	UCatAbilitySystemComponent* ASC = ResolveAbilitySystem();
	const UCatConditionSettings* Settings = GetDefault<UCatConditionSettings>();
	if (!ASC || !Settings->HasDownedThresholds())
	{
		return;
	}
	const bool bWasDowned = Snapshot.bDowned;
	Snapshot.bDowned = ASC->IsPoisonAtLeast(static_cast<float>(Settings->PoisonDownedThreshold));
	Snapshot.RecoveryMode = Snapshot.bDowned ? RecoveryMode : ECatRecoveryMode::None;
	++Snapshot.Revision;
	PublishSnapshot();
	if (!bWasDowned && Snapshot.bDowned)
	{
		UE_LOG(LogCatCharacter, Warning, TEXT("Event=character_downed Character=%s Revision=%lld Recovery=%s"),
			*GetOwner()->GetName(), Snapshot.Revision, *UEnum::GetValueAsString(Snapshot.RecoveryMode));
		if (UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr)
		{
			Fishing->ReleaseFishingOperatorForCharacter(Cast<ACatCharacter>(GetOwner()));
		}
	}
}

// ASC 解析流程：只接受项目唯一 ACatCharacter Owner 并读取其项目 ASC；不从 PlayerState 建第二份身体状态。
UCatAbilitySystemComponent* UCatConditionComponent::ResolveAbilitySystem() const
{
	const ACatCharacter* Character = Cast<ACatCharacter>(GetOwner());
	return Character ? Character->GetCatAbilitySystemComponent() : nullptr;
}

// 幂等键流程：在组件局内内存中组合操作和 RequestId；不包含 StableNetId，不进入日志、复制或 Profile。
FString UCatConditionComponent::MakeTerminalKey(const TCHAR* Operation, const FGuid RequestId)
{
	return FString::Printf(TEXT("%s|%s"), Operation, *RequestId.ToString(EGuidFormats::DigitsWithHyphens));
}

// Snapshot 发布流程：authority 先要求 Owner 立即复制，再向同机只读订阅者广播；无 Owner 时仍广播当前对象变化但不尝试网络写入。
void UCatConditionComponent::PublishSnapshot()
{
	if (AActor* Owner = GetOwner(); Owner && Owner->HasAuthority())
	{
		Owner->ForceNetUpdate();
	}
	OnSnapshotChanged.Broadcast();
}
