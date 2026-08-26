#include "Fishing/CatFishingService.h"

#include "Character/CatCharacter.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "Logging/CatLog.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/CatSurvivalAttributeSet.h"
#include "Condition/CatConditionComponent.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Engine/World.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Fishing/Actors/CatFishEncounterActor.h"
#include "Fishing/Actors/CatFishingHookActor.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/Presentation/CatFishingPresentationSettings.h"
#include "Social/CatSocialService.h"
#include "GameFramework/PlayerState.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

// 创建条件流程：仅 authority Game World 持有会话索引；客户端不能创建平行 StateTree。
bool UCatFishingService::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

// 反初始化流程：先关闭并终止所有会话，再清弱映射；随后交还 WorldSubsystem 生命周期。
void UCatFishingService::Deinitialize()
{
	CloseCommandsAndTerminateAll();
	Sessions.Reset();
	SessionFisherById.Reset();
	ActiveSessionByFisher.Reset();
	BeginCastTerminalCache.Reset();
	BeginCastInProgress.Reset();
	DeployedRodByPlayerState.Reset();
	Super::Deinitialize();
}

FCatBeginCastResult UCatFishingService::BeginCast(AController* FisherController,
	const FCatBeginCastCommand& Command)
{
	FCatBeginCastResult Result;
	Result.Command.CommandType = ECatFishingCommandType::BeginCast;
	Result.Command.RequestId = Command.RequestId;
	const FString StableNetId = ResolveStableNetId(FisherController);
	if (!Command.RequestId.IsValid() || StableNetId.IsEmpty())
	{
		Result.Command.Error = ECatFishingCommandError::InvalidIdentity;
		return Result;
	}
	const FString Key = FString::Printf(TEXT("%s|BeginCast|%s"), *StableNetId,
		*Command.RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	if (const FCatBeginCastResult* Cached = BeginCastTerminalCache.Find(Key)) return *Cached;
	if (BeginCastInProgress.Contains(Key))
	{
		Result.Command.Error = ECatFishingCommandError::DependencyUnavailable;
		return Result;
	}
	BeginCastInProgress.Add(Key);
	const auto Finish = [this, &Key](const FCatBeginCastResult& Candidate)
	{
		BeginCastInProgress.Remove(Key);
		BeginCastTerminalCache.FindOrAdd(Key, Candidate);
		return BeginCastTerminalCache.FindChecked(Key);
	};
	UWorld* World = GetWorld();
	const ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	if (!bCommandsOpen || !GameMode || !GameMode->CanAcceptFishingCommand(FisherController)
		|| !CanControllerStartFishingAction(FisherController))
	{
		Result.Command.Error = ECatFishingCommandError::CommandsClosed;
		return Finish(Result);
	}
	ACatCharacter* Character = FisherController ? Cast<ACatCharacter>(FisherController->GetPawn()) : nullptr;
	APlayerState* PlayerState = FisherController ? FisherController->PlayerState : nullptr;
	UCatEquipmentComponent* Equipment = Character ? Character->GetEquipmentComponent() : nullptr;
	// 抛竿按"正在操作的竿"解析而非"自己拥有的竿"：多人可用别人的竿抛竿（饵料/磨损记在抛竿者自己的装备上）。
	ACatFishingRodActor* Rod = FindRodOperatedBy(PlayerState);
	if (!World || !Character || !PlayerState || !Equipment || !Rod)
	{
		Result.Command.Error = ECatFishingCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	const FCatFishingRodPresentationState& RodState = Rod->GetPresentationState();
	const FCatEquipmentLoadoutSnapshot& Loadout = Equipment->GetSnapshot();
	if (Command.RodActorId != RodState.RodActorId || Command.ExpectedRodActorRevision != RodState.RodActorRevision)
	{
		Result.Command.Error = ECatFishingCommandError::RodActorRevisionConflict;
		return Finish(Result);
	}
	if (!RodState.bDeployed || RodState.bBroken || RodState.OperatorPlayerState != PlayerState)
	{
		Result.Command.Error = RodState.bBroken ? ECatFishingCommandError::RodBroken : ECatFishingCommandError::RodOccupied;
		return Finish(Result);
	}
	if (Command.ExpectedEquipmentRevision != Loadout.Revision)
	{
		Result.Command.Error = ECatFishingCommandError::EquipmentRevisionConflict;
		return Finish(Result);
	}
	if (Loadout.RodDefinitionId != RodState.RodDefinitionId || !Command.ExpectedWaterRegionHandle.IsValid()
		|| Command.ClientCandidateWorldPoint.ContainsNaN())
	{
		Result.Command.Error = ECatFishingCommandError::InvalidPayload;
		return Finish(Result);
	}
	const UCatEquipmentSettings* EquipmentSettings = GetDefault<UCatEquipmentSettings>();
	const UCatEquipmentDefinition* RodDefinition = EquipmentSettings->FindRuntimeDefinition(Loadout.RodDefinitionId);
	const UCatEquipmentDefinition* FloatDefinition = EquipmentSettings->FindRuntimeDefinition(Loadout.FloatDefinitionId);
	const UCatEquipmentDefinition* BaitDefinition = EquipmentSettings->FindRuntimeDefinition(Loadout.BaitDefinitionId);
	if (!RodDefinition || RodDefinition->Kind != ECatEquipmentKind::Rod || !FloatDefinition
		|| FloatDefinition->Kind != ECatEquipmentKind::Float || !BaitDefinition
		|| BaitDefinition->Kind != ECatEquipmentKind::Bait)
	{
		Result.Command.Error = ECatFishingCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	UCatWaterQuerySubsystem* WaterQuery = World->GetSubsystem<UCatWaterQuerySubsystem>();
	const FCatWaterSpatialResult Water = WaterQuery
		? WaterQuery->ResolveCandidatePointToWater(Command.ClientCandidateWorldPoint, Command.ExpectedWaterRegionHandle)
		: FCatWaterSpatialResult{};
	if (!Water.bSucceeded || Water.Containment == ECatWaterContainment::Outside)
	{
		Result.Command.Error = Water.Error == ECatWaterQueryError::AmbiguousRegion
			? ECatFishingCommandError::AmbiguousWater : ECatFishingCommandError::InvalidWaterTarget;
		return Finish(Result);
	}
	const FVector ViewOrigin = Character->GetPawnViewLocation();
	const FVector ToLanding = Water.WaterSurfaceWorldPoint - ViewOrigin;
	const double MaxRange = FMath::Min(RodDefinition->MaximumLineLengthCentimeters,
		FloatDefinition->MaximumCastDistanceCentimeters);
	if (!FMath::IsFinite(MaxRange) || MaxRange <= 0.0 || ToLanding.Length() > MaxRange
		|| ToLanding.IsNearlyZero() || FVector::DotProduct(FisherController->GetControlRotation().Vector(),
			ToLanding.GetSafeNormal()) < 0.5)
	{
		Result.Command.Error = ECatFishingCommandError::CastOutOfRange;
		return Finish(Result);
	}
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(CatBeginCastLineOfSight), true);
	TraceParams.AddIgnoredActor(Character);
	TraceParams.AddIgnoredActor(Rod);
	FHitResult SightHit;
	if (World->LineTraceSingleByChannel(SightHit, ViewOrigin, Water.WaterSurfaceWorldPoint,
		ECC_Visibility, TraceParams))
	{
		Result.Command.Error = ECatFishingCommandError::InvalidWaterTarget;
		return Finish(Result);
	}
	FGuid SessionId = FGuid::NewGuid();
	FGuid CastAttemptId = FGuid::NewGuid();
	while (CastAttemptId == SessionId) CastAttemptId = FGuid::NewGuid();
	const FCatFishingUseReservationResult Reserved = Equipment->BeginFishingUse(SessionId,
		Loadout.RodDefinitionId, Loadout.BaitDefinitionId, Loadout.FloatDefinitionId, Loadout.Revision);
	if (Reserved.Error != ECatDomainCommandError::None)
	{
		Result.Command.Error = Reserved.Error == ECatDomainCommandError::RevisionConflict
			? ECatFishingCommandError::EquipmentRevisionConflict : ECatFishingCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	const UCatFishingPresentationSettings* Presentation = GetDefault<UCatFishingPresentationSettings>();
	UClass* HookClass = Presentation ? Presentation->HookActorClass.LoadSynchronous() : nullptr;
	if (!HookClass || !HookClass->IsChildOf(ACatFishingHookActor::StaticClass()))
	{
		Equipment->ReleaseFishingUse(SessionId);
		Result.Command.Error = ECatFishingCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	const FTransform HookTransform(Rod->GetRodTipWorldTransform().GetRotation(),
		Rod->GetRodTipWorldTransform().GetLocation());
	ACatFishingHookActor* Hook = World->SpawnActorDeferred<ACatFishingHookActor>(HookClass, HookTransform,
		Rod, Character, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Hook)
	{
		Equipment->ReleaseFishingUse(SessionId);
		Result.Command.Error = ECatFishingCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	Hook->DeferInitialPresentationFromAuthority();
	if (!Hook->InitializeAuthoritativeIdentity(SessionId, CastAttemptId))
	{
		Hook->Destroy();
		Equipment->ReleaseFishingUse(SessionId);
		Result.Command.Error = ECatFishingCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	Hook->FinishSpawning(HookTransform);
	ACatFishingSession* Session = World->SpawnActorDeferred<ACatFishingSession>(ACatFishingSession::StaticClass(),
		Character->GetActorTransform(), FisherController, Character, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	FCatFishingAttemptSnapshot Attempt;
	Attempt.RequestId = Command.RequestId;
	Attempt.FishingSessionId = SessionId;
	Attempt.CastAttemptId = CastAttemptId;
	Attempt.FisherPlayerState = PlayerState;
	Attempt.RodActor = Rod;
	Attempt.RodDefinitionId = Loadout.RodDefinitionId;
	Attempt.FloatDefinitionId = Loadout.FloatDefinitionId;
	Attempt.BaitDefinitionId = Loadout.BaitDefinitionId;
	Attempt.EquipmentReservationRevision = Reserved.EquipmentRevision;
	Attempt.RodActorRevision = RodState.RodActorRevision;
	Attempt.ServerCorrectedLandingWorldPoint = Water.WaterSurfaceWorldPoint;
	Attempt.WaterRegion = Water.WaterRegion;
	Attempt.ServerRandomSeed = static_cast<uint64>(GetTypeHash(FGuid::NewGuid()));
	if (!Session || !Session->PrepareSessionFromAuthority(Attempt, FisherController, Character, Hook))
	{
		if (Session) Session->Destroy();
		Hook->Destroy();
		Equipment->ReleaseFishingUse(SessionId);
		Result.Command.Error = ECatFishingCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	Session->FinishSpawning(Character->GetActorTransform());
	if (!Session->StartPreparedSessionLogicFromAuthority())
	{
		Session->AbortPreparedSessionFromAuthority();
		Hook->Destroy();
		Equipment->ReleaseFishingUse(SessionId);
		Result.Command.Error = ECatFishingCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	Sessions.Add(SessionId, Session);
	SessionFisherById.Add(SessionId, StableNetId);
	ActiveSessionByFisher.Add(StableNetId, SessionId);
	Result.Command.bCommitted = true;
	Result.Command.Error = ECatFishingCommandError::None;
	Result.Command.FishingSessionId = SessionId;
	Result.Command.CastAttemptId = CastAttemptId;
	Result.Command.RodActorId = RodState.RodActorId;
	Result.Command.RodActorRevision = RodState.RodActorRevision;
	Result.Command.EquipmentRevision = Reserved.EquipmentRevision;
	Result.WaterRegion = Water.WaterRegion;
	Result.ServerCorrectedLandingWorldPoint = Water.WaterSurfaceWorldPoint;
	const FCatBeginCastResult Frozen = Finish(Result);
	Session->PublishPreparedSessionFromAuthority();
	Hook->PublishInitialPresentationFromAuthority();
	Hook->BeginAuthoritativeFlight(ToLanding.GetSafeNormal() * FMath::Min(MaxRange, 1500.0),
		Water.WaterSurfaceWorldPoint);
	return Frozen;
}

FCatFishingCommandResult UCatFishingService::PlaceRod(AController* Controller, const FCatPlaceRodCommand& Command)
{
	FCatFishingCommandResult Result;
	Result.CommandType = ECatFishingCommandType::PlaceRod;
	Result.RequestId = Command.RequestId;
	UWorld* World = GetWorld();
	ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr;
	APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
	UCatEquipmentComponent* Equipment = Character ? Character->GetEquipmentComponent() : nullptr;
	if (!Command.RequestId.IsValid() || ResolveStableNetId(Controller).IsEmpty())
	{
		Result.Error = ECatFishingCommandError::InvalidIdentity;
		return Result;
	}
	const ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	if (!bCommandsOpen || !GameMode || !GameMode->CanAcceptFishingCommand(Controller)
		|| !CanControllerStartFishingAction(Controller))
	{
		Result.Error = ECatFishingCommandError::CommandsClosed;
		return Result;
	}
	if (!World || !Character || !PlayerState || !Equipment)
	{
		Result.Error = ECatFishingCommandError::DependencyUnavailable;
		return Result;
	}
	if (FindDeployedRod(PlayerState))
	{
		Result.Error = ECatFishingCommandError::ActiveSessionExists;
		return Result;
	}
	const FCatEquipmentLoadoutSnapshot& Loadout = Equipment->GetSnapshot();
	if (Loadout.Revision != Command.ExpectedEquipmentRevision)
	{
		Result.Error = ECatFishingCommandError::EquipmentRevisionConflict;
		return Result;
	}
	const UCatEquipmentDefinition* RodDefinition = GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(Loadout.RodDefinitionId);
	const UCatFishingPresentationSettings* Presentation = GetDefault<UCatFishingPresentationSettings>();
	UClass* RodClass = Presentation ? Presentation->RodActorClass.LoadSynchronous() : nullptr;
	if (!RodDefinition || RodDefinition->Kind != ECatEquipmentKind::Rod || !RodClass
		|| !RodClass->IsChildOf(ACatFishingRodActor::StaticClass()))
	{
		Result.Error = ECatFishingCommandError::DependencyUnavailable;
		return Result;
	}
	const FVector Candidate = Character->GetActorLocation() + Character->GetActorForwardVector() * 150.0;
	FHitResult GroundHit;
	FCollisionQueryParams GroundParams(SCENE_QUERY_STAT(CatPlaceRodGround), false, Character);
	if (!World->LineTraceSingleByChannel(GroundHit, Candidate + FVector(0, 0, 100),
		Candidate - FVector(0, 0, 250), ECC_Visibility, GroundParams) || GroundHit.ImpactNormal.Z < 0.7)
	{
		Result.Error = ECatFishingCommandError::InvalidPayload;
		return Result;
	}
	// 放竿点规则：必须在水域样条线外侧（岸上），且离岸线不超过配置带宽（默认 4m）；找不到任何水域也拒绝。
	{
		UCatWaterQuerySubsystem* WaterQuery = World->GetSubsystem<UCatWaterQuerySubsystem>();
		const FCatWaterSpatialResult Shore = WaterQuery
			? WaterQuery->QueryNearestShoreForPreview(GroundHit.ImpactPoint) : FCatWaterSpatialResult{};
		const double MaxShoreDistance = GetDefault<UCatFishingSettings>()->RodPlacementMaxShoreDistanceCentimeters;
		const bool bOnBank = Shore.bSucceeded && Shore.Containment == ECatWaterContainment::Outside;
		const bool bWithinBand = MaxShoreDistance <= 0.0
			|| FMath::Abs(Shore.SignedDistanceToShoreCm) <= MaxShoreDistance;
		if (!bOnBank || !bWithinBand)
		{
			Result.Error = ECatFishingCommandError::InvalidWaterTarget;
			return Result;
		}
	}
	// 放竿合法性由上面的地面/岸带规则决定；表现 Mesh 的碰撞不能否决生成，故 AlwaysSpawn。
	const FTransform SpawnTransform(Character->GetActorRotation(), GroundHit.ImpactPoint);
	ACatFishingRodActor* Rod = World->SpawnActorDeferred<ACatFishingRodActor>(RodClass, SpawnTransform,
		Controller, Character, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	const FGuid RodActorId = FGuid::NewGuid();
	if (!Rod || !Rod->ConfigureCanonicalAnchorsFromAuthority(RodDefinition->RodTipLocalTransform,
		RodDefinition->StandLocalTransform, RodDefinition->GripLocalTransform)
		|| !Rod->InitializeAuthoritativeIdentity(RodActorId, Loadout.RodDefinitionId, Loadout.RodSkinDefinitionId,
			PlayerState, PlayerState, true, Loadout.bRodBroken))
	{
		if (Rod) Rod->Destroy();
		Result.Error = ECatFishingCommandError::DependencyUnavailable;
		return Result;
	}
	Rod->FinishSpawning(SpawnTransform);
	if (!RegisterDeployedRod(PlayerState, Rod))
	{
		Rod->Destroy();
		Result.Error = ECatFishingCommandError::ActiveSessionExists;
		return Result;
	}
	Result.bCommitted = true;
	Result.Error = ECatFishingCommandError::None;
	Result.RodActorId = RodActorId;
	Result.RodActorRevision = Rod->GetPresentationState().RodActorRevision;
	Result.EquipmentRevision = Loadout.Revision;
	return Result;
}

FCatFishingCommandResult UCatFishingService::OperateRod(AController* Controller, const FCatOperateRodCommand& Command)
{
	FCatFishingCommandResult Result;
	Result.CommandType = ECatFishingCommandType::OperateRod;
	Result.RequestId = Command.Context.RequestId;
	APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
	ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr;
	UWorld* World = GetWorld();
	const ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	if (!bCommandsOpen || !GameMode || !GameMode->CanAcceptFishingCommand(Controller)
		|| !CanControllerStartFishingAction(Controller))
	{
		Result.Error = ECatFishingCommandError::CommandsClosed;
		return Result;
	}
	// 按公开 RodActorId 在全部部署竿中解析：多人允许操作别人的竿（无人操作即可接管）。
	ACatFishingRodActor* Rod = FindDeployedRodById(Command.Context.RodActorId);
	if (!Rod || !Character)
	{
		Result.Error = ECatFishingCommandError::NoRod;
		return Result;
	}
	const FCatFishingRodPresentationState& State = Rod->GetPresentationState();
	if (State.bBroken || State.OperatorPlayerState
		|| FVector::DistSquared(Character->GetActorLocation(), Rod->GetStandWorldTransform().GetLocation()) > FMath::Square(250.0))
	{
		Result.Error = State.bBroken ? ECatFishingCommandError::RodBroken : ECatFishingCommandError::RodOccupied;
		return Result;
	}
	// 该竿若绑着一个还在等口的会话（原钓手离开），接管者必须能合法承接会话；先校验再占位，失败不留半状态。
	ACatFishingSession* BoundSession = FindActiveSessionByRod(Rod);
	if (BoundSession)
	{
		const ECatFishingPhase BoundPhase = BoundSession->GetSnapshot().Phase;
		const bool bTakeoverPhase = BoundPhase == ECatFishingPhase::Waiting
			|| BoundPhase == ECatFishingPhase::Probe || BoundPhase == ECatFishingPhase::TrueBiteWindow;
		if (!bTakeoverPhase || !TransferSessionFisher(BoundSession, Controller))
		{
			Result.Error = ECatFishingCommandError::RodOccupied;
			return Result;
		}
	}
	if (!Rod->SetOperatorFromAuthority(PlayerState, Command.Context.ExpectedRodActorRevision))
	{
		// 接力转移已成功但占位失败：把会话交还原状不可行（原钓手可能已离线），保守起见维持转移结果，
		// 下一次 E 重试占位即可（会话钓手已是本人，TransferSessionFisher 幂等）。
		Result.Error = ECatFishingCommandError::RodActorRevisionConflict;
		return Result;
	}
	// 站位锚点是地面点；角色原点在胶囊中心，必须抬高半高再传送，并用 TeleportTo 让引擎自动排斥穿插，否则会卡进 Landscape。
	const FTransform Stand = Rod->GetStandWorldTransform();
	const float HalfHeight = Character->GetCapsuleComponent()
		? Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.0f;
	const FVector StandLocation = Stand.GetLocation() + FVector(0.0, 0.0, HalfHeight + 2.0f);
	const FRotator StandRotation(0.0, Stand.Rotator().Yaw, 0.0);
	if (!Character->TeleportTo(StandLocation, StandRotation))
	{
		Character->SetActorLocationAndRotation(StandLocation, StandRotation, false, nullptr, ETeleportType::TeleportPhysics);
	}
	// 操作竿位期间锁定移动，进入固定钓鱼姿态；离开竿（LeaveRod）或收竿（PackRod）时恢复行走。视角旋转不受影响。
	if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	Result.bCommitted = true;
	Result.Error = ECatFishingCommandError::None;
	Result.RodActorId = State.RodActorId;
	Result.RodActorRevision = Rod->GetPresentationState().RodActorRevision;
	return Result;
}

FCatFishingCommandResult UCatFishingService::LeaveRod(AController* Controller, const FCatLeaveRodCommand& Command)
{
	FCatFishingCommandResult Result;
	Result.CommandType = ECatFishingCommandType::LeaveRod;
	Result.RequestId = Command.Context.RequestId;
	APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
	// 离开按"正在操作的竿"解析（可能是别人的竿），不再假设操作的就是自己的竿。
	ACatFishingRodActor* Rod = FindRodOperatedBy(PlayerState);
	if (!Rod || Rod->GetPresentationState().RodActorId != Command.Context.RodActorId)
	{
		Result.Error = ECatFishingCommandError::NoRod;
		return Result;
	}
	// 有活跃会话也允许离开：会话与操作位解耦（等口阶段离开＝会话继续走计时，竿钩保持原状）；
	// 搏斗阶段的弃战语义由命令分派层在调用本函数前先行终止会话。
	if (Rod->GetPresentationState().OperatorPlayerState != PlayerState
		|| !Rod->SetOperatorFromAuthority(nullptr, Command.Context.ExpectedRodActorRevision))
	{
		Result.Error = ECatFishingCommandError::RodActorRevisionConflict;
		return Result;
	}
	// 离开竿位：解除操作期的移动锁定。
	if (const ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr)
	{
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Movement->SetMovementMode(MOVE_Walking);
		}
	}
	Result.bCommitted = true;
	Result.Error = ECatFishingCommandError::None;
	Result.RodActorId = Command.Context.RodActorId;
	Result.RodActorRevision = Rod->GetPresentationState().RodActorRevision;
	return Result;
}

FCatFishingCommandResult UCatFishingService::PackRod(AController* Controller, const FCatPackRodCommand& Command)
{
	FCatFishingCommandResult Result;
	Result.CommandType = ECatFishingCommandType::PackRod;
	Result.RequestId = Command.Context.RequestId;
	APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
	ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr;
	ACatFishingRodActor* Rod = FindDeployedRod(PlayerState);
	FGuid SessionId;
	FCatFishingSessionSnapshot Snapshot;
	if (!Rod || !Character || Rod->GetPresentationState().RodActorId != Command.Context.RodActorId)
	{
		Result.Error = ECatFishingCommandError::NoRod;
		return Result;
	}
	if (Rod->GetPresentationState().OperatorPlayerState || TryGetActiveSessionForController(Controller, SessionId, Snapshot)
		|| FVector::DistSquared(Character->GetActorLocation(), Rod->GetActorLocation()) > FMath::Square(250.0))
	{
		Result.Error = ECatFishingCommandError::ActiveSessionExists;
		return Result;
	}
	if (!Rod->SetDeployedFromAuthority(false, Command.Context.ExpectedRodActorRevision))
	{
		Result.Error = ECatFishingCommandError::RodActorRevisionConflict;
		return Result;
	}
	UnregisterDeployedRod(PlayerState, Rod);
	// 收竿：解除可能残留的操作期移动锁定。
	if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
	{
		Movement->SetMovementMode(MOVE_Walking);
	}
	Result.bCommitted = true;
	Result.Error = ECatFishingCommandError::None;
	Result.RodActorId = Command.Context.RodActorId;
	Result.RodActorRevision = Rod->GetPresentationState().RodActorRevision;
	// 不能在这里裸 Destroy：上面 SetDeployedFromAuthority(false) 的 ForceNetUpdate 只是标脏，
	// 真正发包要等下一次 NetDriver tick，那时 Actor 已 pending kill，远端客户端只会收到"销毁"而收不到
	// 这次“已部署变为否”的属性变化，BP_OnRodPresentationChanged 在客户端上不会为收竿触发一次
	// （listen server 本机因为是同步 dispatch 反而正常，所以这个 bug 在单机 PIE 下完全看不出来）。
	// 与 ACatFishingSession::ScheduleTerminalDestroy 对齐：复用同一个终态复制窗，让客户端播完收竿表现再消失。
	// 竿本身在 ACatFishingRodActor::DispatchPresentationChanged 里已经立刻隐藏并关碰撞，窗口期不会留下可见/挡路的残影。
	double TerminalWindowSeconds = 0.0;
	const UCatFishingSettings* FishingSettings = GetDefault<UCatFishingSettings>();
	Rod->SetLifeSpan(FishingSettings && FishingSettings->TryGetTerminalReplicationWindow(TerminalWindowSeconds)
		? static_cast<float>(TerminalWindowSeconds) : KINDA_SMALL_NUMBER); // 配置缺失时下一帧销毁，不无界泄漏
	return Result;
}

// 协作转发流程：先清理终态或失效弱引用并定位真实 Session，未找到返回 NotFound；找到后由会话统一校验 Giant/HookedFight/Revision，以及请求者仍是 Active Controller、持有当前 Character、未倒地且力量/体力为正，任何拒绝都发生在参与集合写入前。
FCatDomainCommandResult UCatFishingService::SubmitFightAssist(const FGuid FishingSessionId,
	AController* AssistingController, const FGuid RequestId, const int64 ExpectedRevision)
{
	CompactSessions();
	if (ACatFishingSession* Session = Sessions.FindRef(FishingSessionId).Get())
	{
		return Session->SubmitFightAssist(AssistingController, RequestId, ExpectedRevision);
	}
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	Result.Error = ECatDomainCommandError::NotFound;
	return Result;
}

// 抢抄转发流程：只定位 Session 并转发；首个合法胜者与 FishInstance 创建完全在 Session→Items Compare-and-Commit 中决定。
FCatScoopResult UCatFishingService::RequestScoop(const FGuid FishingSessionId, AController* ScoopingController,
	const FCatScoopCommand& Command)
{
	CompactSessions();
	if (ACatFishingSession* Session = Sessions.FindRef(FishingSessionId).Get())
	{
		const FCatScoopResult Result = Session->RequestScoop(ScoopingController, Command);
		CompactSessions();
		return Result;
	}
	FCatScoopResult Result;
	Result.Command.RequestId = Command.Context.RequestId;
	Result.Command.Error = ECatDomainCommandError::NotFound;
	return Result;
}

// Character 中断流程：遍历存活弱引用，精确命中钓手或协作者后终止；Resolved 会话自行保持终态。
void UCatFishingService::TerminateSessionsForCharacter(const ACatCharacter* Character)
{
	CompactSessions();
	for (const TPair<FGuid, TWeakObjectPtr<ACatFishingSession>>& Pair : Sessions)
	{
		if (ACatFishingSession* Session = Pair.Value.Get(); Session && Session->InvolvesCharacter(Character))
		{
			Session->TerminateSession(ECatFishingOutcome::Invalidated, TEXT("Character unavailable"));
		}
	}
	CompactSessions();
}

// Teardown 流程：永久关闭新入口，并让每个存活会话进入 Terminated；不等待旧半场或创建补偿鱼。
void UCatFishingService::CloseCommandsAndTerminateAll()
{
	bCommandsOpen = false;
	for (const TPair<FGuid, TWeakObjectPtr<ACatFishingSession>>& Pair : Sessions)
	{
		if (ACatFishingSession* Session = Pair.Value.Get())
		{
			Session->TerminateSession(ECatFishingOutcome::Invalidated, TEXT("Run teardown"));
		}
	}
}

// Session 查询流程：先压缩终态/失效弱引用，再做只读查找；失败查询不建立任何缓存或索引项。
ACatFishingSession* UCatFishingService::FindSession(const FGuid FishingSessionId)
{
	CompactSessions();
	if (!FishingSessionId.IsValid())
	{
		return nullptr;
	}
	const TWeakObjectPtr<ACatFishingSession>* WeakSession = Sessions.Find(FishingSessionId);
	ACatFishingSession* Session = WeakSession ? WeakSession->Get() : nullptr;
	return Session && !Session->IsTerminal() ? Session : nullptr;
}

// Controller 活动会话查询：所有输出先清零，再交叉验证服务器身份、正反索引、存活 Session 与公开 SessionId。
bool UCatFishingService::TryGetActiveSessionForController(const AController* Controller,
	FGuid& OutFishingSessionId, FCatFishingSessionSnapshot& OutSnapshot)
{
	OutFishingSessionId.Invalidate();
	OutSnapshot = FCatFishingSessionSnapshot{};
	CompactSessions();
	const FString StableNetId = ResolveStableNetId(Controller);
	const FGuid* MappedSessionId = StableNetId.IsEmpty() ? nullptr : ActiveSessionByFisher.Find(StableNetId);
	if (!MappedSessionId || !MappedSessionId->IsValid())
	{
		return false;
	}
	const FGuid SessionId = *MappedSessionId;
	const FString MappedFisherId = SessionFisherById.FindRef(SessionId);
	ACatFishingSession* Session = FindSession(SessionId);
	if (!Session || MappedFisherId != StableNetId)
	{
		return false;
	}
	const FCatFishingSessionSnapshot& Snapshot = Session->GetSnapshot();
	if (Snapshot.FishingSessionId != SessionId)
	{
		return false;
	}
	OutFishingSessionId = SessionId;
	OutSnapshot = Snapshot;
	return true;
}

// 鱼竿查询流程：先移除双端任一失效的弱条目，再按服务器 PlayerState 身份只读查找。
ACatFishingRodActor* UCatFishingService::FindDeployedRod(const APlayerState* PlayerState)
{
	CompactDeployedRods();
	if (!PlayerState)
	{
		return nullptr;
	}
	const TWeakObjectPtr<APlayerState> PlayerKey(const_cast<APlayerState*>(PlayerState));
	const TWeakObjectPtr<ACatFishingRodActor>* WeakRod = DeployedRodByPlayerState.Find(PlayerKey);
	return WeakRod ? WeakRod->Get() : nullptr;
}

// 多人竿共享查询组：都先压缩失效登记再线性扫描（部署竿数量=玩家数量级，线性可接受）。
ACatFishingRodActor* UCatFishingService::FindDeployedRodById(const FGuid RodActorId)
{
	CompactDeployedRods();
	if (!RodActorId.IsValid()) return nullptr;
	for (const TPair<TWeakObjectPtr<APlayerState>, TWeakObjectPtr<ACatFishingRodActor>>& Pair : DeployedRodByPlayerState)
	{
		ACatFishingRodActor* Rod = Pair.Value.Get();
		if (Rod && Rod->GetPresentationState().RodActorId == RodActorId) return Rod;
	}
	return nullptr;
}

ACatFishingRodActor* UCatFishingService::FindRodOperatedBy(const APlayerState* PlayerState)
{
	CompactDeployedRods();
	if (!PlayerState) return nullptr;
	for (const TPair<TWeakObjectPtr<APlayerState>, TWeakObjectPtr<ACatFishingRodActor>>& Pair : DeployedRodByPlayerState)
	{
		ACatFishingRodActor* Rod = Pair.Value.Get();
		if (Rod && Rod->GetPresentationState().OperatorPlayerState == PlayerState) return Rod;
	}
	return nullptr;
}

ACatFishingRodActor* UCatFishingService::FindNearestOperableRod(const FVector& WorldLocation,
	const double MaxDistanceCentimeters)
{
	CompactDeployedRods();
	ACatFishingRodActor* Best = nullptr;
	double BestDistanceSquared = FMath::Square(FMath::Max(0.0, MaxDistanceCentimeters));
	for (const TPair<TWeakObjectPtr<APlayerState>, TWeakObjectPtr<ACatFishingRodActor>>& Pair : DeployedRodByPlayerState)
	{
		ACatFishingRodActor* Rod = Pair.Value.Get();
		if (!Rod || Rod->GetPresentationState().bBroken || Rod->GetPresentationState().OperatorPlayerState) continue;
		const double DistanceSquared = FVector::DistSquared(WorldLocation, Rod->GetStandWorldTransform().GetLocation());
		if (DistanceSquared <= BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			Best = Rod;
		}
	}
	return Best;
}

ACatFishingSession* UCatFishingService::FindActiveSessionByRod(const ACatFishingRodActor* RodActor)
{
	CompactSessions();
	if (!RodActor) return nullptr;
	for (const TPair<FGuid, TWeakObjectPtr<ACatFishingSession>>& Pair : Sessions)
	{
		ACatFishingSession* Session = Pair.Value.Get();
		if (Session && !Session->IsTerminal() && Session->GetSnapshot().RodActor == RodActor) return Session;
	}
	return nullptr;
}

ACatFishingSession* UCatFishingService::FindNearestScoopableSession(const FVector& WorldLocation,
	const double MaxDistanceCentimeters)
{
	CompactSessions();
	ACatFishingSession* Best = nullptr;
	double BestDistanceSquared = FMath::Square(FMath::Max(0.0, MaxDistanceCentimeters));
	for (const TPair<FGuid, TWeakObjectPtr<ACatFishingSession>>& Pair : Sessions)
	{
		ACatFishingSession* Session = Pair.Value.Get();
		const ACatFishEncounterActor* Fish = Session ? Session->GetSnapshot().FishEncounterActor : nullptr;
		// 与 Session::RequestScoop 的阶段口径保持一致：搏斗中和近岸都可抢抄（鱼身上的圈一直存在）。
		// 这里只做粗筛路由，真正的射线∩圆判定和 Compare-and-Commit 都在 Session 内部。
		const ECatFishingPhase Phase = Session ? Session->GetSnapshot().Phase : ECatFishingPhase::Created;
		if (!Session || Session->IsTerminal() || !Fish
			|| (Phase != ECatFishingPhase::HookedFight && Phase != ECatFishingPhase::NearShore))
		{
			continue;
		}
		const double DistanceSquared = FVector::DistSquared2D(WorldLocation, Fish->GetActorLocation());
		if (DistanceSquared <= BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			Best = Session;
		}
	}
	return Best;
}

// 接力转移编排：新钓手不能已有自己的活跃会话（单活跃槽位不变量）；会话转移成功后才动正反索引，保持两边一致。
bool UCatFishingService::TransferSessionFisher(ACatFishingSession* Session, AController* NewFisherController)
{
	CompactSessions();
	const FString NewFisherId = ResolveStableNetId(NewFisherController);
	if (!Session || Session->IsTerminal() || NewFisherId.IsEmpty()) return false;
	const FGuid SessionId = Session->GetSnapshot().FishingSessionId;
	const FString OldFisherId = SessionFisherById.FindRef(SessionId);
	if (OldFisherId == NewFisherId) return true; // 已是当前钓手：幂等成功。
	if (const FGuid* Existing = ActiveSessionByFisher.Find(NewFisherId); Existing && FindSession(*Existing))
	{
		return false; // 新钓手自己的会话还活着，不能同时接管别人的。
	}
	if (!Session->TransferFisherFromAuthority(NewFisherController)) return false;
	if (!OldFisherId.IsEmpty() && ActiveSessionByFisher.FindRef(OldFisherId) == SessionId)
	{
		ActiveSessionByFisher.Remove(OldFisherId);
	}
	SessionFisherById.Add(SessionId, NewFisherId);
	ActiveSessionByFisher.Add(NewFisherId, SessionId);
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_session_fisher_reindexed SessionId=%s NewFisher=%s"),
		*SessionId.ToString(EGuidFormats::DigitsWithHyphens), *NewFisherId);
	return true;
}

// 鱼竿登记流程：双端必须存活；首次登记成功，相同 Actor 幂等重放，不替换已有存活 Actor。
bool UCatFishingService::RegisterDeployedRod(APlayerState* PlayerState, ACatFishingRodActor* RodActor)
{
	CompactDeployedRods();
	if (!IsValid(PlayerState) || !IsValid(RodActor))
	{
		return false;
	}
	const TWeakObjectPtr<APlayerState> PlayerKey(PlayerState);
	if (const TWeakObjectPtr<ACatFishingRodActor>* Existing = DeployedRodByPlayerState.Find(PlayerKey))
	{
		return Existing->Get() == RodActor;
	}
	DeployedRodByPlayerState.Add(PlayerKey, RodActor);
	return true;
}

// 鱼竿注销流程：ExpectedRodActor 必须与当前存活值精确匹配；missing/null/mismatch 都保持无副作用。
void UCatFishingService::UnregisterDeployedRod(const APlayerState* PlayerState,
	const ACatFishingRodActor* ExpectedRodActor)
{
	CompactDeployedRods();
	if (!PlayerState || !ExpectedRodActor)
	{
		return;
	}
	const TWeakObjectPtr<APlayerState> PlayerKey(const_cast<APlayerState*>(PlayerState));
	const TWeakObjectPtr<ACatFishingRodActor>* Existing = DeployedRodByPlayerState.Find(PlayerKey);
	if (Existing && Existing->Get() == ExpectedRodActor)
	{
		DeployedRodByPlayerState.Remove(PlayerKey);
	}
}

// Session 诊断流程：逐项统计存活且未终态会话，不把弱 Map 的物理条目数误报为活动数量。
int32 UCatFishingService::GetTrackedSessionCountForDiagnostics() const
{
	int32 LiveSessionCount = 0;
	for (const TPair<FGuid, TWeakObjectPtr<ACatFishingSession>>& Pair : Sessions)
	{
		const ACatFishingSession* Session = Pair.Value.Get();
		if (Session && !Session->IsTerminal())
		{
			++LiveSessionCount;
		}
	}
	return LiveSessionCount;
}

// 鱼竿诊断流程：只统计 key/value 双有效的弱登记，不直接返回 Map::Num。
int32 UCatFishingService::GetDeployedRodCountForDiagnostics() const
{
	int32 LiveRodCount = 0;
	for (const TPair<TWeakObjectPtr<APlayerState>, TWeakObjectPtr<ACatFishingRodActor>>& Pair
		: DeployedRodByPlayerState)
	{
		if (Pair.Key.IsValid() && Pair.Value.IsValid())
		{
			++LiveRodCount;
		}
	}
	return LiveRodCount;
}

// 弱索引压缩流程：移除已销毁或 Resolved/Terminated 会话，并用会话到身份反向键释放精确单活跃槽位；开始终态缓存保留供网络重放。
void UCatFishingService::CompactSessions()
{
	for (auto It = Sessions.CreateIterator(); It; ++It)
	{
		ACatFishingSession* Session = It.Value().Get();
		if (!Session || Session->IsTerminal())
		{
			const FGuid SessionId = It.Key();
			const FString StableNetId = SessionFisherById.FindRef(SessionId);
			if (!StableNetId.IsEmpty() && ActiveSessionByFisher.FindRef(StableNetId) == SessionId)
			{
				ActiveSessionByFisher.Remove(StableNetId);
			}
			SessionFisherById.Remove(SessionId);
			It.RemoveCurrent();
		}
	}
}

// 鱼竿 Registry 压缩流程：任一弱端失效即删除整条登记，不保留可阻塞后续 Place 的幽灵槽位。
void UCatFishingService::CompactDeployedRods()
{
	for (auto It = DeployedRodByPlayerState.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || !It.Value().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

// 身份解析流程：只读当前 Controller 的继承 UniqueId；它仅作为服务器私有幂等/单活跃键，不进入 Fishing 公开快照。
FString UCatFishingService::ResolveStableNetId(const AController* Controller)
{
	const APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
	return PlayerState && PlayerState->GetUniqueId().IsValid() ? PlayerState->GetUniqueId()->ToString() : FString();
}

// 新 Fishing 写口身体 gate 流程：只读取当前 Pawn 的 Condition 快照；倒地或没有正式 Character/Condition 时关闭新钓鱼动作，已有会话终止仍由 Condition 首次倒地回调处理。
bool UCatFishingService::CanControllerStartFishingAction(const AController* Controller)
{
	const ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr;
	const UCatConditionComponent* Conditions = Character ? Character->GetConditionComponent() : nullptr;
	return Conditions && !Conditions->GetSnapshot().bDowned;
}

// 参与者谓词流程：先清所有输出，再用服务器 GameMode Active gate、当前 Pawn、Condition 与 ASC 逐层验证；只有身份有效、未倒地且两项能力都为正有限值才返回真。
bool UCatFishingService::TryGetFightCapability(const AController* Controller, FString& OutStableNetId,
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
	const FString StableNetId = ResolveStableNetId(Controller);
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

// 协作快照流程：所有输出先清零，然后遍历当前 Controller 并只累加统一谓词接受的玩家；断线、倒地或零能力玩家不能扩大 Giant 池。
void UCatFishingService::BuildFightCapabilitySnapshot(int32& OutParticipantCount,
	double& OutFishingStrength, double& OutFightStamina) const
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
