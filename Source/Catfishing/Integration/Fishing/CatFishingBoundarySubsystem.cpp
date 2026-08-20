#include "Integration/Fishing/CatFishingBoundarySubsystem.h"

#include "Framework/Core/CatStableNetId.h"
#include "Character/CatCharacter.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/CatSurvivalAttributeSet.h"
#include "Condition/CatConditionComponent.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Engine/World.h"
#include "Environment/CatChumSpotSubsystem.h"
#include "Environment/CatEnvironmentSettings.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Framework/Game/CatfishingGameMode.h"
#include "Framework/Game/CatfishingGameState.h"
#include "GameFramework/PlayerState.h"
#include "Integration/Fishing/CatFishingBoundaryHash.h"

// 创建条件流程：Boundary 只在 authority Game World 存在；客户端没有资格冻结 encounter 或维护 Journal。
bool UCatFishingBoundarySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

// 反初始化流程：清空全部 run-local 协调缓存；持久恢复和跨进程重放不属于当前原子。
void UCatFishingBoundarySubsystem::Deinitialize()
{
	Journal = FCatFishingOperationJournal();
	BaitResultByOperation.Reset();
	FightCursorLedger = FCatFishingFightCursorLedger();
	AttemptByStartRequestKey.Reset();
	StartContextByAttempt.Reset();
	StartContextByOperation.Reset();
	EncounterSpecByOperation.Reset();
	Super::Deinitialize();
}

// Start 流程：先用服务器身份稳定生成 Attempt，再让 Journal 归并重放；成功只返回 PreCast 上下文，不查询水域或鱼表。
FCatFishingBoundaryStartResult UCatFishingBoundarySubsystem::Start(AController* FisherController, const FGuid RequestId)
{
	FCatFishingBoundaryStartResult Result = RejectStart(RequestId, ECatFishingBoundaryError::InvalidIdentity);
	const FString StableNetId = CatResolveStableNetId(FisherController);
	if (!RequestId.IsValid() || StableNetId.IsEmpty())
	{
		return Result;
	}

	const FCatFishingAttemptId AttemptId = FindOrCreateAttemptId(StableNetId, RequestId);
	if (const FCatFishingStartContext* CachedAttemptContext = StartContextByAttempt.Find(AttemptId.Value))
	{
		// 重放优先：Start 一旦接受，后续相同请求不再依赖当前 Character/GameState 是否仍可读。
		FCatFishingBoundaryRequestHeader Header;
		Header.SchemaVersion = 1;
		Header.AttemptId = AttemptId;
		Header.RequestId.Value = RequestId;
		Header.PrincipalId.CanonicalValue = StableNetId;
		Header.ExpectedRevision = CachedAttemptContext->RunRevision;

		FCatFishingJournalRequest JournalRequest;
		JournalRequest.OperationKind = ECatFishingBoundaryOperationKind::Start;
		JournalRequest.Header = Header;
		JournalRequest.PayloadHash = FCatFishingBoundaryHash::HashOperation(
			ECatFishingBoundaryOperationKind::Start,
			Header,
			TArray<uint8>());

		Result.Header = Journal.AcceptOrReplay(JournalRequest);
		const FString OperationCacheKey = Result.Header.Operation.ToCacheKey();
		if (const FCatFishingStartContext* CachedContext = StartContextByOperation.Find(OperationCacheKey))
		{
			Result.Context = *CachedContext;
		}
		return Result;
	}

	ACatCharacter* Character = FisherController ? Cast<ACatCharacter>(FisherController->GetPawn()) : nullptr;
	const ACatfishingGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
	if (!Character || !GameState)
	{
		return RejectStart(RequestId, ECatFishingBoundaryError::DependencyUnavailable);
	}

	FString ValidatedFisherId;
	ACatCharacter* ValidatedFisherCharacter = nullptr;
	double FisherStrength = 0.0;
	double FisherFightStamina = 0.0;
	if (!TryGetFightCapability(FisherController, ValidatedFisherId, ValidatedFisherCharacter,
		FisherStrength, FisherFightStamina) || ValidatedFisherId != StableNetId || ValidatedFisherCharacter != Character)
	{
		return RejectStart(RequestId, ECatFishingBoundaryError::PolicyUndecided);
	}

	FCatFishingBoundaryRequestHeader Header;
	Header.SchemaVersion = 1;
	Header.AttemptId = AttemptId;
	Header.RequestId.Value = RequestId;
	Header.PrincipalId.CanonicalValue = StableNetId;
	Header.ExpectedRevision = GameState->GetRunPublicState().Revision;

	FCatFishingJournalRequest JournalRequest;
	JournalRequest.OperationKind = ECatFishingBoundaryOperationKind::Start;
	JournalRequest.Header = Header;
	JournalRequest.PayloadHash = FCatFishingBoundaryHash::HashOperation(
		ECatFishingBoundaryOperationKind::Start,
		Header,
		TArray<uint8>());

	Result.Header = Journal.AcceptOrReplay(JournalRequest);
	const FString OperationCacheKey = Result.Header.Operation.ToCacheKey();
	if (const FCatFishingStartContext* CachedContext = StartContextByOperation.Find(OperationCacheKey))
	{
		Result.Context = *CachedContext;
		return Result;
	}
	if (Result.Header.Disposition != ECatFishingBoundaryDisposition::Pending)
	{
		return Result;
	}

	Result.Context.RequestId = RequestId;
	Result.Context.AttemptId = AttemptId;
	Result.Context.PrincipalId.CanonicalValue = StableNetId;
	Result.Context.RunRevision = Header.ExpectedRevision;
	Result.Context.FisherGuardContainerId = Character->GetPersonalFishGuardId();

	FCatFishingBoundaryResultHeader Committed = Result.Header;
	Committed.Disposition = ECatFishingBoundaryDisposition::Committed;
	Committed.Error = ECatFishingBoundaryError::None;
	if (Journal.CommitResult(Result.Header.Operation, Committed))
	{
		Result.Header = Committed;
		StartContextByAttempt.Add(AttemptId.Value, Result.Context);
		StartContextByOperation.Add(OperationCacheKey, Result.Context);
	}
	return Result;
}

