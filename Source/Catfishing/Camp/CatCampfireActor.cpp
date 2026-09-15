#include "Camp/CatCampfireActor.h"

#include "Character/CatCharacter.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Condition/CatConditionComponent.h"
#include "Engine/World.h"
#include "Framework/Game/CatfishingGameState.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"
#include "Logging/CatLog.h"
#include "Net/UnrealNetwork.h"

// 构造流程：只建立布局根、可选外观与复制开关；不注册 Run 写口，不启用任何会阻塞流程的计时。
// 低频 Tick 只做一件事：把走远或离局的人从座位上摘掉，避免留下一把没人的椅子。
ACatCampfireActor::ACatCampfireActor()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;
	CampfireRoot = CreateDefaultSubobject<USceneComponent>(TEXT("CampfireRoot"));
	SetRootComponent(CampfireRoot);
	CampfireMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CampfireMesh"));
	CampfireMesh->SetupAttachment(CampfireRoot);
	CampfireMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
}

// 复制登记流程：只复制落座名单。火是否点亮是 Run 公开阶段的函数，两端各自现算即可，复制它只会多出一份可能对不上的真相。
void ACatCampfireActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, SeatedPlayers);
}

// 复核流程：服务器低频检查每个落座者是否还在局里、还在范围内、没有倒地；任一不成立就让它起身。
// 火熄了（天亮了）也一并清空座位——白天没有火可坐。本流程不阻塞任何人，也不写 Run。
void ACatCampfireActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
	{
		RefreshCampfirePresentation();
		return;
	}
	const bool bLit = IsCampfireLit();
	const int32 PreviousSeatCount = SeatedPlayers.Num();
	if (!bLit)
	{
		SeatedPlayers.Reset();
	}
	else
	{
		SeatedPlayers.RemoveAll([this](const TObjectPtr<APlayerState>& SeatedPlayer)
		{
			const APlayerState* PlayerState = SeatedPlayer.Get();
			AController* Controller = PlayerState ? PlayerState->GetOwningController() : nullptr;
			return !ResolveCharacterNearCampfire(Controller);
		});
	}
	if (SeatedPlayers.Num() != PreviousSeatCount)
	{
		ForceNetUpdate();
	}
	RefreshCampfirePresentation();
}

// 收口流程：清空座位并刷一次表现；表现层据复制值自行收起动作，不需要额外的结束握手。
void ACatCampfireActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		SeatedPlayers.Reset();
	}
	SeatTerminalCache.Reset();
	Super::EndPlay(EndPlayReason);
}

// 交互资格流程：火点着、请求者够得着、且没有倒地。
// 刻意不检查「全体是否在场」「是否结算夜」「是否交过供品」——篝火不是流程里的一环，
// 加任何这类条件都会把它变回已经被删掉的「回看仪式」。
bool ACatCampfireActor::CanInteract_Implementation(AController* RequestingController) const
{
	if (!IsCampfireLit())
	{
		return false;
	}
	const ACatCharacter* Character = ResolveCharacterNearCampfire(RequestingController);
	if (!Character)
	{
		return false;
	}
	const UCatConditionComponent* Condition = Character->GetConditionComponent();
	return !Condition || !Condition->GetSnapshot().bDowned;
}

// 提示文案流程：固定给动作名「坐下」（交互册「显示提示：【F】坐下」）。
// 按键图标由交互提示 UI 自己从 IMC 解析，这里不写死键名；接口没有观察者参数，所以也不按人区分坐/起——
// 同一个键再按一次就是起身，那一步由 Interact 自己翻转，不需要提示文案先知道。
FText ACatCampfireActor::GetInteractionPrompt_Implementation() const
{
	return NSLOCTEXT("CatCampfire", "SitDown", "坐下");
}

// 交互半径流程：返回本实例配置值；它只表示「够得着火堆」，与祭坛的到场圈没有任何关系。
double ACatCampfireActor::GetInteractionRadius_Implementation() const
{
	return InteractionRadiusCentimeters;
}

