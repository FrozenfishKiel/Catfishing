#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Data/CatFishDefinition.h"
#include "Growth/CatGrowthComponent.h"

#include "AbilitySystem/Config/CatAbilitySettings.h"
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "AbilitySystem/Effects/CatFishingStaminaEffect.h"
#include "AbilitySystem/Effects/CatFishingScoopCooldownEffect.h"
#include "AbilitySystem/Effects/CatGrowthAttributeEffect.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbility.h"
#include "Logging/CatLog.h"

namespace
{
	ECatAbilityActivationPolicy ResolveActivationPolicy(const FGameplayTagContainer& Tags)
	{
		if (Tags.HasTagExact(CatFishingAbilityTags::Ability_ActivationPolicy_WhileInputActive))
		{
			return ECatAbilityActivationPolicy::WhileInputActive;
		}
		if (Tags.HasTagExact(CatFishingAbilityTags::Ability_ActivationPolicy_OnGranted))
		{
			return ECatAbilityActivationPolicy::OnGranted;
		}
		return ECatAbilityActivationPolicy::OnInputTriggered;
	}
}

UCatAbilitySystemComponent* UCatAbilitySystemComponent::FindCatAbilitySystemFromActor(AActor* Actor)
{
	// ASC 解析流程：只通过 GAS 标准 AbilitySystemInterface/BlueprintLibrary 查询，再收窄成项目 ASC；
	// 非猫身体、未装配 ASC 或错误 ASC 类型都会返回空，让调用方保持 fail-closed。
	return Actor ? Cast<UCatAbilitySystemComponent>(UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Actor))
		: nullptr;
}

void UCatAbilitySystemComponent::RegisterAbilityInput(const FGameplayAbilitySpecHandle Handle,
	const FGameplayTag InputTag, const ECatAbilityActivationPolicy ActivationPolicy)
{
	if (!Handle.IsValid())
	{
		return;
	}
	ActivationPolicyByHandle.Add(Handle, ActivationPolicy);
	if (InputTag.IsValid())
	{
		SpecHandlesByInputTag.FindOrAdd(InputTag).AddUnique(Handle);
	}
}

void UCatAbilitySystemComponent::UnregisterAbilityInput(const FGameplayAbilitySpecHandle Handle)
{
	ActivationPolicyByHandle.Remove(Handle);
	for (auto It = SpecHandlesByInputTag.CreateIterator(); It; ++It)
	{
		It.Value().RemoveSingleSwap(Handle);
		if (It.Value().IsEmpty())
		{
			It.RemoveCurrent();
		}
	}
	InputPressedSpecHandles.RemoveSingleSwap(Handle);
	InputReleasedSpecHandles.RemoveSingleSwap(Handle);
	InputHeldSpecHandles.RemoveSingleSwap(Handle);
}

void UCatAbilitySystemComponent::AbilityInputTagPressed(const FGameplayTag InputTag)
{
	if (HasMatchingGameplayTag(CatFishingAbilityTags::Cooldown_Fishing_Scoop)) return;
	const TArray<FGameplayAbilitySpecHandle>* Handles = SpecHandlesByInputTag.Find(InputTag);
	if (!Handles)
	{
		return;
	}
	for (const FGameplayAbilitySpecHandle Handle : *Handles)
	{
		if (FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle))
		{
			Spec->InputPressed = true;
			InputPressedSpecHandles.AddUnique(Handle);
			InputHeldSpecHandles.AddUnique(Handle);
		}
	}
}

void UCatAbilitySystemComponent::AbilityInputTagReleased(const FGameplayTag InputTag)
{
	const TArray<FGameplayAbilitySpecHandle>* Handles = SpecHandlesByInputTag.Find(InputTag);
	if (!Handles)
	{
		return;
	}
	for (const FGameplayAbilitySpecHandle Handle : *Handles)
	{
		if (FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle))
		{
			Spec->InputPressed = false;
			InputReleasedSpecHandles.AddUnique(Handle);
			InputHeldSpecHandles.RemoveSingleSwap(Handle);
		}
	}
}

