#include "Camp/CatAltarActor.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Tags/CatStateTags.h"
#include "FishContainers/CatFishGuardActor.h"
#include "Inventory/CatFishOnlyInventoryComponent.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Camp/CatCampHubActor.h"
#include "UI/WorldInfo/CatAltarWorldInfoComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Condition/CatConditionComponent.h"
#include "EngineUtils.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingGameState.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Interaction/CatInteractionSettings.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "Net/UnrealNetwork.h"
#include "Run/CatRunSettings.h"

DEFINE_LOG_CATEGORY_STATIC(LogCatAltar, Log, All);

// 实体初始化：启用始终相关的 Actor 复制和 0.2 秒 Tick，创建阻挡角色及交互射线的雕像根，再挂接只读信息锚点；具体网格由关卡指定。
ACatAltarActor::ACatAltarActor()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.2f;
	StatueMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StatueMesh"));
	SetRootComponent(StatueMesh);
	StatueMesh->SetCollisionProfileName(TEXT("BlockAll"));
	WorldInfo = CreateDefaultSubobject<UCatAltarWorldInfoComponent>(TEXT("WorldInfo"));
	WorldInfo->SetupAttachment(StatueMesh);
}

// 复制注册：保留父类字段，再登记营地关联及地面预览；确认名单由 GameState 复制的公开快照持有，世界鱼引用只留在服务器。
void ACatAltarActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, CampHub);
	DOREPLIFETIME(ThisClass, bGroundOfferingReady);
	DOREPLIFETIME(ThisClass, GroundOfferingPoints);
}

// 地面预览流程：父类 Tick 后仅服务器更新祭坛范围内的可献点数。
// 确认名单改由 GameMode 在发起、输入和玩家生命周期入口更新，Tick 不再轮询或触发翻天。
void ACatAltarActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority()) RefreshGroundOfferingPreview();
}

// 生命周期退出：先核对公开确认是否属于本祭坛，只取消匹配的确认；再让 GameMode 按祭坛身份中止正式过渡。
// 不影响另一座祭坛的确认，最后丢弃冻结散鱼弱引用并交由父类销毁 Actor；未提交的鱼和鱼护库存不会被消费。
void ACatAltarActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ACatfishingGameModeBase* GameMode = GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>())
	{
		if (GameMode->GetRunPublicState().AltarConfirmation.Altar.Get() == this)
		{
			GameMode->CancelAltarConfirmation(NSLOCTEXT("Catfishing", "AltarUnavailable", "祭坛已不可用"));
		}
		GameMode->CancelAltarDayTransition(this, NSLOCTEXT("Catfishing", "AltarUnavailable", "祭坛已不可用"));
	}
	ResetOffering();
	Super::EndPlay(EndPlayReason);
}

// 交互资格：读取同一 Run 阶段与当前 Pawn 身体，服务器额外核对 Active 身份；确认等待中只允许 F8/F9 远程输入，不能再发起另一轮现场交互。
bool ACatAltarActor::CanInteract_Implementation(AController* Controller) const
{
	const ACatfishingGameState* GameState = GetWorld()->GetGameState<ACatfishingGameState>();
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	const UCatAbilitySystemComponent* Condition = Pawn ? Pawn->FindComponentByClass<UCatAbilitySystemComponent>() : nullptr;
	if (!GameState || !Pawn || !Condition || Condition->HasMatchingGameplayTag(CatStateTags::Downed)
		|| GameState->GetRunPublicState().Phase.Phase != ECatRunPhase::NormalNight
		|| !GameState->GetRunPublicState().Phase.bOfferingOpen || GameState->GetRunPublicState().DayTransition.bActive
		|| GameState->GetRunPublicState().AltarConfirmation.State == ECatAltarConfirmationState::Waiting
		|| FVector::DistSquared(Pawn->GetPawnViewLocation(), GetActorLocation()) > FMath::Square(GetInteractionRadius_Implementation()))
	{
		return false;
	}
	const ACatfishingGameModeBase* GameMode = GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>();
	if (HasAuthority())
	{
		if (!GameMode || !GameMode->CanAcceptGameplayCommand(Controller)) return false;
		const UCatInteractionSettings* Settings = GetDefault<UCatInteractionSettings>();
		if (Settings->bRequireServerLineOfSight)
		{
			FHitResult Hit;
			FCollisionQueryParams Query(SCENE_QUERY_STAT(CatAltarInteraction), true, Pawn);
			if (GetWorld()->LineTraceSingleByChannel(Hit, Pawn->GetPawnViewLocation(), GetActorLocation(), Settings->TargetingTraceChannel, Query)
				&& Hit.GetActor() != this) return false;
		}
	}
	return true;
}