// Cast 流程：先用 Start 上下文确认 Attempt 与身份，再让 Journal/缓存优先处理重放；只有首次 Pending 才读取当前 Run、水
// 域、落点窝料、参战能力和鱼表并冻结规格（含本次咬钩间隔）。
FCatFishingBoundaryCastResult UCatFishingBoundarySubsystem::CastAccepted(
	AController* FisherController,
	const FCatFishingCastAcceptedRequest& Request)
{
	FCatFishingBoundaryCastResult Result = RejectCast(Request, ECatFishingBoundaryError::InvalidAttempt);
	if (!Request.AttemptId.Value.IsValid() || !StartContextByAttempt.Contains(Request.AttemptId.Value))
	{
		return Result;
	}

	const FString StableNetId = CatResolveStableNetId(FisherController);
	if (!Request.RequestId.IsValid() || StableNetId.IsEmpty())
	{
		return RejectCast(Request, ECatFishingBoundaryError::InvalidIdentity);
	}

	const FCatFishingStartContext& StartContext = StartContextByAttempt.FindChecked(Request.AttemptId.Value);
	if (StartContext.PrincipalId.CanonicalValue != StableNetId)
	{
		return RejectCast(Request, ECatFishingBoundaryError::InvalidIdentity);
	}

	FCatFishingBoundaryRequestHeader Header;
	Header.SchemaVersion = 1;
	Header.AttemptId = Request.AttemptId;
	Header.RequestId.Value = Request.RequestId;
	Header.PrincipalId.CanonicalValue = StableNetId;
	Header.ExpectedRevision = Request.ExpectedRunRevision;

	FCatFishingJournalRequest JournalRequest;
	JournalRequest.OperationKind = ECatFishingBoundaryOperationKind::Cast;
	JournalRequest.Header = Header;
	JournalRequest.PayloadHash = FCatFishingBoundaryHash::HashOperation(
		ECatFishingBoundaryOperationKind::Cast,
		Header,
		BuildCastPayload(Request));

	Result.Header = Journal.AcceptOrReplay(JournalRequest);
	const FString OperationCacheKey = Result.Header.Operation.ToCacheKey();
	if (const FCatFishingEncounterSpec* CachedSpec = EncounterSpecByOperation.Find(OperationCacheKey))
	{
		Result.EncounterSpec = *CachedSpec;
		return Result;
	}
	if (Result.Header.Disposition != ECatFishingBoundaryDisposition::Pending)
	{
		return Result;
	}

	// 失败闭环：Cast 已被 Journal 接受后，后续 fail-closed 也必须提交稳定 Rejected，避免重放看到悬空 Pending。
	const auto RejectAcceptedCast = [this, &Result](const ECatFishingBoundaryError Error)
	{
		FCatFishingBoundaryResultHeader Rejected = Result.Header;
		Rejected.Disposition = ECatFishingBoundaryDisposition::Rejected;
		Rejected.Error = Error;
		Journal.CommitResult(Result.Header.Operation, Rejected);
		Result.Header = Rejected;
		return Result;
	};

	ACatCharacter* Character = FisherController ? Cast<ACatCharacter>(FisherController->GetPawn()) : nullptr;
	const ACatfishingGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
	UCatWaterQuerySubsystem* WaterQuery = GetWorld() ? GetWorld()->GetSubsystem<UCatWaterQuerySubsystem>() : nullptr;
	if (!Character || !GameState || !WaterQuery)
	{
		return RejectAcceptedCast(ECatFishingBoundaryError::DependencyUnavailable);
	}

	// D₀ 在这里按 authority 的两个位置现算，而不是由调用方随命令报上来：Boundary 手上已经有服务器认可的 Character，
	// 让它自己量一遍，Fishing 侧就没有机会报一个和落点对不上的距离。落点与猫重合（距离为 0）说明落点算错了，
	// 那时 D/L 模型开局就已经贴脸，没有可遛的余地，所以按 fail-closed 拒掉而不是夹成一个最小值。
	const double CastDistanceMeters = FVector::Dist(Character->GetActorLocation(), Request.CastWorldLocation) / 100.0;
	if (!FMath::IsFinite(CastDistanceMeters) || CastDistanceMeters <= 0.0)
	{
		return RejectAcceptedCast(ECatFishingBoundaryError::InvalidRequest);
	}

	const FCatRunPublicState& RunState = GameState->GetRunPublicState();
	FCatWaterQuery Query;
	Query.WorldLocation = Request.CastWorldLocation;
	Query.RunPhase = RunState.Phase;
	Query.RunRevision = RunState.Revision;
	const FCatWaterQueryResult WaterResult = WaterQuery->QueryWaterRegion(Query);
	if (!WaterResult.bSucceeded)
	{
		return RejectAcceptedCast(WaterResult.Error == ECatWaterQueryError::FishingClosed
			? ECatFishingBoundaryError::InvalidOrder
			: ECatFishingBoundaryError::DependencyUnavailable);
	}

	// 咬钩间隔在 Cast 这一刻按落点窝料冻结：窝点子系统只在 authority World 存在，拿不到就按"这里没有窝"算 Total=0；
	// 公式与数值都在 Environment 配置里，配置没就绪时 fail-closed，不拿一个编造的秒数继续。
	const UCatChumSpotSubsystem* ChumSpots = GetWorld()->GetSubsystem<UCatChumSpotSubsystem>();
	const FCatChumSpotSnapshot ChumSpot = ChumSpots ? ChumSpots->QueryChumSpot(Request.CastWorldLocation) : FCatChumSpotSnapshot();
	const double ChumTotal = ChumSpot.bHasSpot ? ChumSpot.Pool.Total() : 0.0;
	double BiteIntervalSeconds = 0.0;
	if (!GetDefault<UCatEnvironmentSettings>()->TryResolveBiteIntervalSeconds(ChumTotal, BiteIntervalSeconds))
	{
		return RejectAcceptedCast(ECatFishingBoundaryError::PolicyUndecided);
	}

	FString ValidatedFisherId;
	ACatCharacter* ValidatedFisherCharacter = nullptr;
	double FisherStrength = 0.0;
	double FisherFightStamina = 0.0;
	if (!TryGetFightCapability(FisherController, ValidatedFisherId, ValidatedFisherCharacter,
		FisherStrength, FisherFightStamina) || ValidatedFisherId != StableNetId || ValidatedFisherCharacter != Character)
	{
		return RejectAcceptedCast(ECatFishingBoundaryError::PolicyUndecided);
	}

	int32 FightCapablePlayerCount = 0;
	double CombinedFishingStrength = 0.0;
	double CombinedFightStamina = 0.0;
	BuildFightCapabilitySnapshot(FightCapablePlayerCount, CombinedFishingStrength, CombinedFightStamina);
	if (FightCapablePlayerCount <= 0)
	{
		return RejectAcceptedCast(ECatFishingBoundaryError::PolicyUndecided);
	}

	const int32 ServerSelectionEntropy = static_cast<int32>(GetTypeHash(FGuid::NewGuid()));
	FCatFishEncounterSelectionRequest SelectionRequest;
	SelectionRequest.RegionId = WaterResult.Region.RegionId;
	SelectionRequest.TimeOfDay = RunState.Environment.TimeOfDay;
	SelectionRequest.Weather = RunState.Environment.Weather;
	SelectionRequest.ActivePlayerCount = FMath::Clamp(FightCapablePlayerCount, 1, 8);
	SelectionRequest.CombinedFishingStrength = CombinedFishingStrength;
	SelectionRequest.CombinedFightStamina = CombinedFightStamina;
	SelectionRequest.SelectionSeed = ServerSelectionEntropy;
	const FCatFishEncounterSelectionResult Selection = GetDefault<UCatFishCatalogSettings>()->GenerateEncounterSelection(
		SelectionRequest,
		GetDefault<UCatEquipmentSettings>());
	if (!Selection.bSelected || Selection.FishDefinitionId.IsNone() || Selection.ContentHashHex.IsEmpty()
		|| !FMath::IsFinite(Selection.FishWeightKilograms) || Selection.FishWeightKilograms <= 0.0)
	{
		return RejectAcceptedCast(ECatFishingBoundaryError::PolicyUndecided);
	}

	Result.EncounterSpec.AttemptId = Request.AttemptId;
	Result.EncounterSpec.FishDefinitionId = Selection.FishDefinitionId;
	Result.EncounterSpec.DataSchemaVersion = Selection.SchemaVersion;
	Result.EncounterSpec.DataRevision = Selection.DataRevision;
	Result.EncounterSpec.ContentHashHex = Selection.ContentHashHex;
	Result.EncounterSpec.SelectionSeed = Selection.SelectionSeed;
	Result.EncounterSpec.WaterRegion = WaterResult.Region;
	Result.EncounterSpec.FishWeightKilograms = Selection.FishWeightKilograms;
	Result.EncounterSpec.bGiant = Selection.BodyClass == ECatFishBodyClass::Giant;
	Result.EncounterSpec.FishFightStamina = Selection.FishFightStamina;
	Result.EncounterSpec.FightParticipantCount = Selection.FightParticipantCount;
	Result.EncounterSpec.CombinedFishingStrength = Selection.CombinedFishingStrength;
	Result.EncounterSpec.CombinedFightStamina = Selection.CombinedFightStamina;
	Result.EncounterSpec.BiteIntervalSeconds = BiteIntervalSeconds;
	Result.EncounterSpec.InitialFishDistanceMeters = CastDistanceMeters;

	FCatFishingBoundaryResultHeader Committed = Result.Header;
	Committed.Disposition = ECatFishingBoundaryDisposition::Committed;
	Committed.Error = ECatFishingBoundaryError::None;
	if (Journal.CommitResult(Result.Header.Operation, Committed))
	{
		Result.Header = Committed;
		EncounterSpecByOperation.Add(OperationCacheKey, Result.EncounterSpec);
	}
	return Result;
}

