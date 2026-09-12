#include "Condition/CatConditionComponent.h"

#include "AbilitySystem/Config/CatAbilitySettings.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Character/CatCharacter.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Logging/CatLog.h"
#include "Logging/CatLogContext.h"
#include "Condition/CatConditionSettings.h"
#include "Data/CatFishDefinition.h"
#include "Fishing/CatFishingService.h"
#include "GameFramework/Controller.h"
#include "Growth/CatGrowthComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

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

// 结束流程：收掉自愈计时器；Character 被销毁或局末清场后不得再有定时回调改一个已消失身体的状态。
void UCatConditionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DownedSelfRecoveryTimer);
		World->GetTimerManager().ClearTimer(StenchTimer);
	}
	Super::EndPlay(EndPlayReason);
}

// Snapshot 读取流程：返回本机服务器真相或客户端最近复制值，不把 ASC 数值复制进第二个 DTO。
const FCatConditionSnapshot& UCatConditionComponent::GetSnapshot() const
{
	return Snapshot;
}

// Wet 写入流程：只接受落水、天气等 authority 反馈和真实变化；提交后增加 Revision/强制更新，明确不触碰成长、搏斗体力、移动能力或 BodyAction。
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
	const bool bInWater = Immersion.Containment != ECatWaterContainment::Outside
		&& OutImmersionDepthCentimeters >= Settings->WetWaterDepthCentimeters;
	ECatWaterExposureState NewExposure = bInWater ? ECatWaterExposureState::Shallow : ECatWaterExposureState::Dry;
	if (Snapshot.WaterExposure == ECatWaterExposureState::Dangerous
		&& bInWater && OutImmersionDepthCentimeters > Settings->DangerousWaterExitDepthCentimeters)
	{
		NewExposure = ECatWaterExposureState::Dangerous;
	}
	else if (bInWater && OutImmersionDepthCentimeters >= Settings->DangerousWaterDepthCentimeters)
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

	// 出水不等于立刻变干：天气也会把猫淋湿（见 GameMode 的雨天驱动），所以离水只清水域档，
	// 湿毛本身留给唯一的 Wet 写口按当前天气与浸没一起裁决。
	const bool bNewWet = bInWater || (Snapshot.bWet && NewExposure != ECatWaterExposureState::Dry);
	const bool bDangerousEntered = Snapshot.WaterExposure != ECatWaterExposureState::Dangerous
		&& NewExposure == ECatWaterExposureState::Dangerous;
	if (Snapshot.bWet == bNewWet && Snapshot.WaterExposure == NewExposure)
	{
		return ECatWaterExposureUpdate::Unchanged;
	}
	Snapshot.bWet = bNewWet;
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

// 疲惫档写入流程：纯演出，只在 authority 侧改快照。它不读写任何 Attribute、不影响倒地、不参与搏斗公式——
// 疲惫数值制 2026-08-15 已废除，这里只准好笑，不准碍事。
void UCatConditionComponent::SetFatigueTierFromAuthority(const ECatFatigueTier NewTier)
{
	const AActor* Owner = GetOwner();
	const UCatConditionSettings* Settings = GetDefault<UCatConditionSettings>();
	if (!Owner || !Owner->HasAuthority() || !Settings || !Settings->IsRuntimeReady()
		|| Snapshot.FatigueTier == NewTier)
	{
		return;
	}
	Snapshot.FatigueTier = NewTier;
	++Snapshot.Revision;
	PublishSnapshot();
	UE_LOG(LogCatCharacter, Log, TEXT("Event=character_fatigue_tier_changed Character=%s Tier=%s Revision=%lld"),
		*Owner->GetName(), *UEnum::GetValueAsString(NewTier), Snapshot.Revision);
}

// 食用预检流程：只读核对 authority、正式身体 runtime、鱼定义、ASC 与 Growth 入口；不修改实物鱼、Attribute、Snapshot 或终态缓存。
ECatDomainCommandError UCatConditionComponent::ValidateFishConsumption(const UCatFishDefinition* FishDefinition,
	const double WeightKilograms) const
{
	const UCatConditionSettings* Settings = GetDefault<UCatConditionSettings>();
	const ACatCharacter* Character = Cast<ACatCharacter>(GetOwner());
	const UCatGrowthComponent* Growth = Character ? Character->GetGrowthComponent() : nullptr;
	return GetOwner() && GetOwner()->HasAuthority() && GetDefault<UCatAbilitySettings>()->IsRuntimeEnabled()
		&& FishDefinition && FishDefinition->IsRuntimeDefinitionReady() && ResolveAbilitySystem()
		&& Settings && Settings->IsRuntimeReady()
		&& Growth && Growth->ValidateFishGrowth(FishDefinition, WeightKilograms) == ECatDomainCommandError::None
		? ECatDomainCommandError::None : ECatDomainCommandError::DependencyUnavailable;
}

