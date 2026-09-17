#include "Growth/CatGrowthComponent.h"
#include "Inventory/CatBackPackComponent.h"
#include "Fishing/CatFishingSession.h"
#include "EngineUtils.h"

#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Character/CatCharacter.h"
#include "Data/CatFishDefinition.h"
#include "GameFramework/Controller.h"
#include "Growth/CatGrowthSettings.h"
#include "Logging/CatLog.h"
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

// Snapshot 读取流程：返回本机服务器真相或客户端最近复制值，不把配表内容复制进第二个 DTO。
const FCatGrowthSnapshot& UCatGrowthComponent::GetSnapshot() const
{
	return Snapshot;
}

// 成长预检流程：只读核对 authority、正式成长 runtime、鱼定义与本条鱼算出来的正经验；不修改实物鱼、Snapshot 或终态缓存。
// 经验由鱼定义自己按「经验系数×实际重量」解析，不可食用鱼与未裁系数都会得到 0，在这里 fail-closed。
ECatDomainCommandError UCatGrowthComponent::ValidateFishGrowth(const UCatFishDefinition* FishDefinition,
	const double WeightKilograms) const
{
	const UCatGrowthSettings* Settings = GetDefault<UCatGrowthSettings>();
	return GetOwner() && GetOwner()->HasAuthority() && Settings && Settings->IsRuntimeReady()
		&& FishDefinition && FishDefinition->IsRuntimeDefinitionReady()
		&& FishDefinition->ResolveEatingExperiencePoints(WeightKilograms) > 0.0
		? ECatDomainCommandError::None : ECatDomainCommandError::DependencyUnavailable;
}

// 效果成长流程：检查服务器权限、经验和成长配置，再推进经验槽、待选次数与选项抽取；防重复提交由调用能力和成本承担。
// 保留旧的逐鱼取整边界：正浮点经验取整为 0 时仍接受进食，但经验槽、选项和总经验均不增加。
void UCatGrowthComponent::GrantExperienceFromEffect(const int32 ExperienceAmount)
{
	// GE 已由权威能力提交；这里只消费有效的成长输入，不再把属性回调当作库存事务回执。
	if (!GetOwner() || !GetOwner()->HasAuthority() || ExperienceAmount < 0
		|| !GetDefault<UCatGrowthSettings>()->IsRuntimeReady()) return;
	AddExperienceFromCommittedFish(ExperienceAmount);
}

// 三选一提交流程：
// 1. 先按 RequestId 重放，网络重试不会选两次。
// 2. 再验证 authority、请求者确实拥有本 Character、配表就绪、组序号未过期、所选项确实在当前这组里。
// 3. 通过后把本次选中叠进 Stacks（按配表上限夹），把真正生效的增量交给承载它的系统，扣一次待选次数并抽下一组。
FCatDomainCommandResult UCatGrowthComponent::ChooseOfferedOptionFromAuthority(AController* RequestingController,
	const FGuid RequestId, const ECatGrowthOptionId OptionId, const int32 OfferSerial)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString Key = MakeTerminalKey(TEXT("GrowthChoice"), RequestId);
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		Result = *Cached;
		MarkCommandReplayed(Result);
		return Result;
	}
	const ACatCharacter* Character = Cast<ACatCharacter>(GetOwner());
	const UCatGrowthSettings* Settings = GetDefault<UCatGrowthSettings>();
	const FCatGrowthOptionConfig* Config = Settings ? Settings->FindOptionConfig(OptionId) : nullptr;
	if (!Character || !Character->HasAuthority() || !RequestId.IsValid() || !RequestingController
		|| Character->GetController() != RequestingController || !Settings || !Settings->IsChoiceRuntimeReady())
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		TerminalCache.Add(Key, Result);
		return Result;
	}
	if (Snapshot.PendingChoiceCount <= 0 || Snapshot.OfferSerial != OfferSerial
		|| !Snapshot.CurrentOffer.Contains(OptionId) || !Config)
	{
		// 面板过期（已经被上一次选择换掉）与伪造选项走同一条拒绝：服务器只认自己发出的那一组。
		Result.Error = ECatDomainCommandError::InvalidPayload;
		TerminalCache.Add(Key, Result);
		return Result;
	}

	const double AppliedDelta = AccumulateStack(OptionId, *Config);
	ApplyOptionEffect(OptionId, AppliedDelta);
	--Snapshot.PendingChoiceCount;
	++Snapshot.CompletedChoiceCount;
	Snapshot.CurrentOffer.Reset();
	++Snapshot.Revision;
	RefreshCurrentOffer();
	PublishSnapshot();
	UE_LOG(LogCatCharacter, Log,
		TEXT("Event=growth_choice_committed Character=%s Option=%s AppliedDelta=%.4f Completed=%d Pending=%d Revision=%lld RequestId=%s World=%s NetMode=%d Authority=1 LocalRole=%d"),
		*GetNameSafe(GetOwner()), *UEnum::GetValueAsString(OptionId), AppliedDelta,
		Snapshot.CompletedChoiceCount, Snapshot.PendingChoiceCount, Snapshot.Revision, *RequestId.ToString(),
		*GetNameSafe(GetWorld()), GetWorld()->GetNetMode(), GetOwner()->GetLocalRole());
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	Result.Revision = Snapshot.Revision;
	TerminalCache.Add(Key, Result);
	return Result;
}

