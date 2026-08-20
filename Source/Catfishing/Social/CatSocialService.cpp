#include "Social/CatSocialService.h"

#include "Framework/Core/CatStableNetId.h"
#include "Character/CatCharacter.h"
#include "Framework/Game/CatfishingGameState.h"
#include "Logging/CatLog.h"
#include "Condition/CatConditionComponent.h"
#include "Collection/CatRunImprintService.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Items/CatItemsService.h"
#include "ShopEconomy/CatShopEconomyService.h"
#include "Social/CatProtectionSignActor.h"
#include "Social/CatSocialSettings.h"
#include "TimerManager.h"

// 创建条件流程：只允许 authority Game World 持有权限、Timer 和 Items escrow 协调；客户端没有平行 Social 写状态。
bool UCatSocialService::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

// 初始化流程：把 Settings 里的两项默认权限抄进运行期策略并写下首个版本号。抄一次之后 Settings 就退出裁决，局主的运行
// 期改动不会在下一次读取时被静态配置覆盖回去。
void UCatSocialService::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const UCatSocialSettings* Settings = GetDefault<UCatSocialSettings>();
	RuntimePolicy.TheftPermission = Settings ? Settings->TheftPermission : ECatDomainPolicy::Unset;
	RuntimePolicy.MischiefPermission = Settings ? Settings->MischiefPermission : ECatDomainPolicy::Unset;
	RuntimePolicy.Revision = 1;
}

// 反初始化流程：复用统一关门/返还路径，随后清一局缓存和索引；若 World 依赖已先失效，不伪造 returned，但仍释放本服务内存。
void UCatSocialService::Deinitialize()
{
	CloseCommandsAndResolveAll();
	ActiveThefts.Reset();
	ActiveTheftByThief.Reset();
	RefreshStolenFishCarriers();
	HostStableNetId.Reset();
	LastManualHelpTimeByPlayer.Reset();
	CommandTerminalCache.Reset();
	CommandPayloadByKey.Reset();
	TheftTerminalCache.Reset();
	TheftPayloadByKey.Reset();
	ProtectionSignByPlayer.Reset();
	Super::Deinitialize();
}

// Teardown 关门流程：先永久拒绝新 Social 命令，再复制活跃 ProtocolId、逐条清唯一 Timer 并让 Items 原位返还；失败协议
// 保留并返回 false，调用方不得先关闭 Items。
bool UCatSocialService::CloseCommandsAndResolveAll()
{
	bCommandsOpen = false;
	TArray<FGuid> Requests;
	ActiveThefts.GetKeys(Requests);
	bool bAllResolved = true;
	for (const FGuid& TheftProtocolId : Requests)
	{
		if (FActiveTheft* Theft = ActiveThefts.Find(TheftProtocolId); Theft && GetWorld())
		{
			GetWorld()->GetTimerManager().ClearTimer(Theft->EatingWindowTimer);
		}
		const FCatTheftResult ReturnResult = ReturnActiveTheft(TheftProtocolId);
		bAllResolved &= ReturnResult.bReturned || ReturnResult.bConsumed;
	}
	return bAllResolved && ActiveThefts.IsEmpty();
}

