#include "Character/CatConditionComponent.h"

#include "AbilitySystemComponent.h"
#include "CatAbilitySettings.h"
#include "CatCharacter.h"
#include "CatLog.h"
#include "CatSurvivalAttributeSet.h"
#include "Character/CatConditionSettings.h"
#include "Data/CatFishDefinition.h"
#include "Fishing/CatFishingService.h"
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

// Wet 写入流程：只接受 authority 和真实变化；提交后增加 Revision/强制更新，明确不触碰 Hunger、Fatigue、Poison 或移动能力。
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

// 食用预检流程：只读核对 authority、正式身体 runtime、鱼定义、ASC 与倒地阈值；不修改实物鱼、Attribute、Snapshot 或终态缓存。
ECatDomainCommandError UCatConditionComponent::ValidateFishConsumption(const UCatFishDefinition* FishDefinition) const
{
	const UCatConditionSettings* Settings = GetDefault<UCatConditionSettings>();
	return GetOwner() && GetOwner()->HasAuthority() && GetDefault<UCatAbilitySettings>()->IsRuntimeEnabled()
		&& FishDefinition && FishDefinition->IsRuntimeDefinitionReady() && ResolveAbilitySystem()
		&& Settings && Settings->HasDownedThresholds()
		? ECatDomainCommandError::None : ECatDomainCommandError::DependencyUnavailable;
}

// 草药预检流程：只读核对 authority、正式身体 runtime、ASC、倒地阈值与两项恢复量；不扣库存、不写 Attribute，也不制造预留状态。
ECatDomainCommandError UCatConditionComponent::ValidateHerbRecovery() const
{
	const UCatConditionSettings* Settings = GetDefault<UCatConditionSettings>();
	return GetOwner() && GetOwner()->HasAuthority() && GetDefault<UCatAbilitySettings>()->IsRuntimeEnabled()
		&& ResolveAbilitySystem() && Settings && Settings->HasDownedThresholds()
		&& FMath::IsFinite(Settings->HerbPoisonRelief) && Settings->HerbPoisonRelief > 0.0
		&& FMath::IsFinite(Settings->HerbFatigueRelief) && Settings->HerbFatigueRelief >= 0.0
		? ECatDomainCommandError::None : ECatDomainCommandError::PolicyUndecided;
}

// 进食流程：先按 RequestId 重放，再验证 authority/定义/ASC；首次减少 Hunger、按 Toxic 增加 Poison，随后只走统一倒地裁决并缓存终态。
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
	UAbilitySystemComponent* ASC = ResolveAbilitySystem();
	if (!RequestId.IsValid() || ValidateFishConsumption(FishDefinition) != ECatDomainCommandError::None)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else
	{
		const double Hunger = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetHungerAttribute());
		const double Poison = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetPoisonAttribute());
		ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetHungerAttribute(), FMath::Max(0.0, Hunger - FishDefinition->HungerRelief));
		if (FishDefinition->FoodSafety == ECatFishFoodSafety::Toxic)
		{
			ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetPoisonAttribute(), Poison + FishDefinition->PoisonIncrease);
		}
		EvaluateDownedFromAttributes(ECatRecoveryMode::None);
		Result.bCommitted = true;
		Result.Error = ECatDomainCommandError::None;
		Result.Revision = Snapshot.Revision;
	}
	TerminalCache.Add(Key, Result);
	return Result;
}

// 野外自救流程：要求请求者正拥有本 Character，再读取显式较慢恢复值；0/非法配置返回 PolicyUndecided，成功交统一恢复路径。
FCatDomainCommandResult UCatConditionComponent::RequestFieldSelfRecovery(AController* RequestingController,
	const FGuid RequestId)
{
	const ACatCharacter* Character = Cast<ACatCharacter>(GetOwner());
	const UCatConditionSettings* Settings = GetDefault<UCatConditionSettings>();
	if (!Character || Character->GetController() != RequestingController || !Settings
		|| !FMath::IsFinite(Settings->FieldRestFatigueRelief) || Settings->FieldRestFatigueRelief <= 0.0)
	{
		FCatDomainCommandResult Result;
		Result.RequestId = RequestId;
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Result;
	}
	return ApplyRecovery(RequestId, ECatRecoveryMode::FieldSelfRecovery, Settings->FieldRestFatigueRelief, 0.0);
}

