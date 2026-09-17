#include "AbilitySystem/Items/CatItemAbilityComponent.h"
#include "AbilitySystem/Items/CatItemGameplayAbility.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/Fragments/CatItemUseFragment.h"
#include "Character/CatCharacter.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Engine/World.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Logging/CatLog.h"

// 构造流程：该组件没有独立复制状态，来源与能力通过库存和 ASC 复制，Tick 只供短暂等待使用。
UCatItemAbilityComponent::UCatItemAbilityComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}
// 开始流程：订阅唯一背包事实源；同时尝试初次授予，ASC 初始化较晚时会再次显式调用刷新。
void UCatItemAbilityComponent::BeginPlay()
{
	Super::BeginPlay();
	if (ACatCharacter* Character = Cast<ACatCharacter>(GetOwner()))
	{
		ObservedInventory = Character->GetInventoryComponent();
		if (ObservedInventory) ObservedInventory->OnInventoryObservedChanged.AddUObject(this, &ThisClass::RefreshGrantedAbilities);
	}
	RefreshGrantedAbilities();
}
// 结束流程：先解绑变化通知，再取消并移除旧 Pawn 的能力；本地待处理意图随组件销毁失效。
void UCatItemAbilityComponent::EndPlay(EEndPlayReason::Type Reason)
{
	if (ObservedInventory) ObservedInventory->OnInventoryObservedChanged.RemoveAll(this);
	if (auto* ASC = UCatAbilitySystemComponent::FindCatAbilitySystemFromActor(GetOwner()); ASC && GetOwner()->HasAuthority())
	{
		for (const auto& Entry : Granted) { ASC->CancelAbilityHandle(Entry.Value); ASC->ClearAbility(Entry.Value); }
	}
	Granted.Reset(); PendingTarget = {}; SetComponentTickEnabled(false);
	Super::EndPlay(Reason);
}
// 来源同步流程：服务器 ActorInfo 就绪后按当前库存授予；来源离库时先请求取消，再标记结束后移除。
// 已进入提交锁的最后一件消费延后收尾，未提交的蓄力立即取消；共享进食能力由角色默认 AbilitySet 管理。
void UCatItemAbilityComponent::RefreshGrantedAbilities()
{
	auto* Character = Cast<ACatCharacter>(GetOwner());
	auto* ASC = Character ? Character->GetCatAbilitySystemComponent() : nullptr;
	if (!Character || !Character->HasAuthority() || !ASC || ASC->GetAvatarActor() != Character) return;
	TSet<FGuid> Present;
	if (auto* Inventory = Character->GetInventoryComponent())
		for (const FCatInventoryEntry& Entry : Inventory->GetInventoryEntries())
		{
			auto* Item = Entry.Instance.Get();
			const auto* Definition = Item ? Item->GetItemDefinition() : nullptr;
			const auto* Use = Definition ? Definition->FindFragment<UCatItemUseFragment>() : nullptr;
			if (!Use || !Use->IsRuntimeReady() || Entry.StackCount <= 0) continue;
			Present.Add(Item->GetItemInstanceId());
			if (!Granted.Contains(Item->GetItemInstanceId()))
				Granted.Add(Item->GetItemInstanceId(), ASC->GiveAbility(FGameplayAbilitySpec(Use->AbilityClass, 1, INDEX_NONE, Item)));
		}
	for (auto It = Granted.CreateIterator(); It; ++It)
		if (!Present.Contains(It.Key())) { ASC->CancelAbilityHandle(It.Value()); ASC->SetRemoveAbilityOnEnd(It.Value()); It.RemoveCurrent(); }
}
// 输入流程：本地控制者提交有效库存引用与身份；已有待处理意图时拒绝新输入，否则冻结来源及持续输入标记。
// 能力配置已可读时由 CDO 采样一次目标，再匹配 Spec；等待期间不重新瞄准，最多等待两秒。
bool UCatItemAbilityComponent::RequestUse(UCatInventoryComponent* Inventory, FGuid ItemId, FGuid RequestId, bool bContinuousInput)
{
	auto* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn || !Pawn->IsLocallyControlled() || !IsValid(Inventory) || !ItemId.IsValid() || !RequestId.IsValid()) return false;
	if (PendingTarget.RequestId.IsValid()) return false;
	if (auto* ASC = UCatAbilitySystemComponent::FindCatAbilitySystemFromActor(GetOwner()))
		if (const auto* Held = ASC->FindAbilitySpecFromHandle(HeldInputHandle); Held && Held->IsActive()) return false;
	PendingTarget = {}; PendingTarget.Inventory = Inventory; PendingTarget.ItemId = ItemId; PendingTarget.RequestId = RequestId;
	PendingTarget.bContinuousInput = bContinuousInput;
	UE_LOG(LogCatCharacter, Log, TEXT("Event=item_use_requested RequestId=%s Item=%s Owner=%s World=%s NetMode=%d Authority=%d LocalRole=%d Source=%s"),
		*RequestId.ToString(), *ItemId.ToString(), *GetNameSafe(GetOwner()), *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()),
		GetOwner()->HasAuthority(), int32(GetOwner()->GetLocalRole()), *GetNameSafe(Inventory->GetOwner()));
	const auto* Entry = Inventory->GetInventoryEntryAtSlot(Inventory->FindInventorySlotIndexFromInstanceId(ItemId));
	const auto* Use = Entry && Entry->Instance && Entry->Instance->GetItemDefinition()
		? Entry->Instance->GetItemDefinition()->FindFragment<UCatItemUseFragment>() : nullptr;
	if (Use && Use->AbilityClass) Use->AbilityClass->GetDefaultObject<UCatItemGameplayAbility>()->CaptureTarget(Cast<APlayerController>(Pawn->GetController()), PendingTarget);
	PendingDeadline = GetWorld()->GetTimeSeconds() + 2.0;
	if (!TryActivatePending()) SetComponentTickEnabled(true);
	return true;
}
// 嘴叼输入流程：以角色当前唯一携带引用核对目标，冻结世界实物 ID；复制尚未就绪时复用一次等待意图。
bool UCatItemAbilityComponent::RequestUseCarriedFish(ACatFishPickupActor* Fish, FGuid RequestId)
{
	auto* Character = Cast<ACatCharacter>(GetOwner());
	if (!Character || !Character->IsLocallyControlled() || !IsValid(Fish) || !RequestId.IsValid()
		|| PendingTarget.RequestId.IsValid() || ACatFishPickupActor::FindCarriedFish(Character) != Fish) return false;
	PendingTarget = {}; PendingTarget.WorldFish = Fish; PendingTarget.ItemId = Fish->GetPresentationState().FishInstanceId;
	PendingTarget.RequestId = RequestId; PendingDeadline = GetWorld()->GetTimeSeconds() + 2.0;
	UE_LOG(LogCatCharacter, Log, TEXT("Event=item_use_requested RequestId=%s Item=%s Owner=%s World=%s NetMode=%d Authority=%d LocalRole=%d Source=%s"),
		*RequestId.ToString(), *PendingTarget.ItemId.ToString(), *GetNameSafe(GetOwner()), *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()),
		GetOwner()->HasAuthority(), int32(GetOwner()->GetLocalRole()), *GetNameSafe(Fish));
	if (!TryActivatePending()) SetComponentTickEnabled(true);
	return true;
}
// 查找流程：读取库存实例或嘴叼鱼配置，再匹配同类能力；随身来源必须绑定该实例，公共容器和嘴叼使用无来源的常驻能力。
// 尚无匹配 Spec 时继续等待，激活明确拒绝则返回失败并停止重试，不按当前快捷格替换来源。
bool UCatItemAbilityComponent::TryActivatePending()
{
	auto* ASC = UCatAbilitySystemComponent::FindCatAbilitySystemFromActor(GetOwner());
	auto* Inventory = PendingTarget.Inventory.Get();
	if (!ASC || (!IsValid(Inventory) && !IsValid(PendingTarget.WorldFish))) return false;
	const auto* Entry = Inventory ? Inventory->GetInventoryEntryAtSlot(Inventory->FindInventorySlotIndexFromInstanceId(PendingTarget.ItemId)) : nullptr;
	auto* Item = Entry ? Entry->Instance.Get() : nullptr;
	const auto* Definition = Item ? Item->GetItemDefinition() : PendingTarget.WorldFish
		? GetDefault<UCatFishCatalogSettings>()->FindRuntimeDefinition(PendingTarget.WorldFish->GetPresentationState().ItemId) : nullptr;
	const auto* Use = Definition ? Definition->FindFragment<UCatItemUseFragment>() : nullptr;
	if (!Use || !Use->IsRuntimeReady()) return false;
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (!Spec.Ability || Spec.Ability->GetClass() != Use->AbilityClass) continue;
		const bool bOwnSource = Item && Spec.SourceObject.Get() == Item;
		const bool bSharedSource = (!Inventory || Inventory->GetOwner() != GetOwner()) && !Spec.SourceObject.IsValid();
		if (!bOwnSource && !bSharedSource) continue;
		PendingHandle = Spec.Handle;
		const bool bHold = PendingTarget.bContinuousInput && Spec.Ability->GetClass()->GetDefaultObject<UCatItemGameplayAbility>()->UsesContinuousInput();
		if (auto* MutableSpec = ASC->FindAbilitySpecFromHandle(Spec.Handle)) MutableSpec->InputPressed = bHold;
		if (bHold) HeldInputHandle = Spec.Handle;
		const bool bActivated = ASC->TryActivateAbility(PendingHandle);
		// 已找到 Spec 的激活拒绝是明确终态，不能自动重试成下一次消费。
		if (!bActivated)
		{
			FCatDomainCommandResult Result;
			Result.RequestId = PendingTarget.RequestId;
			Result.Error = ECatDomainCommandError::InvalidPhase;
			UE_LOG(LogCatCharacter, Warning, TEXT("Event=item_use_activation_rejected RequestId=%s Item=%s Owner=%s World=%s NetMode=%d Result=InvalidPhase"),
				*PendingTarget.RequestId.ToString(), *PendingTarget.ItemId.ToString(), *GetNameSafe(GetOwner()), *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()));
			PendingTarget = {}; PendingHandle = {};
			if (auto* Pawn = Cast<APawn>(GetOwner()))
				if (auto* Controller = Cast<ACatfishingPlayerController>(Pawn->GetController())) Controller->OnCampCommandResultReceived.Broadcast(Result);
		}
		SetComponentTickEnabled(false);
		return true;
	}
	return false;
}
// 取走流程：只有正在激活的 Spec 能取得这次意图，随后清除待处理数据，重复激活不能复用旧输入。
bool UCatItemAbilityComponent::TakeLocalTarget(FGameplayAbilitySpecHandle Handle, FCatItemAbilityTargetData& OutTarget)
{
	if (Handle != PendingHandle || !PendingTarget.RequestId.IsValid()) return false;
	OutTarget = PendingTarget; PendingTarget = {}; PendingHandle = {}; SetComponentTickEnabled(false);
	return true;
}
// 等待流程：复用当前冻结来源尝试解析复制对象；超时给拥有者失败回执，换 Pawn 后由 EndPlay 清理。
void UCatItemAbilityComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	if (TryActivatePending()) return;
	if ((!IsValid(PendingTarget.Inventory) && !IsValid(PendingTarget.WorldFish)) || GetWorld()->GetTimeSeconds() >= PendingDeadline)
	{
		FCatDomainCommandResult Result; Result.RequestId = PendingTarget.RequestId; Result.Error = ECatDomainCommandError::DependencyUnavailable;
		UE_LOG(LogCatCharacter, Warning, TEXT("Event=item_use_source_unavailable RequestId=%s Item=%s Owner=%s World=%s NetMode=%d Result=ReplicationTimeoutOrSourceLost"),
			*PendingTarget.RequestId.ToString(), *PendingTarget.ItemId.ToString(), *GetNameSafe(GetOwner()), *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()));
		if (auto* Pawn = Cast<APawn>(GetOwner()))
			if (auto* Controller = Cast<ACatfishingPlayerController>(Pawn->GetController())) Controller->OnCampCommandResultReceived.Broadcast(Result);
		PendingTarget = {}; SetComponentTickEnabled(false);
	}
}

