#include "Fishing/CatFishingService.h"

#include "Character/CatCharacter.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "Logging/CatLog.h"
#include "Logging/CatLogContext.h"
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
#include "Equipment/CatInventoryItemUseRegistry.h"
#include "Fishing/Actors/CatFishEncounterActor.h"
#include "Fishing/Actors/CatFishingHookActor.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/Presentation/CatFishingPresentationSettings.h"
#include "Social/CatSocialService.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
	// 物品 Use/UnUse 到钓鱼命令错误的映射：这里按放杆/收杆语义解释库存错误，避免复用会话错误码导致日志误导。
	ECatFishingCommandError MapRodInventoryUseError(const ECatDomainCommandError Error)
	{
		switch (Error)
		{
		case ECatDomainCommandError::None: return ECatFishingCommandError::None;
		case ECatDomainCommandError::InvalidPayload: return ECatFishingCommandError::InvalidPayload;
		case ECatDomainCommandError::InvalidIdentity: return ECatFishingCommandError::InvalidIdentity;
		case ECatDomainCommandError::InvalidPhase: return ECatFishingCommandError::ActiveSessionExists;
		case ECatDomainCommandError::NotFound: return ECatFishingCommandError::NoRod;
		case ECatDomainCommandError::RevisionConflict: return ECatFishingCommandError::EquipmentRevisionConflict;
		case ECatDomainCommandError::AlreadyResolved: return ECatFishingCommandError::AlreadyResolved;
		case ECatDomainCommandError::CommandsClosed: return ECatFishingCommandError::CommandsClosed;
		case ECatDomainCommandError::CapacityExceeded: return ECatFishingCommandError::GuardCapacityExceeded;
		default: return ECatFishingCommandError::DependencyUnavailable;
		}
	}

	APlayerController* FindControllerForPlayerState(UWorld* World, const APlayerState* PlayerState)
	{
		if (!World || !PlayerState) return nullptr;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Controller = It->Get();
			if (Controller && Controller->PlayerState == PlayerState) return Controller;
		}
		return nullptr;
	}

	void SnapCharacterToRodSlot(ACatCharacter& Character, const ACatFishingRodActor& Rod, const int32 SlotIndex)
	{
		const FTransform Stand = Rod.GetOperatorStandWorldTransform(SlotIndex);
		const float HalfHeight = Character.GetCapsuleComponent()
			? Character.GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 0.0f;
		const FVector StandLocation = Stand.GetLocation() + FVector(0.0, 0.0, HalfHeight + 2.0f);
		const FRotator StandRotation(0.0, Stand.Rotator().Yaw, 0.0);
		if (!Character.TeleportTo(StandLocation, StandRotation))
		{
			Character.SetActorLocationAndRotation(StandLocation, StandRotation, false, nullptr,
				ETeleportType::TeleportPhysics);
		}
		if (UCharacterMovementComponent* Movement = Character.GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
			Movement->DisableMovement();
		}
	}

	void SnapAllRodOperatorsToCurrentSlots(UWorld* World, const ACatFishingRodActor& Rod)
	{
		if (!World) return;
		const TArray<TObjectPtr<APlayerState>>& Operators = Rod.GetPresentationState().OperatorPlayerStates;
		for (int32 SlotIndex = 0; SlotIndex < Operators.Num(); ++SlotIndex)
		{
			APlayerController* Controller = FindControllerForPlayerState(World, Operators[SlotIndex]);
			if (ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr)
			{
				SnapCharacterToRodSlot(*Character, Rod, SlotIndex);
			}
		}
	}
}

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
	// 会话唯一性属于鱼竿：同一玩家可以依次给多根竿抛线，但一根竿不能叠加第二个未终态会话。
	if (FindActiveSessionByRod(Rod))
	{
		Result.Command.Error = ECatFishingCommandError::ActiveSessionExists;
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
	const FVector ToLandingFromView = Water.WaterSurfaceWorldPoint - ViewOrigin;
	const FVector ToLandingFromRod = Water.WaterSurfaceWorldPoint - Rod->GetRodTipWorldTransform().GetLocation();
	const double MaxRange = FMath::Min(RodDefinition->MaximumLineLengthCentimeters,
		FloatDefinition->MaximumCastDistanceCentimeters);
	if (!FMath::IsFinite(MaxRange) || MaxRange <= 0.0 || ToLandingFromRod.Length() > MaxRange
		|| ToLandingFromView.IsNearlyZero() || ToLandingFromRod.IsNearlyZero()
		|| FVector::DotProduct(FisherController->GetControlRotation().Vector(),
			ToLandingFromView.GetSafeNormal()) < 0.5)
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
		RodState.ItemInstanceId, Loadout.BaitItemInstanceId, Loadout.FloatItemInstanceId,
		Loadout.RodDefinitionId, Loadout.BaitDefinitionId,
		Loadout.FloatDefinitionId, Loadout.Revision);
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
	Attempt.RodItemInstanceId = RodState.ItemInstanceId;
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
	Hook->BeginAuthoritativeFlight(ToLandingFromRod.GetSafeNormal() * FMath::Min(MaxRange, 1500.0),
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
	if (FindDeployedRod(PlayerState) || FindRodOperatedBy(PlayerState))
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
	if (!Loadout.RodItemInstanceId.IsValid() || !RodDefinition || RodDefinition->Kind != ECatEquipmentKind::Rod
		|| !RodDefinition->KeepsInventoryInstanceWhileUsed())
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
	// 架杆只要求前方存在坡度可站立的实体地面，不再依赖水域/岸线样条。
	// 玩家可以在任意地面先架杆；真正抛线时仍由水域命中、鱼竿线长、浮漂射程、朝向和视线共同限制。
	// 表现 Mesh 的碰撞不能否决生成，故 AlwaysSpawn。
	// 放杆的库存事务必须先于 Actor 生成提交：Use 成功后这根实例已经离开背包，后续任一生成或注册失败都要 UnUse 回滚同一实例。
	const FCatInventoryItemUseResult UseResult =
		Equipment->Use(Command.RequestId, Command.ExpectedEquipmentRevision, Loadout.RodItemInstanceId);
	if (UseResult.Error != ECatDomainCommandError::None)
	{
		Result.Error = MapRodInventoryUseError(UseResult.Error);
		Result.EquipmentRevision = UseResult.EquipmentRevision;
		return Result;
	}
	const UCatEquipmentDefinition* UsedRodDefinition =
		GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(UseResult.Item.DefinitionId);
	if (!UsedRodDefinition || UsedRodDefinition->Kind != ECatEquipmentKind::Rod
		|| UseResult.Item.DefinitionId != Loadout.RodDefinitionId)
	{
		Equipment->UnUse(FGuid::NewGuid(), UseResult.Item.ItemInstanceId);
		Result.Error = ECatFishingCommandError::DependencyUnavailable;
		Result.EquipmentRevision = Equipment->GetSnapshot().Revision;
		return Result;
	}
	// 鱼竿 Actor 类在 Use 成功后按被移出的实例定义重读；Equipment 只保证库存事务，表现类型仍由钓鱼服务按鱼竿规则裁决。
	UClass* RodClass = UsedRodDefinition->UseActorClass.LoadSynchronous();
	if (!RodClass || !RodClass->IsChildOf(ACatFishingRodActor::StaticClass()))
	{
		Equipment->UnUse(FGuid::NewGuid(), UseResult.Item.ItemInstanceId);
		Result.Error = ECatFishingCommandError::DependencyUnavailable;
		Result.EquipmentRevision = Equipment->GetSnapshot().Revision;
		return Result;
	}
	const auto RollbackUsedRod = [Equipment, &UseResult]()
	{
		// Actor 还没正式成为场景事实时，回滚只处理库存实例；回滚失败只写诊断，避免掩盖原始放杆失败原因。
		const FCatInventoryItemUseResult Rollback =
			Equipment->UnUse(FGuid::NewGuid(), UseResult.Item.ItemInstanceId);
		if (Rollback.Error != ECatDomainCommandError::None
			&& Rollback.Error != ECatDomainCommandError::AlreadyResolved)
		{
			UE_LOG(LogCatFishing, Warning,
				TEXT("Event=fishing_rod_use_rollback_failed Reason=%s ItemInstance=%s EquipmentRevision=%lld"),
				*UEnum::GetValueAsString(Rollback.Error),
				*UseResult.Item.ItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
				Rollback.EquipmentRevision);
		}
	};
	const FTransform SpawnTransform(Character->GetActorRotation(), GroundHit.ImpactPoint);
	ACatFishingRodActor* Rod = World->SpawnActorDeferred<ACatFishingRodActor>(RodClass, SpawnTransform,
		Controller, Character, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	const FGuid RodActorId = FGuid::NewGuid();
	if (!Rod || !Rod->ConfigureCanonicalAnchorsFromAuthority(UsedRodDefinition->RodTipLocalTransform,
		UsedRodDefinition->StandLocalTransform, UsedRodDefinition->GripLocalTransform)
		|| !Rod->InitializeAuthoritativeIdentity(RodActorId, UseResult.Item.ItemInstanceId,
			UseResult.Item.DefinitionId, Loadout.RodSkinDefinitionId, PlayerState, nullptr, true,
			UseResult.Item.bRodBroken))
	{
		if (Rod) Rod->Destroy();
		RollbackUsedRod();
		Result.Error = ECatFishingCommandError::DependencyUnavailable;
		Result.EquipmentRevision = Equipment->GetSnapshot().Revision;
		return Result;
	}
	Rod->FinishSpawning(SpawnTransform);
	if (!RegisterDeployedRod(PlayerState, Rod))
	{
		Rod->Destroy();
		RollbackUsedRod();
		Result.Error = ECatFishingCommandError::ActiveSessionExists;
		Result.EquipmentRevision = Equipment->GetSnapshot().Revision;
		return Result;
	}
	// PlaceRod 只提交“鱼竿已部署且暂时无人操作”这一件事实，不能在同一帧顺带占用主位。
	// 这样第一次 R 的 bDeployed 跃迁完整驱动放杆动画；第二次 R 才由 OperateRod 写入 0 号右主位并吸附角色。
	Result.bCommitted = true;
	Result.Error = ECatFishingCommandError::None;
	Result.RodActorId = RodActorId;
	Result.RodActorRevision = Rod->GetPresentationState().RodActorRevision;
	Result.EquipmentRevision = UseResult.EquipmentRevision;
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_rod_placed Rod=%s RodId=%s ItemInstance=%s Definition=%s EquipmentRevision=%lld %s"),
		*GetNameSafe(Rod), *RodActorId.ToString(EGuidFormats::DigitsWithHyphens),
		*UseResult.Item.ItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*UseResult.Item.DefinitionId.ToString(), UseResult.EquipmentRevision,
		*CatLogContext::BuildControllerFields(Controller));
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
	// 按公开 RodActorId 在全部部署竿中解析：不限制竿主，只要还有空槽就能加入。
	ACatFishingRodActor* Rod = FindDeployedRodById(Command.Context.RodActorId);
	if (!Rod || !Character)
	{
		Result.Error = ECatFishingCommandError::NoRod;
		return Result;
	}
	// RPC 入口必须自己守住“一名玩家最多占一根竿”的不变量，不能只依赖正常 R 分派先走 Leave。
	// 否则改造客户端可绕过输入层，直接在多根鱼竿数组里同时占位，后续输入查询将变成不确定结果。
	if (ACatFishingRodActor* ExistingRod = FindRodOperatedBy(PlayerState))
	{
		Result.Error = ExistingRod == Rod
			? ECatFishingCommandError::RodOccupied : ECatFishingCommandError::ActiveSessionExists;
		return Result;
	}
	const FCatFishingRodPresentationState State = Rod->GetPresentationState();
	const int32 RequestedSlotIndex = Rod->GetFirstFreeOperatorSlotIndex();
	if (RequestedSlotIndex == INDEX_NONE)
	{
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fishing_rod_operate_rejected Reason=NoFreeOperatorSlot Rod=%s RodId=%s OperatorCount=%d RequestedSlot=%d %s"),
			*GetNameSafe(Rod), *State.RodActorId.ToString(EGuidFormats::DigitsWithHyphens),
			State.OperatorPlayerStates.Num(), RequestedSlotIndex,
			*CatLogContext::BuildControllerFields(Controller));
		Result.Error = ECatFishingCommandError::RodOccupied;
		return Result;
	}
	if (!PlayerState || !Command.Context.RequestId.IsValid() || ResolveStableNetId(Controller).IsEmpty()
		|| State.bBroken || !State.bDeployed
		|| FVector::DistSquared(Character->GetActorLocation(),
			Rod->GetOperatorInteractionWorldTransform().GetLocation()) > FMath::Square(250.0))
	{
		Result.Error = State.bBroken ? ECatFishingCommandError::RodBroken : ECatFishingCommandError::RodOccupied;
		return Result;
	}
	// 辅助位当前承载共享鱼竿会话的站位与接力候选；主位退出时数组会压紧，副位立即晋升并接管。
	// 玩家输入仍只路由当前主位，避免两个独立输入序号域同时驱动单一 Runner。
	// TODO(CooperativeFishing): 合力玩法落地时，从 Rod.OperatorPlayerStates 每次重建 Session 参与集合，
	// 并明确力量、体力消耗与双输入仲裁；不能把“尚未合力”误解成“不允许第二只猫加入同一根竿”。
	// HookedFight 接力会同步迁移 Runner 的 ASC/力量/体力/输入域，不能只改公开 FisherPlayerState。
	ACatFishingSession* BoundSession = FindActiveSessionByRod(Rod);
	const bool bNeedsSessionTakeover = RequestedSlotIndex == 0 && BoundSession
		&& BoundSession->GetSnapshot().FisherPlayerState != PlayerState;
	if (bNeedsSessionTakeover)
	{
		const ECatFishingPhase BoundPhase = BoundSession->GetSnapshot().Phase;
		const bool bTakeoverPhase = BoundPhase == ECatFishingPhase::Waiting
			|| BoundPhase == ECatFishingPhase::Probe || BoundPhase == ECatFishingPhase::TrueBiteWindow
			|| BoundPhase == ECatFishingPhase::HookedFight;
		if (!bTakeoverPhase)
		{
			Result.Error = ECatFishingCommandError::RodOccupied;
			return Result;
		}
	}
	int32 CommittedSlotIndex = INDEX_NONE;
	if (!Rod->AddOperatorFromAuthority(PlayerState, Command.Context.ExpectedRodActorRevision, CommittedSlotIndex)
		|| CommittedSlotIndex != RequestedSlotIndex)
	{
		Result.Error = ECatFishingCommandError::RodActorRevisionConflict;
		return Result;
	}
	if (bNeedsSessionTakeover && !TransferSessionFisher(BoundSession, Controller))
	{
		// 先占主位再迁移会话，避免 Runner 已切给新玩家但鱼竿占位因 Revision 冲突失败。
		// 迁移拒绝时用刚提交后的精确 Revision 回滚本次新占位；同一服务器调用栈内不会夹入第二次写入。
		APlayerState* IgnoredPromotion = nullptr;
		if (!Rod->RemoveOperatorFromAuthority(PlayerState,
			Rod->GetPresentationState().RodActorRevision, IgnoredPromotion))
		{
			UE_LOG(LogCatFishing, Error,
				TEXT("Event=fishing_takeover_slot_rollback_failed SessionId=%s Rod=%s PlayerState=%s RodRevision=%lld"),
				*BoundSession->GetSnapshot().FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
				*GetNameSafe(Rod), *GetNameSafe(PlayerState), Rod->GetPresentationState().RodActorRevision);
		}
		Result.Error = ECatFishingCommandError::RodOccupied;
		return Result;
	}
	// 站位是地面点，统一抬高胶囊半高并锁移动；0 号位在右、1 号位在左。
	SnapCharacterToRodSlot(*Character, *Rod, CommittedSlotIndex);
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_rod_operator_joined Rod=%s RodId=%s Slot=%d OperatorCount=%d Takeover=%s SessionId=%s %s"),
		*GetNameSafe(Rod), *State.RodActorId.ToString(EGuidFormats::DigitsWithHyphens), CommittedSlotIndex,
		Rod->GetOperatorCount(), bNeedsSessionTakeover ? TEXT("true") : TEXT("false"),
		BoundSession ? *BoundSession->GetSnapshot().FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens)
			: TEXT("Invalid"),
		*CatLogContext::BuildControllerFields(Controller));
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
	// 离开按“占用了哪个竿位”解析（可能是别人的竿，也可能是辅助位）。
	ACatFishingRodActor* Rod = FindRodOperatedBy(PlayerState);
	if (!Rod || Rod->GetPresentationState().RodActorId != Command.Context.RodActorId)
	{
		Result.Error = ECatFishingCommandError::NoRod;
		return Result;
	}
	const FCatFishingRodPresentationState State = Rod->GetPresentationState();
	const int32 LeavingSlotIndex = Rod->GetOperatorSlotIndex(PlayerState);
	if (LeavingSlotIndex == INDEX_NONE || State.RodActorRevision != Command.Context.ExpectedRodActorRevision)
	{
		Result.Error = ECatFishingCommandError::RodActorRevisionConflict;
		return Result;
	}
	// 先提交离位，再按实际晋升结果迁移会话，避免 Revision 冲突时 Runner 已错误切给辅助位。
	// 接力依赖暂不满足时也不能阻止原操作手离开，鱼竿会话进入无人值守态。辅助位离开不触碰会话。
	ACatFishingSession* BoundSession = LeavingSlotIndex == 0 ? FindActiveSessionByRod(Rod) : nullptr;
	APlayerState* PromotedPrimary = nullptr;
	if (!Rod->RemoveOperatorFromAuthority(PlayerState, Command.Context.ExpectedRodActorRevision, PromotedPrimary))
	{
		Result.Error = ECatFishingCommandError::RodActorRevisionConflict;
		return Result;
	}
	bool bSessionTransferredToPromotion = false;
	if (PromotedPrimary && BoundSession)
	{
		const ECatFishingPhase Phase = BoundSession->GetSnapshot().Phase;
		const bool bTransferable = Phase == ECatFishingPhase::Waiting || Phase == ECatFishingPhase::Probe
			|| Phase == ECatFishingPhase::TrueBiteWindow || Phase == ECatFishingPhase::HookedFight;
		if (bTransferable)
		{
			if (APlayerController* PromotedController = FindControllerForPlayerState(GetWorld(), PromotedPrimary))
			{
				bSessionTransferredToPromotion = TransferSessionFisher(BoundSession, PromotedController);
			}
		}
	}
	// 离开只释放操作输入与站位，不把会话写成 Escaped/Terminated。即使正处于搏斗，结果也应由鱼线、
	// 体力、主动取消或其他明确终局规则产生，而不是由角色和鱼竿的距离产生。
	if (BoundSession && !bSessionTransferredToPromotion)
	{
		BoundSession->SuspendOperatorFromAuthority();
	}
	// 离开竿位：解除操作期的移动锁定。
	if (const ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr)
	{
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Movement->SetMovementMode(MOVE_Walking);
		}
	}
	// 删除任意编号都会压紧容器；按新编号重排所有剩余角色，不能只处理 1→0 的主位晋升。
	SnapAllRodOperatorsToCurrentSlots(GetWorld(), *Rod);
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_rod_operator_left Rod=%s RodId=%s LeavingSlot=%d RemainingOperators=%d Promoted=%s SessionId=%s %s"),
		*GetNameSafe(Rod), *State.RodActorId.ToString(EGuidFormats::DigitsWithHyphens), LeavingSlotIndex,
		Rod->GetOperatorCount(), *GetNameSafe(PromotedPrimary),
		BoundSession ? *BoundSession->GetSnapshot().FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens)
			: TEXT("Invalid"),
		*CatLogContext::BuildControllerFields(Controller));
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
	UCatEquipmentComponent* Equipment = Character ? Character->GetEquipmentComponent() : nullptr;
	ACatFishingRodActor* Rod = FindDeployedRod(PlayerState);
	if (!Rod || !Character || !Equipment || Rod->GetPresentationState().RodActorId != Command.Context.RodActorId)
	{
		Result.Error = ECatFishingCommandError::NoRod;
		return Result;
	}
	const FCatFishingRodPresentationState RodState = Rod->GetPresentationState();
	if (!Command.Context.RequestId.IsValid() || !RodState.ItemInstanceId.IsValid())
	{
		Result.Error = ECatFishingCommandError::InvalidPayload;
		return Result;
	}
	if (Rod->GetOperatorCount() > 0 || FindActiveSessionByRod(Rod)
		|| FVector::DistSquared(Character->GetActorLocation(), Rod->GetActorLocation()) > FMath::Square(250.0))
	{
		Result.Error = ECatFishingCommandError::ActiveSessionExists;
		return Result;
	}
	if (RodState.RodActorRevision != Command.Context.ExpectedRodActorRevision)
	{
		Result.Error = ECatFishingCommandError::RodActorRevisionConflict;
		return Result;
	}
	// 收杆先提交 UnUse，让库存容量成为能否收回的权威裁决；放不下时 Actor 保持部署，玩家不会凭空复制出第二根竿。
	const FCatInventoryItemUseResult UnUseResult =
		Equipment->UnUse(Command.Context.RequestId, RodState.ItemInstanceId);
	if (UnUseResult.Error != ECatDomainCommandError::None)
	{
		Result.Error = MapRodInventoryUseError(UnUseResult.Error);
		Result.EquipmentRevision = UnUseResult.EquipmentRevision;
		return Result;
	}
	if (!Rod->SetDeployedFromAuthority(false, Command.Context.ExpectedRodActorRevision))
	{
		// 极少数情况下库存已经放回但 Actor 修订冲突，立即把同一实例重新 Use，尽量恢复“场上有竿、背包无竿”的一致状态。
		const FCatInventoryItemUseResult Rollback =
			Equipment->Use(FGuid::NewGuid(), UnUseResult.EquipmentRevision, RodState.ItemInstanceId);
		if (Rollback.Error != ECatDomainCommandError::None
			&& Rollback.Error != ECatDomainCommandError::AlreadyResolved)
		{
			UE_LOG(LogCatFishing, Warning,
				TEXT("Event=fishing_rod_pack_rollback_failed Reason=%s ItemInstance=%s EquipmentRevision=%lld"),
				*UEnum::GetValueAsString(Rollback.Error),
				*RodState.ItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
				Rollback.EquipmentRevision);
		}
		Result.Error = ECatFishingCommandError::RodActorRevisionConflict;
		Result.EquipmentRevision = Equipment->GetSnapshot().Revision;
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
	Result.EquipmentRevision = UnUseResult.EquipmentRevision;
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_rod_packed Rod=%s RodId=%s ItemInstance=%s EquipmentRevision=%lld %s"),
		*GetNameSafe(Rod), *Command.Context.RodActorId.ToString(EGuidFormats::DigitsWithHyphens),
		*RodState.ItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), UnUseResult.EquipmentRevision,
		*CatLogContext::BuildControllerFields(Controller));
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

