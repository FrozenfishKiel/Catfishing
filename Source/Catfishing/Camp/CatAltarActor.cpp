#include "Camp/CatAltarActor.h"
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

// 复制注册：保留父类字段，再登记计数、错误、营地关联及地面预览；接收端由 RepNotify 通知信息组件，确认集合与世界鱼引用留在服务器。
void ACatAltarActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, ConfirmedCount);
	DOREPLIFETIME(ThisClass, EligibleCount);
	DOREPLIFETIME(ThisClass, LastError);
	DOREPLIFETIME(ThisClass, CampHub);
	DOREPLIFETIME(ThisClass, bGroundOfferingReady);
	DOREPLIFETIME(ThisClass, GroundOfferingPoints);
}

// 到场复核：父类 Tick 后仅服务器更新地面预览；取得 Run 且无活动过渡时再刷新确认名单。
// 全员满足时从非空确认集合取请求者并调用正式 GameMode 的翻天入口；客户端、无 Run 或尚未全员确认时不发起结算。
void ACatAltarActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority()) RefreshGroundOfferingPreview();
	const ACatfishingGameState* GameState = GetWorld()->GetGameState<ACatfishingGameState>();
	if (HasAuthority() && GameState && !GameState->GetRunPublicState().DayTransition.bActive && RefreshConfirmations())
	{
		AController* Controller = ConfirmedPlayers.CreateConstIterator()->Get();
		GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>()->BeginAltarDayTransition(this, Controller, FGuid::NewGuid());
	}
}

// 生命周期退出：仅中止自己拥有的过渡；随后丢弃非拥有的弱引用，由父类完成 Actor 销毁。
void ACatAltarActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ACatfishingGameModeBase* GameMode = GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>())
	{
		GameMode->CancelAltarDayTransition(this, NSLOCTEXT("Catfishing", "AltarUnavailable", "祭坛已不可用"));
	}
	FrozenFish.Reset();
	ConfirmedPlayers.Reset();
	Super::EndPlay(EndPlayReason);
}

// 交互资格：读取同一 Run 阶段与当前 Pawn 身体，服务器额外核对 Active 身份；八米只控制到场，不放宽 E 键距离。
bool ACatAltarActor::CanInteract_Implementation(AController* Controller) const
{
	const ACatfishingGameState* GameState = GetWorld()->GetGameState<ACatfishingGameState>();
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	const UCatConditionComponent* Condition = Pawn ? Pawn->FindComponentByClass<UCatConditionComponent>() : nullptr;
	if (!GameState || !Pawn || !Condition || Condition->GetSnapshot().bDowned
		|| GameState->GetRunPublicState().Phase.Phase != ECatRunPhase::NormalNight
		|| !GameState->GetRunPublicState().Phase.bOfferingOpen || GameState->GetRunPublicState().DayTransition.bActive
		|| FVector::DistSquared(Pawn->GetActorLocation(), GetActorLocation()) > FMath::Square(AttendanceRadiusCentimeters)
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

// 提示生成：读取当前确认计数，拼接固定动作名；仅在 LastError 非空时追加换行错误说明，返回文本而不改变确认状态。
FText ACatAltarActor::GetInteractionPrompt_Implementation() const
{
	return FText::Format(NSLOCTEXT("Catfishing", "AltarConfirmPrompt", "献给圣猫  已确认 {0}/{1}{2}"),
		FText::AsNumber(ConfirmedCount), FText::AsNumber(EligibleCount),
		LastError.IsEmpty() ? FText::GetEmpty() : FText::Format(NSLOCTEXT("Catfishing", "AltarErrorSuffix", "\n{0}"), LastError));
}

// 距离读取：使用项目既有 E 键上限，所有端采用相同厘米单位。
double ACatAltarActor::GetInteractionRadius_Implementation() const
{
	return GetDefault<UCatInteractionSettings>()->MaximumServerInteractionDistanceCentimeters;
}

// 确认流程：客户端经 Controller RPC 转发；服务器先剔除失效记录再按玩家去重，只有全员满足才开始冻结供品。
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
			*GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()), *GetName(), *GetNameSafe(Player), *RequestId.ToString());
		Player->ServerRequestInteraction(this, RequestId);
		return true;
	}
	RefreshConfirmations();
	const bool bDuplicate = ConfirmedPlayers.Contains(Controller);
	ConfirmedPlayers.Add(Controller);
	LastError = FText::GetEmpty();
	const bool bAllConfirmed = RefreshConfirmations();
	UE_LOG(LogCatAltar, Display, TEXT("Event=AltarConfirmed World=%s NetMode=%d Authority=1 LocalRole=%d Altar=%s Player=%s RequestId=%s Confirmed=%d Eligible=%d Duplicate=%d"),
		*GetWorld()->GetName(), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()), *GetName(), *GetNameSafe(Controller),
		*RequestId.ToString(), ConfirmedCount, EligibleCount, bDuplicate);
	ForceNetUpdate();
	return !bAllConfirmed || GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>()->BeginAltarDayTransition(this, Controller, RequestId);
}

