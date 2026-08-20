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

// Wet 写入流程：只接受 authority 和真实变化；提交后增加 Revision/强制更新，明确不触碰 Poison 或移动能力。相同值直接返
// 回，所以水域按节拍反复写 true 不会推高 Revision。
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

// 进食流程：先把鱼定义与食用数值固化成本次请求的载荷签名；缓存命中且签名一致才按原终态稳定重放，签名不同说明同一
// RequestId 被换成了另一条鱼，直接拒绝。
// 签名只在本进程、本组件实例的终态缓存里互相比较，既不复制给客户端也不落盘或跨版本保留，所以它的字段集可以随食用效果
// 一起收缩：去掉已删除的饥饿恢复量不会让任何已存在的重放记录失配。
// 首次执行时只有 Toxic 鱼会累加 Poison，Safe 鱼不改任何 Attribute，两者随后都重新裁决倒地并返回 bCommitted。
// 失败分支覆盖 RequestId 无效或 ValidateFishConsumption 不通过，返回 DependencyUnavailable 且不碰任何 Attribute。
// 注意终态缓存写在成功/失败之外，也就是失败结果同样被永久记住：调用到这里时实物鱼已经被 Items 事务不可逆移除，本组件
// 没有资格给同一个 RequestId 第二次结算机会，
// 否则一次失败就能被反复重试成一次真正的加毒。代价是失败无法就地重试——上层要重新走一遍，必须换一个新的 RequestId，用旧 ID 只会拿到 AlreadyResolved。
FCatDomainCommandResult UCatConditionComponent::ConsumeCommittedFish(const FGuid RequestId,
	const UCatFishDefinition* FishDefinition)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString Key = MakeTerminalKey(TEXT("EatFish"), RequestId);
	const FString PayloadSignature = FishDefinition
		? FString::Printf(TEXT("FishDefinitionId=%s|FoodSafety=%d|PoisonIncrease=%.17g"),
			*FishDefinition->FishDefinitionId.ToString(), static_cast<int32>(FishDefinition->FoodSafety),
			FishDefinition->PoisonIncrease)
		: FString(TEXT("FishDefinition=null"));
	switch (CatQueryTerminalReplay(TerminalCache, TerminalPayloadByKey, Key, PayloadSignature, Result, MarkCommandReplayed))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
	}
	UAbilitySystemComponent* ASC = ResolveAbilitySystem();
	if (!RequestId.IsValid() || ValidateFishConsumption(FishDefinition) != ECatDomainCommandError::None)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else
	{
		if (FishDefinition->FoodSafety == ECatFishFoodSafety::Toxic)
		{
			ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetPoisonAttribute(),
				ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetPoisonAttribute()) + FishDefinition->PoisonIncrease);
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

// 野外自救流程：只要求请求者正拥有本 Character，身份不符返回 PolicyUndecided，其余交统一恢复路径。
// 这条路径没有自己的数值配置：疲惫数值制删除后野外自救和营地休息的区别只剩"是否需要在营地范围"，解除倒地都由统一恢复路径清毒完成。
// 倒地阈值未裁（HasDownedThresholds 为 false）时由 ApplyRecovery 拒绝，不在这里重复判断。
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

// 营地休息流程：要求调用者拥有本 Character 且上层已验证固定营地范围（bAtCamp），其余交统一恢复；不会强制等待或启动计时任务。
// 与野外自救同理，解除倒地由统一恢复路径清 Poison 完成，这里没有营地专属的数值。
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

// 搬运完成流程：先固定救援者与营地点事实，再处理终态缓存；同 RequestId 不能从“没到营地”漂移成“已到营地”后绕过首个结论。
// 事实合法后走的是与另外两条恢复路径同一套结算：清空 Poison 再重新裁决倒地。飞书把"伙伴搬运回营地"写成解除倒地的三条路径之一，
// 只记录 RecoveryMode 而不动 Poison 会被下一次裁决立刻判回倒地。
// 缺 ASC 或未裁倒地阈值时返回 DependencyUnavailable 而不是 InvalidPayload：救援事实本身没问题，是身体依赖还没配好，
// 这时不能报成功——报成功等于宣称人被救起来了，实际一个数值都没改。
FCatDomainCommandResult UCatConditionComponent::CompleteCarryToCamp(AController* HelpingController,
	const FGuid RequestId, const bool bAtCampRescuePoint)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString Key = MakeTerminalKey(TEXT("CarryToCamp"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("Helper=%s|AtCampRescuePoint=%s"),
		HelpingController ? *HelpingController->GetName() : TEXT("None"), bAtCampRescuePoint ? TEXT("true") : TEXT("false"));
	switch (CatQueryTerminalReplay(TerminalCache, TerminalPayloadByKey, Key, PayloadSignature, Result, MarkCommandReplayed))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
	}
	if (!GetOwner() || !GetOwner()->HasAuthority() || !HelpingController || !RequestId.IsValid() || !bAtCampRescuePoint)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		TerminalCache.Add(Key, Result);
		TerminalPayloadByKey.Add(Key, PayloadSignature);
		return Result;
	}
	UAbilitySystemComponent* ASC = ResolveAbilitySystem();
	if (!ASC || !GetDefault<UCatConditionSettings>()->HasDownedThresholds())
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		TerminalCache.Add(Key, Result);
		TerminalPayloadByKey.Add(Key, PayloadSignature);
		return Result;
	}
	ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetPoisonAttribute(), 0.0f);
	EvaluateDownedFromAttributes(ECatRecoveryMode::CarriedToCamp);
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