// 抄网转发流程：只定位 Session 并转发；范围裁决、世界鱼创建与嘴叼交接全部由 Session 原子收敛。
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

// Character 中断流程：先终止该 Character 参与的存活会话，再释放其竿位和 Fishing 移动锁。
// 释放不能留给玩家再次按键：倒地后 Fishing gate 已关闭，若仍要求走 LeaveRod 就会形成永久 MOVE_None。
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
	ReleaseOperatorForCharacter(Character);
}

// Run 钓鱼窗口关闭流程：终止当前半场并释放所有竿位/移动锁，但不关闭 World 级命令门；下一天仍可重新上竿。
void UCatFishingService::SuspendFishingAndReleaseOperators()
{
	TerminateAllSessionsAndReleaseOperators(TEXT("Fishing window closed"));
}

// Teardown 流程：永久关闭新入口，并让每个存活会话进入 Terminated；随后释放全部竿位，不能把 MOVE_None 带进结算或退出流程。
void UCatFishingService::CloseCommandsAndTerminateAll()
{
	bCommandsOpen = false;
	TerminateAllSessionsAndReleaseOperators(TEXT("Run teardown"));
}

void UCatFishingService::TerminateAllSessionsAndReleaseOperators(const TCHAR* DiagnosticReason)
{
	for (const TPair<FGuid, TWeakObjectPtr<ACatFishingSession>>& Pair : Sessions)
	{
		if (ACatFishingSession* Session = Pair.Value.Get())
		{
			Session->TerminateSession(ECatFishingOutcome::Invalidated, DiagnosticReason);
		}
	}
	CompactSessions();
	ReleaseAllRodOperatorsAndRestoreMovement();
}