// 提示生成：只返回现场发起动作；全队名单、倒计时和取消原因由顶部确认窗口从 RunPublicState 读取。
FText ACatAltarActor::GetInteractionPrompt_Implementation() const
{
	return NSLOCTEXT("Catfishing", "AltarConfirmPrompt", "献给圣猫");
}

// 距离读取：使用项目既有 E 键上限，所有端采用相同厘米单位。
double ACatAltarActor::GetInteractionRadius_Implementation() const
{
	return GetDefault<UCatInteractionSettings>()->MaximumServerInteractionDistanceCentimeters;
}

// 发起流程：客户端仍经既有 Controller RPC 转发；服务器只把现场合法交互交给 GameMode 创建确认快照。
// 供品不会在此时冻结，远程确认、超时和玩家生命周期都由同一份公开快照裁决。
bool ACatAltarActor::Interact_Implementation(AController* Controller, FGuid RequestId)
{
	if (!RequestId.IsValid() || !CanInteract_Implementation(Controller))
	{
		return false;
	}
	if (!HasAuthority())
	{
		ACatfishingPlayerController* Player = Cast<ACatfishingPlayerController>(Controller);
		if (!Player || !Player->IsLocalController()) return false;
		UE_LOG(LogCatAltar, Log, TEXT("Event=AltarConfirmationRequested World=%s NetMode=%d Authority=0 LocalRole=%d Altar=%s Player=%s RequestId=%s"),
			*GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()), *GetName(), *GetNameSafe(Player), *RequestId.ToString(EGuidFormats::DigitsWithHyphens));
		Player->ServerRequestInteraction(this, RequestId);
		return true;
	}
	ACatfishingGameModeBase* GameMode = GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>();
	if (!GameMode) return false;
	const bool bAccepted = GameMode->BeginAltarConfirmation(this, Controller, RequestId);
	UE_LOG(LogCatAltar, Display, TEXT("Event=AltarConfirmationRequested World=%s NetMode=%d Authority=1 LocalRole=%d Altar=%s Player=%s RequestId=%s Accepted=%d"),
		*GetWorld()->GetName(), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()), *GetName(), *GetNameSafe(Controller),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens), bAccepted);
	return bAccepted;
}

// 配置预检流程：确认请求开始前验证服务器、World、供品半径、当前日参数及完整重量档边界。
// 它不会收集、冻结或消费鱼，因此等待期间地面供品仍可被正常移动，正式过场才以当时的一批为准。
bool ACatAltarActor::CanPrepareOffering(FText& OutError) const
{
	OutError = FText::GetEmpty();
	if (!HasAuthority() || !GetWorld() || !FMath::IsFinite(OfferingRadiusCentimeters) || OfferingRadiusCentimeters <= 0.0f
		|| !GetDefault<UCatRunSettings>())
	{
		OutError = NSLOCTEXT("Catfishing", "AltarOfferingConfigurationUnavailable", "祭坛供品配置暂不可用");
		return false;
	}
	const ACatfishingGameState* GameState = GetWorld()->GetGameState<ACatfishingGameState>();
	const UCatRunSettings* Settings = GetDefault<UCatRunSettings>();
	float DayLengthSeconds = 0.0f;
	FCatRunDailyOfferingTuning Tuning;
	ECatOfferingWeightClass WeightClass;
	int32 OfferingPoints = 0;
	// 分类函数在处理任意正重量前都会验证全部档位；这里用一千克只触发配置校验，不读取现场供品也不预定点数。
	if (!GameState || !Settings->TryGetDayParameters(GameState->GetRunPublicState().Phase.DayIndex, DayLengthSeconds, Tuning)
		|| !Settings->TryClassifyOfferingWeight(1.0, WeightClass, OfferingPoints))
	{
		OutError = NSLOCTEXT("Catfishing", "AltarOfferingConfigurationInvalid", "祭坛供品配置无效");
		return false;
	}
	return true;
}

// 冻结流程：先核对服务器权限、请求标识和供品配置，再用共用筛选器填充散鱼引用、鱼护槽快照和分类命令。
// 收集成功才写入祭坛与命令的请求标识，随后预检同一批来源；失败输出不可提交，已写的短期引用由调用方 ResetOffering 收口，空集合合法。
bool ACatAltarActor::FreezeOffering(AController* Controller, FGuid RequestId, FCatOfferingSettlementCommand& OutCommand, FText& OutError)
{
	if (!RequestId.IsValid() || !CanPrepareOffering(OutError)) return false;
	int32 PreviewPoints = 0;
	if (!CollectOffering(FrozenFish, FrozenGuards, OutCommand, PreviewPoints, OutError)) return false;
	OfferingRequestId = RequestId;
	OutCommand.Context.RequestId = RequestId;
	return ValidateFrozenOffering(Controller, RequestId, OutError);
}