// 名单刷新：
// 1. 无正式 GameMode 则返回 false；读取 Run 后，换天或供品窗口关闭且尚有确认/错误时重置本轮记录。
// 2. 开放夜晚将每名 Active 且非倒地玩家计入分母；具备 Pawn、状态组件且在到场范围内者才进入在场集合。
// 3. 剔除已不在场的确认并更新计数；人数变化才记录日志、请求复制并通知本地信息牌，最后返回非空且全员确认的结果。
bool ACatAltarActor::RefreshConfirmations()
{
	ACatfishingGameModeBase* GameMode = GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>();
	if (!GameMode) return false;
	const FCatRunPublicState& Run = GameMode->GetRunPublicState();
	const int32 PreviousConfirmed = ConfirmedCount;
	const int32 PreviousEligible = EligibleCount;
	const bool bOfferingNight = Run.Phase.Phase == ECatRunPhase::NormalNight && Run.Phase.bOfferingOpen;
	if (ConfirmationDay != Run.Phase.DayIndex || (!bOfferingNight && (ConfirmedCount > 0 || !LastError.IsEmpty())))
	{
		ResetOffering();
		ConfirmationDay = Run.Phase.DayIndex;
	}
	TSet<TWeakObjectPtr<AController>> PresentPlayers;
	EligibleCount = 0;
	if (bOfferingNight)
	{
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Player = It->Get();
			APawn* Pawn = Player ? Player->GetPawn() : nullptr;
			const UCatConditionComponent* Condition = Pawn ? Pawn->FindComponentByClass<UCatConditionComponent>() : nullptr;
			if (!GameMode->IsControllerActive(Player) || (Condition && Condition->GetSnapshot().bDowned)) continue;
			++EligibleCount;
			if (Pawn && Condition && FVector::DistSquared(Pawn->GetActorLocation(), GetActorLocation()) <= FMath::Square(AttendanceRadiusCentimeters))
			{
				PresentPlayers.Add(Player);
			}
		}
	}
	for (auto It = ConfirmedPlayers.CreateIterator(); It; ++It)
	{
		if (!PresentPlayers.Contains(*It)) It.RemoveCurrent();
	}
	ConfirmedCount = ConfirmedPlayers.Num();
	if (PreviousConfirmed != ConfirmedCount || PreviousEligible != EligibleCount)
	{
		UE_LOG(LogCatAltar, Display, TEXT("Event=AltarAttendanceChanged World=%s NetMode=%d Authority=1 LocalRole=%d Altar=%s Day=%d Confirmed=%d Eligible=%d"),
			*GetWorld()->GetName(), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()), *GetName(), ConfirmationDay, ConfirmedCount, EligibleCount);
		ForceNetUpdate();
		OnRep_InfoChanged();
	}
	return EligibleCount > 0 && ConfirmedCount == EligibleCount;
}

// 冻结流程：先核对服务器权限、请求标识、有限正半径及全员确认，再用共用筛选器填充冻结鱼引用和分类命令。
// 收集成功才写入祭坛与命令的请求标识，随后预检同一批鱼；失败输出不可提交，已写的短期引用由调用方 ResetOffering 收口，空集合合法。
bool ACatAltarActor::FreezeOffering(AController* Controller, FGuid RequestId, FCatOfferingSettlementCommand& OutCommand, FText& OutError)
{
	if (!HasAuthority() || !RequestId.IsValid() || !FMath::IsFinite(OfferingRadiusCentimeters) || OfferingRadiusCentimeters <= 0.0f
		|| !FMath::IsFinite(AttendanceRadiusCentimeters) || AttendanceRadiusCentimeters <= 0.0f || !RefreshConfirmations()) return false;
	int32 PreviewPoints = 0;
	if (!CollectOffering(FrozenFish, OutCommand, PreviewPoints, OutError)) return false;
	OfferingRequestId = RequestId;
	OutCommand.Context.RequestId = RequestId;
	return ValidateFrozenOffering(Controller, RequestId, OutError);
}