// Bait 接收流程：先确认 Attempt 确实来自本 Boundary 已接受 Start，再校验身份与载荷；普通饵只发行语义 Receipt。
// 注意这里有真实副作用：标了 bConsumesSpecialBait 的请求会真的调 Equipment 扣一次特殊饵库存，扣失败就把
// operation 写成稳定 Rejected，不会留下悬空 Pending；扣成功之后发出的 Receipt 带的是 Equipment 返回的真 Revision。
FCatFishingBaitResult UCatFishingBoundarySubsystem::BaitAccepted(const FCatFishingBiteAcceptedRequest& Request)
{
	if (!Request.AttemptId.Value.IsValid() || !StartContextByAttempt.Contains(Request.AttemptId.Value))
	{
		return RejectBait(Request, ECatFishingBoundaryError::InvalidAttempt);
	}
	if (!Request.RequestId.IsValid() || Request.PrincipalId.CanonicalValue.IsEmpty()
		|| Request.BaitDefinitionId.IsNone() || (Request.bConsumesSpecialBait && !Request.BiteToken.IsValid()))
	{
		return RejectBait(Request, ECatFishingBoundaryError::InvalidRequest);
	}
	const FCatFishingStartContext& StartContext = StartContextByAttempt.FindChecked(Request.AttemptId.Value);
	if (StartContext.PrincipalId.CanonicalValue != Request.PrincipalId.CanonicalValue)
	{
		return RejectBait(Request, ECatFishingBoundaryError::InvalidIdentity);
	}

	FCatFishingBoundaryRequestHeader Header;
	Header.SchemaVersion = 1;
	Header.AttemptId = Request.AttemptId;
	Header.RequestId.Value = Request.RequestId;
	Header.PrincipalId = Request.PrincipalId;
	Header.ExpectedRevision = Request.ExpectedRevision;

	FCatFishingJournalRequest JournalRequest;
	JournalRequest.OperationKind = ECatFishingBoundaryOperationKind::Bait;
	JournalRequest.Header = Header;
	JournalRequest.PayloadHash = FCatFishingBoundaryHash::HashOperation(
		ECatFishingBoundaryOperationKind::Bait,
		Header,
		BuildBaitPayload(Request));

	FCatFishingBaitResult Result;
	Result.Header = Journal.AcceptOrReplay(JournalRequest);
	const FString OperationCacheKey = Result.Header.Operation.ToCacheKey();
	if (const FCatFishingBaitResult* Cached = BaitResultByOperation.Find(OperationCacheKey))
	{
		FCatFishingBaitResult Replay = *Cached;
		Replay.Header = Result.Header;
		return Replay;
	}
	if (Result.Header.Disposition != ECatFishingBoundaryDisposition::Pending)
	{
		return Result;
	}
	int64 ReceiptDomainRevision = Request.ExpectedRevision;
	if (Request.bConsumesSpecialBait)
	{
		// 特殊饵扣库存只认服务器现场反查出的角色；request 自带的对象指针一律不信任，找不到人就 fail-closed。
		const AController* OwnerController = CatFindControllerByStableNetId(GetWorld(), Request.PrincipalId.CanonicalValue);
		ACatCharacter* Character = OwnerController ? Cast<ACatCharacter>(OwnerController->GetPawn()) : nullptr;
		UCatEquipmentComponent* Equipment = Character ? Character->GetEquipmentComponent() : nullptr;
		const FCatDomainCommandResult ConsumeResult = Equipment
			? Equipment->ConsumeRunConsumableFromAuthority(Request.RequestId, Request.ExpectedRevision, Request.BaitDefinitionId)
			: FCatDomainCommandResult();
		if (!ConsumeResult.bCommitted)
		{
			FCatFishingBoundaryResultHeader Rejected = Result.Header;
			Rejected.Disposition = ECatFishingBoundaryDisposition::Rejected;
			Rejected.Error = ConsumeResult.Error == ECatDomainCommandError::RevisionConflict
				? ECatFishingBoundaryError::RevisionConflict
				: ConsumeResult.Error == ECatDomainCommandError::CapacityExceeded
					? ECatFishingBoundaryError::CapacityExceeded
					: ConsumeResult.Error == ECatDomainCommandError::InvalidPayload
						? ECatFishingBoundaryError::InvalidRequest
						: ECatFishingBoundaryError::DependencyUnavailable;
			Rejected.Revision = ConsumeResult.Revision;
			Journal.CommitResult(Result.Header.Operation, Rejected);
			Result.Header = Rejected;
			return Result;
		}
		ReceiptDomainRevision = ConsumeResult.Revision;
	}

	FCatFishingBoundaryResultHeader Committed = Result.Header;
	Committed.Disposition = ECatFishingBoundaryDisposition::Committed;
	Committed.Error = ECatFishingBoundaryError::None;
	Committed.Revision = ReceiptDomainRevision;
	if (!Journal.CommitResult(Result.Header.Operation, Committed))
	{
		return RejectBait(Request, ECatFishingBoundaryError::DependencyUnavailable);
	}

	Result.Header = Committed;
	Result.Receipt.ReceiptId.Value = FGuid::NewGuid();
	Result.Receipt.Operation = Committed.Operation;
	Result.Receipt.Kind = ECatFishingReceiptKind::BaitAccepted;
	Result.Receipt.PayloadHash = Committed.PayloadHash;
	Result.Receipt.DomainRevision = ReceiptDomainRevision;
	BaitResultByOperation.Add(OperationCacheKey, Result);
	return Result;
}