// 共用筛选：
// 1. 清空所有输出；无服务器权限、World 或有限正半径时返回 false。
// 2. 扫描本世界散鱼，保留 Available、未隐藏、无父附着且根位置在厘米半径内的对象；按真实千克重量分类。
// 3. 再扫描地面鱼护，复制每个槽位快照并把非空护内鱼按同一分类口径累计；同一鱼 GUID 出现两次时拒绝整批。
// 4. 身份、定义、重量、负点数或累计溢出异常都会返回 false；调用方必须以返回值为准，不能提交部分输出。
// 5. 本方法只生成预览/冻结输入，不预留库存、不隐藏散鱼、不消费实物，空集合合法。
bool ACatAltarActor::CollectOffering(TArray<TWeakObjectPtr<ACatFishPickupActor>>& OutFish,
	TMap<TWeakObjectPtr<ACatFishGuardActor>, TArray<FCatInventoryEntry>>& OutGuards, FCatOfferingSettlementCommand& OutCommand, int32& OutPoints, FText& OutError) const
{
	OutFish.Reset();
	OutGuards.Reset();
	OutCommand = FCatOfferingSettlementCommand();
	OutPoints = 0;
	OutError = FText::GetEmpty();
	if (!HasAuthority() || !GetWorld() || !FMath::IsFinite(OfferingRadiusCentimeters) || OfferingRadiusCentimeters <= 0.0f) return false;
	const UCatRunSettings* Settings = GetDefault<UCatRunSettings>();
	TSet<FGuid> Seen;
	// 两种来源只在取身份与重量处不同，分类和点数保持一个口径；重复身份不会二次计分或消费。
	const auto AddFish = [&](FGuid Id, int32  ItemId, double Weight)
	{
		if (!Id.IsValid() || Seen.Contains(Id)) return false;
		ECatOfferingWeightClass WeightClass;
		int32 Points = 0;
		if (!Settings->TryClassifyOfferingWeight(Weight, WeightClass, Points) || Points < 0 || OutPoints > MAX_int32 - Points) return false;
		switch (WeightClass)
		{
		case ECatOfferingWeightClass::Small: ++OutCommand.SmallFishCount; break;
		case ECatOfferingWeightClass::Medium: ++OutCommand.MediumFishCount; break;
		case ECatOfferingWeightClass::Large: ++OutCommand.LargeFishCount; break;
		case ECatOfferingWeightClass::Giant: ++OutCommand.GiantFishCount; break;
		}
		if (Settings->IsStinkyOfferingFish(ItemId)) ++OutCommand.StinkyFishCount;
		OutPoints += Points;
		Seen.Add(Id);
		return true;
	};
	for (TActorIterator<ACatFishPickupActor> It(GetWorld()); It; ++It)
	{
		const FCatFishPickupPresentationState& Fish = It->GetPresentationState();
		if (Fish.State != ECatFishPickupState::Available || It->GetAttachParentActor() || It->IsHidden()
			|| FVector::DistSquared(It->GetActorLocation(), GetActorLocation()) > FMath::Square(OfferingRadiusCentimeters)) continue;
		if (!AddFish(Fish.FishInstanceId, Fish.ItemId, Fish.WeightKilograms))
		{
			OutError = NSLOCTEXT("Catfishing", "AltarInvalidFish", "供品身份或重量无效，请检查供品");
			return false;
		}
		OutFish.Add(*It);
	}
	for (TActorIterator<ACatFishGuardActor> It(GetWorld()); It; ++It)
	{
		if (!It->IsGrounded() || It->GetAttachParentActor() || It->IsHidden()
			|| FVector::DistSquared(It->GetActorLocation(), GetActorLocation()) > FMath::Square(OfferingRadiusCentimeters)) continue;
		UCatInventoryComponent* Inventory = It->GetFishInventoryComponent();
		if (!Inventory || Inventory->HasPreparedRemoval()) return false;
		TArray<FCatInventoryEntry>& Slots = OutGuards.Add(*It);
		for (int32 Index = 0; Index < Inventory->GetInventorySlotCount(); ++Index)
		{
			const FCatInventoryEntry* Entry = Inventory->GetInventoryEntryAtSlot(Index);
			if (!Entry) return false;
			Slots.Add(*Entry);
			if (!Entry->Instance && Entry->StackCount == 0) continue;
			const UCatFishInventoryItemInstance* Fish = Cast<UCatFishInventoryItemInstance>(Entry->Instance);
			if (!Fish || Entry->StackCount != 1 || !Fish->GetFishDefinition()
				|| !AddFish(Fish->GetItemInstanceId(), Fish->GetFishDefinition()->ItemId, Fish->GetFishWeightKilograms()))
			{
				OutError = NSLOCTEXT("Catfishing", "AltarInvalidGuardFish", "鱼护内供品身份或重量无效");
				return false;
			}
		}
	}
	return true;
}

