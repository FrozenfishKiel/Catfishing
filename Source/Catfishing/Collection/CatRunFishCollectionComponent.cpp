#include "Collection/CatRunFishCollectionComponent.h"

#include "Engine/World.h"
#include "Framework/Game/CatfishingGameState.h"
#include "GameFramework/PlayerState.h"
#include "Logging/CatLog.h"
#include "Net/UnrealNetwork.h"

UCatRunFishCollectionComponent::UCatRunFishCollectionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UCatRunFishCollectionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, Snapshot);
}

void UCatRunFishCollectionComponent::SynchronizeRunFromAuthority(const FCatRunPublicState& Run)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	if (Snapshot.RunId != Run.Phase.RunId)
	{
		Captures.Reset();
		Snapshot = FCatRunFishCollectionSnapshot();
		Snapshot.RunId = Run.Phase.RunId;
		bRunClosed = false;
		bCanRestore = Run.Phase.RunId.IsValid() && Run.Phase.Phase == ECatRunPhase::NotStarted;
		bAcceptingCaptures = false;
		Publish(TEXT("run_collection_started"));
	}
	const bool bEnding = Run.Phase.Phase == ECatRunPhase::Ending || Run.Phase.Phase == ECatRunPhase::Ended;
	if (bEnding && !bRunClosed)
	{
		bRunClosed = true;
		bCanRestore = false;
		bAcceptingCaptures = false;
		// 房主退出是局中断点，必须先让 Save 采样；自然局末则清空记录，绝不带进新局。
		if (Run.EndReason != ECatRunEndReason::HostExit)
		{
			Captures.Reset();
			Snapshot.Pages.Reset();
			Publish(TEXT("run_collection_cleared"));
		}
		else
		{
			Publish(TEXT("run_collection_suspended"));
		}
	}
	bAcceptingCaptures = Snapshot.RunId.IsValid() && !bRunClosed
		&& (Run.Phase.Phase == ECatRunPhase::DayActive || Run.Phase.Phase == ECatRunPhase::NormalNight
			|| Run.Phase.Phase == ECatRunPhase::SuccessSettlementNight);
	if (bAcceptingCaptures) bCanRestore = false;
}

bool UCatRunFishCollectionComponent::RecordCaptureFromAuthority(const FGuid FishInstanceId,
	const int32  ItemId, const FString& HookerStableNetId)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !bAcceptingCaptures)
	{
		LogRejected(TEXT("CommandsClosedOrNotAuthority"), FishInstanceId);
		return false;
	}
	if (!FishInstanceId.IsValid() || (ItemId == 0) || HookerStableNetId.IsEmpty())
	{
		LogRejected(TEXT("InvalidCapture"), FishInstanceId);
		return false;
	}
	if (const FCatRunFishCollectionCapture* Existing = Captures.FindByPredicate(
		[FishInstanceId](const auto& Record) { return Record.FishInstanceId == FishInstanceId; }))
	{
		const bool bSame = Existing->ItemId == ItemId && Existing->HookerStableNetId == HookerStableNetId;
		if (!bSame) LogRejected(TEXT("CaptureIdentityConflict"), FishInstanceId);
		return bSame;
	}
	FCatRunFishCollectionCapture Capture;
	Capture.FishInstanceId = FishInstanceId;
	Capture.ItemId = ItemId;
	Capture.HookerStableNetId = HookerStableNetId;
	if (const auto* Existing = Captures.FindByPredicate(
		[&HookerStableNetId](const auto& Record) { return Record.HookerStableNetId == HookerStableNetId; }))
	{
		Capture.Pawprint = Existing->Pawprint;
	}
	else
	{
		Capture.Pawprint.RegistrantId = FGuid::NewGuid();
		if (const AGameStateBase* GameState = Cast<AGameStateBase>(GetOwner()))
		{
			for (const APlayerState* Player : GameState->PlayerArray)
			{
				if (Player && Player->GetUniqueId().IsValid() && Player->GetUniqueId()->ToString() == HookerStableNetId)
				{
					Capture.Pawprint.DisplayName = Player->GetPlayerName();
					break;
				}
			}
		}
	}
	Captures.Add(Capture);
	RebuildPages();
	UE_LOG(LogCatRun, Log,
		TEXT("Event=run_collection_capture_recorded World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s RunId=%s FishInstanceId=%s ItemId=%s RegistrantId=%s DisplayNameResolved=%d"),
		*GetNameSafe(GetWorld()), GetWorld()->GetNetMode(), GetOwner()->HasAuthority(), GetOwner()->GetLocalRole(),
		*GetNameSafe(GetOwner()), *Snapshot.RunId.ToString(), *FishInstanceId.ToString(), *FString::FromInt(ItemId),
		*Capture.Pawprint.RegistrantId.ToString(), !Capture.Pawprint.DisplayName.IsEmpty());
	Publish(TEXT("run_collection_published"));
	return true;
}