// 偷鱼开始流程：先按身份、客户端 RequestId 与载荷签名确认合法重放，再验证真实容器宿主/主人、双方状态与距离，然后查鱼
// 主人的防骚扰牌；全部通过后才分配服务器 ProtocolId 建 escrow 和唯一 Timer。护栏拒绝都发生在 Items 写入之前，不存在先
// 扣鱼再回滚。
FCatTheftResult UCatSocialService::BeginTheft(AController* ThiefController, const FCatTheftCommand& Command)
{
	FCatTheftResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	const UCatSocialSettings* Settings = GetDefault<UCatSocialSettings>();
	const FString ThiefStableNetId = CatResolveStableNetId(ThiefController);
	ACatCharacter* ThiefCharacter = ThiefController ? Cast<ACatCharacter>(ThiefController->GetPawn()) : nullptr;
	UCatItemsService* Items = GetWorld() ? GetWorld()->GetSubsystem<UCatItemsService>() : nullptr;
	FCatContainerSnapshot SourceSnapshot;
	ECatContainerKind SourceKind = ECatContainerKind::Unknown;
	FString SourceOwnerStableNetId;
	AActor* SourceAuthorityActor = nullptr;
	if (ThiefStableNetId.IsEmpty() || !Command.Context.RequestId.IsValid())
	{
		Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
		return Result;
	}
	const FString TerminalKey = MakeTerminalKey(ThiefStableNetId, TEXT("BeginTheft"), Command.Context.RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("ExpectedRevision=%lld|FishInstanceId=%s|SourceContainerId=%s"),
		Command.Context.ExpectedRevision,
		*Command.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*Command.SourceContainerId.ToString(EGuidFormats::DigitsWithHyphens));
	switch (CatQueryTerminalReplay(TheftTerminalCache, TheftPayloadByKey, TerminalKey, PayloadSignature, Result, [](FCatTheftResult& R){ MarkCommandReplayed(R.Command); }))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
	}
	const auto Finish = [this, &TerminalKey, &PayloadSignature](const FCatTheftResult& TerminalResult)
	{
		TheftTerminalCache.Add(TerminalKey, TerminalResult);
		TheftPayloadByKey.Add(TerminalKey, PayloadSignature);
		return TerminalResult;
	};
	if (!bCommandsOpen)
	{
		Result.Command.Error = ECatDomainCommandError::CommandsClosed;
		return Finish(Result);
	}
	// 局主的偷取开关是三道护栏里最粗的一道，所以它排在所有其他准入之前：关掉之后不需要再去读容器、距离或牌子，整条入口就已经不存在了。
	// Disabled 与 Unset 都不放行，但拒绝原因分开：前者是局主明确关了，后者是这一局还没人裁决过。
	if (RuntimePolicy.TheftPermission != ECatDomainPolicy::Enabled)
	{
		Result.Command.Error = RuntimePolicy.TheftPermission == ECatDomainPolicy::Disabled
			? ECatDomainCommandError::PermissionDenied : ECatDomainCommandError::PolicyUndecided;
		return Finish(Result);
	}
	if (!Settings->AreTheftParametersReady() || !IsCharacterSociallyActive(ThiefCharacter) || !Items
		|| !Items->TryGetContainerSnapshot(Command.SourceContainerId, SourceSnapshot)
		|| !Items->TryGetContainerAuthorityContext(Command.SourceContainerId, SourceKind,
			SourceOwnerStableNetId, SourceAuthorityActor)
		|| !SourceAuthorityActor || SourceAuthorityActor->GetWorld() != GetWorld()
		|| FVector::DistSquared(ThiefCharacter->GetActorLocation(), SourceAuthorityActor->GetActorLocation())
			> FMath::Square(Settings->TheftInteractionRangeCentimeters))
	{
		Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
		return Finish(Result);
	}
	if (SourceKind == ECatContainerKind::PersonalGuard)
	{
		AController* VictimController = CatFindControllerByStableNetId(GetWorld(), SourceOwnerStableNetId);
		const ACatCharacter* VictimCharacter = VictimController ? Cast<ACatCharacter>(VictimController->GetPawn()) : nullptr;
		if (SourceOwnerStableNetId.IsEmpty() || !IsCharacterSociallyActive(VictimCharacter))
		{
			Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
			return Finish(Result);
		}
	}
	if (ActiveTheftByThief.Contains(ThiefStableNetId))
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPhase;
		return Finish(Result);
	}
	const FCatFishInstance* SourceFish = SourceSnapshot.Fish.FindByPredicate([&Command](const FCatFishInstance& Fish)
	{
		return Fish.FishInstanceId == Command.FishInstanceId;
	});
	AController* FishOwnerController = SourceFish ? CatFindControllerByStableNetId(GetWorld(), SourceFish->OwnerStableNetId) : nullptr;
	const ACatCharacter* FishOwnerCharacter = FishOwnerController ? Cast<ACatCharacter>(FishOwnerController->GetPawn()) : nullptr;
	// 个人鱼护偷的是某个在场玩家的私人东西，所以仍要求原钓手在场且可交互；共享缸装的是团队储备，谁钓的、人还在不在都
	// 不改变它可被偷，只保留鱼必须有明确主人这一条。
	if (!SourceFish || SourceFish->OwnerStableNetId.IsEmpty()
		|| (SourceKind == ECatContainerKind::PersonalGuard
			&& (!IsCharacterSociallyActive(FishOwnerCharacter)
				|| SourceOwnerStableNetId != SourceFish->OwnerStableNetId)))
	{
		Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	if (SourceKind == ECatContainerKind::SharedFishTank
		&& Settings->SharedTankRecoveryPolicy == ECatSharedTankRecoveryPolicy::Undecided)
	{
		Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
		return Finish(Result);
	}
	// 立牌即完整免打扰：鱼主人本人此刻站在自己牌子的半径里，这条鱼就不许被偷。判定放在这里是因为它必须早于下面的
	// escrow，拒绝时 Items 的写口一次都没被调用（前面只读了容器快照），源容器 Revision 不动。主人不在场（共享缸里掉线
	// 玩家的鱼）时没有可判定的位置，护栏不生效。
	if (FishOwnerCharacter && FishOwnerController
		&& IsPlayerProtectedAt(FishOwnerController->PlayerState, FishOwnerCharacter->GetActorLocation()))
	{
		Result.Command.Error = ECatDomainCommandError::PermissionDenied;
		return Finish(Result);
	}
	FCatFishTheftCommand ItemsCommand;
	ItemsCommand.Context = Command.Context;
	ItemsCommand.Context.StableNetId = ThiefStableNetId;
	ItemsCommand.TheftProtocolId = FGuid::NewGuid();
	ItemsCommand.FishInstanceId = Command.FishInstanceId;
	ItemsCommand.SourceContainerId = Command.SourceContainerId;
	const FCatFishTheftResult ItemsResult = Items->BeginFishTheft(ItemsCommand);
	Result.Command = ItemsResult.Command;
	Result.TheftProtocolId = ItemsResult.TheftProtocolId;
	Result.FishInstanceId = ItemsResult.Fish.FishInstanceId;
	if (!ItemsResult.Command.bCommitted)
	{
		return Finish(Result);
	}
	// Items 已接受同一个服务器 ProtocolId 后，Social 才建立活跃索引；客户端 RequestId 存进协议里：除了做 Begin 终态缓
	// 存键，后续吃掉赃物时还要把它传给 Items 做幂等。
	FActiveTheft& Theft = ActiveThefts.Add(ItemsResult.TheftProtocolId);
	Theft.ClientRequestId = Command.Context.RequestId;
	Theft.ThiefStableNetId = ThiefStableNetId;
	Theft.VictimStableNetId = ItemsResult.Fish.OwnerStableNetId;
	Theft.ThiefCharacter = ThiefCharacter;
	Theft.SourceKind = SourceKind;
	Theft.Fish = ItemsResult.Fish;
	Theft.Result = Result;
	Theft.Result.bRecoveryWindowOpen = true;
	Theft.Result.TheftProtocolId = ItemsResult.TheftProtocolId;
	ActiveTheftByThief.Add(ThiefStableNetId, ItemsResult.TheftProtocolId);
	FTimerDelegate TimerDelegate = FTimerDelegate::CreateUObject(this, &ThisClass::HandleTheftWindowExpired, ItemsResult.TheftProtocolId);
	GetWorld()->GetTimerManager().SetTimer(Theft.EatingWindowTimer, TimerDelegate,
		static_cast<float>(Settings->TheftEatingWindowSeconds), false);
	// 协议已经完整成立，这里才把"这只猫嘴里叼着一条赃物"发布成全场可见的事实；早于此发布会让被拒绝的偷取也在别人屏幕上闪一下。
	RefreshStolenFishCarriers();
	UE_LOG(LogCatSocial, Log, TEXT("Event=social_theft_started ProtocolId=%s FishInstanceId=%s SourceKind=%s WindowSeconds=%.3f"),
		*ItemsResult.TheftProtocolId.ToString(EGuidFormats::DigitsWithHyphens), *Theft.Fish.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*UEnum::GetValueAsString(Theft.SourceKind), Settings->TheftEatingWindowSeconds);
	TheftTerminalCache.Add(TerminalKey, Theft.Result);
	TheftPayloadByKey.Add(TerminalKey, PayloadSignature);
	return Theft.Result;
}
// 追回流程：先用与本文件其它命令同一套共享模板按"Catcher 身份+CatchTheft+ProtocolId"这个终态键做重放判定，
// 再按服务器 ProtocolId 定位协议，验证真实受害者/共享策略、双方可交互状态与权威距离；成功后原位返还并移除协议。
FCatTheftResult UCatSocialService::CatchTheft(AController* CatchingController, const FGuid TheftProtocolId)
{
	FCatTheftResult Result;
	Result.TheftProtocolId = TheftProtocolId;
	const FString CatcherStableNetId = CatResolveStableNetId(CatchingController);
	const UCatSocialSettings* Settings = GetDefault<UCatSocialSettings>();
	if (!TheftProtocolId.IsValid() || CatcherStableNetId.IsEmpty())
	{
		Result.Command.Error = ECatDomainCommandError::NotFound;
		return Result;
	}
	const FString TerminalKey = MakeTerminalKey(CatcherStableNetId, TEXT("CatchTheft"), TheftProtocolId);
	// 签名里只有 TheftProtocolId，因为追回这条命令能被调用方决定的东西就只有"谁来追"和"追哪一份协议"两项，
	// 而追的人已经作为身份段进了终态键。受害者是谁、鱼是哪条、双方站在哪、共享缸给不给追，全是服务器自己从协议和
	// World 里读出来的事实，写进签名等于让服务器防自己，只会让签名变成一串跟着实现漂移的噪声。
	// 于是这条命令的载荷冲突分支按构造就走不到——这是"入参全在键里"的结果，不是漏写：日后真给追回加了新入参，
	// 它必须补进这里，否则同一个 RequestId 就能带着两种意图共用一份终态。
	const FString PayloadSignature = FString::Printf(TEXT("TheftProtocolId=%s"),
		*TheftProtocolId.ToString(EGuidFormats::DigitsWithHyphens));
	switch (CatQueryTerminalReplay(TheftTerminalCache, TheftPayloadByKey, TerminalKey, PayloadSignature, Result, [](FCatTheftResult& R){ MarkCommandReplayed(R.Command); }))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
	}
	const auto Finish = [this, &TerminalKey, &PayloadSignature](const FCatTheftResult& TerminalResult)
	{
		// 空间或状态拒绝不是追回协议终态；只缓存 Items 已原位返还的成功结果，使同一捕手重试不复制鱼，同时允许仍在窗口内重新接近。
		// 两张表必须成对写：终态缓存有条目而签名表没有，共享模板会把它读成载荷冲突，受害者第二次点追回就会收到"载荷非法"。
		if (TerminalResult.bReturned || TerminalResult.bConsumed || TerminalResult.Command.bCommitted)
		{
			TheftTerminalCache.Add(TerminalKey, TerminalResult);
			TheftPayloadByKey.Add(TerminalKey, PayloadSignature);
		}
		return TerminalResult;
	};
	if (!bCommandsOpen)
	{
		Result.Command.Error = ECatDomainCommandError::CommandsClosed;
		return Finish(Result);
	}
	FActiveTheft* Theft = ActiveThefts.Find(TheftProtocolId);
	if (!Theft)
	{
		Result.Command.Error = ECatDomainCommandError::NotFound;
		return Finish(Result);
	}
	ACatCharacter* CatcherCharacter = CatchingController ? Cast<ACatCharacter>(CatchingController->GetPawn()) : nullptr;
	ACatCharacter* ThiefCharacter = Theft->ThiefCharacter.Get();
	AController* VictimController = CatFindControllerByStableNetId(GetWorld(), Theft->VictimStableNetId);
	const bool bVictimAuthorityMatches = VictimController && VictimController == CatchingController;
	const bool bAllowed = bVictimAuthorityMatches
		|| (Theft->SourceKind == ECatContainerKind::SharedFishTank
			&& Settings->SharedTankRecoveryPolicy == ECatSharedTankRecoveryPolicy::AnyActivePlayer);
	// 追回不看运行期偷取权限：局主中途关掉偷取只该停止新的偷，已经在窗口里的那条鱼仍必须能被追回来，否则关开关反而把赃物锁死在小偷手上。
	if (!bAllowed || !Settings->AreTheftParametersReady() || !IsCharacterSociallyActive(CatcherCharacter)
		|| !IsCharacterSociallyActive(ThiefCharacter)
		|| FVector::DistSquared(CatcherCharacter->GetActorLocation(), ThiefCharacter->GetActorLocation())
			> FMath::Square(Settings->TheftCatchRangeCentimeters))
	{
		Result.Command.Error = bAllowed ? ECatDomainCommandError::PolicyUndecided : ECatDomainCommandError::PermissionDenied;
		return Finish(Result);
	}
	GetWorld()->GetTimerManager().ClearTimer(Theft->EatingWindowTimer);
	const FActiveTheft Frozen = *Theft;
	Result = ReturnActiveTheft(TheftProtocolId);
	if (Result.bReturned)
	{
		SubmitCaughtImprint(Frozen, CatcherStableNetId);
		UE_LOG(LogCatSocial, Log, TEXT("Event=social_theft_caught ProtocolId=%s FishInstanceId=%s Returned=true"),
			*TheftProtocolId.ToString(EGuidFormats::DigitsWithHyphens), *Frozen.Fish.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens));
	}
	return Finish(Result);
}
// 偷鱼售出流程：先按身份、协议状态和到店位置做准入，再用这条鱼冻结的重量向 ShopEconomy 问一个服务器价；
// 问不出价就在鱼被冻进售卖准备态之前整笔拒绝。拿到价之后才把 Items escrow 锁定到同一售鱼请求，让 ShopEconomy 用该请求入账，最后 drain 掉 escrow。
// 钱包已成功但 drain 失败时不缓存 Social 终态，允许同 RequestId 继续重试收口。
FCatTheftResult UCatSocialService::SellStolenFish(AController* ThiefController, const FGuid TheftProtocolId,
	const FGuid RequestId, const int64 ExpectedWalletRevision)
{
	FCatTheftResult Result;
	Result.TheftProtocolId = TheftProtocolId;
	Result.Command.RequestId = RequestId;
	const FString ThiefStableNetId = CatResolveStableNetId(ThiefController);
	if (!RequestId.IsValid() || !TheftProtocolId.IsValid() || ThiefStableNetId.IsEmpty())
	{
		Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
		return Result;
	}

	const FString TerminalKey = MakeTerminalKey(ThiefStableNetId, TEXT("SellTheft"), RequestId);
	// 签名里不再有成交价，因为成交价已经不是调用方能提交的东西：它由 TheftProtocolId 指向的那条鱼的重量在服务器上算出来。
	// 同一 RequestId 下调用方真正还能改的意图只剩这两项——换一条偷鱼协议，或者换一个公款版本前提，两者都算换了意图。
	// 所以幂等判据没有因为删掉价格而变松：能被调用方漂移的字段一个不少地仍在签名里，服务器自己算出来的量不需要写进签名去防自己。
	const FString PayloadSignature = FString::Printf(TEXT("TheftProtocolId=%s|ExpectedWalletRevision=%lld"),
		*TheftProtocolId.ToString(EGuidFormats::DigitsWithHyphens),
		ExpectedWalletRevision);
	switch (CatQueryTerminalReplay(TheftTerminalCache, TheftPayloadByKey, TerminalKey, PayloadSignature, Result, [](FCatTheftResult& R){ MarkCommandReplayed(R.Command); }))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		Result.Command.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
	}
	const auto CacheAndReturn = [this, &TerminalKey, &PayloadSignature](const FCatTheftResult& TerminalResult)
	{
		TheftTerminalCache.Add(TerminalKey, TerminalResult);
		TheftPayloadByKey.Add(TerminalKey, PayloadSignature);
		return TerminalResult;
	};

	if (!bCommandsOpen)
	{
		Result.Command.Error = ECatDomainCommandError::CommandsClosed;
		return CacheAndReturn(Result);
	}

	FActiveTheft* Theft = ActiveThefts.Find(TheftProtocolId);
	if (!Theft)
	{
		Result.Command.Error = ECatDomainCommandError::NotFound;
		return CacheAndReturn(Result);
	}

	Result = Theft->Result;
	Result.Command.RequestId = RequestId;
	Result.Command.bCommitted = false;
	Result.Command.Error = ECatDomainCommandError::InvalidPayload;
	Result.TheftProtocolId = TheftProtocolId;
	Result.FishInstanceId = Theft->Fish.FishInstanceId;
	if (Theft->ThiefStableNetId != ThiefStableNetId)
	{
		Result.Command.Error = ECatDomainCommandError::PermissionDenied;
		return CacheAndReturn(Result);
	}
	// 只查这一个开关就够，不是漏检其余三个终态：bReturned 和 bSold 从来不写在活跃协议上（返还与售出都写在
	// 各自的局部结果里，写完立刻把协议移出 ActiveThefts），bConsumed 唯一一次写入与 bRecoveryWindowOpen=false
	// 在同一处成对发生。所以对任何还能被找到的活跃协议来说，这个条件与四项全查完全等价。
	if (!Theft->Result.bRecoveryWindowOpen)
	{
		Result.Command.Error = ECatDomainCommandError::AlreadyResolved;
		return CacheAndReturn(Result);
	}
	// 空间前提：飞书把"一路奔向商店卖掉"的那段路程算作追回窗口的一半，所以卖鱼必须真的发生在商店旁边。
	// 这个拒绝故意不进终态缓存：人不在商店只是此刻的位置事实，小偷带着同一个 RequestId 跑到商店应该还能把这笔卖成，
	// 而不是被自己第一次太早按下的按钮永久判死。锚点或距离未登记时同样落在这里，售出因此整体不可达。
	if (!GetDefault<UCatSocialSettings>()->IsTheftSaleReady() || !IsAtShopAnchor(Theft->ThiefCharacter.Get()))
	{
		Result.Command.Error = ECatDomainCommandError::InvalidPhase;
		return Result;
	}

	UCatItemsService* Items = GetWorld() ? GetWorld()->GetSubsystem<UCatItemsService>() : nullptr;
	UCatShopEconomyService* Economy = GetWorld() ? GetWorld()->GetSubsystem<UCatShopEconomyService>() : nullptr;
	if (!Items || !Economy)
	{
		Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		return CacheAndReturn(Result);
	}

	// 定价发生在这里，而且刻意排在 PrepareStolenFishSale 之前：估价只要鱼的重量，而重量在偷鱼协议建立那一刻就冻结在 Theft->Fish 上，
	// 根本不需要先把鱼锁进售卖准备态才问得出价格。放在前面的收益是，估不出价这条路上鱼一次都没被冻结过，也就不存在"冻了又得记得解冻"的分支。
	// 估不出价必须整笔拒绝而不是自己补一个数：收鱼价没被裁定或这条鱼落不进任何体重档位时，任何补上去的价格都是凭空造出来的公款收入。
	// 用 PolicyUndecided 而不是 InvalidPayload，是因为拒绝的原因不在调用方填了什么，而在产品还没把这条鱼该值多少钱拍下来。
	int32 ServerSaleValue = 0;
	if (!Economy->TryAppraiseFishSale(Theft->Fish.WeightKilograms, ServerSaleValue))
	{
		Result.Command.Error = ECatDomainCommandError::PolicyUndecided;
		return CacheAndReturn(Result);
	}

	const FCatFishTheftResult Prepared = Items->PrepareStolenFishSale(TheftProtocolId, RequestId, ThiefStableNetId);
	if (!Prepared.Command.bCommitted && Prepared.Command.Error != ECatDomainCommandError::AlreadyResolved)
	{
		Result.Command = Prepared.Command;
		return CacheAndReturn(Result);
	}
	// 过了这一行，鱼已经被冻进售卖准备态：它既不在受害者鱼护里，也还没被卖掉。所以从这里开始的每一条失败分支都必须调用
	// CancelPreparedStolenFishSale 把它解冻，否则这条鱼会永远卡在准备态——追不回、吃不掉、也卖不出去。
	// 解冻的边界止于钱包入账成功：钱一旦进了公款账本，正确的收口是 drain 掉 escrow 而不是退回去，此时再取消等于凭空多出一条鱼。

	FCatShopFishSaleCommand SaleCommand;
	SaleCommand.Context.RequestId = RequestId;
	SaleCommand.Context.ExpectedRevision = ExpectedWalletRevision;
	SaleCommand.Context.StableNetId = ThiefStableNetId;
	SaleCommand.FishInstanceId = Theft->Fish.FishInstanceId;
	SaleCommand.ItemsCommitId = TheftProtocolId;
	// 来源固定写成 StolenEscrow：能走到这个函数的鱼只可能是仍扣在 Social escrow 里的赃物。
	// ShopEconomy 对 Unknown 来源一律拒收，账本也靠这个字段把偷来的钱和自己钓的钱分开记。
	SaleCommand.SourceKind = ECatShopFishSaleSource::StolenEscrow;
	// 重量和价格都只来自服务器：重量是捕获时冻结在鱼实例上的那个值，价格是上面用同一个重量现场估出来的。
	// ShopEconomy 入账时会拿这里的重量再估一次并与 SaleValue 核对，两边同源所以必然相等；
	// 之所以还要把价格带过去，是让"最终入账多少钱"在命令里是一个明确的数，而不是靠两侧各算一遍碰运气对上。
	SaleCommand.WeightKilograms = Theft->Fish.WeightKilograms;
	SaleCommand.SaleValue = ServerSaleValue;

	ECatDomainCommandError SaleValidationError = ECatDomainCommandError::None;
	int64 CurrentWalletRevision = 0;
	const bool bSaleCanBeApplied = Economy->ValidateFishSale(SaleCommand, SaleValidationError, CurrentWalletRevision);
	if (!bSaleCanBeApplied && SaleValidationError != ECatDomainCommandError::AlreadyResolved)
	{
		Items->CancelPreparedStolenFishSale(TheftProtocolId, RequestId, ThiefStableNetId);
		Result.Command.Error = SaleValidationError;
		Result.Command.Revision = CurrentWalletRevision;
		return CacheAndReturn(Result);
	}

	const FCatShopTransactionResult SaleResult = Economy->ApplyFishSale(SaleCommand);
	const bool bWalletAccepted = SaleResult.Command.bCommitted
		|| (SaleResult.Command.Error == ECatDomainCommandError::AlreadyResolved && SaleResult.Transaction.TransactionId.IsValid());
	if (!bWalletAccepted)
	{
		Items->CancelPreparedStolenFishSale(TheftProtocolId, RequestId, ThiefStableNetId);
		Result.Command.Error = SaleResult.Command.Error;
		Result.Command.Revision = SaleResult.Command.Revision;
		return CacheAndReturn(Result);
	}

	const FCatFishTheftResult ItemsCommit = Items->CommitPreparedStolenFishSale(TheftProtocolId, RequestId, ThiefStableNetId);
	if (!ItemsCommit.Command.bCommitted)
	{
		Result.Command = ItemsCommit.Command;
		Result.EconomyRevision = SaleResult.Wallet.Revision;
		return Result;
	}

	GetWorld()->GetTimerManager().ClearTimer(Theft->EatingWindowTimer);
	Result.Command = ItemsCommit.Command;
	Result.Command.RequestId = RequestId;
	Result.bRecoveryWindowOpen = false;
	Result.bReturned = false;
	Result.bConsumed = false;
	Result.bSold = true;
	Result.EconomyRevision = SaleResult.Wallet.Revision;
	TheftTerminalCache.Add(MakeTerminalKey(Theft->ThiefStableNetId, TEXT("BeginTheft"), Theft->ClientRequestId), Result);
	ActiveTheftByThief.Remove(Theft->ThiefStableNetId);
	ActiveThefts.Remove(TheftProtocolId);
	// 鱼已经从 escrow 里卖掉，嘴里不再有赃物：公开叼鱼事实必须跟着这一步清掉，否则场上会一直显示一只叼着不存在的鱼的猫。
	RefreshStolenFishCarriers();
	UE_LOG(LogCatSocial, Log, TEXT("Event=social_theft_sold ProtocolId=%s FishInstanceId=%s WalletRevision=%lld"),
		*TheftProtocolId.ToString(EGuidFormats::DigitsWithHyphens),
		*ItemsCommit.Fish.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		SaleResult.Wallet.Revision);
	return CacheAndReturn(Result);
}

