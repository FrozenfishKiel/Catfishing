#include "Condition/CatConditionComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/CatAbilitySettings.h"
#include "Character/CatCharacter.h"
#include "Logging/CatLog.h"
#include "AbilitySystem/CatSurvivalAttributeSet.h"
#include "Condition/CatConditionSettings.h"
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
		&& FishDefinition && FishDefinition->HasRuntimeConsumptionEffect() && ResolveAbilitySystem()
		&& Settings && Settings->HasDownedThresholds()
		? ECatDomainCommandError::None : ECatDomainCommandError::DependencyUnavailable;
}

// 进食流程：先把鱼定义和食用数值固化为本次请求的载荷签名；缓存命中时必须同签名才允许稳定重放，首次执行才写 ASC 和 Snapshot。
FCatDomainCommandResult UCatConditionComponent::ConsumeCommittedFish(const FGuid RequestId,
	const UCatFishDefinition* FishDefinition)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString Key = MakeTerminalKey(TEXT("EatFish"), RequestId);
	const FString PayloadSignature = FishDefinition
		? FString::Printf(TEXT("FishDefinitionId=%s|FoodSafety=%d|HungerRelief=%.17g|PoisonIncrease=%.17g"),
			*FishDefinition->FishDefinitionId.ToString(), static_cast<int32>(FishDefinition->FoodSafety),
			FishDefinition->HungerRelief, FishDefinition->PoisonIncrease)
		: FString(TEXT("FishDefinition=null"));
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		if (const FString* CachedPayload = TerminalPayloadByKey.Find(Key); CachedPayload && *CachedPayload != PayloadSignature)
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
			return Result;
		}
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
	TerminalPayloadByKey.Add(Key, PayloadSignature);
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

// 搬运完成流程：先固定救援者与营地点事实，再处理终态缓存；同 RequestId 不能从“没到营地”漂移成“已到营地”后绕过首个结论。
FCatDomainCommandResult UCatConditionComponent::CompleteCarryToCamp(AController* HelpingController,
	const FGuid RequestId, const bool bAtCampRescuePoint)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString Key = MakeTerminalKey(TEXT("CarryToCamp"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("Helper=%s|AtCampRescuePoint=%s"),
		HelpingController ? *HelpingController->GetName() : TEXT("None"), bAtCampRescuePoint ? TEXT("true") : TEXT("false"));
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		if (const FString* CachedPayload = TerminalPayloadByKey.Find(Key); CachedPayload && *CachedPayload != PayloadSignature)
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
			return Result;
		}
		Result = *Cached;
		Result.bCommitted = false;
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	if (!GetOwner() || !GetOwner()->HasAuthority() || !HelpingController || !RequestId.IsValid() || !bAtCampRescuePoint)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		TerminalCache.Add(Key, Result);
		TerminalPayloadByKey.Add(Key, PayloadSignature);
		return Result;
	}
	Snapshot.RecoveryMode = ECatRecoveryMode::CarriedToCamp;
	++Snapshot.Revision;
	PublishSnapshot();
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	Result.Revision = Snapshot.Revision;
	TerminalCache.Add(Key, Result);
	TerminalPayloadByKey.Add(Key, PayloadSignature);
	return Result;
}

// Snapshot 复制回调流程：客户端只消费完整离散事实；表现系统可查询它，但这里不写 ASC、不请求救援也不推导死亡。
void UCatConditionComponent::OnRep_Snapshot()
{
	OnSnapshotChanged.Broadcast();
}

// 统一恢复流程：把恢复模式和数值作为 RequestId 的业务事实；缓存命中先挡住参数漂移，首次执行才修改 ASC 并重新裁决 Downed。
FCatDomainCommandResult UCatConditionComponent::ApplyRecovery(const FGuid RequestId, const ECatRecoveryMode Mode,
	const double FatigueRelief, const double PoisonRelief)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString Key = MakeTerminalKey(*UEnum::GetValueAsString(Mode), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("Mode=%d|FatigueRelief=%.17g|PoisonRelief=%.17g"),
		static_cast<int32>(Mode), FatigueRelief, PoisonRelief);
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		if (const FString* CachedPayload = TerminalPayloadByKey.Find(Key); CachedPayload && *CachedPayload != PayloadSignature)
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
			return Result;
		}
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
	TerminalPayloadByKey.Add(Key, PayloadSignature);
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