// 共用筛选：
// 1. 清空所有输出；无服务器权限、World 或有限正半径时返回 false。
// 2. 扫描本世界鱼，保留 Available、无父附着且根位置在厘米半径内的对象；按真实千克重量分类，并单独累计臭鱼数量。
// 3. 分类失败、负点数或累计将溢出时拒绝整批；输出可能保留此前已累计的部分，调用方必须以返回值为准，只有分类失败设置具体错误文本。
// 4. 将有效点数和弱引用写入输出；空集合返回 true，本方法不冻结鱼的可交互状态、不消费实物。
bool ACatAltarActor::CollectOffering(TArray<TWeakObjectPtr<ACatFishPickupActor>>& OutFish,
	FCatOfferingSettlementCommand& OutCommand, int32& OutPoints, FText& OutError) const
{
	OutFish.Reset();
	OutCommand = FCatOfferingSettlementCommand();
	OutPoints = 0;
	OutError = FText::GetEmpty();
	if (!HasAuthority() || !GetWorld() || !FMath::IsFinite(OfferingRadiusCentimeters) || OfferingRadiusCentimeters <= 0.0f) return false;
	const UCatRunSettings* Settings = GetDefault<UCatRunSettings>();
	for (TActorIterator<ACatFishPickupActor> It(GetWorld()); It; ++It)
	{
		const FCatFishPickupPresentationState& Fish = It->GetPresentationState();
		if (Fish.State != ECatFishPickupState::Available || It->GetAttachParentActor()
			|| FVector::DistSquared(It->GetActorLocation(), GetActorLocation()) > FMath::Square(OfferingRadiusCentimeters)) continue;
		ECatOfferingWeightClass WeightClass;
		int32 Points = 0;
		if (!Settings->TryClassifyOfferingWeight(Fish.WeightKilograms, WeightClass, Points))
		{
			OutError = NSLOCTEXT("Catfishing", "AltarInvalidWeight", "供品重量或重量档配置无效");
			return false;
		}
		switch (WeightClass)
		{
		case ECatOfferingWeightClass::Small: ++OutCommand.SmallFishCount; break;
		case ECatOfferingWeightClass::Medium: ++OutCommand.MediumFishCount; break;
		case ECatOfferingWeightClass::Large: ++OutCommand.LargeFishCount; break;
		case ECatOfferingWeightClass::Giant: ++OutCommand.GiantFishCount; break;
		}
		if (Settings->IsStinkyOfferingFish(Fish.FishDefinitionId)) ++OutCommand.StinkyFishCount;
		if (Points < 0 || OutPoints > MAX_int32 - Points) return false;
		OutPoints += Points;
		OutFish.Add(*It);
	}
	return true;
}

// 预览发布：服务器 Tick 调用共用筛选器取得临时总数；失败将公开点数清零并标为未就绪，临时鱼引用随函数退出释放。
// 就绪标记或点数变化时才写入展示字段、通知本地信息牌、请求复制并记录日志；相同结果直接返回，不触发供品提交。
void ACatAltarActor::RefreshGroundOfferingPreview()
{
	TArray<TWeakObjectPtr<ACatFishPickupActor>> Candidates;
	FCatOfferingSettlementCommand Command;
	FText Error;
	int32 Points = 0;
	const bool bReady = CollectOffering(Candidates, Command, Points, Error);
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

// 提交前整批预检：重新核对全员资格，再校验同一批地面鱼与消费依赖；新入场或恢复后未确认的玩家会阻止提交，不重新选鱼。
bool ACatAltarActor::ValidateFrozenOffering(AController* Controller, FGuid RequestId, FText& OutError)
{
	if (!HasAuthority() || RequestId != OfferingRequestId) return false;
	if (!RefreshConfirmations() || !ConfirmedPlayers.Contains(Controller))
	{
		OutError = NSLOCTEXT("Catfishing", "AltarParticipantsChanged", "参与玩家状态已变化，请重新确认");
		return false;
	}
	for (const TWeakObjectPtr<ACatFishPickupActor>& Fish : FrozenFish)
	{
		if (!Fish.IsValid() || Fish->GetPresentationState().State != ECatFishPickupState::Available
			|| Fish->GetAttachParentActor() || !Fish->CanConsumeFromAuthority(Controller))
		{
			OutError = NSLOCTEXT("Catfishing", "AltarOfferingChanged", "供品已变化或暂时无法消费，请重新确认");
			return false;
		}
	}
	return true;
}

// 实物消费：调用既有单鱼入口，不操作库存；上层必须在同一游戏线程先通过整批预检和 GAS 提交。
bool ACatAltarActor::ConsumeFrozenOffering(AController* Controller, FGuid RequestId)
{
	if (!HasAuthority() || RequestId != OfferingRequestId) return false;
	for (const TWeakObjectPtr<ACatFishPickupActor>& Fish : FrozenFish)
	{
		if (!Fish.IsValid() || !Fish->ConsumeFromAuthority(Controller, RequestId))
		{
			UE_LOG(LogCatAltar, Error, TEXT("Event=AltarConsumeFailed World=%s NetMode=%d Authority=1 LocalRole=%d Altar=%s RequestId=%s Fish=%s"),
				*GetWorld()->GetName(), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()), *GetName(), *RequestId.ToString(), *GetNameSafe(Fish.Get()));
			return false;
		}
	}
	FrozenFish.Reset();
	return true;
}

// 收口：先释放冻结鱼引用、请求标识和确认集合，再把确认数清零并保存传入错误，最后通知本地信息牌并请求复制。
// 不销毁未提交的鱼、不改变 Run 阶段或当前应参与人数；人数由下一次名单复核更新。
void ACatAltarActor::ResetOffering(const FText& Error)
{
	FrozenFish.Reset();
	OfferingRequestId.Invalidate();
	ConfirmedPlayers.Reset();
	ConfirmedCount = 0;
	LastError = Error;
	OnRep_InfoChanged();
	ForceNetUpdate();
}