// 恶作剧权限流程：先按身份、RequestId 和目标签名确认合法重放，再从双方权威 Pawn 验证状态与距离，最后查目标的防骚扰
// 牌。全程不看频率：恶作剧密度不设系统上限，也不为搏斗等关键时刻额外豁免。
FCatDomainCommandResult UCatSocialService::RequestMischief(AController* InstigatorController,
	AController* TargetController, const FGuid RequestId)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const UCatSocialSettings* Settings = GetDefault<UCatSocialSettings>();
	const FString InstigatorId = CatResolveStableNetId(InstigatorController);
	const FString TargetStableNetId = CatResolveStableNetId(TargetController);
	const APlayerState* TargetPlayerState = TargetController ? TargetController->PlayerState : nullptr;
	const ACatCharacter* InstigatorCharacter = InstigatorController ? Cast<ACatCharacter>(InstigatorController->GetPawn()) : nullptr;
	const ACatCharacter* TargetCharacter = TargetController ? Cast<ACatCharacter>(TargetController->GetPawn()) : nullptr;
	if (!RequestId.IsValid() || InstigatorId.IsEmpty())
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Result;
	}
	const FString TerminalKey = MakeTerminalKey(InstigatorId, TEXT("Mischief"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("Target=%s"), *TargetStableNetId);
	switch (CatQueryTerminalReplay(CommandTerminalCache, CommandPayloadByKey, TerminalKey, PayloadSignature, Result, MarkCommandReplayed))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
	}
	const auto Finish = [this, &TerminalKey, &PayloadSignature](const FCatDomainCommandResult& TerminalResult)
	{
		CommandTerminalCache.Add(TerminalKey, TerminalResult);
		CommandPayloadByKey.Add(TerminalKey, PayloadSignature);
		return TerminalResult;
	};
	if (!bCommandsOpen)
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		return Finish(Result);
	}
	// 与偷取同理，局主的恶作剧开关排在所有其他准入之前；Disabled 是"局主关了"，Unset 是"这一局还没裁决过"。
	if (RuntimePolicy.MischiefPermission != ECatDomainPolicy::Enabled)
	{
		Result.Error = RuntimePolicy.MischiefPermission == ECatDomainPolicy::Disabled
			? ECatDomainCommandError::PermissionDenied : ECatDomainCommandError::PolicyUndecided;
		return Finish(Result);
	}
	if (!Settings->AreMischiefParametersReady() || !TargetPlayerState || InstigatorController == TargetController
		|| !IsCharacterSociallyActive(InstigatorCharacter) || !IsCharacterSociallyActive(TargetCharacter)
		|| FVector::DistSquared(InstigatorCharacter->GetActorLocation(), TargetCharacter->GetActorLocation())
			> FMath::Square(Settings->MischiefInteractionRangeCentimeters))
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Finish(Result);
	}
	if (IsPlayerProtectedAt(TargetPlayerState, TargetCharacter->GetActorLocation()))
	{
		Result.Error = ECatDomainCommandError::PermissionDenied;
		return Finish(Result);
	}
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	return Finish(Result);
}