#if WITH_DEV_AUTOMATION_TESTS
// 自动化注入流程：只接受格式完整的 StartContext，并直接写入 Attempt 索引；生产路径仍必须通过 Start 创建 Attempt。
void UCatFishingBoundarySubsystem::AddAcceptedStartContextForAutomation(const FCatFishingStartContext& Context)
{
	if (Context.AttemptId.Value.IsValid() && !Context.PrincipalId.CanonicalValue.IsEmpty())
	{
		StartContextByAttempt.Add(Context.AttemptId.Value, Context);
	}
}
#endif

// Fight 接收流程：先确认 Attempt 来自 Start，再按 Start 身份校验 request，最后把 cursor 顺序交给 FightCursorLedger；
// 这里不写 GAS、Equipment 或 Session Snapshot。
FCatFishingFightResult UCatFishingBoundarySubsystem::FightAccepted(const FCatFishingFightExchangeRequest& Request)
{
	if (!Request.AttemptId.Value.IsValid() || !StartContextByAttempt.Contains(Request.AttemptId.Value))
	{
		return RejectFight(Request, ECatFishingBoundaryError::InvalidAttempt);
	}
	if (!Request.RequestId.IsValid() || Request.PrincipalId.CanonicalValue.IsEmpty()
		|| Request.Cursor <= 0
		|| !FMath::IsFinite(Request.FishStaminaCost) || Request.FishStaminaCost <= 0.0
		|| !FMath::IsFinite(Request.ParticipantStaminaCost) || Request.ParticipantStaminaCost <= 0.0
		|| !FMath::IsFinite(Request.RodDurabilityCost) || Request.RodDurabilityCost <= 0.0)
	{
		return RejectFight(Request, ECatFishingBoundaryError::InvalidRequest);
	}
	const FCatFishingStartContext& StartContext = StartContextByAttempt.FindChecked(Request.AttemptId.Value);
	if (StartContext.PrincipalId.CanonicalValue != Request.PrincipalId.CanonicalValue)
	{
		return RejectFight(Request, ECatFishingBoundaryError::InvalidIdentity);
	}

	FCatFishingBoundaryRequestHeader Header;
	Header.SchemaVersion = 1;
	Header.AttemptId = Request.AttemptId;
	Header.RequestId.Value = Request.RequestId;
	Header.PrincipalId = Request.PrincipalId;
	Header.ExpectedRevision = Request.ExpectedRevision;

	FCatFishingFightResult Result;
	Result.Header = FightCursorLedger.AcceptOrReplay(Request, FCatFishingBoundaryHash::HashOperation(
		ECatFishingBoundaryOperationKind::Fight,
		Header,
		BuildFightPayload(Request)));
	return Result;
}

