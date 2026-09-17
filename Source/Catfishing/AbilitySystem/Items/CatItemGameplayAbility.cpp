#include "AbilitySystem/Items/CatItemGameplayAbility.h"
#include "AbilitySystem/Items/CatItemAbilityComponent.h"
#include "AbilitySystem/Costs/CatAbilityCost_Item.h"
#include "AbilitySystem/Effects/CatFishExperienceEffect.h"
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "AbilitySystemComponent.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Inventory/CatInventoryAccessRules.h"
#include "Inventory/Fragments/CatItemUseFragment.h"
#include "Character/CatCharacter.h"
#include "Condition/CatConditionComponent.h"
#include "Data/CatFishDefinition.h"
#include "Data/CatFishCatalogSettings.h"
#include "Growth/CatGrowthComponent.h"
#include "Fishing/CatFishingService.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Collection/CatRunImprintService.h"
#include "Collection/CatFishCollectionLayers.h"
#include "GameFramework/PlayerState.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "Logging/CatLog.h"
#include "TimerManager.h"
#include "Misc/ScopeExit.h"

// 能力配置流程：资源成本由专用对象处理，身体动作标签使取消和互斥继续使用现有 GAS 契约。
UCatItemGameplayAbility::UCatItemGameplayAbility()
{
	AdditionalCosts.Add(CreateDefaultSubobject<UCatAbilityCost_Item>(TEXT("SourceItemCost")));
	SetAssetTags(FGameplayTagContainer(CatFishingAbilityTags::Ability_Body_Action));
	BlockAbilitiesWithTag.AddTag(CatFishingAbilityTags::Ability_Body_Action);
}
// 启动流程：清空上次激活状态，绑定目标监听并设置五秒接收期限；本地发送冻结意图，远端服务器读取已缓存数据。
// 未能取得本地意图或收到无效目标时结束能力；有效目标会清除接收期限，转入独立的前摇任务。
void UCatItemGameplayAbility::ActivateAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	UseTarget = {}; CommittedSource = nullptr; bTargetAccepted = false; bResourceCommitted = false; bUseCommitted = false;
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!ASC || !ActorInfo->AvatarActor.IsValid()) { EndAbility(Handle, ActorInfo, ActivationInfo, true, true); return; }
	TargetDelegate = ASC->AbilityTargetDataSetDelegate(Handle, ActivationInfo.GetActivationPredictionKey()).AddUObject(this, &ThisClass::ReceiveTargetData);
	GetWorld()->GetTimerManager().SetTimer(TargetTimeout, this, &ThisClass::CancelPendingUse, 5.0f, false);
	if (ActorInfo->IsLocallyControlled())
	{
		auto* Binding = ActorInfo->AvatarActor->FindComponentByClass<UCatItemAbilityComponent>();
		FCatItemAbilityTargetData Target;
		if (!Binding || !Binding->TakeLocalTarget(Handle, Target)) { EndAbility(Handle, ActorInfo, ActivationInfo, true, true); return; }
		FGameplayAbilityTargetDataHandle Data(new FCatItemAbilityTargetData(Target));
		FScopedPredictionWindow Prediction(ASC, true);
		if (!ActorInfo->IsNetAuthority()) ASC->CallServerSetReplicatedTargetData(Handle, ActivationInfo.GetActivationPredictionKey(), Data, FGameplayTag(), ASC->ScopedPredictionKey);
		ReceiveTargetData(Data, FGameplayTag());
	}
	else ASC->CallReplicatedTargetDataDelegatesIfSet(Handle, ActivationInfo.GetActivationPredictionKey());
}
// 目标接受流程：只接受一种数据结构且每次激活只接受一次；校验成功才播放预测蒙太奇并开始前摇任务。
void UCatItemGameplayAbility::ReceiveTargetData(const FGameplayAbilityTargetDataHandle& Data, FGameplayTag ApplicationTag)
{
	if (bTargetAccepted) return;
	if (Data.Num() != 1 || !Data.Get(0) || Data.Get(0)->GetScriptStruct() != FCatItemAbilityTargetData::StaticStruct())
	{ EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); return; }
	UseTarget = *static_cast<const FCatItemAbilityTargetData*>(Data.Get(0)); bTargetAccepted = true;
	UE_LOG(LogCatCharacter, Log, TEXT("Event=item_ability_target_received RequestId=%s Item=%s Ability=%s Owner=%s World=%s NetMode=%d Authority=%d LocalRole=%d PredictionKey=%d"),
		*UseTarget.RequestId.ToString(), *UseTarget.ItemId.ToString(), *GetClass()->GetName(), *GetNameSafe(GetAvatarActorFromActorInfo()),
		*GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), CurrentActorInfo->IsNetAuthority(),
		int32(GetAvatarActorFromActorInfo()->GetLocalRole()), CurrentActivationInfo.GetActivationPredictionKey().Current);
	GetWorld()->GetTimerManager().ClearTimer(TargetTimeout);
	if (!ValidateUse()) { EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); return; }
	const UCatItemUseFragment* Config = GetUseConfiguration();
	if (Config->Montage)
	{
		auto* MontageTask = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this, NAME_None, Config->Montage);
		MontageTask->OnInterrupted.AddDynamic(this, &ThisClass::CancelPendingUse);
		MontageTask->OnCancelled.AddDynamic(this, &ThisClass::CancelPendingUse);
		MontageTask->ReadyForActivation();
		if (!IsActive()) return;
	}
	if (Config->CommitDelay <= 0.f) { CommitUse(); return; }
	auto* Delay = UAbilityTask_WaitDelay::WaitDelay(this, Config->CommitDelay);
	Delay->OnFinish.AddDynamic(this, &ThisClass::CommitUse); Delay->ReadyForActivation();
}
// 来源读取流程：每次根据不可变实例 ID 查真实库存；格子移动允许继续，实例被移出则不寻找同种替代。
UCatInventoryItemInstance* UCatItemGameplayAbility::ResolveSourceItem() const
{
	if (!IsValid(UseTarget.Inventory)) return nullptr;
	const auto* Entry = UseTarget.Inventory->GetInventoryEntryAtSlot(UseTarget.Inventory->FindInventorySlotIndexFromInstanceId(UseTarget.ItemId));
	return Entry && Entry->StackCount > 0 ? Entry->Instance.Get() : nullptr;
}
// 配置读取流程：库存来源按实例定义读取，嘴叼来源按世界鱼的物品编号读取正式目录；提交前读取，来源失效返回空。
const UCatItemUseFragment* UCatItemGameplayAbility::GetUseConfiguration() const
{
	const auto* Item = ResolveSourceItem();
	if (IsValid(UseTarget.WorldFish))
	{
		const auto* Definition = GetDefault<UCatFishCatalogSettings>()->FindRuntimeDefinition(UseTarget.WorldFish->GetPresentationState().ItemId);
		return Definition ? Definition->FindFragment<UCatItemUseFragment>() : nullptr;
	}
	return Item && Item->GetItemDefinition() ? Item->GetItemDefinition()->FindFragment<UCatItemUseFragment>() : nullptr;
}
// 权限预检流程：先检查动作与角色状态，再区分嘴叼、随身或公共容器来源；嘴叼必须匹配唯一携带鱼，随身必须匹配 Spec 来源。
// 公共容器复核同世界、距离和真实组件；服务器最后检查局内命令准入与钓鱼动作门。
bool UCatItemGameplayAbility::ValidateUse() const
{
	const auto* Character = Cast<ACatCharacter>(GetAvatarActorFromActorInfo());
	const auto* Item = ResolveSourceItem();
	const auto* Config = GetUseConfiguration();
	const auto* Spec = GetCurrentAbilitySpec();
	if (!Character || !UseTarget.RequestId.IsValid() || !Config || !Config->IsRuntimeReady()
		|| Config->AbilityClass != GetClass() || !Spec || !Character->GetConditionComponent()
		|| Character->GetConditionComponent()->GetSnapshot().bDowned) return false;
	if (IsValid(UseTarget.WorldFish))
	{
		if (UseTarget.Inventory || Spec->SourceObject.IsValid() || ACatFishPickupActor::FindCarriedFish(Character) != UseTarget.WorldFish
			|| UseTarget.WorldFish->GetPresentationState().FishInstanceId != UseTarget.ItemId
			|| (Character->HasAuthority() && !UseTarget.WorldFish->CanConsumeFromAuthority(Character->GetController()))) return false;
	}
	else if (!Item) return false;
	else if (Spec->SourceObject.IsValid())
	{
		if (Spec->SourceObject.Get() != Item || UseTarget.Inventory != Character->GetInventoryComponent()) return false;
	}
	else if (CatInventoryAccessRules::ResolveReachableFishContainer(UseTarget.Inventory->GetOwner(), Character) != UseTarget.Inventory) return false;
	if (Character->HasAuthority())
	{
		const auto* GameMode = Character->GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>();
		if (!GameMode || !GameMode->CanAcceptGameplayCommand(Character->GetController())) return false;
		auto* Fishing = Character->GetWorld()->GetSubsystem<UCatFishingService>();
		if (!AllowsUseDuringActiveFishing() && Fishing && Fishing->IsActiveItemUseBlockedForController(Character->GetController())) return false;
	}
	return true;
}
// 通用行为配置流程：要求效果清单非空；这里只验证配置入口存在，不保证 GE 的条件、免疫或执行结果一定产生收益，领域行为覆写各自约束。
bool UCatItemGameplayAbility::ValidateUseConfiguration(const UCatItemUseFragment& Configuration, FText& OutError) const
{
	if (!Configuration.Effects.IsEmpty()) return true;
	OutError = NSLOCTEXT("CatItem", "MissingUseEffect", "效果型使用至少需要一个使用效果，不能只消费物品而没有作用。");
	return false;
}
// 单鱼配置流程：固定单条消费与成长效果类型，不解析 GE 内部修饰器；实际经验仍由本条鱼的重量和鱼种计算。
bool UCatGA_ConsumeFish::ValidateUseConfiguration(const UCatItemUseFragment& Configuration, FText& OutError) const
{
	if (!Super::ValidateUseConfiguration(Configuration, OutError)) return false;
	if (Configuration.ConsumeCount == 1 && Configuration.Effects.ContainsByPredicate([](const TSubclassOf<UGameplayEffect>& Effect)
		{ return Effect && Effect->IsChildOf(UCatGE_FishExperience::StaticClass()); })) return true;
	OutError = NSLOCTEXT("CatItem", "FishUseCount", "吃鱼的消耗数量必须为 1，且使用效果须包含 CatGE_FishExperience 或其子类；每次按这一条鱼的实际重量结算。");
	return false;
}
// 参数收集流程：普通道具复制命名数值配置；需要实例数据的能力在此基础上覆盖已声明的参数。
void UCatItemGameplayAbility::GatherEffectParameters(TMap<FGameplayTag, float>& Parameters) const
{
	if (const auto* Config = GetUseConfiguration()) Parameters = Config->Magnitudes;
}
// 默认成功通知为空；没有领域消费者的道具不因此产生第二条消息或状态链。
void UCatItemGameplayAbility::OnUseCommitted(UCatInventoryItemInstance* ConsumedItem) {}