// 放牌流程：先按身份、RequestId 和位置签名确认合法重放，再验证 Pawn、牌子自身范围和有限位置；首次提交复用每人唯一
// Actor 并配置保护。这里读的是 IsProtectionSignReady 而不是 IsMischiefReady：牌子是护栏，关掉恶作剧不该顺带让人放不了
// 牌，否则挡偷窃的那一半也一起没了。
FCatDomainCommandResult UCatSocialService::PlaceProtectionSign(AController* RequestingController,
	const FGuid RequestId, const FVector SignLocation)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const UCatSocialSettings* Settings = GetDefault<UCatSocialSettings>();
	const FString StableNetId = CatResolveStableNetId(RequestingController);
	APawn* Pawn = RequestingController ? RequestingController->GetPawn() : nullptr;
	APlayerState* PlayerState = RequestingController ? RequestingController->PlayerState : nullptr;
	if (!RequestId.IsValid() || StableNetId.IsEmpty())
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Result;
	}
	const FString TerminalKey = MakeTerminalKey(StableNetId, TEXT("PlaceProtectionSign"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("SignLocation=%s"), *SignLocation.ToString());
	switch (CatQueryTerminalReplay(CommandTerminalCache, CommandPayloadByKey, TerminalKey, PayloadSignature, Result, MarkCommandReplayed))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
	}
	const auto Finish = [this, &TerminalKey, &PayloadSignature](const FCatDomainCommandResult& TerminalResult)
	{
		CommandTerminalCache.Add(TerminalKey, TerminalResult);
		CommandPayloadByKey.Add(TerminalKey, PayloadSignature);
		return TerminalResult;
	};
	if (!bCommandsOpen)
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		return Finish(Result);
	}
	if (!Settings->IsProtectionSignReady() || !Pawn || !PlayerState
		|| SignLocation.ContainsNaN()
		|| FVector::DistSquared(Pawn->GetActorLocation(), SignLocation)
			> FMath::Square(Settings->ProtectionSignPlacementRangeCentimeters))
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Finish(Result);
	}
	ACatProtectionSignActor* Sign = ProtectionSignByPlayer.FindRef(StableNetId).Get();
	if (!Sign)
	{
		Sign = GetWorld()->SpawnActor<ACatProtectionSignActor>(ACatProtectionSignActor::StaticClass(),
			SignLocation, FRotator::ZeroRotator);
		ProtectionSignByPlayer.Add(StableNetId, Sign);
	}
	else
	{
		Sign->SetActorLocation(SignLocation, false, nullptr, ETeleportType::TeleportPhysics);
	}
	if (!Sign || !Sign->ConfigureProtection(PlayerState, Settings->ProtectionSignRadiusCentimeters))
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	return Finish(Result);
}

