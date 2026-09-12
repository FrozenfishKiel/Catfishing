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

// 复制注册：保留父类字段，再登记应到人数、倒计时三项、错误、营地关联及地面预览；接收端由 RepNotify 通知信息组件，
// 到场集合、发起人和请求标识都留在服务器——客户端不重建「谁同意了」这种事实，也不自行推进或撤销倒计时。
void ACatAltarActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, EligibleCount);
	DOREPLIFETIME(ThisClass, bCountdownActive);
	DOREPLIFETIME(ThisClass, CountdownDeadlineServerTimeSeconds);
	DOREPLIFETIME(ThisClass, CountdownTotalSeconds);
	DOREPLIFETIME(ThisClass, LastError);
	DOREPLIFETIME(ThisClass, CampHub);
	DOREPLIFETIME(ThisClass, bGroundOfferingReady);
	DOREPLIFETIME(ThisClass, GroundOfferingPoints);
}

// 到场复核：父类 Tick 后仅服务器更新地面预览；取得 Run 且无活动过渡时再刷新到场名单。
// 倒计时里只要还有合格的人不在到场圈内就中断；走满截止时间才请求翻天——发起人仍在圈内由他提交，
// 否则改由任一在场者提交，因为到场本身就是同意，不需要再认一次「是谁按的」。
void ACatAltarActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority()) return;
	RefreshGroundOfferingPreview();
	const ACatfishingGameState* GameState = GetWorld()->GetGameState<ACatfishingGameState>();
	if (!GameState || GameState->GetRunPublicState().DayTransition.bActive) return;
	const bool bEveryoneAttending = RefreshAttendance();
	if (!bCountdownActive) return;
	if (!bEveryoneAttending)
	{
		CancelCountdown(NSLOCTEXT("Catfishing", "AltarAttendanceBroken", "有猫走出到场范围，献祭中断"));
		return;
	}
	if (GetWorld()->GetTimeSeconds() < CountdownDeadlineServerTimeSeconds) return;
	AController* Committer = CountdownInitiator.IsValid() && PresentPlayers.Contains(CountdownInitiator)
		? CountdownInitiator.Get() : nullptr;
	for (const TWeakObjectPtr<AController>& Present : PresentPlayers)
	{
		if (Committer) break;
		Committer = Present.Get();
	}
	ACatfishingGameModeBase* GameMode = GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>();
	if (!Committer || !GameMode)
	{
		CancelCountdown(NSLOCTEXT("Catfishing", "AltarCommitterUnavailable", "献祭发起失败，请重新与石像互动"));
		return;
	}
	// 先收掉倒计时字段再进 GameMode：过渡开始后倒计时条不该继续显示，失败时也不会在下一帧拿同一个截止时间重试。
	const FGuid RequestId = CountdownRequestId;
	CancelCountdown(FText::GetEmpty());
	GameMode->BeginAltarDayTransition(this, Committer, RequestId);
}

// 生命周期退出：仅中止自己拥有的过渡；随后丢弃非拥有的弱引用，由父类完成 Actor 销毁。
void ACatAltarActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ACatfishingGameModeBase* GameMode = GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>())
	{
		GameMode->CancelAltarDayTransition(this, NSLOCTEXT("Catfishing", "AltarUnavailable", "祭坛已不可用"));
	}
	FrozenFish.Reset();
	PresentPlayers.Reset();
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

// 提示生成：倒计时期间显示现算的剩余秒数，其余时候只给固定动作名；仅在 LastError 非空时追加换行说明。
// 这里不再拼「已确认 N/M」——到场判定本身就是同意，代码不维护第二份确认计数（2026-09-11 裁决⑥）。
FText ACatAltarActor::GetInteractionPrompt_Implementation() const
{
	const FText Suffix = LastError.IsEmpty()
		? FText::GetEmpty() : FText::Format(NSLOCTEXT("Catfishing", "AltarErrorSuffix", "\n{0}"), LastError);
	double RemainingSeconds = 0.0;
	double TotalSeconds = 0.0;
	if (TryGetOfferingCountdown(RemainingSeconds, TotalSeconds))
	{
		FNumberFormattingOptions Options;
		Options.MinimumFractionalDigits = 1;
		Options.MaximumFractionalDigits = 1;
		return FText::Format(NSLOCTEXT("Catfishing", "AltarCountdownPrompt", "献给圣猫  倒计时 {0} 秒{1}"),
			FText::AsNumber(RemainingSeconds, &Options), Suffix);
	}
	return FText::Format(NSLOCTEXT("Catfishing", "AltarOfferPrompt", "献给圣猫{0}"), Suffix);
}

