#include "Environment/CatChumFieldSubsystem.h"

#include "Engine/World.h"
#include "Environment/CatChumFieldSettings.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "TimerManager.h"

namespace CatChumFieldSubsystemPrivate
{
	static bool IsFiniteVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	static bool GuidLess(const FGuid& Left, const FGuid& Right)
	{
		if (Left.A != Right.A) return Left.A < Right.A;
		if (Left.B != Right.B) return Left.B < Right.B;
		if (Left.C != Right.C) return Left.C < Right.C;
		return Left.D < Right.D;
	}
}

void UCatChumFieldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	EnsureCleanupTimer();
}

void UCatChumFieldSubsystem::OnWorldBeginPlay(UWorld& InWorld)

{
	Super::OnWorldBeginPlay(InWorld);
	EnsureCleanupTimer();
}

void UCatChumFieldSubsystem::EnsureCleanupTimer()
{
	if (IsAuthorityRuntimeReady() && !GetWorld()->GetTimerManager().IsTimerActive(CleanupTimerHandle))
	{
		const double Interval = GetDefault<UCatChumFieldSettings>()->ExpiredCleanupIntervalSeconds;
		GetWorld()->GetTimerManager().SetTimer(CleanupTimerHandle, this,
			&UCatChumFieldSubsystem::HandleCleanupTimer, static_cast<float>(Interval), true);
	}
}

void UCatChumFieldSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CleanupTimerHandle);
	}
	PendingByToken.Reset();
	PendingTokenByRequest.Reset();
	FieldsById.Reset();
	FieldIdsByRegion.Reset();
	FieldSetRevisionByRegion.Reset();
	BudgetByRegion.Reset();
	TerminalByIdentityAndRequest.Reset();
	Super::Deinitialize();
}

bool UCatChumFieldSubsystem::IsAuthorityRuntimeReady() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client && GetDefault<UCatChumFieldSettings>()->IsRuntimeReady();
}

FCatPrepareChumFieldResult UCatChumFieldSubsystem::MakePrepareError(const ECatChumFieldError Error)
{
	FCatPrepareChumFieldResult Result;
	Result.Error = Error;
	return Result;
}

ECatChumFieldError UCatChumFieldSubsystem::MapWaterError(const ECatWaterQueryError Error)
{
	return Error == ECatWaterQueryError::StaleGeometry ? ECatChumFieldError::StaleGeometry
		: ECatChumFieldError::InvalidWaterTarget;
}