// 输入结束流程：复制等待期间先记录松开或取消；已激活时只向原 Spec 投递事件，AbilityTask 使用自身激活键完成跨网络配对。
void UCatItemAbilityComponent::ReleaseUseInput(bool bCancelled)
{
	if (PendingTarget.RequestId.IsValid())
	{
		if (bCancelled) { PendingTarget = {}; PendingHandle = {}; SetComponentTickEnabled(false); }
		else PendingTarget.bContinuousInput = false;
	}
	auto* ASC = UCatAbilitySystemComponent::FindCatAbilitySystemFromActor(GetOwner());
	const auto Handle = HeldInputHandle; HeldInputHandle = {};
	auto* Spec = ASC ? ASC->FindAbilitySpecFromHandle(Handle) : nullptr;
	if (!Spec || !Spec->IsActive()) return;
	Spec->InputPressed = false;
	if (bCancelled) { ASC->CancelAbilityHandle(Handle); return; }
	const auto* Ability = Spec->GetPrimaryInstance();
	if (!Ability) return;
	ASC->AbilitySpecInputReleased(*Spec);
	ASC->InvokeReplicatedEvent(EAbilityGenericReplicatedEvent::InputReleased, Handle, Ability->GetCurrentActivationInfo().GetActivationPredictionKey());
}