void UCatFishingService::ReleaseOperatorForCharacter(const ACatCharacter* Character)
{
	if (!Character)
	{
		return;
	}

	APlayerState* PlayerState = Character->GetPlayerState();
	ACatFishingRodActor* Rod = FindRodOperatedBy(PlayerState);
	bool bRemoved = false;
	APlayerState* IgnoredPromotion = nullptr;
	if (PlayerState && Rod)
	{
		const int64 ExpectedRevision = Rod->GetPresentationState().RodActorRevision;
		bRemoved = Rod->RemoveOperatorFromAuthority(PlayerState, ExpectedRevision, IgnoredPromotion);
		if (!bRemoved)
		{
			UE_LOG(LogCatFishing, Error,
				TEXT("Event=fishing_operator_force_release_failed PlayerState=%s RodActorId=%s Revision=%lld"),
				*GetNameSafe(PlayerState),
				*Rod->GetPresentationState().RodActorId.ToString(EGuidFormats::DigitsWithHyphens), ExpectedRevision);
		}
	}

	// 角色已经失去钓鱼资格；即使竿状态异常也必须解除由 Fishing 写入的 MOVE_None，避免门关后永久卡死。
	if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
	{
		Movement->SetMovementMode(MOVE_Walking);
	}

	if (bRemoved && Rod)
	{
		SnapAllRodOperatorsToCurrentSlots(GetWorld(), *Rod);
	}
}