// 手动求助流程：先按身份、RequestId 和求助类型签名确认合法重放，再校验两种 Manual 类型、范围、冷却和 Pawn；首次成功才递增 Revision。
FCatDomainCommandResult UCatSocialService::RequestManualHelp(AController* RequestingController,
	const FGuid RequestId, const ECatHelpSignalKind Kind)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const UCatSocialSettings* Settings = GetDefault<UCatSocialSettings>();
	const FString StableNetId = CatResolveStableNetId(RequestingController);
	const APawn* Pawn = RequestingController ? RequestingController->GetPawn() : nullptr;
	ACatfishingGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
	if (!RequestId.IsValid() || StableNetId.IsEmpty())
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Result;
	}
	const FString TerminalKey = MakeTerminalKey(StableNetId, TEXT("ManualHelp"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("Kind=%d"), static_cast<int32>(Kind));
	switch (CatQueryTerminalReplay(CommandTerminalCache, CommandPayloadByKey, TerminalKey, PayloadSignature, Result, MarkCommandReplayed))
	{
	case ECatTerminalReplayOutcome::PayloadMismatch:
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	case ECatTerminalReplayOutcome::Replayed:
		return Result;
	case ECatTerminalReplayOutcome::FirstAttempt:
		break;
	}
	const auto Finish = [this, &TerminalKey, &PayloadSignature](const FCatDomainCommandResult& TerminalResult)
	{
		CommandTerminalCache.Add(TerminalKey, TerminalResult);
		CommandPayloadByKey.Add(TerminalKey, PayloadSignature);
		return TerminalResult;
	};
	if (!bCommandsOpen)
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		return Finish(Result);
	}
	if (!Settings->IsManualHelpReady() || !Pawn || !GameState
		|| (Kind != ECatHelpSignalKind::ManualFishing && Kind != ECatHelpSignalKind::ManualDowned))
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Finish(Result);
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (const double* LastTime = LastManualHelpTimeByPlayer.Find(StableNetId);
		LastTime && Now - *LastTime < Settings->ManualHelpCooldownSeconds)
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
		return Finish(Result);
	}
	FCatHelpSignalSnapshot Signal;
	Signal.SignalId = RequestId;
	Signal.Kind = Kind;
	Signal.SourceLocation = Pawn->GetActorLocation();
	Signal.RadiusCentimeters = Settings->ManualHelpRadiusCentimeters;
	Signal.bGlobal = false;
	Signal.Revision = ++HelpSignalRevision;
	GameState->SetHelpSignalFromAuthority(Signal);
	LastManualHelpTimeByPlayer.Add(StableNetId, Now);
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	UE_LOG(LogCatSocial, Log, TEXT("Event=social_manual_help RequestId=%s Kind=%s Revision=%lld RadiusCm=%.3f"),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens), *UEnum::GetValueAsString(Kind), Signal.Revision,
		Signal.RadiusCentimeters);
	return Finish(Result);
}