void UCatAbilitySystemComponent::ProcessAbilityInput(const float DeltaTime, const bool bGamePaused)
{
	(void)DeltaTime;
	if (bGamePaused || HasMatchingGameplayTag(CatFishingAbilityTags::Cooldown_Fishing_Scoop))
	{
		return;
	}

	TArray<FGameplayAbilitySpecHandle> AbilitiesToActivate;
	for (const FGameplayAbilitySpecHandle Handle : InputHeldSpecHandles)
	{
		const FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle);
		const ECatAbilityActivationPolicy* Policy = ActivationPolicyByHandle.Find(Handle);
		if (Spec && Policy && *Policy == ECatAbilityActivationPolicy::WhileInputActive && !Spec->IsActive())
		{
			AbilitiesToActivate.AddUnique(Handle);
		}
	}
	for (const FGameplayAbilitySpecHandle Handle : InputPressedSpecHandles)
	{
		if (FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle))
		{
			if (Spec->IsActive())
			{
				// Task 订阅当前能力实例的激活键；Spec 上的旧键不能用于实例化能力的输入事件。
				const UGameplayAbility* Instance = Spec->GetPrimaryInstance();
				const FPredictionKey ActivationKey = Instance ? Instance->GetCurrentActivationInfo().GetActivationPredictionKey() : Spec->ActivationInfo.GetActivationPredictionKey();
				AbilitySpecInputPressed(*Spec);
				InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputPressed, Handle,
					ActivationKey);
			}
			else if (ActivationPolicyByHandle.FindRef(Handle) != ECatAbilityActivationPolicy::OnGranted)
			{
				AbilitiesToActivate.AddUnique(Handle);
			}
		}
	}
	for (const FGameplayAbilitySpecHandle Handle : AbilitiesToActivate)
	{
		TryActivateAbility(Handle);
	}
	for (const FGameplayAbilitySpecHandle Handle : InputReleasedSpecHandles)
	{
		if (FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle); Spec && Spec->IsActive())
		{
			// 与 WaitInputRelease 使用同一实例激活键，避免松开事件留在无人监听的旧 Spec 键下。
			const UGameplayAbility* Instance = Spec->GetPrimaryInstance();
			const FPredictionKey ActivationKey = Instance ? Instance->GetCurrentActivationInfo().GetActivationPredictionKey() : Spec->ActivationInfo.GetActivationPredictionKey();
			AbilitySpecInputReleased(*Spec);
			InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputReleased, Handle,
				ActivationKey);
		}
	}
	InputPressedSpecHandles.Reset();
	InputReleasedSpecHandles.Reset();
}

void UCatAbilitySystemComponent::ResetAbilityInput()
{
	for (const FGameplayAbilitySpecHandle Handle : InputHeldSpecHandles)
	{
		if (FGameplayAbilitySpec* Spec = FindAbilitySpecFromHandle(Handle))
		{
			Spec->InputPressed = false;
		}
	}
	InputPressedSpecHandles.Reset();
	InputReleasedSpecHandles.Reset();
	InputHeldSpecHandles.Reset();
}

bool UCatAbilitySystemComponent::CancelBodyActionAbilitiesFromAuthority()
{
	// 取消流程：
	// 1. 只允许服务器 owner 发起，避免客户端本地预测直接终止服务器事务窗口。
	// 2. 先扫描当前活跃 AbilitySpec，只命中带 BodyAction 资产标签的实例；没有活跃 BodyAction 时保持无副作用。
	// 3. 命中后再交给 GAS 标准 CancelAbilities，让 AbilityTask、EndAbility 和复制收尾按引擎路径完成。
	if (!IsOwnerActorAuthoritative())
	{
		return false;
	}
	FGameplayTagContainer BodyActionTags;
	BodyActionTags.AddTag(CatFishingAbilityTags::Ability_Body_Action);
	bool bHasActiveBodyAction = false;
	for (const FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		if (Spec.IsActive() && Spec.Ability && Spec.Ability->GetAssetTags().HasAny(BodyActionTags))
		{
			bHasActiveBodyAction = true;
			break;
		}
	}
	if (!bHasActiveBodyAction)
	{
		return false;
	}
	CancelAbilities(&BodyActionTags, nullptr, nullptr);
	return true;
}

