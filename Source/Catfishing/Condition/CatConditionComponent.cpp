#include "Condition/CatConditionComponent.h"

#include "AbilitySystem/Config/CatAbilitySettings.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Character/CatCharacter.h"
#include "Logging/CatLog.h"
#include "Logging/CatLogContext.h"
#include "Condition/CatConditionSettings.h"
#include "Data/CatFishDefinition.h"
#include "Fishing/CatFishingService.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Growth/CatGrowthComponent.h"
#include "Net/UnrealNetwork.h"

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

// Wet 写入流程：只接受 authority 和真实变化；提交后增加 Revision/强制更新，明确不触碰 Poison、成长、搏斗体力或移动能力。
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
	OutImmersionDepthCentimeters = 0.0;
	ACatCharacter* Character = Cast<ACatCharacter>(GetOwner());
	const UCatConditionSettings* Settings = GetDefault<UCatConditionSettings>();
	UCatWaterQuerySubsystem* Water = GetWorld() ? GetWorld()->GetSubsystem<UCatWaterQuerySubsystem>() : nullptr;
	if (!Character || !Character->HasAuthority() || !Settings || !Settings->HasWaterExposureThresholds()
		|| !Water || !WaterRegion.IsValid() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.0)
	{
		return ECatWaterExposureUpdate::Unavailable;
	}
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	if (!Capsule)
	{
		return ECatWaterExposureUpdate::Unavailable;
	}
	const FVector FootPoint = Character->GetActorLocation()
		- FVector::UpVector * Capsule->GetScaledCapsuleHalfHeight();
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

// 草药预检流程：只读核对 authority、施药者 Pawn、正式身体 runtime、ASC、倒地阈值、恢复量和服务器距离；不扣库存、不写 Attribute，也不制造预留状态。
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

// 进食流程：先按 RequestId 重放，再验证 authority/定义/项目 ASC/Growth；Toxic 鱼只通过 ApplyPoisonDelta/GE 增加 Poison。
// Poison 提交失败时不推进 Growth 或 Downed，避免实物鱼已消费后写出半套身体事实；成功后才推进经验槽、裁决倒地并缓存终态。
FCatDomainCommandResult UCatConditionComponent::ConsumeCommittedFish(const FGuid RequestId,
	const UCatFishDefinition* FishDefinition)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString Key = MakeTerminalKey(TEXT("EatFish"), RequestId);
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		Result = *Cached;
		Result.bCommitted = false;
		Result.Error = ECatDomainCommandError::AlreadyResolved;
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
			Growth->ApplyCommittedFish(RequestId, FishDefinition);
			EvaluateDownedFromAttributes(ECatRecoveryMode::None);
			Result.bCommitted = true;
			Result.Error = ECatDomainCommandError::None;
			Result.Revision = Snapshot.Revision;
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
		Result.bCommitted = false;
		Result.Error = ECatDomainCommandError::AlreadyResolved;
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
		Result.bCommitted = false;
		Result.Error = ECatDomainCommandError::AlreadyResolved;
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
		Result.bCommitted = false;
		Result.Error = ECatDomainCommandError::AlreadyResolved;
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
			Fishing->TerminateSessionsForCharacter(Cast<ACatCharacter>(GetOwner()));
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