// 进食流程：先按 RequestId 重放，再验证 authority/定义/项目 ASC/Growth。
//
// 中毒按鱼各配、无渐进升级（猫册 §3.1.4，09-12 收口）：这条鱼是不是重毒由它自己的食用结论说了算，
// 最重一档＝吃下即倒地，别的档位是按鱼种配置的限时 buff，不累加、不推高任何跨鱼计数。
// 墓碑（2026-09-12）：原实现是 `Toxic 鱼 → ApplyPoisonDelta(PoisonIncrease)`，再由阈值 100 裁决倒地，
// 后果是连吃两条轻毒鱼也会倒地——那正是设计 2026-08-21 砍掉的渐进加重模型。
FCatDomainCommandResult UCatConditionComponent::ConsumeCommittedFish(const FGuid RequestId,
	const UCatFishDefinition* FishDefinition, const double WeightKilograms)
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
	if (!RequestId.IsValid() || ValidateFishConsumption(FishDefinition, WeightKilograms) != ECatDomainCommandError::None)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else
	{
		const FCatDomainCommandResult GrowthResult = Growth->ApplyCommittedFish(RequestId, FishDefinition,
			WeightKilograms);
		if (!CatIsAcceptedDomainCommandResult(GrowthResult))
		{
			Result.Error = GrowthResult.bTerminalReplay ? GrowthResult.ReplayedTerminalError : GrowthResult.Error;
			Result.Revision = Snapshot.Revision;
		}
		else
		{
			// 黄色体力：来源＝特定鱼种的食用效果（数值成长页 §4）。护盾无上限，正向直接累加。
			if (FMath::IsFinite(FishDefinition->YellowStaminaGrant) && FishDefinition->YellowStaminaGrant > 0.0)
			{
				ASC->ApplyYellowFightStaminaDelta(static_cast<float>(FishDefinition->YellowStaminaGrant));
			}
			if (FishDefinition->FoodSafety == ECatFishFoodSafety::SevereToxic)
			{
				ApplySevereToxicityFromAuthority();
			}
			// 臭臭鱼的「请勿靠近」：吃下即起 90 秒臭气（联机社交 §3.1.4、吃鱼效果页）。
			// 名册与时长都在 CatConditionSettings；哪一项没配都只是这个副作用不发生，进食本身照常成立。
			const UCatConditionSettings* ConditionSettings = GetDefault<UCatConditionSettings>();
			if (ConditionSettings->IsStenchFish(FishDefinition->FishDefinitionId))
			{
				if (ConditionSettings->HasStench())
				{
					ApplyStenchFromAuthority(ConditionSettings->StenchSeconds);
				}
				else
				{
					UE_LOG(LogCatCharacter, Warning,
						TEXT("Event=character_stench_unconfigured Character=%s Fish=%s Reason=StenchSecondsUnset"),
						*GetNameSafe(GetOwner()), *FishDefinition->FishDefinitionId.ToString());
				}
			}
			else if (ConditionSettings->StenchFishDefinitionIds.IsEmpty())
			{
				// 名册整张空：这不是「这条鱼不臭」，是没人填过名册。记一次，免得「请勿靠近」静默消失。
				UE_LOG(LogCatCharacter, Warning,
					TEXT("Event=character_stench_roster_empty Character=%s Fish=%s Reason=StenchFishDefinitionIdsEmpty"),
					*GetNameSafe(GetOwner()), *FishDefinition->FishDefinitionId.ToString());
			}
			Result.bCommitted = true;
			Result.Error = ECatDomainCommandError::None;
			Result.Revision = Snapshot.Revision;
		}
	}
	TerminalCache.Add(Key, Result);
	return Result;
}

// 重毒身体后果流程：唯一的「把猫打倒」入口。倒地本身没有数值刻度——按鱼各配、无渐进升级，
// 所以这里不接受强度参数，只接受「这一口是最重一档」这个结论。
bool UCatConditionComponent::ApplySevereToxicityFromAuthority()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Snapshot.bDowned)
	{
		return false;
	}
	SetDownedFromAuthority(true, ECatRecoveryMode::None);
	return Snapshot.bDowned;
}