FCatPrepareChumFieldResult UCatChumFieldSubsystem::PrepareField(const FCatPrepareChumFieldRequest& Request)
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client)
	{
		return MakePrepareError(ECatChumFieldError::DependencyUnavailable);
	}
	if (!GetDefault<UCatChumFieldSettings>()->IsRuntimeReady())
	{
		return MakePrepareError(ECatChumFieldError::FeatureDisabled);
	}
	if (Request.StableNetId.IsEmpty())
	{
		return MakePrepareError(ECatChumFieldError::InvalidIdentity);
	}
	if (!Request.Command.RequestId.IsValid() || !Request.Command.ExpectedWaterRegionHandle.IsValid()
		|| Request.Command.ChumDefinitionId.IsNone() || Request.Command.Quantity <= 0
		|| !FMath::IsFinite(Request.ServerTime)
		|| !CatChumFieldSubsystemPrivate::IsFiniteVector(Request.ServerCorrectedCenter))
	{
		return MakePrepareError(ECatChumFieldError::InvalidPayload);
	}
	CleanupExpiredFields(Request.ServerTime);
	const FCatChumRequestKey RequestKey{Request.StableNetId, Request.Command.RequestId};
	if (TerminalByIdentityAndRequest.Contains(RequestKey))
	{
		return MakePrepareError(ECatChumFieldError::AlreadyResolved);
	}
	if (const FGuid* ExistingTokenId = PendingTokenByRequest.Find(RequestKey))
	{
		if (const FCatPendingChumField* Existing = PendingByToken.Find(*ExistingTokenId))
		{
			FCatPrepareChumFieldResult Replay;
			Replay.bPrepared = true; Replay.Error = ECatChumFieldError::None;
			Replay.CommitToken.Value = *ExistingTokenId; Replay.FieldId = Existing->State.FieldId;
			Replay.WaterRegion = Existing->State.WaterRegion; Replay.CorrectedCenter = Existing->State.CenterWorldPoint;
			Replay.StartServerTime = Existing->State.StartServerTime; Replay.ExpireServerTime = Existing->State.ExpireServerTime;
			return Replay;
		}
	}

	const UCatWaterQuerySubsystem* WaterQuery = GetWorld()->GetSubsystem<UCatWaterQuerySubsystem>();
	if (!WaterQuery)
	{
		return MakePrepareError(ECatChumFieldError::DependencyUnavailable);
	}
	const FCatWaterSpatialResult Water = WaterQuery->QueryWaterPoint(
		Request.ServerCorrectedCenter, Request.Command.ExpectedWaterRegionHandle);
	if (!Water.bSucceeded || Water.Containment == ECatWaterContainment::Outside)
	{
		return MakePrepareError(MapWaterError(Water.Error));
	}
	FCatChumRuntimeInfluence RuntimeInfluence;
	if (!Request.Influence.BuildRuntimeInfluence(Request.Command.Quantity, RuntimeInfluence))
	{
		return MakePrepareError(ECatChumFieldError::InvalidPayload);
	}
	const double RawContribution = RuntimeInfluence.BaseContribution.Fishy
		+ RuntimeInfluence.BaseContribution.Fragrant + RuntimeInfluence.BaseContribution.Fermented;
	if (!FMath::IsFinite(RawContribution) || RawContribution <= 0.0)
	{
		return MakePrepareError(ECatChumFieldError::InvalidPayload);
	}

	const UCatChumFieldSettings* Settings = GetDefault<UCatChumFieldSettings>();
	FCatChumBudgetState& Budget = BudgetByRegion.FindOrAdd(Water.WaterRegion.RegionId);
	if (Budget.ActiveCount + Budget.PendingCount >= Settings->MaxActiveFieldsPerRegion
		|| Budget.ReservedRawContribution + RawContribution > Settings->MaxRawContributionPerRegion)
	{
		return MakePrepareError(ECatChumFieldError::FieldCapacityExceeded);
	}

	FGuid TokenId = FGuid::NewGuid();
	FGuid FieldId = FGuid::NewGuid();
	while (PendingByToken.Contains(TokenId)) TokenId = FGuid::NewGuid();
	while (FieldsById.Contains(FieldId)) FieldId = FGuid::NewGuid();
	FCatPendingChumField Pending;
	Pending.RequestKey = RequestKey;
	Pending.RawContribution = RawContribution;
	Pending.State.FieldId = FieldId;
	Pending.State.WaterRegion = Water.WaterRegion;
	Pending.State.ChumDefinitionId = Request.Command.ChumDefinitionId;
	Pending.State.CenterWorldPoint = Water.WaterSurfaceWorldPoint;
	Pending.State.Influence = MoveTemp(RuntimeInfluence);
	Pending.State.StartServerTime = Request.ServerTime;
	Pending.State.ExpireServerTime = Request.ServerTime + Pending.State.Influence.DurationSeconds;
	Pending.State.Source = Request.Source;
	Pending.State.OwnerStableNetId = Request.StableNetId;
	PendingByToken.Add(TokenId, Pending);
	PendingTokenByRequest.Add(RequestKey, TokenId);
	++Budget.PendingCount;
	Budget.ReservedRawContribution += RawContribution;

	FCatPrepareChumFieldResult Result;
	Result.bPrepared = true; Result.Error = ECatChumFieldError::None; Result.CommitToken.Value = TokenId;
	Result.FieldId = FieldId; Result.WaterRegion = Water.WaterRegion; Result.CorrectedCenter = Water.WaterSurfaceWorldPoint;
	Result.StartServerTime = Request.ServerTime; Result.ExpireServerTime = Pending.State.ExpireServerTime;
	return Result;
}