// Character ASC ActorInfo 建立流程：
// 1. 先读取项目能力设置，只有显式启用的 Full 复制策略才建立 Owner/Avatar，未启用时主动清理引擎可能留下的临时 ActorInfo。
// 2. 再把同一个 Character Actor 同时作为 Owner 和 Avatar 写入 GAS，这是当前项目选择的 Character-owned ASC 边界。
// 3. 返回值只表示 ActorInfo 是否可继续用于授予 Ability 或播种属性；具体属性、AbilitySet 和输入资产的完整性由各自后续入口再裁决。
bool UCatAbilitySystemComponent::InitializeCharacterOwnerAvatar(AActor* CharacterOwnerAvatar)
{
	const UCatAbilitySettings* Settings = GetDefault<UCatAbilitySettings>();
	if (!CharacterOwnerAvatar || !Settings || !Settings->IsRuntimeEnabled())
	{
		ClearActorInfo();
		return false;
	}
	SetReplicationMode(EGameplayEffectReplicationMode::Full);
	InitAbilityActorInfo(CharacterOwnerAvatar, CharacterOwnerAvatar);
	return true;
}

// 默认 AbilitySet 授予流程：
// 1. 先要求本 ASC 已处在 authority Owner 上、尚未授予，并且项目设置声明 Fishing GAS 资产完整。
// 2. 再同步加载配置的 AbilitySet，通过 AbilitySet 自己的 GiveToAbilitySystem 写入 Ability、输入标签和初始效果。
// 3. 只有整组授予成功才记录句柄和已授予状态；失败保持无临时代用品，后续重占有仍可重试。
bool UCatAbilitySystemComponent::GrantConfiguredDefaultAbilitySetFromAuthority()
{
	const UCatAbilitySettings* Settings = GetDefault<UCatAbilitySettings>();
	if (!GetOwnerActor() || !IsOwnerActorAuthoritative() || bConfiguredDefaultAbilitySetGranted
		|| !Settings || !Settings->IsFishingRuntimeReady())
	{
		return false;
	}
	const UCatAbilitySet* AbilitySet = Settings->DefaultAbilitySet.LoadSynchronous();
	bConfiguredDefaultAbilitySetGranted = AbilitySet
		&& AbilitySet->GiveToAbilitySystem(this, ConfiguredDefaultAbilitySetHandles);
	return bConfiguredDefaultAbilitySetGranted;
}

// 默认 AbilitySet 撤销流程：
// 1. 只读取本 ASC 记录的授予句柄，不重新读取设置或猜测当前资产路径，避免销毁尾声同步加载无关资源。
// 2. TakeFromAbilitySystem 会撤销 AbilitySpec、初始 GameplayEffect 和输入索引；重复调用只清空空句柄集合。
// 3. 最后清掉已授予标记，让同一个组件在极端生命周期重入时仍保持幂等。
void UCatAbilitySystemComponent::RevokeConfiguredDefaultAbilitySet()
{
	ConfiguredDefaultAbilitySetHandles.TakeFromAbilitySystem(this);
	bConfiguredDefaultAbilitySetGranted = false;
}

