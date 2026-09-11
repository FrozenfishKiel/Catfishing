#include "Camp/CatAltarActor.h"

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

// 实体初始化：网格由关卡指定；雕像阻挡角色和交互射线，确认资格由服务器每 0.2 秒复核。
ACatAltarActor::ACatAltarActor()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.2f;
	StatueMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StatueMesh"));
	SetRootComponent(StatueMesh);
	StatueMesh->SetCollisionProfileName(TEXT("BlockAll"));
}

// 复制注册：只增加展示用计数和错误说明，确认集合与世界鱼引用留在服务器。
void ACatAltarActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, ConfirmedCount);
	DOREPLIFETIME(ThisClass, EligibleCount);
	DOREPLIFETIME(ThisClass, LastError);
}

// 到场复核：客户端不裁决；人数因离开或倒地变化后，剩余全员已确认时仍走同一 GameMode 入口。
void ACatAltarActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
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

// 提示生成：复用现有 E 键提示显示确认进度，动作名称不随房主或个人确认状态改变。
FText ACatAltarActor::GetInteractionPrompt_Implementation() const
{
	return FText::Format(NSLOCTEXT("Catfishing", "AltarConfirmPrompt", "跳过下一天  已确认 {0}/{1}{2}"),
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

// 名单刷新：房间里每名 Active 且非倒地的玩家都计入分母；未到场不会被排除，只撤销其确认。换天和关闭供品窗口时清空。
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
	}
	return EligibleCount > 0 && ConfirmedCount == EligibleCount;
}

// 冻结流程：按服务器根位置筛全部 Available 鱼，重量档和臭鱼沿用 RunSettings；记录同一批弱引用后统一预检，空集合合法。
bool ACatAltarActor::FreezeOffering(AController* Controller, FGuid RequestId, FCatOfferingSettlementCommand& OutCommand, FText& OutError)
{
	if (!HasAuthority() || !RequestId.IsValid() || !FMath::IsFinite(OfferingRadiusCentimeters) || OfferingRadiusCentimeters <= 0.0f
		|| !FMath::IsFinite(AttendanceRadiusCentimeters) || AttendanceRadiusCentimeters <= 0.0f || !RefreshConfirmations()) return false;
	FrozenFish.Reset();
	OfferingRequestId = RequestId;
	OutCommand = FCatOfferingSettlementCommand();
	OutCommand.Context.RequestId = RequestId;
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
		FrozenFish.Add(*It);
	}
	return ValidateFrozenOffering(Controller, RequestId, OutError);
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

// 收口：只释放本祭坛的短期引用和确认，既不销毁未提交的鱼，也不自行更改 Run 阶段。
void ACatAltarActor::ResetOffering(const FText& Error)
{
	FrozenFish.Reset();
	OfferingRequestId.Invalidate();
	ConfirmedPlayers.Reset();
	ConfirmedCount = 0;
	LastError = Error;
	ForceNetUpdate();
}