// 统一恢复流程：把恢复模式作为 RequestId 的业务事实；缓存命中先挡住参数漂移，首次执行才修改 ASC 并重新裁决 Downed。
// 恢复对 Poison 的处理是清零而不是按量削减：飞书猫咪与状态册 §3.1.5 把这几条路径写成"解除倒地"，没有给任何毒值削减量，
// 而 EvaluateDownedFromAttributes 每次都从 Poison 重算 bDowned，部分削减一旦仍在阈值以上就会当场把人判回倒地，
// 与"单人可爬回营地休息自愈，保证不卡死"这条硬约束直接冲突。
// 载荷签名只剩 Mode：它只在本进程、本组件实例的终态缓存里互相比较，不复制也不落盘，去掉已删除的疲惫减量不会让已有重放记录失配。
FCatDomainCommandResult UCatConditionComponent::ApplyRecovery(const FGuid RequestId, const ECatRecoveryMode Mode)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	// 操作名必须是固定字面量、把 Mode 留给载荷签名，跟同文件的 EatFish / CarryToCamp 一致。
	// 以前这里把 Mode 拼进了键：野外自愈和营地休息因此落进两个不同的槽位，同一个 RequestId 分别发给这两条 RPC
	// 会各执行一次，而"换恢复参数会被拒绝"这条声明的不变量在签名侧永远比不出差异（键已经先分开了），形同虚设。
	const FString Key = MakeTerminalKey(TEXT("Recovery"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("Mode=%d"), static_cast<int32>(Mode));
	switch (CatQueryTerminalReplay(TerminalCache, TerminalPayloadByKey, Key, PayloadSignature, Result, MarkCommandReplayed))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
	}
	UAbilitySystemComponent* ASC = ResolveAbilitySystem();
	const UCatConditionSettings* Settings = GetDefault<UCatConditionSettings>();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !RequestId.IsValid() || !ASC || !Settings->HasDownedThresholds())
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
	}
	else
	{
		ASC->SetNumericAttributeBase(UCatSurvivalAttributeSet::GetPoisonAttribute(), 0.0f);
		EvaluateDownedFromAttributes(Mode);
		Result.bCommitted = true;
		Result.Error = ECatDomainCommandError::None;
		Result.Revision = Snapshot.Revision;
	}
	TerminalCache.Add(Key, Result);
	TerminalPayloadByKey.Add(Key, PayloadSignature);
	return Result;
}

// 倒地裁决流程：读取 ASC Poison 数值与显式阈值，更新唯一 Downed/RecoveryMode；首次进入倒地时终止相关 FishingSession，
// 始终没有死亡分支。倒地来源仅中毒（飞书猫咪状态册 v1.7）。
// 飞书 §3.1.5 的第三条恢复路径「翻天时未获救的倒地者自动救起、清晨在营地醒来」不由本组件发起：Run 换日时
// `ACatfishingGameModeBase::ApplyDayBreakSideEffects()` 会对仍然倒地的玩家调用本组件的营地休息入口，所以四条恢复路径
// 最终都汇到这里解除倒地。
void UCatConditionComponent::EvaluateDownedFromAttributes(const ECatRecoveryMode RecoveryMode)
{
	UAbilitySystemComponent* ASC = ResolveAbilitySystem();
	const UCatConditionSettings* Settings = GetDefault<UCatConditionSettings>();
	if (!ASC || !Settings->HasDownedThresholds())
	{
		return;
	}
	const bool bWasDowned = Snapshot.bDowned;
	Snapshot.bDowned = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetPoisonAttribute()) >= Settings->PoisonDownedThreshold;
	// RecoveryMode 记录这次裁决是由哪条路径触发的，恢复成功、人已站起来之后仍然保留：Snapshot 把它定义为最近一次被服务器接受的恢复方式。
	// 恢复路径现在会清掉 Poison，裁决后必然不倒地，所以旧写法（不倒地就抹成 None）会让三条恢复路径的记录全部立刻消失。
	// 吃鱼这类非恢复入口传 None，会把上一次的路径清掉。
	Snapshot.RecoveryMode = RecoveryMode;
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