void UCatFishingService::ReleaseAllRodOperatorsAndRestoreMovement()
{
	CompactDeployedRods();
	TSet<ACatFishingRodActor*> ProcessedRods;
	for (const TPair<TWeakObjectPtr<APlayerState>, TWeakObjectPtr<ACatFishingRodActor>>& Pair : DeployedRodByPlayerState)
	{
		ACatFishingRodActor* Rod = Pair.Value.Get();
		if (!Rod || ProcessedRods.Contains(Rod))
		{
			continue;
		}
		ProcessedRods.Add(Rod);
		ReleaseRodOperatorsAndRestoreMovement(Rod);
	}
}

void UCatFishingService::ReleaseRodOperatorsAndRestoreMovement(ACatFishingRodActor* Rod)
{
	if (!Rod)
	{
		return;
	}

	// RemoveOperator 会压紧数组，因此必须先复制原操作人列表，再按当前 Revision 逐个移除。
	const TArray<TObjectPtr<APlayerState>> Operators = Rod->GetPresentationState().OperatorPlayerStates;
	for (APlayerState* Operator : Operators)
	{
		if (!Operator)
		{
			continue;
		}
		APlayerState* IgnoredPromotion = nullptr;
		const int64 ExpectedRevision = Rod->GetPresentationState().RodActorRevision;
		if (!Rod->RemoveOperatorFromAuthority(Operator, ExpectedRevision, IgnoredPromotion))
		{
			UE_LOG(LogCatFishing, Error,
				TEXT("Event=fishing_operator_window_release_failed PlayerState=%s RodActorId=%s Revision=%lld"),
				*GetNameSafe(Operator),
				*Rod->GetPresentationState().RodActorId.ToString(EGuidFormats::DigitsWithHyphens), ExpectedRevision);
		}

		ACatCharacter* OperatorCharacter = Cast<ACatCharacter>(Operator->GetPawn());
		if (!OperatorCharacter)
		{
			if (APlayerController* Controller = FindControllerForPlayerState(GetWorld(), Operator))
			{
				OperatorCharacter = Cast<ACatCharacter>(Controller->GetPawn());
			}
		}
		if (OperatorCharacter)
		{
			if (UCharacterMovementComponent* Movement = OperatorCharacter->GetCharacterMovement())
			{
				Movement->SetMovementMode(MOVE_Walking);
			}
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

// Controller 活动会话查询：所有输出先清零，再按当前主操作位定位鱼竿及其唯一活动 Session。
// 玩家离开某根竿后，那根竿的会话继续运行，但不会继续截获该玩家在另一根竿上的输入。
bool UCatFishingService::TryGetActiveSessionForController(const AController* Controller,
	FGuid& OutFishingSessionId, FCatFishingSessionSnapshot& OutSnapshot)
{
	OutFishingSessionId.Invalidate();
	OutSnapshot = FCatFishingSessionSnapshot{};
	CompactSessions();
	APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
	ACatFishingRodActor* Rod = FindRodOperatedBy(PlayerState);
	if (!Rod || !Rod->IsPrimaryOperator(PlayerState))
	{
		return false;
	}
	ACatFishingSession* Session = FindActiveSessionByRod(Rod);
	if (!Session || Session->GetSnapshot().FisherPlayerState != PlayerState)
	{
		return false;
	}
	const FCatFishingSessionSnapshot& Snapshot = Session->GetSnapshot();
	if (!Snapshot.FishingSessionId.IsValid() || Snapshot.RodActor != Rod)
	{
		return false;
	}
	OutFishingSessionId = Snapshot.FishingSessionId;
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
		if (Rod && Rod->GetOperatorSlotIndex(const_cast<APlayerState*>(PlayerState)) != INDEX_NONE) return Rod;
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
		if (!Rod || !Rod->GetPresentationState().bDeployed || Rod->GetPresentationState().bBroken) continue;
		const int32 FreeSlotIndex = Rod->GetFirstFreeOperatorSlotIndex();
		if (FreeSlotIndex == INDEX_NONE) continue;
		const double DistanceSquared = FVector::DistSquared(WorldLocation,
			Rod->GetOperatorInteractionWorldTransform().GetLocation());
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
		// 这里只做粗筛路由，真正的射线∩圆判定和嘴叼世界鱼交接都在 Session 内部。
		const ECatFishingPhase Phase = Session ? Session->GetSnapshot().Phase : ECatFishingPhase::Created;
		if (!Session || Session->IsTerminal() || !Fish
			|| (Phase != ECatFishingPhase::HookedFight && Phase != ECatFishingPhase::NearShore
				&& Phase != ECatFishingPhase::ExhaustedReel))
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

// 接力转移编排：会话唯一性由鱼竿保证；等口只迁移身份，HookedFight 由 Session 连同 Runner 资源一起迁移。
bool UCatFishingService::TransferSessionFisher(ACatFishingSession* Session, AController* NewFisherController)
{
	CompactSessions();
	const FString NewFisherId = ResolveStableNetId(NewFisherController);
	if (!Session || Session->IsTerminal() || NewFisherId.IsEmpty()) return false;
	if (Session->GetFisherStableNetIdForAuthority() == NewFisherId) return true;
	if (!Session->TransferFisherFromAuthority(NewFisherController)) return false;
	// 成功事件由 Session 的唯一状态写口记录完整且脱敏的 Controller 上下文，服务层不再重复输出原始 StableNetId。
	return true;
}

// 鱼竿登记流程：先把 Actor 的物品实例身份交给通用登记器占位；再写鱼竿自己的玩家弱索引；任一侧失败都会拒绝这次放杆。
bool UCatFishingService::RegisterDeployedRod(APlayerState* PlayerState, ACatFishingRodActor* RodActor)
{
	CompactDeployedRods();
	if (!IsValid(PlayerState) || !IsValid(RodActor))
	{
		return false;
	}
	UCatInventoryItemUseRegistry* ItemUseRegistry = GetWorld()
		? GetWorld()->GetSubsystem<UCatInventoryItemUseRegistry>() : nullptr;
	if (!ItemUseRegistry || !ItemUseRegistry->RegisterWorldItemActor(RodActor))
	{
		return false;
	}
	const TWeakObjectPtr<APlayerState> PlayerKey(PlayerState);
	if (const TWeakObjectPtr<ACatFishingRodActor>* Existing = DeployedRodByPlayerState.Find(PlayerKey))
	{
		const bool bSameActor = Existing->Get() == RodActor;
		if (!bSameActor)
		{
			ItemUseRegistry->UnregisterWorldItemActor(RodActor);
		}
		return bSameActor;
	}
	DeployedRodByPlayerState.Add(PlayerKey, RodActor);
	return true;
}

// 鱼竿注销流程：ExpectedRodActor 必须与当前存活值精确匹配；missing/null/mismatch 都保持无副作用。
void UCatFishingService::UnregisterDeployedRod(const APlayerState* PlayerState,
	const ACatFishingRodActor* ExpectedRodActor)
{
	if (!PlayerState || !ExpectedRodActor)
	{
		CompactDeployedRods();
		return;
	}
	const TWeakObjectPtr<APlayerState> PlayerKey(const_cast<APlayerState*>(PlayerState));
	const TWeakObjectPtr<ACatFishingRodActor>* Existing = DeployedRodByPlayerState.Find(PlayerKey);
	// EndPlay 时普通 Weak.Get() 已可能返回空；允许 pending-kill 读取只用于和调用方 Actor 做同一性校验及最终补偿。
	ACatFishingRodActor* ExistingRod = Existing ? Existing->Get(true) : nullptr;
	if (ExistingRod == ExpectedRodActor)
	{
		// EndPlay/异常销毁也从这里注销；先恢复该竿全部操作人的移动，不能只丢掉 Registry 线索。
		ReleaseRodOperatorsAndRestoreMovement(ExistingRod);
		if (UCatInventoryItemUseRegistry* ItemUseRegistry = GetWorld()
			? GetWorld()->GetSubsystem<UCatInventoryItemUseRegistry>() : nullptr)
		{
			ItemUseRegistry->UnregisterWorldItemActor(const_cast<ACatFishingRodActor*>(ExpectedRodActor));
		}
		DeployedRodByPlayerState.Remove(PlayerKey);
	}
	CompactDeployedRods();
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

// 弱索引压缩流程：移除已销毁或 Resolved/Terminated 会话；开始终态缓存保留供网络重放。
void UCatFishingService::CompactSessions()
{
	for (auto It = Sessions.CreateIterator(); It; ++It)
	{
		ACatFishingSession* Session = It.Value().Get();
		if (!Session || Session->IsTerminal())
		{
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

// 身份解析流程：只读当前 Controller 的继承 UniqueId；它用于服务器私有幂等和参与者身份，不进入 Fishing 公开快照。
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