FCatPlaceChumResult UCatChumFieldSubsystem::ActivatePreparedFieldDeferred(
	const FCatChumFieldCommitToken Token, const int64 EquipmentRevision)
{
	FCatPlaceChumResult Result;
	Result.EquipmentRevision = EquipmentRevision;
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client || !Token.IsValid())
	{
		Result.Error = ECatChumFieldError::DependencyUnavailable;
		return Result;
	}
	FCatPendingChumField* Pending = PendingByToken.Find(Token.Value);
	if (!Pending)
	{
		Result.Error = ECatChumFieldError::AlreadyResolved;
		return Result;
	}
	const FCatPendingChumField Frozen = *Pending;
	FCatChumBudgetState& Budget = BudgetByRegion.FindOrAdd(Frozen.State.WaterRegion.RegionId);
	--Budget.PendingCount;
	++Budget.ActiveCount;
	FieldsById.Add(Frozen.State.FieldId, Frozen.State);
	FieldIdsByRegion.FindOrAdd(Frozen.State.WaterRegion.RegionId).Add(Frozen.State.FieldId);
	PendingTokenByRequest.Remove(Frozen.RequestKey);
	PendingByToken.Remove(Token.Value);
	const int64 Revision = ++FieldSetRevisionByRegion.FindOrAdd(Frozen.State.WaterRegion.RegionId);

	Result.RequestId = Frozen.RequestKey.RequestId;
	Result.bCommitted = true; Result.Error = ECatChumFieldError::None; Result.FieldId = Frozen.State.FieldId;
	Result.WaterRegion = Frozen.State.WaterRegion; Result.ServerCorrectedCenter = Frozen.State.CenterWorldPoint;
	Result.StartServerTime = Frozen.State.StartServerTime; Result.ExpireServerTime = Frozen.State.ExpireServerTime;
	Result.EquipmentRevision = EquipmentRevision; Result.ChumFieldSetRevision = Revision;
	return Result;
}

void UCatChumFieldSubsystem::PublishActivatedField(const FGuid FieldId)
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client) return;
	FCatChumFieldState* Field = FieldsById.Find(FieldId);
	if (!Field || Field->bPublicationFlushed) return;
	bool bTerminalStored = false;
	for (const TPair<FCatChumRequestKey, FCatPlaceChumResult>& Pair : TerminalByIdentityAndRequest)
	{
		if (Pair.Key.StableNetId == Field->OwnerStableNetId && Pair.Value.bCommitted && Pair.Value.FieldId == FieldId)
		{
			bTerminalStored = true;
			break;
		}
	}
	if (!bTerminalStored) return;
	Field->bPublicationFlushed = true;
	OnFieldActivated.Broadcast(FieldId);
}

void UCatChumFieldSubsystem::AbortPreparedField(const FCatChumFieldCommitToken Token)
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client || !Token.IsValid()) return;
	const FCatPendingChumField* Pending = PendingByToken.Find(Token.Value);
	if (!Pending) return;
	FCatChumBudgetState& Budget = BudgetByRegion.FindOrAdd(Pending->State.WaterRegion.RegionId);
	Budget.PendingCount = FMath::Max(0, Budget.PendingCount - 1);
	Budget.ReservedRawContribution = FMath::Max(0.0, Budget.ReservedRawContribution - Pending->RawContribution);
	PendingTokenByRequest.Remove(Pending->RequestKey);
	PendingByToken.Remove(Token.Value);
}

bool UCatChumFieldSubsystem::TryGetTerminalResult(const FString& StableNetId, const FGuid RequestId,
	FCatPlaceChumResult& OutResult) const
{
	OutResult = FCatPlaceChumResult();
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client || StableNetId.IsEmpty() || !RequestId.IsValid()) return false;
	if (const FCatPlaceChumResult* Found = TerminalByIdentityAndRequest.Find({StableNetId, RequestId}))
	{
		OutResult = *Found;
		return true;
	}
	return false;
}

void UCatChumFieldSubsystem::StoreTerminalResult(const FString& StableNetId, const FCatPlaceChumResult& Result)
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client || StableNetId.IsEmpty() || !Result.RequestId.IsValid()) return;
	const FCatChumRequestKey Key{StableNetId, Result.RequestId};
	if (!TerminalByIdentityAndRequest.Contains(Key))
	{
		TerminalByIdentityAndRequest.Add(Key, Result);
	}
}