// Fight 资源提交流程：只有属于已接受 Start 的 Attempt 才能提交资源 Receipt；重复提交由 cursor ledger 返回首次 Receipt。
FCatFishingFightResult UCatFishingBoundarySubsystem::CommitFightResourcesApplied(
	const FCatFishingOperationKey& Operation,
	const int64 DomainRevision,
	const bool bRodBroken)
{
	if (!Operation.AttemptId.Value.IsValid() || !StartContextByAttempt.Contains(Operation.AttemptId.Value))
	{
		FCatFishingFightResult Result;
		Result.Header.Disposition = ECatFishingBoundaryDisposition::Rejected;
		Result.Header.Error = ECatFishingBoundaryError::InvalidAttempt;
		Result.Header.Operation = Operation;
		Result.Header.Revision = DomainRevision;
		return Result;
	}
	return FightCursorLedger.CommitResourcesApplied(Operation, DomainRevision, bRodBroken);
}

// Fight 封存流程：Capture 前只允许已知 Attempt 封存最终 cursor；未知 Attempt 不进入 Fight cursor ledger。
FCatFishingBoundaryResultHeader UCatFishingBoundarySubsystem::SealFinalFightCursor(
	const FCatFishingFinalFightCursor& FinalCursor,
	const FGuid RequestId)
{
	if (!FinalCursor.AttemptId.Value.IsValid() || !StartContextByAttempt.Contains(FinalCursor.AttemptId.Value))
	{
		FCatFishingBoundaryResultHeader Result;
		Result.Disposition = ECatFishingBoundaryDisposition::Rejected;
		Result.Error = ECatFishingBoundaryError::InvalidAttempt;
		Result.Operation.AttemptId = FinalCursor.AttemptId;
		Result.Revision = FinalCursor.Cursor;
		return Result;
	}
	return FightCursorLedger.SealFinalCursor(FinalCursor, RequestId);
}