// 距离读取：使用项目既有 E 键上限，所有端采用相同厘米单位。
double ACatAltarActor::GetInteractionRadius_Implementation() const
{
	return GetDefault<UCatInteractionSettings>()->MaximumServerInteractionDistanceCentimeters;
}

// 发起流程：客户端经 Controller RPC 转发；服务器先复核全员是否都在到场圈内，再由这一个人开启唯一倒计时。
// 倒计时已经在走时重复按下只是受理、不重开也不缩短；不再按人去重，也不再累计确认数（2026-09-11 裁决⑥）。
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
		UE_LOG(LogCatAltar, Log, TEXT("Event=AltarCountdownRequested World=%s NetMode=%d Authority=0 LocalRole=%d Altar=%s Player=%s RequestId=%s"),
			*GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()), *GetName(), *GetNameSafe(Player), *RequestId.ToString());
		Player->ServerRequestInteraction(this, RequestId);
		return true;
	}
	if (bCountdownActive)
	{
		return true;
	}
	const bool bEveryoneAttending = RefreshAttendance();
	const bool bCountdownConfigured = FMath::IsFinite(OfferingCountdownSeconds) && OfferingCountdownSeconds > 0.0f;
	if (!bEveryoneAttending || !bCountdownConfigured)
	{
		LastError = bEveryoneAttending
			? NSLOCTEXT("Catfishing", "AltarCountdownUnconfigured", "献祭倒计时未配置")
			: NSLOCTEXT("Catfishing", "AltarAttendanceIncomplete", "还有猫没到祭坛");
		UE_LOG(LogCatAltar, Log, TEXT("Event=AltarCountdownRejected World=%s NetMode=%d Authority=1 LocalRole=%d Altar=%s Player=%s RequestId=%s Present=%d Eligible=%d Configured=%d"),
			*GetWorld()->GetName(), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()), *GetName(), *GetNameSafe(Controller),
			*RequestId.ToString(), PresentPlayers.Num(), EligibleCount, bCountdownConfigured);
		OnRep_InfoChanged();
		ForceNetUpdate();
		return false;
	}
	bCountdownActive = true;
	CountdownTotalSeconds = OfferingCountdownSeconds;
	CountdownDeadlineServerTimeSeconds = GetWorld()->GetTimeSeconds() + CountdownTotalSeconds;
	CountdownInitiator = Controller;
	CountdownRequestId = RequestId;
	LastError = FText::GetEmpty();
	UE_LOG(LogCatAltar, Display, TEXT("Event=AltarCountdownStarted World=%s NetMode=%d Authority=1 LocalRole=%d Altar=%s Player=%s RequestId=%s Eligible=%d CountdownSeconds=%.3f"),
		*GetWorld()->GetName(), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()), *GetName(), *GetNameSafe(Controller),
		*RequestId.ToString(), EligibleCount, CountdownTotalSeconds);
	OnRep_InfoChanged();
	ForceNetUpdate();
	return true;
}

// 名单刷新：
// 1. 无正式 GameMode 则返回 false；读取 Run 后，换天或供品窗口关闭且仍有倒计时/错误时重置本轮记录。
// 2. 开放夜晚将每名 Active 且非倒地玩家计入分母；具备 Pawn、状态组件且在到场范围内者才进入在场集合。
// 3. 在场或分母变化才记录日志、请求复制并通知本地信息牌，最后返回分母非空且全员都在圈内的结果。
bool ACatAltarActor::RefreshAttendance()
{
	ACatfishingGameModeBase* GameMode = GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>();
	if (!GameMode) return false;
	const FCatRunPublicState& Run = GameMode->GetRunPublicState();
	const int32 PreviousPresent = PresentPlayers.Num();
	const int32 PreviousEligible = EligibleCount;
	const bool bOfferingNight = Run.Phase.Phase == ECatRunPhase::NormalNight && Run.Phase.bOfferingOpen;
	if (OfferingDay != Run.Phase.DayIndex || (!bOfferingNight && (bCountdownActive || !LastError.IsEmpty())))
	{
		ResetOffering();
		OfferingDay = Run.Phase.DayIndex;
	}
	PresentPlayers.Reset();
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
	if (PreviousPresent != PresentPlayers.Num() || PreviousEligible != EligibleCount)
	{
		UE_LOG(LogCatAltar, Display, TEXT("Event=AltarAttendanceChanged World=%s NetMode=%d Authority=1 LocalRole=%d Altar=%s Day=%d Present=%d Eligible=%d Countdown=%d"),
			*GetWorld()->GetName(), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()), *GetName(), OfferingDay,
			PresentPlayers.Num(), EligibleCount, bCountdownActive);
		ForceNetUpdate();
		OnRep_InfoChanged();
	}
	return EligibleCount > 0 && PresentPlayers.Num() == EligibleCount;
}