// 臭气写入流程：只在 authority 侧提交，重复吃只把结束时间整体后移（没有层数或强度这一说）。
// 它不碰倒地、不碰恢复方式、不碰任何 Attribute——「请勿靠近」是一层社交屏蔽，不是身体损伤。
bool UCatConditionComponent::ApplyStenchFromAuthority(const double DurationSeconds)
{
	UWorld* World = GetWorld();
	const UCatConditionSettings* Settings = GetDefault<UCatConditionSettings>();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !World || !Settings || !Settings->IsRuntimeReady()
		|| !FMath::IsFinite(DurationSeconds) || DurationSeconds <= 0.0)
	{
		return false;
	}
	Snapshot.bStench = true;
	Snapshot.StenchEndsServerTimeSeconds = World->GetTimeSeconds() + DurationSeconds;
	++Snapshot.Revision;
	PublishSnapshot();
	World->GetTimerManager().SetTimer(StenchTimer, this, &ThisClass::HandleStenchElapsed, DurationSeconds, false);
	UE_LOG(LogCatCharacter, Log,
		TEXT("Event=character_stench_started Character=%s DurationSeconds=%.2f EndsAt=%.2f Revision=%lld"),
		*GetOwner()->GetName(), DurationSeconds, Snapshot.StenchEndsServerTimeSeconds, Snapshot.Revision);
	return true;
}

// 臭气到点流程：只清这一层状态；此刻猫可能仍倒地、仍湿着，那些各走各的入口。
void UCatConditionComponent::HandleStenchElapsed()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Snapshot.bStench)
	{
		return;
	}
	Snapshot.bStench = false;
	Snapshot.StenchEndsServerTimeSeconds = 0.0;
	++Snapshot.Revision;
	PublishSnapshot();
	UE_LOG(LogCatCharacter, Log, TEXT("Event=character_stench_ended Character=%s Revision=%lld"),
		*GetOwner()->GetName(), Snapshot.Revision);
}

// 野外自救流程：要求请求者正拥有本 Character；单人局也走得通，不要求其他玩家在场。
// 休息是解除倒地的两条正式路径之一（猫册 v1.3：草药删除后只剩救援与休息）。
FCatDomainCommandResult UCatConditionComponent::RequestFieldSelfRecovery(AController* RequestingController,
	const FGuid RequestId)
{
	const ACatCharacter* Character = Cast<ACatCharacter>(GetOwner());
	if (!Character || Character->GetController() != RequestingController)
	{
		FCatDomainCommandResult Result;
		Result.RequestId = RequestId;
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Result;
	}
	return ApplyRecovery(RequestId, ECatRecoveryMode::FieldSelfRecovery);
}

// 营地休息流程：要求调用者拥有本 Character 且上层已验证固定营地范围；成功即起身。
FCatDomainCommandResult UCatConditionComponent::RequestCampRest(AController* RequestingController,
	const FGuid RequestId, const bool bAtCamp)
{
	const ACatCharacter* Character = Cast<ACatCharacter>(GetOwner());
	if (!bAtCamp || !Character || Character->GetController() != RequestingController)
	{
		FCatDomainCommandResult Result;
		Result.RequestId = RequestId;
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Result;
	}
	return ApplyRecovery(RequestId, ECatRecoveryMode::CampRest);
}

// 搬运完成流程：先重放已完成 RequestId，再要求真实救援者、固定营地落点和目标仍处于 Downed。
// 「伙伴搬运回营地即解除倒地」（猫册 §3.1.5）：到点就起身，不需要再补一次营地休息。
// 墓碑（2026-09-12）：原实现只写 RecoveryMode=CarriedToCamp、既不清倒地也不动中毒值，
// 被搬回营地的猫还得自己再休息一次才能站起来，与设计相反。
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
	// 臭着的猫搬不动（2026-08-21 裁定：搬运算「帮助」，被臭气屏蔽）。这是设计有意留的段子——
	// 「太臭了没法救」，他只能自己爬回营、等自愈计时，或者等翻天自动救起。
	// 注意屏蔽的只有「别人来搬」：自救、营地休息、自愈到点、翻天救起四条路都不读臭气。
	if (Snapshot.bStench)
	{
		Result.Error = ECatDomainCommandError::PermissionDenied;
		TerminalCache.Add(Key, Result);
		UE_LOG(LogCatCharacter, Log,
			TEXT("Event=character_rescue_rejected Character=%s Reason=Stench RequestId=%s"),
			*GetNameSafe(GetOwner()), *RequestId.ToString(EGuidFormats::DigitsWithHyphens));
		return Result;
	}
	SetDownedFromAuthority(false, ECatRecoveryMode::CarriedToCamp);
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	Result.Revision = Snapshot.Revision;
	TerminalCache.Add(Key, Result);
	return Result;
}