// Close 流程：同时关闭通用 Journal 与 Fight cursor 账；已接受记录仍靠各自缓存优先重放，新 operation 才被拒绝。
void UCatFishingBoundarySubsystem::CloseAttempt(const FCatFishingAttemptId& AttemptId)
{
	Journal.CloseAttempt(AttemptId);
	FightCursorLedger.CloseAttempt(AttemptId);
}

// 参与者谓词流程：先清输出，再按 GameMode active gate、Pawn、Condition、ASC 和两项正有限能力逐层验证。
bool UCatFishingBoundarySubsystem::TryGetFightCapability(const AController* Controller, FString& OutStableNetId,
	ACatCharacter*& OutCharacter, double& OutFishingStrength, double& OutFightStamina)
{
	OutStableNetId.Reset();
	OutCharacter = nullptr;
	OutFishingStrength = 0.0;
	OutFightStamina = 0.0;
	const UWorld* World = Controller ? Controller->GetWorld() : nullptr;
	const ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr;
	const UCatConditionComponent* Conditions = Character ? Character->GetConditionComponent() : nullptr;
	const UAbilitySystemComponent* ASC = Character ? Character->GetAbilitySystemComponent() : nullptr;
	const FString StableNetId = CatResolveStableNetId(Controller);
	if (!World || !GameMode || !Character || !Conditions || !ASC || StableNetId.IsEmpty()
		|| !GameMode->CanAcceptGameplayCommand(Controller) || Conditions->GetSnapshot().bDowned)
	{
		return false;
	}
	const double Strength = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFishingStrengthAttribute());
	const double FightStamina = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFightStaminaAttribute());
	if (!FMath::IsFinite(Strength) || Strength <= 0.0
		|| !FMath::IsFinite(FightStamina) || FightStamina <= 0.0)
	{
		return false;
	}
	OutStableNetId = StableNetId;
	OutCharacter = Character;
	OutFishingStrength = Strength;
	OutFightStamina = FightStamina;
	return true;
}

