#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Tags/CatStateTags.h"
#include "AbilitySystem/Effects/CatGE_PersistentState.h"
#include "Data/CatFishDefinition.h"
#include "Growth/CatGrowthComponent.h"
#include "AbilitySystem/Items/CatItemAbilityComponent.h"

#include "AbilitySystem/Config/CatAbilitySettings.h"
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "AbilitySystem/Effects/CatFishingStaminaEffect.h"
#include "AbilitySystem/Effects/CatFishingScoopCooldownEffect.h"
#include "AbilitySystem/Effects/CatGrowthAttributeEffect.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Abilities/GameplayAbility.h"
#include "Logging/CatLog.h"

UCatAbilitySystemComponent* UCatAbilitySystemComponent::FindCatAbilitySystemFromActor(AActor* Actor)
{
	// ASC 解析流程：只通过 GAS 标准 AbilitySystemInterface/BlueprintLibrary 查询，再收窄成项目 ASC；
	// 非猫身体、未装配 ASC 或错误 ASC 类型都会返回空，让调用方保持 fail-closed。
	return Actor ? Cast<UCatAbilitySystemComponent>(UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Actor))
		: nullptr;
}

// 输入按下流程：直接从 GAS Spec 的来源标签匹配能力，记录本帧边沿；不再维护第二份标签到句柄索引。
void UCatAbilitySystemComponent::AbilityInputTagPressed(const FGameplayTag InputTag)
{
	// 钓鱼负责人尚未迁移抄网操作锁，保留现有准入限制，不能把缺少新状态标签当成允许操作。
	if (HasMatchingGameplayTag(CatFishingAbilityTags::Cooldown_Fishing_Scoop)) return;
	if (!InputTag.IsValid()) return;
	for (FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
		{
			Spec.InputPressed = true;
			InputPressedSpecHandles.AddUnique(Spec.Handle);
			InputHeldSpecHandles.AddUnique(Spec.Handle);
		}
	}
}

// 输入松开流程：直接从 GAS Spec 的来源标签匹配能力，记录本帧边沿；不再维护第二份标签到句柄索引。
void UCatAbilitySystemComponent::AbilityInputTagReleased(const FGameplayTag InputTag)
{
	if (!InputTag.IsValid()) return;
	for (FGameplayAbilitySpec& Spec : GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
		{
			Spec.InputPressed = false;
			InputReleasedSpecHandles.AddUnique(Spec.Handle);
			InputHeldSpecHandles.RemoveSingleSwap(Spec.Handle);
		}
	}
}

// 输入帧流程：暂停或现有抄网操作锁期间保留边沿；先收集按住策略，再转发按下或激活，最后转发释放并清空本帧边沿。
// 输入消费流程：暂停或抄网操作锁期间保留边沿；先收集持续按住和首次按下的待激活 Spec。
// 活动实例直接收到同一激活键下的按下事件；激活请求统一执行后再转发释放事件，最后清除本帧边沿而保留按住集合。
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
		if (Spec && Spec->GetDynamicSpecSourceTags().HasTagExact(CatFishingAbilityTags::Ability_ActivationPolicy_WhileInputActive) && !Spec->IsActive())
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
			else if (!Spec->GetDynamicSpecSourceTags().HasTagExact(CatFishingAbilityTags::Ability_ActivationPolicy_OnGranted))
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
// 3. 绑定一次倒地取消监听并刷新当前物品来源授予；返回值只表示 ActorInfo 是否可继续用于授予 Ability 或播种属性；具体属性、AbilitySet 和输入资产的完整性由各自后续入口再裁决。
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
	if (!DownedTagHandle.IsValid()) DownedTagHandle = RegisterGameplayTagEvent(CatStateTags::Downed, EGameplayTagEventType::NewOrRemoved).AddUObject(this, &ThisClass::HandleDownedTagChanged);
	if (auto* Items = CharacterOwnerAvatar->FindComponentByClass<UCatItemAbilityComponent>()) Items->RefreshGrantedAbilities();
	return true;
}

// 默认 AbilitySet 授予流程：
// 1. 先要求本 ASC 已处在 authority Owner 上、尚未授予，并且项目设置启用 GAS；具体集合有效性由 AbilitySet 校验。
// 2. 再同步加载配置的 AbilitySet，通过 AbilitySet 自己的 GiveToAbilitySystem 写入 Ability、输入标签和初始效果。
// 3. 以本来源已有句柄判断是否授予，只有整组授予成功才保留句柄；失败保持无临时代用品，后续重占有仍可重试。
bool UCatAbilitySystemComponent::GrantConfiguredDefaultAbilitySetFromAuthority()
{
	const UCatAbilitySettings* Settings = GetDefault<UCatAbilitySettings>();
	if (!GetOwnerActor() || !IsOwnerActorAuthoritative() || ConfiguredDefaultAbilitySetHandles.HasAnyGrantedHandle()
		|| !Settings || !Settings->IsRuntimeEnabled())
	{
		return false;
	}
	const UCatAbilitySet* AbilitySet = Settings->DefaultAbilitySet.LoadSynchronous();
	return AbilitySet && AbilitySet->GiveToAbilitySystem(this, ConfiguredDefaultAbilitySetHandles);
}