// 翻天自动救起流程：只在 authority 侧对仍倒地的猫生效，传送回营地由调用方先做。
// 「翻天时仍在倒地的自动救起，清晨在营地醒来」（猫册 §3.1.5）；没倒地的猫是空操作。
bool UCatConditionComponent::CompleteDayBreakRescueFromAuthority()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Snapshot.bDowned)
	{
		return false;
	}
	SetDownedFromAuthority(false, ECatRecoveryMode::DayBreakAutoRescue);
	return true;
}

// Snapshot 复制回调流程：客户端只消费完整离散事实；表现系统可查询它，但这里不写 ASC、不请求救援也不推导死亡。
void UCatConditionComponent::OnRep_Snapshot()
{
	OnSnapshotChanged.Broadcast();
}

// 统一恢复流程：按 Mode+RequestId 幂等重放，验证 authority 与运行 gate 后直接解除倒地。
// 墓碑（2026-09-12）：原实现是「按各路径的清毒点数减 Poison，再拿阈值重算 bDowned」，
// 于是营地休息一次只清一半、野外休息要按七次。渐进模型删除后恢复就是布尔的：休息到了就起来。
FCatDomainCommandResult UCatConditionComponent::ApplyRecovery(const FGuid RequestId, const ECatRecoveryMode Mode)
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
	const UCatConditionSettings* Settings = GetDefault<UCatConditionSettings>();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid() || !Settings || !Settings->IsRuntimeReady())
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
	}
	else
	{
		// 没倒地时休息也算成功：它是猫味动作，不是只有倒地才能按的急救键。
		SetDownedFromAuthority(false, Mode);
		Result.bCommitted = true;
		Result.Error = ECatDomainCommandError::None;
		Result.Revision = Snapshot.Revision;
	}
	TerminalCache.Add(Key, Result);
	return Result;
}

// 倒地写入流程：唯一的 bDowned 写口。
// 首次倒地释放该身体的钓鱼操作位（钓鱼中倒地＝中断钓鱼），并按配置起一次性自愈计时；
// 起身时收掉计时器。始终没有死亡分支——项目不存在状态死亡。
void UCatConditionComponent::SetDownedFromAuthority(const bool bNewDowned, const ECatRecoveryMode RecoveryMode)
{
	UWorld* World = GetWorld();
	const UCatConditionSettings* Settings = GetDefault<UCatConditionSettings>();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Settings || !Settings->IsRuntimeReady())
	{
		return;
	}
	const bool bWasDowned = Snapshot.bDowned;
	Snapshot.bDowned = bNewDowned;
	Snapshot.RecoveryMode = bNewDowned ? ECatRecoveryMode::None : RecoveryMode;
	++Snapshot.Revision;
	PublishSnapshot();
	if (!bWasDowned && bNewDowned)
	{
		UE_LOG(LogCatCharacter, Warning, TEXT("Event=character_downed Character=%s Revision=%lld"),
			*GetOwner()->GetName(), Snapshot.Revision);
		if (UCatFishingService* Fishing = World ? World->GetSubsystem<UCatFishingService>() : nullptr)
		{
			Fishing->ReleaseFishingOperatorForCharacter(Cast<ACatCharacter>(GetOwner()));
		}
		if (World && Settings->HasDownedSelfRecovery())
		{
			World->GetTimerManager().SetTimer(DownedSelfRecoveryTimer, this,
				&ThisClass::HandleDownedSelfRecoveryElapsed, Settings->DownedSelfRecoverySeconds, false);
		}
	}
	else if (bWasDowned && !bNewDowned)
	{
		UE_LOG(LogCatCharacter, Log, TEXT("Event=character_recovered Character=%s Recovery=%s Revision=%lld"),
			*GetOwner()->GetName(), *UEnum::GetValueAsString(RecoveryMode), Snapshot.Revision);
		if (World)
		{
			World->GetTimerManager().ClearTimer(DownedSelfRecoveryTimer);
		}
	}
}

// 自愈到点流程：倒地满配置时长后自己站起来。
// 它让爬回营地与队友搬运降级为加速手段而不是唯一出路，也解掉「全队都倒地、这一天结束不了」的死锁。
void UCatConditionComponent::HandleDownedSelfRecoveryElapsed()
{
	if (Snapshot.bDowned)
	{
		SetDownedFromAuthority(false, ECatRecoveryMode::SelfHealTimeout);
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