// 协作快照流程：遍历当前 PlayerController，只累加统一谓词接受的参与者，避免无效玩家扩大 Giant 池。
void UCatFishingBoundarySubsystem::BuildFightCapabilitySnapshot(
	int32& OutParticipantCount,
	double& OutFishingStrength,
	double& OutFightStamina) const
{
	OutParticipantCount = 0;
	OutFishingStrength = 0.0;
	OutFightStamina = 0.0;
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const AController* Controller = It->Get();
		FString StableNetId;
		ACatCharacter* Character = nullptr;
		double Strength = 0.0;
		double FightStamina = 0.0;
		if (TryGetFightCapability(Controller, StableNetId, Character, Strength, FightStamina))
		{
			++OutParticipantCount;
			OutFishingStrength += Strength;
			OutFightStamina += FightStamina;
		}
	}
}

// Attempt 复用流程：同一身份和 Start RequestId 先查缓存，未命中才生成 run-local AttemptId。
FCatFishingAttemptId UCatFishingBoundarySubsystem::FindOrCreateAttemptId(const FString& StableNetId, const FGuid RequestId)
{
	const FString CacheKey = MakeStartRequestCacheKey(StableNetId, RequestId);
	if (const FCatFishingAttemptId* Existing = AttemptByStartRequestKey.Find(CacheKey))
	{
		return *Existing;
	}
	FCatFishingAttemptId AttemptId;
	AttemptId.Value = FGuid::NewGuid();
	AttemptByStartRequestKey.Add(CacheKey, AttemptId);
	return AttemptId;
}

// Key 规则：Start Attempt 复用只按服务器身份和 Start RequestId；OperationKind 在 Journal 层另行隔离。
FString UCatFishingBoundarySubsystem::MakeStartRequestCacheKey(const FString& StableNetId, const FGuid RequestId)
{
	return FString::Printf(TEXT("%s|%s"),
		*StableNetId,
		*RequestId.ToString(EGuidFormats::Digits));
}

// Payload 流程：Bait payload 写入 bait 定义、BiteToken 和特殊饵标记，保证同 RequestId 的 bait 语义漂移会被 Journal 拒绝。
TArray<uint8> UCatFishingBoundarySubsystem::BuildBaitPayload(const FCatFishingBiteAcceptedRequest& Request)
{
	TArray<uint8> Payload;
	FCatFishingBoundaryHash::AppendString(Payload, Request.BaitDefinitionId.ToString());
	FCatFishingBoundaryHash::AppendString(Payload, Request.BiteToken.ToString(EGuidFormats::Digits));
	FCatFishingBoundaryHash::AppendString(Payload, Request.bConsumesSpecialBait ? TEXT("special") : TEXT("ordinary"));
	return Payload;
}