// 默认 AbilitySet 撤销流程：
// 1. 只读取本 ASC 记录的授予句柄，不重新读取设置或猜测当前资产路径，避免销毁尾声同步加载无关资源。
// 2. TakeFromAbilitySystem 会撤销 AbilitySpec、初始 GameplayEffect，Spec 撤销会清理对应输入边沿；重复调用只清空空句柄集合。
// 3. 句柄集合本身就是授予所有权，不另存已授予标记。
void UCatAbilitySystemComponent::RevokeConfiguredDefaultAbilitySet()
{
	ConfiguredDefaultAbilitySetHandles.TakeFromAbilitySystem(this);
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

// 撤销流程：清除仅属于本 Spec 的输入边沿，再让 GAS 回收能力；不存在额外注册表或激活策略副本。
void UCatAbilitySystemComponent::OnRemoveAbility(FGameplayAbilitySpec& AbilitySpec)
{
	InputPressedSpecHandles.RemoveSingleSwap(AbilitySpec.Handle);
	InputReleasedSpecHandles.RemoveSingleSwap(AbilitySpec.Handle);
	InputHeldSpecHandles.RemoveSingleSwap(AbilitySpec.Handle);
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

// 来源更新流程：校验服务器与来源，空集合撤销；相同活动效果直接返回，否则先授予新效果再移除旧句柄，避免共同 Tag 短暂归零。
// 新效果创建或应用失败时返回 false，保留原来源的活动效果；成功后替换句柄，GAS 的 Tag 通知和复制负责驱动消费者。
bool UCatAbilitySystemComponent::SetStateTagsFromAuthority(FName Source, const FGameplayTagContainer& Tags)
{
	if (!IsOwnerActorAuthoritative() || Source.IsNone()) return false;
	const FActiveGameplayEffectHandle Previous = StateEffects.FindRef(Source);
	if (Tags.IsEmpty())
	{
		StateEffects.Remove(Source);
		if (Previous.IsValid())
		{
			RemoveActiveGameplayEffect(Previous);
			UE_LOG(LogCatCharacter, Log, TEXT("Event=gas_state_source_removed Source=%s Owner=%s World=%s Authority=1 NetMode=%d"),
				*Source.ToString(), *GetNameSafe(GetOwner()), *GetNameSafe(GetWorld()), int32(GetOwner()->GetNetMode()));
		}
		return true;
	}
	if (const FActiveGameplayEffect* Active = GetActiveGameplayEffect(Previous); Active && Active->Spec.DynamicGrantedTags == Tags) return true;
	FGameplayEffectContextHandle Context = MakeEffectContext();
	Context.AddSourceObject(this);
	FGameplayEffectSpecHandle Spec = MakeOutgoingSpec(UCatGE_PersistentState::StaticClass(), 1, Context);
	if (!Spec.IsValid()) return false;
	Spec.Data->DynamicGrantedTags = Tags;
	const FActiveGameplayEffectHandle Applied = ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	if (!Applied.IsValid())
	{
		UE_LOG(LogCatCharacter, Warning, TEXT("Event=gas_state_source_rejected Source=%s Owner=%s Reason=EffectNotApplied"), *Source.ToString(), *GetNameSafe(GetOwner()));
		return false;
	}
	StateEffects.Add(Source, Applied);
	if (Previous.IsValid()) RemoveActiveGameplayEffect(Previous);
	UE_LOG(LogCatCharacter, Log, TEXT("Event=gas_state_source_changed Source=%s Tags=%s Owner=%s World=%s Authority=1 NetMode=%d"),
		*Source.ToString(), *Tags.ToStringSimple(), *GetNameSafe(GetOwner()), *GetNameSafe(GetWorld()), int32(GetOwner()->GetNetMode()));
	return true;
}

// 全量清理流程：先移走所有权表再撤销效果，效果回调看不到旧句柄；每个来源独立移除，不清空其他系统状态。
void UCatAbilitySystemComponent::ClearStateSourcesFromAuthority()
{
	if (!IsOwnerActorAuthoritative()) return;
	const auto Effects = MoveTemp(StateEffects);
	for (const auto& Pair : Effects) RemoveActiveGameplayEffect(Pair.Value);
}

// 倒地通知流程：服务器在 Tag 首次出现时只取消明确声明倒地中断的能力；求助未声明该标签，状态移除也不自动重启动作。
void UCatAbilitySystemComponent::HandleDownedTagChanged(FGameplayTag Tag, int32 Count)
{
	if (Count > 0 && IsOwnerActorAuthoritative())
	{
		const FGameplayTagContainer InterruptTags(CatStateTags::AbilityInterruptOnDowned);
		CancelAbilities(&InterruptTags);
	}
}

// 最终退出流程：解绑本组件的状态回调，再回收状态与默认授予；ActorInfo 的短暂清理不删除仍属于同一身体的状态。
void UCatAbilitySystemComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	RegisterGameplayTagEvent(CatStateTags::Downed, EGameplayTagEventType::NewOrRemoved).Remove(DownedTagHandle);
	ClearStateSourcesFromAuthority();
	RevokeConfiguredDefaultAbilitySet();
	Super::EndPlay(Reason);
}