// Character 初始属性播种流程：
// 1. 先要求已建立 Owner/Avatar 的 authority ASC，且本组件尚未成功播种；ActorInfo 未就绪、客户端调用或重占有都不触碰属性基值。
// 2. 再按 Character 传入的 CatDefinitionId 读取完整配置；配置缺失、未就绪或数值非法时只记录原有诊断并返回 false，不把半套数值写入 ASC。
// 3. 配置完整后一次写入 FishingStrength、MaxFightStamina 与黄色体力护盾段的基值，确保这些身体数值只通过 GAS 边界进入 AttributeSet。
// 4. 最后按新上限把 FightStamina 播种到满；这是本身体第一次拿到体力，不是搏斗入口——
//    2026-09-11 裁决④之后体力是跨竿资源，进搏斗与终局路径都不再回满，只有这里播种一次。
bool UCatAbilitySystemComponent::InitializeCharacterAttributesFromDefinition(const FName CatDefinitionId)
{
	if (!GetOwnerActor() || !GetAvatarActor() || !IsOwnerActorAuthoritative()
		|| bInitialCharacterAttributesApplied)
	{
		return bInitialCharacterAttributesApplied;
	}

	const UCatAbilitySettings* Settings = GetDefault<UCatAbilitySettings>();
	float FishingStrength = 0.0f;
	float MaxFightStamina = 0.0f;
	if (!Settings || !Settings->TryGetInitialAttributesForCharacter(CatDefinitionId, FishingStrength,
		MaxFightStamina))
	{
		if (!CatDefinitionId.IsNone())
		{
			UE_LOG(LogCatCharacter, Warning,
				TEXT("Event=initial_attributes_unresolved CatDefinitionId=%s Reason=DefinitionMissingOrNotReady"),
				*CatDefinitionId.ToString());
		}
		return false;
	}

	SetNumericAttributeBase(UCatSurvivalAttributeSet::GetFishingStrengthAttribute(), FishingStrength);
	// 黄色体力是当天的额外储备，新身体开局一律为 0；不从品种模板播种，也不由当前吃鱼入口授予。
	SetNumericAttributeBase(UCatSurvivalAttributeSet::GetYellowFightStaminaAttribute(), 0.0f);
	SetNumericAttributeBase(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute(), MaxFightStamina);
	if (!SeedFightStaminaToMaximumFromAuthority())
	{
		return false;
	}
	bInitialCharacterAttributesApplied = true;
	return true;
}

bool UCatAbilitySystemComponent::SeedFightStaminaToMaximumFromAuthority()
{
	// 新身体首次播种；入场、交接、终态和 ActorInfo 刷新均不调用。
	// 1. 拒绝缺 Owner/Avatar 或非 authority 的调用。
	// 2. 读取本身体绿段上限；上限未播种或非法时返回 false。
	// 3. 最后只提交到上限的 delta，沿用正式 GameplayEffect 写口，保持属性委托、复制和日志观察同源。
	// 只有身体属性播种会走这里。搏斗入口、终局路径不得调用：连续硬仗要有代价，
	// 空条靠搏斗外 5 点/秒回满（约 20 秒），小鱼干的价值窗就是省下这 20 秒。
	if (!GetOwnerActor() || !GetAvatarActor() || !IsOwnerActorAuthoritative())
	{
		return false;
	}
	const float Baseline = GetNumericAttribute(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute());
	if (!FMath::IsFinite(Baseline) || Baseline <= 0.0f)
	{
		return false;
	}
	const float Current = GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	if (!FMath::IsFinite(Current))
	{
		return false;
	}
	return FMath::IsNearlyEqual(Current, Baseline) || ApplyFishingStaminaDelta(Baseline - Current);
}

// 力量提交流程：只接受 authority 的有限增量，通过正式 GE 的 SetByCaller 写进 FishingStrength。
// 三选一「力量 +10」是唯一调用方；属性的非负规整仍由 AttributeSet 负责，这里不自己夹。
bool UCatAbilitySystemComponent::ApplyFishingStrengthDelta(const float Delta)
{
	if (!FMath::IsFinite(Delta) || Delta == 0.0f || !GetOwnerActor() || !GetAvatarActor()
		|| !IsOwnerActorAuthoritative())
	{
		return false;
	}
	const FGameplayEffectSpecHandle Spec = MakeOutgoingSpec(UCatGE_FishingStrengthDelta::StaticClass(), 1.0f,
		MakeEffectContext());
	if (!Spec.IsValid())
	{
		return false;
	}
	Spec.Data->SetSetByCallerMagnitude(UCatGE_FishingStrengthDelta::GetFishingStrengthDeltaTag(), Delta);
	return ApplyGameplayEffectSpecToSelf(*Spec.Data.Get()).WasSuccessfullyApplied();
}