// 预览发布：服务器 Tick 调用共用筛选器取得临时总数；失败将公开点数清零并标为未就绪，临时散鱼引用和鱼护快照随函数退出释放。
// 就绪标记或点数变化时才写入展示字段、通知本地信息牌、请求复制并记录日志；相同结果直接返回，不触发供品提交。
void ACatAltarActor::RefreshGroundOfferingPreview()
{
	TArray<TWeakObjectPtr<ACatFishPickupActor>> Candidates;
	TMap<TWeakObjectPtr<ACatFishGuardActor>, TArray<FCatInventoryEntry>> Guards;
	FCatOfferingSettlementCommand Command;
	FText Error;
	int32 Points = 0;
	const bool bReady = CollectOffering(Candidates, Guards, Command, Points, Error);
	if (bGroundOfferingReady == bReady && GroundOfferingPoints == (bReady ? Points : 0)) return;
	bGroundOfferingReady = bReady;
	GroundOfferingPoints = bReady ? Points : 0;
	OnRep_InfoChanged();
	ForceNetUpdate();
	UE_LOG(LogCatAltar, Log, TEXT("Event=AltarGroundPreviewChanged World=%s NetMode=%d Authority=1 LocalRole=%d Altar=%s Ready=%d Points=%d"),
		*GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()), *GetName(), bReady, GroundOfferingPoints);
}

// 读取摘要：未知时清输出并返回失败，调用者必须显示暂不可用，不能展示清出的零。
bool ACatAltarActor::TryGetGroundOfferingPoints(int32& OutPoints) const
{
	OutPoints = bGroundOfferingReady ? GroundOfferingPoints : 0;
	return bGroundOfferingReady;
}

// 关系读取：只返回有效的显式营地关联，不在 UI 请求中建立或修改营地关系。
ACatCampHubActor* ACatAltarActor::GetCampHub() const
{
	return IsValid(CampHub) ? CampHub.Get() : nullptr;
}

// 本地通知：服务器显式发布或客户端复制到达时，若信息组件存在则递增其内容序号；无组件直接结束，不重新计算供品或产生网络请求。
void ACatAltarActor::OnRep_InfoChanged()
{
	if (WorldInfo) WorldInfo->NotifyInfoChanged();
}

// 提交前核对冻结的来源、位置、实例和数量：
// 1. 先确认请求仍匹配本祭坛且没有同步提交正在进行。
// 2. 对散鱼逐条复核 Available、无附着、消费资格和仍在半径内。
// 3. 对鱼护复核仍在地面、未隐藏、未被其它事务预留、槽位数量和每格实例/数量完全等于冻结快照。
// 4. 任一差异都让本次确认失效；不把此刻的新鱼替换进冻结名单，也不因点数相同接受另一条实例。
bool ACatAltarActor::ValidateFrozenOffering(AController* Controller, FGuid RequestId, FText& OutError)
{
	OutError = NSLOCTEXT("Catfishing", "AltarOfferingChanged", "供品已变化或暂时无法消费，请重新确认");
	if (!HasAuthority() || RequestId != OfferingRequestId || bOfferingCommitInProgress) return false;
	for (const TWeakObjectPtr<ACatFishPickupActor>& Fish : FrozenFish)
	{
		if (!Fish.IsValid() || Fish->GetPresentationState().State != ECatFishPickupState::Available
			|| Fish->GetAttachParentActor() || !Fish->CanConsumeFromAuthority(Controller)
			|| FVector::DistSquared(Fish->GetActorLocation(), GetActorLocation()) > FMath::Square(OfferingRadiusCentimeters)) return false;
	}
	for (const auto& Pair : FrozenGuards)
	{
		ACatFishGuardActor* Guard = Pair.Key.Get();
		if (!IsValid(Guard) || !Guard->IsGrounded() || Guard->GetAttachParentActor() || Guard->IsHidden()
			|| FVector::DistSquared(Guard->GetActorLocation(), GetActorLocation()) > FMath::Square(OfferingRadiusCentimeters)) return false;
		UCatInventoryComponent* Inventory = Guard->GetFishInventoryComponent();
		if (!Inventory || Inventory->HasPreparedRemoval() || Inventory->GetInventorySlotCount() != Pair.Value.Num()) return false;
		for (int32 Index = 0; Index < Pair.Value.Num(); ++Index)
		{
			const FCatInventoryEntry* Entry = Inventory->GetInventoryEntryAtSlot(Index);
			if (!Entry || Entry->Instance != Pair.Value[Index].Instance || Entry->StackCount != Pair.Value[Index].StackCount) return false;
		}
	}
	OutError = FText::GetEmpty();
	return true;
}

