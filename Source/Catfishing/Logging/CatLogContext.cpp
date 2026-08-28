#include "Logging/CatLogContext.h"

#include "Environment/CatWaterTypes.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Online/CatOnlineSettings.h"

namespace
{
	const TCHAR* NetModeToLogValue(const ENetMode NetMode)
	{
		switch (NetMode)
		{
		case NM_Standalone: return TEXT("Standalone");
		case NM_DedicatedServer: return TEXT("DedicatedServer");
		case NM_ListenServer: return TEXT("ListenServer");
		case NM_Client: return TEXT("Client");
		default: return TEXT("Unknown");
		}
	}
}

FString CatLogContext::BuildStableNetIdValue(const APlayerState* PlayerState)
{
	if (!PlayerState || !PlayerState->GetUniqueId().IsValid())
	{
		return TEXT("Invalid");
	}
	return GetDefault<UCatOnlineSettings>()->StableNetIdExposure == ECatPolicyDecision::Enabled
		? PlayerState->GetUniqueId()->ToString()
		: TEXT("Valid(Redacted)");
}

FString CatLogContext::BuildControllerFields(const AController* Controller)
{
	const APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	const FVector PawnLocation = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
	const FRotator ControlRotation = Controller ? Controller->GetControlRotation() : FRotator::ZeroRotator;
	return FString::Printf(
		TEXT("Controller=%s PlayerState=%s StableNetId=%s IsLocalController=%s NetMode=%s Pawn=%s PawnRole=%s PawnLocation=%s ControlRotation=%s"),
		*GetNameSafe(Controller),
		*GetNameSafe(PlayerState),
		*BuildStableNetIdValue(PlayerState),
		Controller && Controller->IsLocalController() ? TEXT("true") : TEXT("false"),
		Controller ? NetModeToLogValue(Controller->GetNetMode()) : TEXT("Unknown"),
		*GetNameSafe(Pawn),
		Pawn ? *UEnum::GetValueAsString(Pawn->GetLocalRole()) : TEXT("None"),
		*PawnLocation.ToCompactString(),
		*ControlRotation.ToCompactString());
}

FString CatLogContext::BuildWaterSpatialFields(const TCHAR* Prefix, const FVector& QueryLocation,
	const FCatWaterSpatialResult& Result)
{
	const TCHAR* SafePrefix = Prefix && Prefix[0] != TEXT('\0') ? Prefix : TEXT("Water");
	return FString::Printf(
		TEXT("%sQueryLocation=%s %sSucceeded=%s %sError=%s %sContainment=%s %sRegion=%s "
			"%sGeometryRevision=%lld %sVerticalDeltaCm=%.3f %sSignedShoreDistanceCm=%.3f "
			"%sNearestShoreKind=%s %sNearestShore=%s %sSurface=%s"),
		SafePrefix, *QueryLocation.ToCompactString(),
		SafePrefix, Result.bSucceeded ? TEXT("true") : TEXT("false"),
		SafePrefix, *UEnum::GetValueAsString(Result.Error),
		SafePrefix, *UEnum::GetValueAsString(Result.Containment),
		SafePrefix, *Result.WaterRegion.RegionId.ToString(),
		SafePrefix, Result.WaterRegion.GeometryRevision,
		SafePrefix, Result.VerticalDeltaCm,
		SafePrefix, Result.SignedDistanceToShoreCm,
		SafePrefix, *UEnum::GetValueAsString(Result.NearestShoreKind),
		SafePrefix, *Result.NearestShoreWorldPoint.ToCompactString(),
		SafePrefix, *Result.WaterSurfaceWorldPoint.ToCompactString());
}