// 体力上限提交流程：先按 GE 改 MaxFightStamina，再对当前体力补同样的差值。
// 「提升时当场按差值补满」是升级效果页 §2 明写的口径，它只补本次提升的那一段，不是回满——
// 2026-09-11 裁决④删掉的是「进搏斗补满」，这一条是成长带来的新增上限，两者不冲突。
bool UCatAbilitySystemComponent::ApplyMaxFightStaminaDelta(const float Delta)
{
	if (!FMath::IsFinite(Delta) || Delta == 0.0f || !GetOwnerActor() || !GetAvatarActor()
		|| !IsOwnerActorAuthoritative())
	{
		return false;
	}
	const FGameplayEffectSpecHandle Spec = MakeOutgoingSpec(UCatGE_MaxFightStaminaDelta::StaticClass(), 1.0f,
		MakeEffectContext());
	if (!Spec.IsValid())
	{
		return false;
	}
	Spec.Data->SetSetByCallerMagnitude(UCatGE_MaxFightStaminaDelta::GetMaxFightStaminaDeltaTag(), Delta);
	if (!ApplyGameplayEffectSpecToSelf(*Spec.Data.Get()).WasSuccessfullyApplied())
	{
		return false;
	}
	return Delta <= 0.0f || ApplyFishingStaminaDelta(Delta);
}

void UCatAbilitySystemComponent::ClearActorInfo()
{
	ResetAbilityInput();
	Super::ClearActorInfo();
}

void UCatAbilitySystemComponent::OnGiveAbility(FGameplayAbilitySpec& AbilitySpec)
{
	Super::OnGiveAbility(AbilitySpec);
	const FGameplayTag InputRoot = FGameplayTag::RequestGameplayTag(FName(TEXT("Cat.Input")));
	const FGameplayTagContainer& Tags = AbilitySpec.GetDynamicSpecSourceTags();
	for (const FGameplayTag Tag : Tags)
	{
		if (Tag.MatchesTag(InputRoot) && Tag != InputRoot)
		{
			RegisterAbilityInput(AbilitySpec.Handle, Tag, ResolveActivationPolicy(Tags));
		}
	}
}

void UCatAbilitySystemComponent::OnRemoveAbility(FGameplayAbilitySpec& AbilitySpec)
{
	UnregisterAbilityInput(AbilitySpec.Handle);
	Super::OnRemoveAbility(AbilitySpec);
}

// 数值成长 §4 / 2026-09-13 裁决②：先扣绿再扣黄；一张 GE 提交整笔账，恢复只走绿。
bool UCatAbilitySystemComponent::ApplyFishingStaminaDelta(const float Delta)
{
	if (!FMath::IsFinite(Delta) || Delta == 0.0f || !GetOwnerActor() || !GetAvatarActor()
		|| !IsOwnerActorAuthoritative()) return false;
	const float Green = GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	const float Yellow = GetNumericAttribute(UCatSurvivalAttributeSet::GetYellowFightStaminaAttribute());
	if (!FMath::IsFinite(Green) || Green < 0 || !FMath::IsFinite(Yellow) || Yellow < 0) return false;
	const float GreenDelta = Delta < 0 ? -FMath::Min(Green, -Delta) : Delta;
	const float YellowDelta = Delta < 0 ? -FMath::Min(Yellow, -(Delta - GreenDelta)) : 0.0f;
	const auto Spec = MakeOutgoingSpec(UCatGE_FishingStaminaDelta::StaticClass(), 1.0f, MakeEffectContext());
	if (!Spec.IsValid()) return false;
	Spec.Data->SetSetByCallerMagnitude(CatFishingAbilityTags::Data_Fishing_FightStaminaDelta, GreenDelta);
	Spec.Data->SetSetByCallerMagnitude(UCatGE_FishingStaminaDelta::GetYellowDeltaTag(), YellowDelta);
	return ApplyGameplayEffectSpecToSelf(*Spec.Data.Get()).WasSuccessfullyApplied();
}