// 加成读取流程：只回答已经记进 Stacks 的合计量；未选过的项返回 0，调用方据此保持基础值。
double UCatGrowthComponent::GetTotalMagnitude(const ECatGrowthOptionId OptionId) const
{
	const FCatGrowthOptionStack* Stack = Snapshot.Stacks.FindByPredicate(
		[OptionId](const FCatGrowthOptionStack& Entry) { return Entry.OptionId == OptionId; });
	return Stack ? Stack->TotalMagnitude : 0.0;
}

// Snapshot 复制回调流程：客户端只消费完整成长事实；表现系统可查询它，但这里不弹面板、不授加成。
void UCatGrowthComponent::OnRep_Snapshot()
{
	OnSnapshotChanged.Broadcast();
}

// 经验入槽流程：累计总经验后按当前槽长循环扣槽；连满累计待选次数，随后按配表抽出当前这一组待选三项。
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
	RefreshCurrentOffer();
	PublishSnapshot();
}

// 抽取流程：
// 1. 待选次数为 0 或面板还在（上一组没选完）时什么都不做——连满是逐个弹出，不同时开两块面板。
// 2. 候选＝配表有效行 ∩ 已到出现次序 ∩ 未顶上限；有上限且已顶满的项不再占抽取名额，避免抽到必然无效的选项。
// 3. 用服务器 RNG 无放回抽 OptionsPerOffer 项保证同次互异；候选不足一组时宁可少发几项也不重复同一项。
void UCatGrowthComponent::RefreshCurrentOffer()
{
	const UCatGrowthSettings* Settings = GetDefault<UCatGrowthSettings>();
	if (Snapshot.PendingChoiceCount <= 0 || Snapshot.CurrentOffer.Num() > 0
		|| !Settings || !Settings->IsChoiceRuntimeReady())
	{
		return;
	}
	const int32 NextOrdinal = Snapshot.CompletedChoiceCount + 1;
	TArray<ECatGrowthOptionId> Candidates;
	for (const FCatGrowthOptionConfig& Row : Settings->OptionPool)
	{
		if (Row.OptionId == ECatGrowthOptionId::None || !FMath::IsFinite(Row.MagnitudePerPick)
			|| Row.MagnitudePerPick == 0.0 || Row.UnlockAtChoiceOrdinal > NextOrdinal
			|| Candidates.Contains(Row.OptionId) || Candidates.Num() >= Settings->MaxOptionPoolSize)
		{
			continue;
		}
		const bool bCapped = Row.MaxTotalMagnitude > 0.0
			&& FMath::Abs(GetTotalMagnitude(Row.OptionId)) >= Row.MaxTotalMagnitude - UE_DOUBLE_KINDA_SMALL_NUMBER;
		if (!bCapped)
		{
			Candidates.Add(Row.OptionId);
		}
	}
	const int32 OfferSize = FMath::Min(Settings->OptionsPerOffer, Candidates.Num());
	for (int32 Index = 0; Index < OfferSize; ++Index)
	{
		const int32 Pick = FMath::RandRange(0, Candidates.Num() - 1);
		Snapshot.CurrentOffer.Add(Candidates[Pick]);
		Candidates.RemoveAtSwap(Pick);
	}
	if (Snapshot.CurrentOffer.Num() > 0)
	{
		++Snapshot.OfferSerial;
	}
	else
	{
		// 全池顶满时不发空面板，待选次数留在快照里；玩家没有可选项，也不该看到一块空卡。
		UE_LOG(LogCatCharacter, Warning,
			TEXT("Event=growth_offer_empty Character=%s Pending=%d Completed=%d Reason=NoEligibleOption"),
			*GetNameSafe(GetOwner()), Snapshot.PendingChoiceCount, Snapshot.CompletedChoiceCount);
	}
}

