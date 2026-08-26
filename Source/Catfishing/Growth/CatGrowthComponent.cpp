#include "Growth/CatGrowthComponent.h"

#include "Data/CatFishDefinition.h"
#include "Growth/CatGrowthSettings.h"
#include "Net/UnrealNetwork.h"

// 构造流程：开启默认复制并关闭 Tick；Snapshot 初始 Revision=0 表示尚未提交任何吃鱼成长事实。
UCatGrowthComponent::UCatGrowthComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
}

// 复制声明流程：保留父类字段并注册单一 Snapshot；幂等缓存和调用身份只留 authority。
void UCatGrowthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, Snapshot);
}

// Snapshot 读取流程：返回本机服务器真相或客户端最近复制值，不把 Buff 池或鱼表内容复制进第二个 DTO。
const FCatGrowthSnapshot& UCatGrowthComponent::GetSnapshot() const
{
	return Snapshot;
}

// 成长预检流程：只读核对 authority、正式成长 runtime、鱼定义和正经验；不修改实物鱼、Snapshot 或终态缓存。
ECatDomainCommandError UCatGrowthComponent::ValidateFishGrowth(const UCatFishDefinition* FishDefinition) const
{
	const UCatGrowthSettings* Settings = GetDefault<UCatGrowthSettings>();
	return GetOwner() && GetOwner()->HasAuthority() && Settings && Settings->IsRuntimeReady()
		&& FishDefinition && FishDefinition->IsRuntimeDefinitionReady()
		&& FMath::IsFinite(FishDefinition->EatingExperience) && FishDefinition->EatingExperience > 0.0
		? ECatDomainCommandError::None : ECatDomainCommandError::DependencyUnavailable;
}

// 吃鱼成长流程：先按 RequestId 重放，再验证 authority/定义；首次提交只写经验槽和待选次数，Buff 选择留给后续正式 UI/选项池。
FCatDomainCommandResult UCatGrowthComponent::ApplyCommittedFish(const FGuid RequestId,
	const UCatFishDefinition* FishDefinition)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString Key = MakeTerminalKey(TEXT("EatFishGrowth"), RequestId);
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		Result = *Cached;
		Result.bCommitted = false;
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	if (!RequestId.IsValid() || ValidateFishGrowth(FishDefinition) != ECatDomainCommandError::None)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else
	{
		AddExperienceFromCommittedFish(FMath::FloorToInt(FishDefinition->EatingExperience));
		Result.bCommitted = true;
		Result.Error = ECatDomainCommandError::None;
		Result.Revision = Snapshot.Revision;
	}
	TerminalCache.Add(Key, Result);
	return Result;
}

// Snapshot 复制回调流程：客户端只消费完整成长事实；表现系统可查询它，但这里不弹面板、不授 Buff。
void UCatGrowthComponent::OnRep_Snapshot()
{
	OnSnapshotChanged.Broadcast();
}

// 经验入槽流程：累计总经验后按当前槽长循环扣槽；连满只增加待选次数，避免在 Buff 具体内容未裁时提前生成效果。
void UCatGrowthComponent::AddExperienceFromCommittedFish(const int32 ExperienceAmount)
{
	const UCatGrowthSettings* Settings = GetDefault<UCatGrowthSettings>();
	if (!Settings || !Settings->IsRuntimeReady() || ExperienceAmount <= 0)
	{
		return;
	}
	Snapshot.TotalExperience += ExperienceAmount;
	Snapshot.ExperienceInCurrentSlot += ExperienceAmount;
	while (Snapshot.ExperienceInCurrentSlot >= Settings->ExperiencePerChoiceSlot)
	{
		Snapshot.ExperienceInCurrentSlot -= Settings->ExperiencePerChoiceSlot;
		++Snapshot.PendingChoiceCount;
	}
	++Snapshot.Revision;
	PublishSnapshot();
}

// 幂等键流程：在组件局内内存中组合操作和 RequestId；不包含 StableNetId，不进入日志、复制或 Profile。
FString UCatGrowthComponent::MakeTerminalKey(const TCHAR* Operation, const FGuid RequestId)
{
	return FString::Printf(TEXT("%s|%s"), Operation, *RequestId.ToString(EGuidFormats::DigitsWithHyphens));
}

// Snapshot 发布流程：authority 先要求 Owner 立即复制，再向同机只读订阅者广播；无 Owner 时仍广播当前对象变化但不尝试网络写入。
void UCatGrowthComponent::PublishSnapshot()
{
	if (AActor* Owner = GetOwner(); Owner && Owner->HasAuthority())
	{
		Owner->ForceNetUpdate();
	}
	OnSnapshotChanged.Broadcast();
}