// 倒计时收口：只清倒计时四个字段并保留传入说明；不动冻结鱼、不改 Run 阶段，也不改当前应到人数。
// 提交路径传空说明，中断路径传可展示原因；两条路径都必须经过这里，避免信息牌上留着一条走不完的进度条。
void ACatAltarActor::CancelCountdown(const FText& Error)
{
	const bool bWasActive = bCountdownActive;
	bCountdownActive = false;
	CountdownDeadlineServerTimeSeconds = 0.0;
	CountdownTotalSeconds = 0.0;
	CountdownInitiator.Reset();
	CountdownRequestId.Invalidate();
	LastError = Error;
	if (bWasActive)
	{
		UE_LOG(LogCatAltar, Display, TEXT("Event=AltarCountdownEnded World=%s NetMode=%d Authority=%d LocalRole=%d Altar=%s Present=%d Eligible=%d Reason=%s"),
			*GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), HasAuthority() ? 1 : 0, static_cast<int32>(GetLocalRole()),
			*GetName(), PresentPlayers.Num(), EligibleCount, Error.IsEmpty() ? TEXT("Committed") : *Error.ToString());
	}
	OnRep_InfoChanged();
	ForceNetUpdate();
}

// 冻结流程：先核对服务器权限、请求标识、有限正半径及全员仍在到场圈内，再用共用筛选器填充冻结鱼引用和分类命令。
// 收集成功才写入祭坛与命令的请求标识，随后预检同一批鱼；失败输出不可提交，已写的短期引用由调用方 ResetOffering 收口，空集合合法。
bool ACatAltarActor::FreezeOffering(AController* Controller, FGuid RequestId, FCatOfferingSettlementCommand& OutCommand, FText& OutError)
{
	if (!HasAuthority() || !RequestId.IsValid() || !FMath::IsFinite(OfferingRadiusCentimeters) || OfferingRadiusCentimeters <= 0.0f
		|| !FMath::IsFinite(AttendanceRadiusCentimeters) || AttendanceRadiusCentimeters <= 0.0f || !RefreshAttendance()) return false;
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

// 倒计时读取：不在倒计时、总秒数无效或客户端拿不到 GameState 时返回 false 并清零，调用方必须显示暂不可用。
// 剩余秒数从复制的服务器截止时间现算——服务器读自己的世界时钟，客户端读 GameState 的服务器时钟，两端不各存一份倒计时。
bool ACatAltarActor::TryGetOfferingCountdown(double& OutRemainingSeconds, double& OutTotalSeconds) const
{
	OutRemainingSeconds = 0.0;
	OutTotalSeconds = 0.0;
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (!bCountdownActive || !FMath::IsFinite(CountdownDeadlineServerTimeSeconds)
		|| !FMath::IsFinite(CountdownTotalSeconds) || CountdownTotalSeconds <= 0.0
		|| (!HasAuthority() && !GameState)) return false;
	const double ServerNowSeconds = HasAuthority() ? GetWorld()->GetTimeSeconds() : GameState->GetServerWorldTimeSeconds();
	OutRemainingSeconds = FMath::Clamp(CountdownDeadlineServerTimeSeconds - ServerNowSeconds, 0.0, CountdownTotalSeconds);
	OutTotalSeconds = CountdownTotalSeconds;
	return true;
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

// 提交前整批预检：重新核对全员仍在到场圈内且提交者本人也在圈内，再校验同一批地面鱼与消费依赖；有人走出圈就阻止提交，不重新选鱼。
// 名单复核自己可能因为换天而清掉本轮供品，所以复核之后必须再确认一次请求标识仍然是这一批——
// 否则冻结鱼数组已被清空，下面的循环会对空集合返回真，把「没交齐」伪装成一次合法提交。
bool ACatAltarActor::ValidateFrozenOffering(AController* Controller, FGuid RequestId, FText& OutError)
{
	if (!HasAuthority() || RequestId != OfferingRequestId) return false;
	if (!RefreshAttendance() || RequestId != OfferingRequestId || !PresentPlayers.Contains(Controller))
	{
		OutError = NSLOCTEXT("Catfishing", "AltarAttendanceChanged", "到场玩家已变化，请重新与石像互动");
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

// 收口：先释放冻结鱼引用与请求标识，再走统一的倒计时收口保存传入说明并通知本地信息牌。
// 不销毁未提交的鱼、不改变 Run 阶段或当前应到人数；人数由下一次名单复核更新。
void ACatAltarActor::ResetOffering(const FText& Error)
{
	FrozenFish.Reset();
	OfferingRequestId.Invalidate();
	CancelCountdown(Error);
}