// 叠加流程：同项跨次累加，按配表上限夹住绝对值（减益类配表写正上限、实际量为负，所以按绝对值比较）。
// 累计加成达到配表上限后，本次属性增量必须为 0；调用方只应用剩余额度，避免属性实际值超过成长记录。
double UCatGrowthComponent::AccumulateStack(const ECatGrowthOptionId OptionId, const FCatGrowthOptionConfig& Config)
{
	FCatGrowthOptionStack* Stack = Snapshot.Stacks.FindByPredicate(
		[OptionId](const FCatGrowthOptionStack& Entry) { return Entry.OptionId == OptionId; });
	if (!Stack)
	{
		Stack = &Snapshot.Stacks[Snapshot.Stacks.AddDefaulted()];
		Stack->OptionId = OptionId;
	}
	const double PreviousTotal = Stack->TotalMagnitude;
	double NewTotal = PreviousTotal + Config.MagnitudePerPick;
	if (Config.MaxTotalMagnitude > 0.0)
	{
		NewTotal = FMath::Clamp(NewTotal, -Config.MaxTotalMagnitude, Config.MaxTotalMagnitude);
	}
	Stack->TotalMagnitude = NewTotal;
	++Stack->TimesChosen;
	return NewTotal - PreviousTotal;
}

// 生效流程：属性三项沿用写口，背包及已存在的等待/完美窗当场通知；持续计算消费者直接读累计量。
// 力量与搏斗体力上限写进 ASC（体力上限提升时当场按差值补满，见升级效果页 §2）；移速改物理身体的速度缩放。
// 其余各项（放线回体、buff 时长、背包格数、完美窗、后勤扩容、咬钩间隔、渔获重量、竿磨损）
// 由各自的消费系统读 GetTotalMagnitude，本组件不替它们保存第二份数值。
void UCatGrowthComponent::ApplyOptionEffect(const ECatGrowthOptionId OptionId, const double AppliedDelta)
{
	if (AppliedDelta == 0.0 || !FMath::IsFinite(AppliedDelta))
	{
		return;
	}
	ACatCharacter* Character = Cast<ACatCharacter>(GetOwner());
	UCatAbilitySystemComponent* ASC = Character ? Character->GetCatAbilitySystemComponent() : nullptr;
	switch (OptionId)
	{
	case ECatGrowthOptionId::FishingStrength:
		if (ASC)
		{
			ASC->ApplyFishingStrengthDelta(static_cast<float>(AppliedDelta));
		}
		break;
	case ECatGrowthOptionId::MaxFightStamina:
		if (ASC)
		{
			ASC->ApplyMaxFightStaminaDelta(static_cast<float>(AppliedDelta));
		}
		break;
	case ECatGrowthOptionId::InventorySlots:
		if (Character)
			if (auto* Backpack = Cast<UCatBackPackComponent>(Character->GetInventoryComponent()))
				Backpack->InitializePlayerInventorySlotCapacityFromAuthority();
		break;
	case ECatGrowthOptionId::PerfectWindow:
	case ECatGrowthOptionId::BiteInterval:
		if (GetWorld())
			for (TActorIterator<ACatFishingSession> It(GetWorld()); It; ++It)
				It->RefreshGrowthFromAuthority(Character, OptionId, AppliedDelta);
		break;
	case ECatGrowthOptionId::MoveSpeed:
		if (Character)
		{
			Character->RefreshLocomotionSpeedScale();
		}
		break;
	default:
		break;
	}
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