// Giant 提示流程：要求 Social 总 gate、有效会话 ID 和当前 Pawn；直接发布全局系统信号，不占用玩家手动求助冷却。
void UCatSocialService::BroadcastGiantFishingPrompt(AController* FisherController, const FGuid FishingSessionId)
{
	const UCatSocialSettings* Settings = GetDefault<UCatSocialSettings>();
	const APawn* Pawn = FisherController ? FisherController->GetPawn() : nullptr;
	ACatfishingGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
	if (!bCommandsOpen || !Settings->bEnableSocialRuntime || !FishingSessionId.IsValid() || !Pawn || !GameState)
	{
		return;
	}
	FCatHelpSignalSnapshot Signal;
	Signal.SignalId = FishingSessionId;
	Signal.Kind = ECatHelpSignalKind::GiantFishSystem;
	Signal.SourceLocation = Pawn->GetActorLocation();
	Signal.bGlobal = true;
	Signal.Revision = ++HelpSignalRevision;
	GameState->SetHelpSignalFromAuthority(Signal);
	UE_LOG(LogCatSocial, Log, TEXT("Event=social_giant_prompt SessionId=%s Revision=%lld Global=true"),
		*FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), Signal.Revision);
}

// Character 取消流程：只匹配作为小偷的弱 Character；逐条清 Timer 并原位返还，避免掉线/倒地把负面影响扩大成永久丢鱼。
void UCatSocialService::CancelTheftsForCharacter(const ACatCharacter* Character)
{
	if (!Character)
	{
		return;
	}
	TArray<FGuid> Requests;
	for (const TPair<FGuid, FActiveTheft>& Pair : ActiveThefts)
	{
		if (Pair.Value.ThiefCharacter.Get() == Character)
		{
			Requests.Add(Pair.Key);
		}
	}
	for (const FGuid& TheftProtocolId : Requests)
	{
		if (FActiveTheft* Theft = ActiveThefts.Find(TheftProtocolId))
		{
			GetWorld()->GetTimerManager().ClearTimer(Theft->EatingWindowTimer);
		}
		ReturnActiveTheft(TheftProtocolId);
	}
}

// 进食到期流程：按服务器 ProtocolId 验证协议、Character、鱼定义并完成身体 preflight；任一依赖失效就返还，全部有效才让
// Items 不可逆消费并用原客户端 RequestId 关联效果。
void UCatSocialService::HandleTheftWindowExpired(const FGuid TheftProtocolId)
{
	FActiveTheft* Theft = ActiveThefts.Find(TheftProtocolId);
	// ClearTimer 后若回调已进入队列，关门事实仍优先把 escrow 返还，不能在 Host teardown 中继续执行吃鱼副作用。
	if (!bCommandsOpen)
	{
		ReturnActiveTheft(TheftProtocolId);
		return;
	}
	UCatItemsService* Items = GetWorld() ? GetWorld()->GetSubsystem<UCatItemsService>() : nullptr;
	ACatCharacter* Character = Theft ? Theft->ThiefCharacter.Get() : nullptr;
	UCatConditionComponent* Conditions = Character ? Character->GetConditionComponent() : nullptr;
	UCatFishDefinition* Definition = Theft
		? GetDefault<UCatFishCatalogSettings>()->FindRuntimeDefinition(Theft->Fish.FishDefinitionId) : nullptr;
	if (!Theft || !Items || !Conditions || !Definition
		|| Conditions->ValidateFishConsumption(Definition) != ECatDomainCommandError::None)
	{
		ReturnActiveTheft(TheftProtocolId);
		return;
	}
	// 空间前提：飞书把"找地方进食"算作追回窗口的另一半，所以窗口走完还不够，人还得真的跑开了才吃得下去。
	// 没跑开就把鱼原样还回去，而不是把窗口续上——续窗会让小偷可以无限期扣着赃物，追回反而更难收口。
	if (!HasEscapedVictimForConsumption(*Theft))
	{
		ReturnActiveTheft(TheftProtocolId);
		return;
	}
	const FCatFishTheftResult ItemsResult = Items->CommitStolenFishConsumption(TheftProtocolId);
	if (!ItemsResult.Command.bCommitted)
	{
		ReturnActiveTheft(TheftProtocolId);
		return;
	}
	Conditions->ConsumeCommittedFish(Theft->ClientRequestId, Definition);
	Theft->Result.Command = ItemsResult.Command;
	Theft->Result.bRecoveryWindowOpen = false;
	Theft->Result.bConsumed = true;
	// 协议即将从活跃表移除，先把 Begin 请求的重放值推进到 consumed 终态；后续网络重试不会退回 NotFound 或再次消费。
	TheftTerminalCache.Add(MakeTerminalKey(Theft->ThiefStableNetId, TEXT("BeginTheft"), Theft->ClientRequestId), Theft->Result);
	ActiveTheftByThief.Remove(Theft->ThiefStableNetId);
	ActiveThefts.Remove(TheftProtocolId);
	// 鱼已被不可逆吃掉，嘴里不再有赃物：公开叼鱼事实在协议离开活跃表后立刻重建。
	RefreshStolenFishCarriers();
	UE_LOG(LogCatSocial, Log, TEXT("Event=social_theft_consumed ProtocolId=%s FishInstanceId=%s"),
		*TheftProtocolId.ToString(EGuidFormats::DigitsWithHyphens), *ItemsResult.Fish.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens));
}