// 生效流程：客户端只等待权威结束；服务器先准备全部效果再支付一次成本，成功后施加 GE 并通知消费者，不恢复整份库存快照。
void UCatItemGameplayAbility::CommitUse()
{
	if (bUseCommitted || !IsActive()) return;
	if (!CurrentActorInfo || !CurrentActorInfo->IsNetAuthority()) return;
	// 库存和属性通知可同步请求取消；沿 GAS 的作用域锁延后收尾，确保已扣成本的这次结算完成后才释放来源。
	IncrementListLock();
	ON_SCOPE_EXIT { DecrementListLock(); };
	if (!ValidateUse()) { EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); return; }
	CommittedSource = ResolveSourceItem();
	const auto* Config = GetUseConfiguration();
	TMap<FGameplayTag, float> Parameters; GatherEffectParameters(Parameters);
	TArray<FGameplayEffectSpecHandle> Effects;
	FGameplayEffectContextHandle Context = MakeEffectContext(CurrentSpecHandle, CurrentActorInfo);
	Context.AddSourceObject(CommittedSource ? static_cast<UObject*>(CommittedSource.Get()) : UseTarget.WorldFish.Get());
	for (const auto& EffectClass : Config->Effects)
	{
		FGameplayEffectSpecHandle Effect = CurrentActorInfo->AbilitySystemComponent->MakeOutgoingSpec(EffectClass, GetAbilityLevel(), Context);
		if (!Effect.IsValid()) { EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); return; }
		for (const auto& Parameter : Parameters) Effect.Data->SetSetByCallerMagnitude(Parameter.Key, Parameter.Value);
		Effects.Add(Effect);
	}
	if (!CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo) || !bResourceCommitted)
	{ EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); return; }
	bUseCommitted = true;
	for (const auto& Effect : Effects) CurrentActorInfo->AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*Effect.Data.Get());
	OnUseCommitted(CommittedSource);
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}
// 取消流程：只结束仍在等待或前摇的激活；成本已经提交后不再用表现中断否定权威结果。
void UCatItemGameplayAbility::CancelPendingUse()
{
	if (IsActive() && !bTargetAccepted)
		UE_LOG(LogCatCharacter, Warning, TEXT("Event=item_ability_target_timeout Ability=%s Owner=%s World=%s NetMode=%d Authority=%d PredictionKey=%d Result=NoTargetData"),
			*GetClass()->GetName(), *GetNameSafe(GetAvatarActorFromActorInfo()), *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()),
			CurrentActorInfo && CurrentActorInfo->IsNetAuthority(), CurrentActivationInfo.GetActivationPredictionKey().Current);
	if (IsActive() && !bUseCommitted)
		CancelAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true);
}
// 收尾流程：提交作用域尚未退出时先让 GAS 延后结束；正式收尾清除超时、目标监听与复制数据，再回送权威结果。
// 清空本次来源后才让引擎发布结束事件，避免结束监听者重激活后又被旧收尾擦掉；没有请求 ID 的超时不伪造业务回执。
void UCatItemGameplayAbility::EndAbility(FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (!IsActive()) return;
	if (ScopeLockCount > 0)
	{
		Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
		return;
	}
	if (GetWorld()) GetWorld()->GetTimerManager().ClearTimer(TargetTimeout);
	if (ActorInfo && ActorInfo->AbilitySystemComponent.IsValid())
	{
		auto* ASC = ActorInfo->AbilitySystemComponent.Get();
		ASC->AbilityTargetDataSetDelegate(Handle, ActivationInfo.GetActivationPredictionKey()).Remove(TargetDelegate);
		ASC->ConsumeClientReplicatedTargetData(Handle, ActivationInfo.GetActivationPredictionKey());
	}
	if (ActorInfo && ActorInfo->IsNetAuthority() && UseTarget.RequestId.IsValid())
	{
		FCatDomainCommandResult Result; Result.RequestId = UseTarget.RequestId; Result.bCommitted = bUseCommitted;
		Result.Error = bUseCommitted ? ECatDomainCommandError::None : ECatDomainCommandError::InvalidPhase;
		if (auto* Controller = Cast<ACatfishingPlayerController>(ActorInfo->PlayerController.Get())) Controller->ClientReceiveCampCommandResult(Result);
		UE_LOG(LogCatCharacter, Log, TEXT("Event=item_ability_terminal RequestId=%s Item=%s Ability=%s World=%s NetMode=%d Authority=1 Committed=%d Cancelled=%d"),
			*UseTarget.RequestId.ToString(), *UseTarget.ItemId.ToString(), *GetClass()->GetName(), *GetNameSafe(GetWorld()),
			GetWorld() ? int32(GetWorld()->GetNetMode()) : -1, bUseCommitted, bWasCancelled);
	}
	UseTarget = {}; CommittedSource = nullptr; bTargetAccepted = false; bResourceCommitted = false;
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
// 食用预检流程：公共规则通过后读取鱼实例与可食用标记，服务器再检查成长配置和本条鱼的实际经验。
bool UCatGA_ConsumeFish::ValidateUse() const
{
	if (!Super::ValidateUse()) return false;
	const auto* Fish = Cast<UCatFishInventoryItemInstance>(ResolveSourceItem());
	const auto* Character = Cast<ACatCharacter>(GetAvatarActorFromActorInfo());
	const auto* Definition = Fish ? Fish->GetFishDefinition() : UseTarget.WorldFish
		? GetDefault<UCatFishCatalogSettings>()->FindRuntimeDefinition(UseTarget.WorldFish->GetPresentationState().ItemId) : nullptr;
	const double Weight = Fish ? Fish->GetFishWeightKilograms() : UseTarget.WorldFish ? UseTarget.WorldFish->GetPresentationState().WeightKilograms : 0.0;
	return Definition && Definition->IsEdible() && Character->GetGrowthComponent()
		&& (!Character->HasAuthority() || Character->GetGrowthComponent()->ValidateFishGrowth(Definition, Weight) == ECatDomainCommandError::None);
}
// 实例参数流程：按服务器保存的实际重量计算经验；覆盖配置同名值，客户端不能提交更大的重量或经验。
void UCatGA_ConsumeFish::GatherEffectParameters(TMap<FGameplayTag, float>& Parameters) const
{
	Super::GatherEffectParameters(Parameters);
	if (const auto* Fish = Cast<UCatFishInventoryItemInstance>(ResolveSourceItem()))
		Parameters.Add(UCatGE_FishExperience::GetExperienceTag(), float(FMath::FloorToInt(Fish->GetFishDefinition()->ResolveEatingExperiencePoints(Fish->GetFishWeightKilograms()))));
	else if (const auto* WorldFish = UseTarget.WorldFish.Get())
		Parameters.Add(UCatGE_FishExperience::GetExperienceTag(), float(FMath::FloorToInt(WorldFish->GetFishDefinition()->ResolveEatingExperiencePoints(WorldFish->GetPresentationState().WeightKilograms))));
}
// 食用成功流程：从库存实例或世界鱼读取鱼种，记录当前食用者的知识；库存鱼再释放隐藏载体，嘴叼鱼已由成本完成实物销毁。
void UCatGA_ConsumeFish::OnUseCommitted(UCatInventoryItemInstance* ConsumedItem)
{
	auto* Fish = Cast<UCatFishInventoryItemInstance>(ConsumedItem);
	auto* Character = Cast<ACatCharacter>(GetAvatarActorFromActorInfo());
	const auto* Definition = Fish ? Fish->GetFishDefinition() : UseTarget.WorldFish ? UseTarget.WorldFish->GetFishDefinition() : nullptr;
	const auto* PlayerState = Character ? Character->GetPlayerState() : nullptr;
	if (Definition && CatFishCollectionLayers::HasKnowledgeLayer(Definition) && PlayerState && PlayerState->GetUniqueId().IsValid())
		if (auto* Imprints = GetWorld()->GetSubsystem<UCatRunImprintService>()) Imprints->RecordFishKnowledge(Definition->ItemId, PlayerState->GetUniqueId()->ToString());
	if (Fish) if (auto* Actor = Fish->GetWorldActor()) Actor->Destroy();
}