bool UCatRunFishCollectionComponent::ValidateCaptures(const TArray<FCatRunFishCollectionCapture>& SavedCaptures)
{
	TSet<FGuid> FishIds;
	TMap<FString, FGuid> Registrants;
	TMap<FGuid, FString> Identities;
	for (const auto& Capture : SavedCaptures)
	{
		if (!Capture.FishInstanceId.IsValid() || (Capture.ItemId == 0)
			|| Capture.HookerStableNetId.IsEmpty() || !Capture.Pawprint.RegistrantId.IsValid()
			|| FishIds.Contains(Capture.FishInstanceId)) return false;
		const FGuid* Registrant = Registrants.Find(Capture.HookerStableNetId);
		const FString* Identity = Identities.Find(Capture.Pawprint.RegistrantId);
		if ((Registrant && *Registrant != Capture.Pawprint.RegistrantId)
			|| (Identity && *Identity != Capture.HookerStableNetId)) return false;
		FishIds.Add(Capture.FishInstanceId);
		Registrants.Add(Capture.HookerStableNetId, Capture.Pawprint.RegistrantId);
		Identities.Add(Capture.Pawprint.RegistrantId, Capture.HookerStableNetId);
	}
	return true;
}

bool UCatRunFishCollectionComponent::RestoreCapturesFromAuthority(const TArray<FCatRunFishCollectionCapture>& SavedCaptures)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !bCanRestore || !Captures.IsEmpty()
		|| !ValidateCaptures(SavedCaptures))
	{
		LogRejected(TEXT("InvalidRestoreOrLifecycle"), FGuid());
		return false;
	}
	Captures = SavedCaptures;
	bCanRestore = false;
	RebuildPages();
	Publish(TEXT("run_collection_restored"));
	return true;
}

void UCatRunFishCollectionComponent::RebuildPages()
{
	Snapshot.Pages.Reset();
	for (const auto& Capture : Captures)
	{
		auto* Page = Snapshot.Pages.FindByPredicate(
			[&Capture](const auto& Entry) { return Entry.ItemId == Capture.ItemId; });
		if (!Page)
		{
			Page = &Snapshot.Pages.AddDefaulted_GetRef();
			Page->ItemId = Capture.ItemId;
		}
		if (!Page->Pawprints.ContainsByPredicate([&Capture](const auto& Pawprint)
			{ return Pawprint.RegistrantId == Capture.Pawprint.RegistrantId; }))
		{
			Page->Pawprints.Add(Capture.Pawprint);
		}
	}
}

void UCatRunFishCollectionComponent::Publish(const TCHAR* Event)
{
	++Snapshot.Revision;
	GetOwner()->ForceNetUpdate();
	UE_LOG(LogCatRun, Log,
		TEXT("Event=%s World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s RunId=%s Revision=%lld Pages=%d Captures=%d"),
		Event, *GetNameSafe(GetWorld()), GetWorld()->GetNetMode(), GetOwner()->HasAuthority(), GetOwner()->GetLocalRole(),
		*GetNameSafe(GetOwner()), *Snapshot.RunId.ToString(), Snapshot.Revision, Snapshot.Pages.Num(), Captures.Num());
	OnCollectionChanged.Broadcast();
}

void UCatRunFishCollectionComponent::OnRep_Snapshot()
{
	UE_LOG(LogCatRun, Log,
		TEXT("Event=run_collection_received World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s RunId=%s Revision=%lld Pages=%d"),
		*GetNameSafe(GetWorld()), GetWorld()->GetNetMode(), GetOwner()->HasAuthority(), GetOwner()->GetLocalRole(),
		*GetNameSafe(GetOwner()), *Snapshot.RunId.ToString(), Snapshot.Revision, Snapshot.Pages.Num());
	OnCollectionChanged.Broadcast();
}

void UCatRunFishCollectionComponent::LogRejected(const TCHAR* Reason, const FGuid FishInstanceId) const
{
	UE_LOG(LogCatRun, Warning,
		TEXT("Event=run_collection_rejected World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s RunId=%s FishInstanceId=%s Reason=%s"),
		*GetNameSafe(GetWorld()), GetWorld() ? GetWorld()->GetNetMode() : -1,
		GetOwner() && GetOwner()->HasAuthority(), GetOwner() ? static_cast<int32>(GetOwner()->GetLocalRole()) : -1,
		*GetNameSafe(GetOwner()), *Snapshot.RunId.ToString(), *FishInstanceId.ToString(), Reason);
}