// 返还辅助流程：按服务器 ProtocolId 调用 Items 原位返还；成功后用原客户端 RequestId 更新 Begin 终态，再移除 thief 索引/协议。
FCatTheftResult UCatSocialService::ReturnActiveTheft(const FGuid TheftProtocolId)
{
	FCatTheftResult Result;
	Result.TheftProtocolId = TheftProtocolId;
	FActiveTheft* Theft = ActiveThefts.Find(TheftProtocolId);
	Result.Command.RequestId = Theft ? Theft->ClientRequestId : FGuid();
	UCatItemsService* Items = GetWorld() ? GetWorld()->GetSubsystem<UCatItemsService>() : nullptr;
	if (!Theft || !Items)
	{
		Result.Command.Error = ECatDomainCommandError::NotFound;
		return Result;
	}
	const FCatFishTheftResult ItemsResult = Items->ReturnStolenFish(TheftProtocolId);
	Result = Theft->Result;
	Result.Command = ItemsResult.Command;
	if (ItemsResult.Command.bCommitted)
	{
		Result.bRecoveryWindowOpen = false;
		Result.bReturned = true;
		// 返还成功是该 Begin 请求的最终事实；必须在移除私有协议前冻结身份键与完整 returned 结果。
		TheftTerminalCache.Add(MakeTerminalKey(Theft->ThiefStableNetId, TEXT("BeginTheft"), Theft->ClientRequestId), Result);
		ActiveTheftByThief.Remove(Theft->ThiefStableNetId);
		ActiveThefts.Remove(TheftProtocolId);
		// 鱼已经物归原主，嘴里不再有赃物。返还是追回、取消和 teardown 三条路径的共同出口，所以这一处重建覆盖了它们全部。
		RefreshStolenFishCarriers();
	}
	return Result;
}

// 被抓印记流程：先要求正式事件、Run 和两名不同参与者，再提交一份共同 Candidate；批量接口会先为双方建齐并索引 Planned
// 记录，确认没有部分缺失后才逐条投递，暂时离线的接收者沿用原计划重试。该成像链是返还成功后的可选副作用，不会改写
// theft returned 终态。
void UCatSocialService::SubmitCaughtImprint(const FActiveTheft& Theft, const FString& CatcherStableNetId)
{
	const UCatSocialSettings* Settings = GetDefault<UCatSocialSettings>();
	const ACatfishingGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
	UCatRunImprintService* Imprint = GetWorld() ? GetWorld()->GetSubsystem<UCatRunImprintService>() : nullptr;
	if (!Settings || Settings->TheftCaughtImprintEventId.IsNone() || !GameState || !Imprint
		|| Theft.ThiefStableNetId.IsEmpty() || CatcherStableNetId.IsEmpty()
		|| Theft.ThiefStableNetId == CatcherStableNetId)
	{
		return;
	}
	FCatImprintCandidate Candidate;
	Candidate.CandidateId = FGuid::NewGuid();
	Candidate.RunId = GameState->GetRunPublicState().Phase.RunId;
	Candidate.EventType = Settings->TheftCaughtImprintEventId;
	Candidate.SubjectId = Theft.Fish.FishInstanceId;
	Candidate.FishDefinitionId = Theft.Fish.FishDefinitionId;
	Candidate.ParticipantStableNetIds = {Theft.ThiefStableNetId, CatcherStableNetId};
	Candidate.ParticipantCount = Candidate.ParticipantStableNetIds.Num();
	if (Imprint->SubmitImprintCandidate(Candidate))
	{
		TArray<FCatCapturePlan> CapturePlans;
		Imprint->CreateCapturePlansForParticipants(Candidate.CandidateId,
			Candidate.ParticipantStableNetIds, false, CapturePlans);
	}
}