FCatChumSample UCatChumFieldSubsystem::SampleChumAtPoint(const FVector& WorldPoint,
	const FCatWaterRegionHandle& ExpectedHandle, const double ServerTime) const
{
	FCatChumSample Result;
	Result.SampleServerTime = ServerTime;
	if (!IsAuthorityRuntimeReady() || !FMath::IsFinite(ServerTime)) return Result;
	const UCatWaterQuerySubsystem* WaterQuery = GetWorld()->GetSubsystem<UCatWaterQuerySubsystem>();
	if (!WaterQuery) return Result;
	const FCatWaterSpatialResult Water = WaterQuery->QueryWaterPoint(WorldPoint, ExpectedHandle);
	if (!Water.bSucceeded || Water.Containment == ECatWaterContainment::Outside)
	{
		Result.Error = MapWaterError(Water.Error);
		return Result;
	}
	Result.bSucceeded = true; Result.Error = ECatChumFieldError::None; Result.WaterRegion = Water.WaterRegion;
	Result.ChumFieldSetRevision = FieldSetRevisionByRegion.FindRef(Water.WaterRegion.RegionId);
	if (const TArray<FGuid>* RegionFields = FieldIdsByRegion.Find(Water.WaterRegion.RegionId))
	{
		Result.ContributingFieldIds = *RegionFields;
		Result.ContributingFieldIds.RemoveAll([this, &WorldPoint, ServerTime](const FGuid& Id)
		{
			const FCatChumFieldState* Field = FieldsById.Find(Id);
			if (!Field || ServerTime < Field->StartServerTime || ServerTime >= Field->ExpireServerTime) return true;
			return FVector2D::Distance(FVector2D(Field->CenterWorldPoint), FVector2D(WorldPoint))
				> Field->Influence.RadiusCentimeters;
		});
		Result.ContributingFieldIds.Sort(CatChumFieldSubsystemPrivate::GuidLess);
		for (const FGuid& Id : Result.ContributingFieldIds)
		{
			const FCatChumFieldState& Field = FieldsById.FindChecked(Id);
			const double Distance = FVector2D::Distance(FVector2D(Field.CenterWorldPoint), FVector2D(WorldPoint))
				/ Field.Influence.RadiusCentimeters;
			const double Time = (ServerTime - Field.StartServerTime) / (Field.ExpireServerTime - Field.StartServerTime);
			const double Weight = Field.Influence.DistanceFalloff.Evaluate(Distance)
				* Field.Influence.TimeFalloff.Evaluate(Time);
			Result.EffectiveChumVector.Accumulate(Field.Influence.BaseContribution.ScaledBy(Weight));
		}
	}
	Result.ContributingFieldCount = Result.ContributingFieldIds.Num();
	return Result;
}

int32 UCatChumFieldSubsystem::CleanupExpiredFields(const double ServerTime)
{
	if (!GetWorld() || GetWorld()->GetNetMode() == NM_Client || !FMath::IsFinite(ServerTime)) return 0;
	TArray<FGuid> Expired;
	TSet<FName> AffectedRegions;
	for (const TPair<FGuid, FCatChumFieldState>& Pair : FieldsById)
	{
		if (ServerTime >= Pair.Value.ExpireServerTime)
		{
			Expired.Add(Pair.Key);
			AffectedRegions.Add(Pair.Value.WaterRegion.RegionId);
		}
	}
	Expired.Sort(CatChumFieldSubsystemPrivate::GuidLess);
	for (const FGuid& Id : Expired)
	{
		const FCatChumFieldState Field = FieldsById.FindChecked(Id);
		if (TArray<FGuid>* RegionIds = FieldIdsByRegion.Find(Field.WaterRegion.RegionId))
		{
			RegionIds->Remove(Id);
			if (RegionIds->IsEmpty()) FieldIdsByRegion.Remove(Field.WaterRegion.RegionId);
		}
		FCatChumBudgetState& Budget = BudgetByRegion.FindOrAdd(Field.WaterRegion.RegionId);
		Budget.ActiveCount = FMath::Max(0, Budget.ActiveCount - 1);
		const double Raw = Field.Influence.BaseContribution.Fishy + Field.Influence.BaseContribution.Fragrant
			+ Field.Influence.BaseContribution.Fermented;
		Budget.ReservedRawContribution = FMath::Max(0.0, Budget.ReservedRawContribution - Raw);
		FieldsById.Remove(Id);
	}
	for (const FName RegionId : AffectedRegions)
	{
		++FieldSetRevisionByRegion.FindOrAdd(RegionId);
	}
	for (const FGuid& Id : Expired) OnFieldRemoved.Broadcast(Id);
	return Expired.Num();
}

void UCatChumFieldSubsystem::HandleCleanupTimer()
{
	if (const UWorld* World = GetWorld()) CleanupExpiredFields(World->GetTimeSeconds());
}
