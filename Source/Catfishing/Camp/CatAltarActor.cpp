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
// 不影响另一座祭坛的确认，最后丢弃冻结鱼弱引用并交由父类销毁 Actor，未提交鱼不会被消费。
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
	FrozenFish.Reset();
	Super::EndPlay(EndPlayReason);
}

// 交互资格：读取同一 Run 阶段与当前 Pawn 身体，服务器额外核对 Active 身份；确认等待中只允许 F8/F9 远程输入，不能再发起另一轮现场交互。
bool ACatAltarActor::CanInteract_Implementation(AController* Controller) const
{
	const ACatfishingGameState* GameState = GetWorld()->GetGameState<ACatfishingGameState>();
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	const UCatConditionComponent* Condition = Pawn ? Pawn->FindComponentByClass<UCatConditionComponent>() : nullptr;
	if (!GameState || !Pawn || !Condition || Condition->GetSnapshot().bDowned
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
	// 分类函数在处理任意正重量前都会验证全部档位；这里用一千克只触发配置校验，不读取地面鱼也不预定点数。
	if (!GameState || !Settings->TryGetDayParameters(GameState->GetRunPublicState().Phase.DayIndex, DayLengthSeconds, Tuning)
		|| !Settings->TryClassifyOfferingWeight(1.0, WeightClass, OfferingPoints))
	{
		OutError = NSLOCTEXT("Catfishing", "AltarOfferingConfigurationInvalid", "祭坛供品配置无效");
		return false;
	}
	return true;
}

// 冻结流程：先核对服务器权限、请求标识和供品配置，再用共用筛选器填充冻结鱼引用和分类命令。
// 收集成功才写入祭坛与命令的请求标识，随后预检同一批鱼；失败输出不可提交，已写的短期引用由调用方 ResetOffering 收口，空集合合法。
bool ACatAltarActor::FreezeOffering(AController* Controller, FGuid RequestId, FCatOfferingSettlementCommand& OutCommand, FText& OutError)
{
	if (!RequestId.IsValid() || !CanPrepareOffering(OutError)) return false;
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

// 提交前整批预检：只校验正式过场冻结的同一批地面鱼与消费依赖；全员同意已由 GameMode 在进入过场前裁决，不在这里重复检查。
bool ACatAltarActor::ValidateFrozenOffering(AController* Controller, FGuid RequestId, FText& OutError)
{
	if (!HasAuthority() || RequestId != OfferingRequestId) return false;
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
				*GetWorld()->GetName(), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()), *GetName(), *RequestId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(Fish.Get()));
			return false;
		}
	}
	FrozenFish.Reset();
	return true;
}

// 收口流程：只释放冻结鱼引用与关联标识，不销毁未提交的鱼，也不改写由 GameMode 持有的确认结果。
void ACatAltarActor::ResetOffering()
{
	FrozenFish.Reset();
	OfferingRequestId.Invalidate();
}