// 整批提交流程：
// 1. 先复核冻结批次并设置重入门，避免结算回调再次验证或清理同一批。
// 2. 逐条准备散鱼消费，再逐个鱼护准备护内鱼整批移除；任何准备失败都跳过结算并释放已取得的预留。
// 3. 所有来源准备成功后同步调用结算回调；只有回调接受才提交库存清空和散鱼销毁。
// 4. 接受后销毁护内鱼保管 Actor、统一广播库存变化并清掉冻结状态；拒绝时数量、实物和鱼护本体都保持原样。
bool ACatAltarActor::ConsumeFrozenOffering(AController* Controller, FGuid RequestId, TFunction<bool()> CommitSettlement)
{
	FText Error;
	if (!ValidateFrozenOffering(Controller, RequestId, Error)) return false;
	TGuardValue<bool> Committing(bOfferingCommitInProgress, true);
	TArray<ACatFishPickupActor*> PreparedFish;
	TArray<UCatInventoryComponent*> PreparedInventories;
	TArray<AActor*> RetainedFishActors;
	bool bPrepared = true;
	for (const auto& Fish : FrozenFish)
	{
		if (!Fish->PrepareConsumptionFromAuthority(Controller, RequestId)) { bPrepared = false; break; }
		PreparedFish.Add(Fish.Get());
	}
	if (bPrepared) for (const auto& Pair : FrozenGuards)
	{
		UCatInventoryComponent* Inventory = Pair.Key->GetFishInventoryComponent();
		TArray<FGuid> Ids;
		for (const FCatInventoryEntry& Entry : Pair.Value) if (Entry.Instance)
		{
			Ids.Add(Entry.Instance->GetItemInstanceId());
			if (AActor* Actor = Entry.Instance->GetWorldActor()) RetainedFishActors.AddUnique(Actor);
		}
		if (!Inventory->PrepareRemovalFromAuthority(RequestId, Ids)) { bPrepared = false; break; }
		PreparedInventories.Add(Inventory);
	}
	const bool bAccepted = bPrepared && (!CommitSettlement || CommitSettlement());
	for (UCatInventoryComponent* Inventory : PreparedInventories) Inventory->FinishRemovalFromAuthority(RequestId, bAccepted, false);
	for (ACatFishPickupActor* Fish : PreparedFish) Fish->FinishConsumptionFromAuthority(Controller, RequestId, bAccepted);
	if (bAccepted)
	{
		for (AActor* Actor : RetainedFishActors) if (IsValid(Actor)) Actor->Destroy();
		for (UCatInventoryComponent* Inventory : PreparedInventories) Inventory->BroadcastInventoryChange();
		FrozenFish.Reset();
		FrozenGuards.Reset();
		OfferingRequestId.Invalidate();
	}
	UE_LOG(LogCatAltar, Log, TEXT("Event=AltarOfferingBatchFinished RequestId=%s Altar=%s Prepared=%d Accepted=%d Fish=%d Guards=%d World=%s NetMode=%d Authority=1 LocalRole=%d"),
		*RequestId.ToString(), *GetName(), bPrepared, bAccepted, PreparedFish.Num(), PreparedInventories.Num(), *GetNameSafe(GetWorld()), GetNetMode(), GetLocalRole());
	return bAccepted;
}

// 非提交期清除本轮候选；同步结算回调不能清空正在配对完成的批次，已取得的散鱼和库存预留由消费入口成对完成。
void ACatAltarActor::ResetOffering()
{
	if (bOfferingCommitInProgress) return;
	FrozenFish.Reset();
	FrozenGuards.Reset();
	OfferingRequestId.Invalidate();
}