// 黄色储备修改流程：校验服务器与有限数值，负向调整限制到零；同值直接成功，其余通过即时 GE 写入并记录结果。
bool UCatAbilitySystemComponent::ApplyYellowFightStaminaDelta(const float Delta)
{
	if (!FMath::IsFinite(Delta) || !GetOwnerActor() || !GetAvatarActor() || !IsOwnerActorAuthoritative()) return false;
	const float Current = GetNumericAttribute(UCatSurvivalAttributeSet::GetYellowFightStaminaAttribute());
	const float Target = FMath::Max(0.0f, Current + Delta);
	if (!FMath::IsFinite(Current) || !FMath::IsFinite(Target)) return false;
	if (Target == Current) return true;
	const auto Spec = MakeOutgoingSpec(UCatGE_YellowFightStaminaDelta::StaticClass(), 1.0f, MakeEffectContext());
	if (!Spec.IsValid()) return false;
	Spec.Data->SetSetByCallerMagnitude(UCatGE_FishingStaminaDelta::GetYellowDeltaTag(), Target - Current);
	const bool Applied = ApplyGameplayEffectSpecToSelf(*Spec.Data.Get()).WasSuccessfullyApplied();
	UE_LOG(LogCatCharacter, Log, TEXT("Event=yellow_stamina_changed World=%s NetMode=%d Authority=1 LocalRole=%d Actor=%s Before=%.6f Delta=%.6f After=%.6f Result=%s"),
		*GetNameSafe(GetWorld()), int32(GetOwnerActor()->GetNetMode()), int32(GetOwnerActor()->GetLocalRole()),
		*GetNameSafe(GetAvatarActor()), Current, Delta, GetYellowFightStamina(), Applied ? TEXT("Applied") : TEXT("Rejected"));
	return Applied;
}

// 清晨清空流程：读取当前黄色储备，以相反增量复用唯一写口；权限和非法值仍由写口拒绝。
bool UCatAbilitySystemComponent::ClearYellowFightStaminaFromAuthority()
{
	return ApplyYellowFightStaminaDelta(-GetNumericAttribute(UCatSurvivalAttributeSet::GetYellowFightStaminaAttribute()));
}

// 当前可用体力读取流程：将绿色和黄色属性转为 double 后相加，供支付与显示读取，不在查询时改属性。
double UCatAbilitySystemComponent::GetTotalFightStamina() const
{
	return double(GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute()))
		+ double(GetNumericAttribute(UCatSurvivalAttributeSet::GetYellowFightStaminaAttribute()));
}

// 容量读取流程：先读取绿色上限；非法或非正值原样交给调用方判断，其余加当前黄色储备形成总容量。
double UCatAbilitySystemComponent::GetTotalFightStaminaCapacity() const
{
	const double Maximum = GetNumericAttribute(UCatSurvivalAttributeSet::GetMaxFightStaminaAttribute());
	return !FMath::IsFinite(Maximum) || Maximum <= 0 ? Maximum
		: Maximum + double(GetNumericAttribute(UCatSurvivalAttributeSet::GetYellowFightStaminaAttribute()));
}

// 储备读取流程：返回 ASC 当前黄色属性快照，查询不修正数值也不产生玩法副作用。
float UCatAbilitySystemComponent::GetYellowFightStamina() const
{
	return GetNumericAttribute(UCatSurvivalAttributeSet::GetYellowFightStaminaAttribute());
}