// Payload 流程：Cast payload 写入 Attempt、Cast Request、位置和 Start Revision，保证同 RequestId 的业务漂移会被 Journal 拒绝。
TArray<uint8> UCatFishingBoundarySubsystem::BuildCastPayload(const FCatFishingCastAcceptedRequest& Request)
{
	TArray<uint8> Payload;
	FCatFishingBoundaryHash::AppendString(Payload, Request.AttemptId.Value.ToString(EGuidFormats::Digits));
	FCatFishingBoundaryHash::AppendString(Payload, Request.RequestId.ToString(EGuidFormats::Digits));
	FCatFishingBoundaryHash::AppendString(Payload, FString::Printf(TEXT("%.3f|%.3f|%.3f"),
		Request.CastWorldLocation.X,
		Request.CastWorldLocation.Y,
		Request.CastWorldLocation.Z));
	FCatFishingBoundaryHash::AppendString(Payload, FString::Printf(TEXT("%lld"), static_cast<long long>(Request.ExpectedRunRevision)));
	return Payload;
}

// Payload 流程：Fight payload 写入 cursor 与三类资源消耗量；Attempt、Principal 和 Revision 已由公共 Header 进入 Hash。
TArray<uint8> UCatFishingBoundarySubsystem::BuildFightPayload(const FCatFishingFightExchangeRequest& Request)
{
	TArray<uint8> Payload;
	FCatFishingBoundaryHash::AppendString(Payload, FString::Printf(TEXT("%lld"), static_cast<long long>(Request.Cursor)));
	FCatFishingBoundaryHash::AppendString(Payload, FString::Printf(TEXT("%.6f"), Request.FishStaminaCost));
	FCatFishingBoundaryHash::AppendString(Payload, FString::Printf(TEXT("%.6f"), Request.ParticipantStaminaCost));
	FCatFishingBoundaryHash::AppendString(Payload, FString::Printf(TEXT("%.6f"), Request.RodDurabilityCost));
	return Payload;
}

// 拒绝构造流程：Start 拒绝保留 RequestId 方便调用方关联重试，但不生成 Attempt 或 Operation。
FCatFishingBoundaryStartResult UCatFishingBoundarySubsystem::RejectStart(
	const FGuid RequestId,
	const ECatFishingBoundaryError Error)
{
	FCatFishingBoundaryStartResult Result;
	Result.Context.RequestId = RequestId;
	Result.Header.Disposition = ECatFishingBoundaryDisposition::Rejected;
	Result.Header.Error = Error;
	return Result;
}

// 拒绝构造流程：Cast 拒绝保留 RequestId 和 Attempt 方便诊断，但不冻结鱼种、水域或重量。
FCatFishingBoundaryCastResult UCatFishingBoundarySubsystem::RejectCast(
	const FCatFishingCastAcceptedRequest& Request,
	const ECatFishingBoundaryError Error)
{
	FCatFishingBoundaryCastResult Result;
	Result.EncounterSpec.AttemptId = Request.AttemptId;
	Result.Header.Disposition = ECatFishingBoundaryDisposition::Rejected;
	Result.Header.Error = Error;
	return Result;
}

// 拒绝构造流程：Bait 拒绝只回填 Attempt 与 Revision 供诊断；Receipt 保持无效，明确表示没有扣饵或确认普通饵。
FCatFishingBaitResult UCatFishingBoundarySubsystem::RejectBait(
	const FCatFishingBiteAcceptedRequest& Request,
	const ECatFishingBoundaryError Error)
{
	FCatFishingBaitResult Result;
	Result.Header.Disposition = ECatFishingBoundaryDisposition::Rejected;
	Result.Header.Error = Error;
	Result.Header.Operation.AttemptId = Request.AttemptId;
	Result.Header.Revision = Request.ExpectedRevision;
	return Result;
}

// 拒绝构造流程：Fight 拒绝只回填 Attempt 与 Revision；Receipt 列表保持为空，避免把资源未写入伪装成已提交。
FCatFishingFightResult UCatFishingBoundarySubsystem::RejectFight(
	const FCatFishingFightExchangeRequest& Request,
	const ECatFishingBoundaryError Error)
{
	FCatFishingFightResult Result;
	Result.Header.Disposition = ECatFishingBoundaryDisposition::Rejected;
	Result.Header.Error = Error;
	Result.Header.Operation.AttemptId = Request.AttemptId;
	Result.Header.Revision = Request.ExpectedRevision;
	return Result;
}