// 营地休息流程：要求调用者拥有本 Character 且上层已验证固定营地范围，再用显式营地恢复值交统一恢复；不会强制等待或启动计时任务。
FCatDomainCommandResult UCatConditionComponent::RequestCampRest(AController* RequestingController,
	const FGuid RequestId, const bool bAtCamp)
{
	const ACatCharacter* Character = Cast<ACatCharacter>(GetOwner());
	const UCatConditionSettings* Settings = GetDefault<UCatConditionSettings>();
	if (!bAtCamp || !Character || Character->GetController() != RequestingController || !Settings
		|| !FMath::IsFinite(Settings->CampRestFatigueRelief) || Settings->CampRestFatigueRelief <= 0.0)
	{
		FCatDomainCommandResult Result;
		Result.RequestId = RequestId;
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Result;
	}
	return ApplyRecovery(RequestId, ECatRecoveryMode::CampRest, Settings->CampRestFatigueRelief, 0.0);
}

// 草药恢复流程：上层完成库存事务后调用；这里只验证 authority/Controller/显式恢复值，允许本人或伙伴但不在本组件复制援助者身份。
FCatDomainCommandResult UCatConditionComponent::ApplyCommittedHerbRecovery(AController* HelpingController,
	const FGuid RequestId)
{
	const UCatConditionSettings* Settings = GetDefault<UCatConditionSettings>();
	if (!HelpingController || ValidateHerbRecovery() != ECatDomainCommandError::None)
	{
		FCatDomainCommandResult Result;
		Result.RequestId = RequestId;
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Result;
	}
	return ApplyRecovery(RequestId, ECatRecoveryMode::Herb, Settings->HerbFatigueRelief, Settings->HerbPoisonRelief);
}

// 搬运完成流程：要求真实救援者和固定营地落点事实；不修改 Attribute，只把仍倒地者的恢复方式标为 CarriedToCamp，后续休息/草药继续处理阈值。
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

// 统一恢复流程：按 Mode+RequestId 幂等重放，验证 authority/ASC/阈值 gate 后非负减少属性，再重新裁决 Downed 并缓存结果。
FCatDomainCommandResult UCatConditionComponent::ApplyRecovery(const FGuid RequestId, const ECatRecoveryMode Mode,
	const double FatigueRelief, const double PoisonRelief)
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
	UAbilitySystemComponent* ASC = ResolveAbilitySystem();
	const UCatConditionSettings* Settings = GetDefault<UCatConditionSettings>();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid() || !ASC || !Settings->HasDownedThresholds()
		|| !FMath::IsFinite(FatigueRelief) || FatigueRelief < 0.0 || !FMath::IsFinite(PoisonRelief) || PoisonRelief < 0.0)
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
	}
	else
	{
		const double Fatigue = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFatigueAttribute());
		const double Poison = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetPoisonAttribute());
		ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFatigueAttribute(), FMath::Max(0.0, Fatigue - FatigueRelief));
		ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetPoisonAttribute(), FMath::Max(0.0, Poison - PoisonRelief));
		EvaluateDownedFromAttributes(Mode);
		Result.bCommitted = true;
		Result.Error = ECatDomainCommandError::None;
		Result.Revision = Snapshot.Revision;
	}
	TerminalCache.Add(Key, Result);
	return Result;
}

// 倒地裁决流程：读取 ASC 两项数值与显式阈值，更新唯一 Downed/RecoveryMode；首次进入倒地时终止相关 FishingSession，始终没有死亡分支。
void UCatConditionComponent::EvaluateDownedFromAttributes(const ECatRecoveryMode RecoveryMode)
{
	UAbilitySystemComponent* ASC = ResolveAbilitySystem();
	const UCatConditionSettings* Settings = GetDefault<UCatConditionSettings>();
	if (!ASC || !Settings->HasDownedThresholds())
	{
		return;
	}
	const bool bWasDowned = Snapshot.bDowned;
	Snapshot.bDowned = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetPoisonAttribute()) >= Settings->PoisonDownedThreshold
		|| ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFatigueAttribute()) >= Settings->FatigueDownedThreshold;
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

// ASC 解析流程：只接受项目唯一 ACatCharacter Owner 并直接读取其 IAbilitySystemInterface 返回值；不从 PlayerState 建第二份身体状态。
UAbilitySystemComponent* UCatConditionComponent::ResolveAbilitySystem() const
{
	const ACatCharacter* Character = Cast<ACatCharacter>(GetOwner());
	return Character ? Character->GetAbilitySystemComponent() : nullptr;
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