// 落座流程：
// 1. 客户端只转发意图，服务器复核身份、范围与点火状态；同一 RequestId 重放只返回首次结果，不会坐下又站起。
// 2. 已坐下的人再按一次就是起身——一个键管两件事，符合交互册「想去就去、想走就走」。
// 3. 全程不写 Run、不发 GAS、不产生 ready，也不给任何数值收益：坐火边是纯粹的松弛表现。
bool ACatCampfireActor::Interact_Implementation(AController* RequestingController, const FGuid RequestId)
{
	if (!RequestingController)
	{
		return false;
	}
	if (!HasAuthority())
	{
		// 与祭坛同构：本地只转发，服务器会在同一函数里重新复核范围与阶段。
		ACatfishingPlayerController* Player = Cast<ACatfishingPlayerController>(RequestingController);
		if (!Player || !Player->IsLocalController())
		{
			return false;
		}
		Player->ServerRequestInteraction(this, RequestId);
		return true;
	}
	APlayerState* PlayerState = RequestingController->PlayerState;
	if (!PlayerState || !RequestId.IsValid())
	{
		return false;
	}
	const FString TerminalKey = FString::Printf(TEXT("%d|%s"), PlayerState->GetPlayerId(),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	if (const bool* Cached = SeatTerminalCache.Find(TerminalKey))
	{
		return *Cached;
	}
	const bool bWasSeated = IsPlayerSeated(PlayerState);
	bool bResult = false;
	if (bWasSeated)
	{
		SeatedPlayers.RemoveAll([PlayerState](const TObjectPtr<APlayerState>& Seated) { return Seated.Get() == PlayerState; });
		bResult = true;
	}
	else if (CanInteract_Implementation(RequestingController))
	{
		SeatedPlayers.AddUnique(PlayerState);
		bResult = true;
	}
	SeatTerminalCache.Add(TerminalKey, bResult);
	if (bResult)
	{
		ForceNetUpdate();
		RefreshCampfirePresentation();
		UE_LOG(LogCatfishing, Log,
			TEXT("Event=campfire_seat_changed PlayerId=%d Seated=%s SeatCount=%d"),
			PlayerState->GetPlayerId(), bWasSeated ? TEXT("false") : TEXT("true"), SeatedPlayers.Num());
	}
	return bResult;
}

// 点火判定流程：入夜即点亮、整夜都在。
// 三种夜晚里只排除一种——世界进度归零的团灭，营地册 §3.1.5 明写「到 0 立即结束，不进结算夜，不再生火」；
// 正式 RunFlow 归零直接进 Ending，所以这里同时排除 Ending/Ended 里带着归零原因的那一支。
// 白天不点：设计给篝火的是「一局的治愈收尾」，白天点着它就没有收尾可言了。
bool ACatCampfireActor::IsCampfireLit() const
{
	const ACatfishingGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
	if (!GameState)
	{
		return false;
	}
	const FCatRunPublicState& Run = GameState->GetRunPublicState();
	if (Run.EndReason == ECatRunEndReason::WorldProgressDepleted)
	{
		return false;
	}
	return Run.Phase.Phase == ECatRunPhase::NormalNight
		|| Run.Phase.Phase == ECatRunPhase::SuccessSettlementNight
		|| Run.Phase.Phase == ECatRunPhase::FailureSettlementNight;
}

// 落座名单读取流程：拷贝一份裸指针数组给蓝图（UFUNCTION 不能返回 TObjectPtr）；顺序即落座先后。
TArray<APlayerState*> ACatCampfireActor::GetSeatedPlayers() const
{
	TArray<APlayerState*> SeatedPlayerStates;
	SeatedPlayerStates.Reserve(SeatedPlayers.Num());
	for (const TObjectPtr<APlayerState>& Seated : SeatedPlayers)
	{
		if (APlayerState* SeatedPlayerState = Seated.Get())
		{
			SeatedPlayerStates.Add(SeatedPlayerState);
		}
	}
	return SeatedPlayerStates;
}

// 落座查询流程：只比较 PlayerState 身份；名单里的失效项由服务器 Tick 清理，这里不做隐式修剪。
bool ACatCampfireActor::IsPlayerSeated(const APlayerState* PlayerState) const
{
	return PlayerState && SeatedPlayers.ContainsByPredicate(
		[PlayerState](const TObjectPtr<APlayerState>& Seated) { return Seated.Get() == PlayerState; });
}

// 复制回调流程：只刷本机表现；落座名单是唯一复制状态，客户端不据它推断任何权威结果。
void ACatCampfireActor::OnRep_SeatedPlayers()
{
	RefreshCampfirePresentation();
}

// 表现刷新流程：点火状态或名单变化时给蓝图一次机会。
// 原生层刻意不播动画、不生成火焰特效、也不决定叠罗汉怎么摆——那些是动作表现集与美术资产的事，
// 这里只保证「资产一挂上就能用」：蓝图拿到的是点火位与按落座先后排好的玩家名单。
void ACatCampfireActor::RefreshCampfirePresentation()
{
	const bool bLit = IsCampfireLit();
	const uint32 SeatSignature = MakeSeatSignature();
	// 低频 Tick 每 0.25 秒都会走到这里；只有点火状态或名单真的变了才通知一次，
	// 否则松弛动作会被反复重播。首帧无条件发一次，让表现层拿到初始状态。
	if (bHasPresentedOnce && bLit == bLastPresentedLit && SeatSignature == LastPresentedSeatSignature)
	{
		return;
	}
	bHasPresentedOnce = true;
	bLastPresentedLit = bLit;
	LastPresentedSeatSignature = SeatSignature;
	TArray<APlayerState*> SeatedPlayerStates;
	SeatedPlayerStates.Reserve(SeatedPlayers.Num());
	for (const TObjectPtr<APlayerState>& Seated : SeatedPlayers)
	{
		if (APlayerState* PlayerState = Seated.Get())
		{
			SeatedPlayerStates.Add(PlayerState);
		}
	}
	BP_RefreshCampfirePresentation(bLit, SeatedPlayerStates);
}

// 名单签名流程：把落座顺序与每个人的 PlayerId 混进一个整数；只给表现去重用，不作为身份或顺序凭据。
uint32 ACatCampfireActor::MakeSeatSignature() const
{
	uint32 Signature = static_cast<uint32>(SeatedPlayers.Num());
	for (const TObjectPtr<APlayerState>& Seated : SeatedPlayers)
	{
		const APlayerState* PlayerState = Seated.Get();
		Signature = HashCombine(Signature, GetTypeHash(PlayerState ? PlayerState->GetPlayerId() : 0));
	}
	return Signature;
}

// 范围解析流程：读取 Controller 当前 Character 并比较世界距离。
// 两端都会调用它——本地用来决定准星要不要出提示，服务器用来做权威复核；服务器那一次才是算数的那一次。
ACatCharacter* ACatCampfireActor::ResolveCharacterNearCampfire(AController* Controller) const
{
	ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr;
	return Character && InteractionRadiusCentimeters > 0.0f
		&& FVector::DistSquared(Character->GetActorLocation(), GetActorLocation())
			<= FMath::Square(static_cast<double>(InteractionRadiusCentimeters))
		? Character : nullptr;
}
