#include "Condition/CatConditionComponent.h"

#include "Character/CatCharacter.h"
#include "Condition/CatConditionSettings.h"
#include "Data/CatFishDefinition.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Fishing/CatFishingService.h"
#include "GameFramework/Controller.h"
#include "Growth/CatGrowthComponent.h"
#include "Logging/CatLog.h"
#include "Logging/CatLogContext.h"
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

// Snapshot 读取流程：返回本机服务器真相或客户端最近复制值，不把任何属性数值复制进第二个 DTO。
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

// 水域暴露更新流程：
// 1. 先确认 Character、authority、阈值配置、水域子系统和固定步时长齐全，缺任一项都不猜湿身结果。
// 2. 再用脚点查询指定 WaterRegion 的浸没深度，并按湿润阈值、危险进入阈值和退出滞回维护离散状态。
// 3. 状态没有变化时只返回 Unchanged；危险首次进入用 Warning 记录并交给 Fishing 终局入口处理。
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

// 食用预检流程：只读核对 authority、正式鱼定义和成长入口；通过后上层才可以不可逆移除库存实物。
ECatDomainCommandError UCatConditionComponent::ValidateFishConsumption(const UCatFishDefinition* FishDefinition) const
{
	const ACatCharacter* Character = Cast<ACatCharacter>(GetOwner());
	const UCatGrowthComponent* Growth = Character ? Character->GetGrowthComponent() : nullptr;
	return GetOwner() && GetOwner()->HasAuthority() && FishDefinition && FishDefinition->IsRuntimeDefinitionReady()
		&& Growth && Growth->ValidateFishGrowth(FishDefinition) == ECatDomainCommandError::None
		? ECatDomainCommandError::None : ECatDomainCommandError::DependencyUnavailable;
}

// 进食流程：先按 RequestId 回放首次终态，再复用预检并提交成长；进食不会再写入中毒、倒地或其他身体数值。
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
	const ACatCharacter* Character = Cast<ACatCharacter>(GetOwner());
	UCatGrowthComponent* Growth = Character ? Character->GetGrowthComponent() : nullptr;
	if (!RequestId.IsValid() || ValidateFishConsumption(FishDefinition) != ECatDomainCommandError::None)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else
	{
		Result = Growth->ApplyCommittedFish(RequestId, FishDefinition);
		Result.RequestId = RequestId;
		if (CatIsAcceptedDomainCommandResult(Result))
		{
			Result.bCommitted = true;
			Result.Error = ECatDomainCommandError::None;
			Result.Revision = Snapshot.Revision;
		}
	}
	TerminalCache.Add(Key, Result);
	return Result;
}

// 倒地写入流程：只供服务器开发验证入口提交离散身体事实；相同状态不产生复制噪声，首次倒地会终止该角色的进行中钓鱼会话。
bool UCatConditionComponent::SetDownedFromAuthority(const bool bNewDowned)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return false;
	}
	if (Snapshot.bDowned == bNewDowned)
	{
		return true;
	}
	Snapshot.bDowned = bNewDowned;
	Snapshot.RecoveryMode = ECatRecoveryMode::None;
	++Snapshot.Revision;
	PublishSnapshot();
	UE_LOG(LogCatCharacter, Log,
		TEXT("Event=character_downed_changed Character=%s Downed=%s Revision=%lld World=%s NetMode=%d Authority=true LocalRole=%d"),
		*Owner->GetName(), bNewDowned ? TEXT("true") : TEXT("false"), Snapshot.Revision,
		*GetNameSafe(GetWorld()), GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : INDEX_NONE,
		static_cast<int32>(Owner->GetLocalRole()));
	if (bNewDowned)
	{
		if (UCatFishingService* Fishing = GetWorld() ? GetWorld()->GetSubsystem<UCatFishingService>() : nullptr)
		{
			Fishing->ReleaseFishingOperatorForCharacter(Cast<ACatCharacter>(Owner));
		}
	}
	return true;
}

// 搬运完成流程：先重放已完成 RequestId，再要求真实救援者、固定营地落点和目标仍处于 Downed；不修改倒地事实，只把恢复方式标为 CarriedToCamp。
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

// Snapshot 复制回调流程：客户端只消费完整离散事实；表现系统可查询它，但这里不触发新的身体命令。
void UCatConditionComponent::OnRep_Snapshot()
{
	OnSnapshotChanged.Broadcast();
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