// 终态键构造流程：显式拼接服务器身份、操作域与标准 GUID；操作名隔离相同 RequestId 的不同语义，原始身份只留在服务私有映射中。
FString UCatSocialService::MakeTerminalKey(const FString& StableNetId, const TCHAR* Operation, const FGuid RequestId)
{
	return FString::Printf(TEXT("%s|%s|%s"), *StableNetId, Operation,
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
}

// Social 状态检查流程：要求项目 Character 和 Condition 均有效且未倒地；缺组件或倒地都不能发起、被授权或完成空间交互。
bool UCatSocialService::IsCharacterSociallyActive(const ACatCharacter* Character)
{
	const UCatConditionComponent* Conditions = Character ? Character->GetConditionComponent() : nullptr;
	return Conditions && !Conditions->GetSnapshot().bDowned;
}

// 护栏查询流程：遍历当前 World 的全部牌子，只要有一块认这个 PlayerState 且该位置在它半径内就返回 true；一块都没命中才
// 返回 false。缺目标身份或 World 时直接返回 false，因为此时没有可判定的保护关系，调用方在更早的准入检查里已经处理了这
// 两种情况。
bool UCatSocialService::IsPlayerProtectedAt(const APlayerState* TargetPlayerState, const FVector& InteractionLocation) const
{
	if (!TargetPlayerState || !GetWorld())
	{
		return false;
	}
	for (TActorIterator<ACatProtectionSignActor> It(GetWorld()); It; ++It)
	{
		if (It->ProtectsAgainst(TargetPlayerState, InteractionLocation))
		{
			return true;
		}
	}
	return false;
}

// 公开叼鱼列表读取流程：直接返回服务器当前发布的那一份，不在读取时重算。它是给复制出口和只读查询用的，调用方不能借引用改写。
const TArray<FCatStolenFishCarrySnapshot>& UCatSocialService::GetStolenFishCarriers() const
{
	return PublicStolenFishCarriers;
}

// 运行期策略读取流程：直接返回本局唯一那份权限事实；偷取与恶作剧入口都只读它，不再回头读 Settings。
const FCatSocialPolicySnapshot& UCatSocialService::GetSocialPolicy() const
{
	return RuntimePolicy;
}

// 局主登记流程：只把传入 Controller 解析成服务器私有身份并整体替换。传空指针会把身份清空，等于"这一局暂时没有可认的局主"，此后任何人改策略都会被拒。
// 这里刻意不自己推断局主：谁是局主属于会话成员关系，本服务没有那份事实，猜错会把权限交给不该有权限的人。
void UCatSocialService::SetHostAuthority(const AController* HostController)
{
	HostStableNetId = CatResolveStableNetId(HostController);
}

// 策略改写流程：先要求有效 RequestId 和可解析身份，再确认命令未关门，然后只放行已登记的局主；通过后整体替换两项权限，权限真的变了才推进版本。
// 这里没有终态缓存，也不需要：命令写的是绝对值而不是增量，同一份权限重复提交产生的状态完全一样，天然幂等。
// 值没变时返回 AlreadyResolved 而不是成功，因为本次确实没有发生写入，用 None 会让调用方误以为自己刚改动了策略。
FCatDomainCommandResult UCatSocialService::SetSocialPolicy(AController* RequestingController, const FGuid RequestId,
	const ECatDomainPolicy NewTheftPermission, const ECatDomainPolicy NewMischiefPermission)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	Result.Revision = RuntimePolicy.Revision;
	const FString RequesterStableNetId = CatResolveStableNetId(RequestingController);
	if (!RequestId.IsValid() || RequesterStableNetId.IsEmpty())
	{
		Result.Error = ECatDomainCommandError::PolicyUndecided;
		return Result;
	}
	if (!bCommandsOpen)
	{
		Result.Error = ECatDomainCommandError::CommandsClosed;
		return Result;
	}
	if (HostStableNetId.IsEmpty() || RequesterStableNetId != HostStableNetId)
	{
		Result.Error = ECatDomainCommandError::PermissionDenied;
		return Result;
	}
	if (RuntimePolicy.TheftPermission == NewTheftPermission
		&& RuntimePolicy.MischiefPermission == NewMischiefPermission)
	{
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		return Result;
	}
	RuntimePolicy.TheftPermission = NewTheftPermission;
	RuntimePolicy.MischiefPermission = NewMischiefPermission;
	++RuntimePolicy.Revision;
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	Result.Revision = RuntimePolicy.Revision;
	UE_LOG(LogCatSocial, Log, TEXT("Event=social_policy_changed Theft=%s Mischief=%s Revision=%lld"),
		*UEnum::GetValueAsString(RuntimePolicy.TheftPermission),
		*UEnum::GetValueAsString(RuntimePolicy.MischiefPermission), RuntimePolicy.Revision);
	return Result;
}

// 公开叼鱼列表重建流程：推进一次版本号，然后按当前活跃协议整体重建列表。
// 列表是派生量而不是第二份状态，所以每次都整体重算而不是增量维护——返还、吃掉、售出各自把协议从活跃表里移走之后，
// 这里自然什么都找不到，不存在"某条终态忘了清"的可能。
// 解析不出 PlayerState 的协议会被跳过：那说明小偷的 Character 已经不在场上，此刻没有可以指给旁观者看的目标；
// 这类协议随后由 CancelTheftsForCharacter 或 teardown 把鱼还回去。
// 这里也是唯一需要把新列表推给复制出口的位置：偷鱼状态的每一次公开变化都经过这一个函数，接复制时只需在此发布一次。
void UCatSocialService::RefreshStolenFishCarriers()
{
	++StolenFishCarryRevision;
	PublicStolenFishCarriers.Reset();
	for (const TPair<FGuid, FActiveTheft>& Pair : ActiveThefts)
	{
		const ACatCharacter* ThiefCharacter = Pair.Value.ThiefCharacter.Get();
		APlayerState* ThiefPlayerState = ThiefCharacter ? ThiefCharacter->GetPlayerState() : nullptr;
		if (!ThiefPlayerState)
		{
			continue;
		}
		FCatStolenFishCarrySnapshot& Carrier = PublicStolenFishCarriers.AddDefaulted_GetRef();
		Carrier.CarrierPlayerState = ThiefPlayerState;
		Carrier.FishDefinitionId = Pair.Value.Fish.FishDefinitionId;
		Carrier.bStolen = true;
		Carrier.Revision = StolenFishCarryRevision;
	}
	// 投递端只挂在这一个地方：所有会改变公开叼鱼集合的路径（开始偷、被抓、吃掉、卖掉、窗口到期返还、角色离场）
	// 最后都会回到这个函数，所以在这里推一次就等于全部推到。
	// 不在这里推的后果不是“延迟一点”，而是客户端永远拿不到这份数据——受害者和旁观者根本不知道该追谁。
	if (ACatfishingGameState* CatGameState = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr)
	{
		CatGameState->SetStolenFishCarriersFromAuthority(PublicStolenFishCarriers);
	}
}

// 商店距离流程：要求售出配置齐全和有效小偷角色，再遍历当前 World 找带商店标签的 Actor，命中任意一个就通过。
// 找的是标签而不是某个具体类：商店锚点由关卡摆放，Social 不生成商店也不持有商店坐标；关卡一个锚点都没标时必然返回 false，售出保持不可达。
bool UCatSocialService::IsAtShopAnchor(const ACatCharacter* ThiefCharacter) const
{
	const UCatSocialSettings* Settings = GetDefault<UCatSocialSettings>();
	if (!ThiefCharacter || !GetWorld() || !Settings->IsTheftSaleReady())
	{
		return false;
	}
	const double RangeSquared = FMath::Square(Settings->TheftSaleShopRangeCentimeters);
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(Settings->TheftSaleShopAnchorTag)
			&& FVector::DistSquared(ThiefCharacter->GetActorLocation(), It->GetActorLocation()) <= RangeSquared)
		{
			return true;
		}
	}
	return false;
}

// 逃离距离流程：要求进食配置齐全、小偷角色仍在场、受害者当前也在场，然后比较两者的服务器权威距离。
// 受害者不在场（掉线，或共享缸里那条鱼的原主人已经离开）时返回 false：此时没有"跑离谁"这个可判定对象，
// 按 fail-closed 宁可把鱼还回去，也不放行一个无法验证空间前提的不可逆消费。这条取舍的后果是：
// 主人已经离开的共享缸赃物在当前规则下吃不掉，只会到期返还——这是有意的保守行为，不是遗漏。
bool UCatSocialService::HasEscapedVictimForConsumption(const FActiveTheft& Theft) const
{
	const UCatSocialSettings* Settings = GetDefault<UCatSocialSettings>();
	const ACatCharacter* ThiefCharacter = Theft.ThiefCharacter.Get();
	const AController* VictimController = CatFindControllerByStableNetId(GetWorld(), Theft.VictimStableNetId);
	const APawn* VictimPawn = VictimController ? VictimController->GetPawn() : nullptr;
	if (!Settings->IsTheftConsumptionReady() || !ThiefCharacter || !VictimPawn)
	{
		return false;
	}
	return FVector::DistSquared(ThiefCharacter->GetActorLocation(), VictimPawn->GetActorLocation())
		>= FMath::Square(Settings->TheftConsumeVictimEscapeDistanceCentimeters);
}
