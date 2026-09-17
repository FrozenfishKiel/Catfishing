#include "Fishing/CatFishingService.h"
#include "Equipment/CatEquippedDefinition.h"
#include "Inventory/Fragments/CatEquippableItemFragment.h"
#include "UObject/UObjectIterator.h"
#include "Equipment/CatEquipmentInventoryItemInstance.h"
#include "Inventory/CatInventorySettings.h"
#include "Equipment/Fragments/CatEquipmentFragment_Rod.h"
#include "Equipment/Fragments/CatEquipmentFragment_Float.h"

#include "Character/CatCharacter.h"
#include "Character/Physics/CatPhysicalBodyComponent.h"
#include "Fishing/Integration/CatFishingPhysicalRodComponent.h"
#include "Components/BoxComponent.h"
#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Framework/Game/CatfishingGameModeBase.h"
#include "Framework/Game/CatfishingPlayerController.h"
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
#include "Equipment/CatEquipmentItemDefinition.h"
#include "Equipment/CatFishingResourceCustodian.h"
#include "Fishing/Actors/CatFishEncounterActor.h"
#include "Fishing/Actors/CatFishingHookActor.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingSession.h"
#include "Fishing/CatFishingSettings.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"
#include "Fishing/Presentation/CatFishingPresentationSettings.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatBackPackComponent.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Social/CatSocialService.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Components/CapsuleComponent.h"

namespace
{
	// 物品 Use/UnUse 到钓鱼命令错误的映射：放杆外层已单独校验装备选择版本；这里遇到 RevisionConflict 只说明库存借出或归还看到的事实过期。
	ECatFishingCommandError MapRodInventoryUseError(const ECatDomainCommandError Error)
	{
		switch (Error)
		{
		case ECatDomainCommandError::None: return ECatFishingCommandError::None;
		case ECatDomainCommandError::InvalidPayload: return ECatFishingCommandError::InvalidPayload;
		case ECatDomainCommandError::InvalidIdentity: return ECatFishingCommandError::InvalidIdentity;
		case ECatDomainCommandError::InvalidPhase: return ECatFishingCommandError::ActiveSessionExists;
		case ECatDomainCommandError::NotFound: return ECatFishingCommandError::NoRod;
		case ECatDomainCommandError::RevisionConflict: return ECatFishingCommandError::RevisionConflict;
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
	DeployedRodsByPlayerState.Reset();
	PreservedRodEquipment.Reset();
	ResourceCustodians.Reset();
	Super::Deinitialize();
}

// 抛竿请求的服务器流程：
// 1. 先用玩家稳定身份和 RequestId 形成幂等键，重复请求复用首次终态，正在处理的同键请求直接拒绝。
// 2. 再按 GameMode、身体状态、鱼竿占用、装备版本和水域依赖逐层校验；任一依赖缺失都会进入统一 Finish 收口。
// 3. Finish 负责清理进行中标记、缓存终态，并在依赖缺失时输出诊断；鱼饵余量只读取正式库存组件，旧 Equipment Snapshot 仅提供当前选择标识。
// 4. 全部依赖成立后才创建服务器 Session、扣减鱼饵并推进鱼竿/会话事实，客户端只通过复制观察结果。
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
	const TCHAR* DependencyStage = TEXT("ActorDependencies");
	ECatDomainCommandError EquipmentError = ECatDomainCommandError::None;
	TWeakObjectPtr<UCatEquipmentComponent> RodEquipment;
	const auto Finish = [this, &Key, &Command, FisherController, &DependencyStage, &EquipmentError, &RodEquipment](const FCatBeginCastResult& Candidate)
	{
		if (Candidate.Command.Error == ECatFishingCommandError::DependencyUnavailable)
		{
			const ACatCharacter* Character = FisherController ? Cast<ACatCharacter>(FisherController->GetPawn()) : nullptr;
			const UCatEquipmentComponent* Equipment = Character ? Character->GetEquipmentComponent() : nullptr;
			const UCatInventoryComponent* Inventory = Character ? Character->GetInventoryComponent() : nullptr;
			const FCatEquipmentLoadoutSnapshot Loadout = Equipment ? Equipment->GetSnapshot() : FCatEquipmentLoadoutSnapshot{};
			const ACatFishingRodActor* RequestedRod = FindDeployedRodById(Command.RodActorId);
			const FCatFishingRodPresentationState RequestedRodState = RequestedRod
				? RequestedRod->GetPresentationState() : FCatFishingRodPresentationState{};
			int32 BaitQuantity = 0;
			// 依赖拒绝日志里的数量只从正式库存读；旧 Equipment Snapshot 保留选择字段，但不再代表玩家实际还持有多少鱼饵。
			if (Inventory && !(Loadout.BaitItemId == 0))
			{
				for (const FCatInventoryEntry& Entry : Inventory->GetInventoryEntries())
				{
					const UCatInventoryItemInstance* Instance = Entry.Instance.Get();
					if (Instance && Instance->GetItemId() == Loadout.BaitItemId && Entry.StackCount > 0)
					{
						BaitQuantity += Entry.StackCount;
					}
				}
			}
			UE_LOG(LogCatFishing, Warning,
				TEXT("Event=begin_cast_dependency_rejected RequestId=%s Stage=%s EquipmentError=%s World=%s RodActorId=%s RodDefinition=%s RodItemInstanceId=%s BaitDefinition=%s BaitItemInstanceId=%s BaitQuantity=%d FloatDefinition=%s FloatItemInstanceId=%s EquipmentRevision=%lld CastEquipment=%s RodEquipment=%s %s"),
				*Command.RequestId.ToString(EGuidFormats::DigitsWithHyphens), DependencyStage,
				*UEnum::GetValueAsString(EquipmentError), *GetNameSafe(GetWorld()), *Command.RodActorId.ToString(),
				*FString::FromInt(RequestedRodState.RodItemId), *RequestedRodState.ItemInstanceId.ToString(),
				*FString::FromInt(Loadout.BaitItemId), *Loadout.BaitItemInstanceId.ToString(), BaitQuantity,
				*FString::FromInt(Loadout.FloatItemId), *Loadout.FloatItemInstanceId.ToString(), Loadout.Revision,
				*GetPathNameSafe(Equipment), *GetPathNameSafe(RodEquipment.Get()),
				*CatLogContext::BuildControllerFields(FisherController));
		}
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
	if (Equipment && Equipment->FishingResourceOwnerStableId.IsEmpty()) Equipment->FishingResourceOwnerStableId = StableNetId;
	// 按实际操作竿定位；竿及耐久留在部署者账本，鱼饵和鱼漂来自当前抛钩者。
	ACatFishingRodActor* Rod = FindRodOperatedBy(PlayerState);
	if (!World || !Character || !PlayerState || !Equipment || !Rod)
	{
		Result.Command.Error = ECatFishingCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	// 单嘴约束：嘴里叼着鱼不能抛竿；背包鱼护不占嘴，得先放进鱼护或扔下（钓鱼规则 §5.2）。
	// 抢抄与拾取已各自带同一条前置，这里补上抛竿这一路，三个入口共用 GetMouthCarriedActor() 这一个事实源。
	if (Character->GetMouthCarriedActor() != nullptr)
	{
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=begin_cast_mouth_occupied RequestId=%s Carried=%s %s"),
			*Command.RequestId.ToString(EGuidFormats::DigitsWithHyphens),
			*GetNameSafe(Character->GetMouthCarriedActor()),
			*CatLogContext::BuildControllerFields(FisherController));
		Result.Command.Error = ECatFishingCommandError::InvalidPhase;
		return Finish(Result);
	}
	const FCatFishingRodPresentationState RodState = Rod->GetPresentationState();
	const FCatEquipmentLoadoutSnapshot Loadout = Equipment->GetSnapshot();
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
	DependencyStage = TEXT("RodEquipmentOwner");
	// 活体部署者继续精确绑定原角色；真正离场后只从已迁移的原实例托管入口解析。
	const ACatCharacter* RodOwnerCharacter = Cast<ACatCharacter>(Rod->GetInstigator());
	const TWeakObjectPtr<UCatEquipmentComponent>* Preserved = PreservedRodEquipment.Find(RodState.RodActorId);
	const bool bPreservedRod = Preserved && Preserved->IsValid();
	if (!bPreservedRod && (!IsValid(RodOwnerCharacter) || RodOwnerCharacter->GetWorld() != World
		|| !IsValid(RodState.OwnerPlayerState) || RodOwnerCharacter->GetPlayerState() != RodState.OwnerPlayerState))
	{
		Result.Command.Error = ECatFishingCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	RodEquipment = ResolveRodEquipmentFromAuthority(Rod);
	if (!RodEquipment.IsValid())
	{
		Result.Command.Error = ECatFishingCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	const AController* InitialRodOwnerController = bPreservedRod ? nullptr : RodOwnerCharacter->GetController();

	if (!Command.ExpectedWaterRegionHandle.IsValid()
		|| Command.ClientCandidateWorldPoint.ContainsNaN())
	{
		Result.Command.Error = ECatFishingCommandError::InvalidPayload;
		return Finish(Result);
	}
	DependencyStage = TEXT("EquipmentDefinitions");
	const UCatInventorySettings* EquipmentSettings = GetDefault<UCatInventorySettings>();
	// 两根部署竿可以与背包当前选择不同；射程、耐久和会话必须绑定实际操作的实例。
	const UCatEquipmentItemDefinition* RodDefinition = EquipmentSettings->FindRuntimeDefinition<UCatEquipmentItemDefinition>(RodState.RodItemId);
	const UCatEquipmentItemDefinition* FloatDefinition = EquipmentSettings->FindRuntimeDefinition<UCatEquipmentItemDefinition>(Loadout.FloatItemId);
	const UCatEquipmentItemDefinition* BaitDefinition = EquipmentSettings->FindRuntimeDefinition<UCatEquipmentItemDefinition>(Loadout.BaitItemId);
	if (!RodDefinition || !RodDefinition->CanServeFishingRod() || !FloatDefinition
		|| !FloatDefinition->CanServeFishingFloat() || !BaitDefinition
		|| !BaitDefinition->CanServeFishingBait())
	{
		Result.Command.Error = ECatFishingCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	UCatWaterQuerySubsystem* WaterQuery = World->GetSubsystem<UCatWaterQuerySubsystem>();
	// 点击点先单独裁决：射程与遮挡判的是玩家「点得对不对」，散布是系统随后加的随机，不让玩家为它买单（钓鱼规则 §3.1）。
	const FCatWaterSpatialResult ClickedWater = WaterQuery
		? WaterQuery->ResolveCandidatePointToWater(Command.ClientCandidateWorldPoint, Command.ExpectedWaterRegionHandle)
		: FCatWaterSpatialResult{};
	if (!ClickedWater.bSucceeded || ClickedWater.Containment == ECatWaterContainment::Outside)
	{
		Result.Command.Error = ClickedWater.Error == ECatWaterQueryError::AmbiguousRegion
			? ECatFishingCommandError::AmbiguousWater : ECatFishingCommandError::InvalidWaterTarget;
		return Finish(Result);
	}
	const FVector ViewOrigin = Character->GetPawnViewLocation();
	// 墓碑（2026-09-14，T13）：删除竿尖射程起点；钓鱼规则 §3.1 的 D_click 从猫站立点算。
	// 竿尖只负责漂飞行表现，不给不同杆长额外射程。
	const FVector RangeOrigin = Character->GetBodyFootPointWorld();
	const FVector ToLandingFromView = ClickedWater.WaterSurfaceWorldPoint - ViewOrigin;
	const FVector ToLandingFromCat = ClickedWater.WaterSurfaceWorldPoint - RangeOrigin;
	const UCatEquipmentFragment_Float* FloatFragment = FloatDefinition->FindFragment<UCatEquipmentFragment_Float>();
	// 墓碑（2026-09-14，T13；钓鱼规则 §3.1/§4.5）：漂射程不再与竿 Lmax 取 min；线长超限由真咬 D0 独立结算鱼逃。
	const double MaxRange = FloatFragment->MaximumCastDistanceCentimeters;
	if (!FMath::IsFinite(MaxRange) || MaxRange <= 0.0 || ToLandingFromCat.Length() > MaxRange
		|| ToLandingFromView.IsNearlyZero() || ToLandingFromCat.IsNearlyZero())
	{
		UE_LOG(LogCatFishing, Warning, TEXT("Event=cast_range_rejected World=%s Request=%s DistanceCm=%.2f MaximumCm=%.2f Rod=%s Float=%s Landing=%s %s"),
			*GetNameSafe(World), *Command.RequestId.ToString(EGuidFormats::DigitsWithHyphens), ToLandingFromCat.Length(), MaxRange,
			*FString::FromInt(RodState.RodItemId), *FString::FromInt(Loadout.FloatItemId),
			*ClickedWater.WaterSurfaceWorldPoint.ToString(), *CatLogContext::BuildControllerFields(FisherController));
		Result.Command.Error = ECatFishingCommandError::CastOutOfRange;
		return Finish(Result);
	}
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(CatBeginCastLineOfSight), true);
	TraceParams.AddIgnoredActor(Character);
	TraceParams.AddIgnoredActor(Rod);
	FHitResult SightHit;
	if (World->LineTraceSingleByChannel(SightHit, ViewOrigin, ClickedWater.WaterSurfaceWorldPoint,
		ECC_Visibility, TraceParams))
	{
		Result.Command.Error = ECatFishingCommandError::InvalidWaterTarget;
		return Finish(Result);
	}
	// 落点散布流程（钓鱼规则 §3.1，台账 D-23）：
	// 1. 以点击点为圆心做均匀圆盘随机偏移，半径＝漂的精准度（高/中/低 ＝ 0.5/1/1.5 米，值配在漂 DA 的误差半径上）。
	// 2. 偏移偶尔把落点推出射程圆时收敛到边界——「够不到那边」已按点击点拦过，散布不得把玩家推成超程。
	// 3. 偏出水面即落岸：判空竿收回、不损饵、可重抛。这一步仍在 BeginFishingUse 之前，装备与鱼饵都还没预留，拒绝即零损失。
	FVector LandingCandidate = ClickedWater.WaterSurfaceWorldPoint;
	const double ScatterRadiusCentimeters = FloatFragment->MaximumCastErrorRadiusCentimeters;
	if (FMath::IsFinite(ScatterRadiusCentimeters) && ScatterRadiusCentimeters > 0.0)
	{
		// 均匀圆盘：角度取均匀分布，半径按 sqrt 缩放，否则样本会往圆心堆成正态。随机只在服务器摇，客户端观察复制结果。
		const double ScatterAngleRadians = FMath::FRandRange(0.0, 2.0 * UE_DOUBLE_PI);
		const double ScatterRadius = ScatterRadiusCentimeters * FMath::Sqrt(FMath::FRandRange(0.0, 1.0));
		LandingCandidate += FVector(FMath::Cos(ScatterAngleRadians) * ScatterRadius,
			FMath::Sin(ScatterAngleRadians) * ScatterRadius, 0.0);
		const FVector ScatteredFromCat = LandingCandidate - RangeOrigin;
		const double ScatteredDistance = ScatteredFromCat.Length();
		if (ScatteredDistance > MaxRange && ScatteredDistance > UE_DOUBLE_SMALL_NUMBER)
		{
			LandingCandidate = RangeOrigin + ScatteredFromCat * (MaxRange / ScatteredDistance);
		}
	}
	const FCatWaterSpatialResult Water = LandingCandidate.Equals(ClickedWater.WaterSurfaceWorldPoint)
		? ClickedWater
		: WaterQuery->ResolveCandidatePointToWater(LandingCandidate, Command.ExpectedWaterRegionHandle);
	if (!Water.bSucceeded || Water.Containment == ECatWaterContainment::Outside)
	{
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=cast_scatter_landed_ashore RequestId=%s Float=%s ScatterRadiusCm=%.2f ClickedPoint=%s LandingPoint=%s WaterError=%s %s"),
			*Command.RequestId.ToString(EGuidFormats::DigitsWithHyphens), *FString::FromInt(Loadout.FloatItemId),
			ScatterRadiusCentimeters, *ClickedWater.WaterSurfaceWorldPoint.ToString(), *LandingCandidate.ToString(),
			*UEnum::GetValueAsString(Water.Error), *CatLogContext::BuildControllerFields(FisherController));
		Result.Command.Error = Water.Error == ECatWaterQueryError::AmbiguousRegion
			? ECatFishingCommandError::AmbiguousWater : ECatFishingCommandError::InvalidWaterTarget;
		return Finish(Result);
	}
	// 水面修正不能把实际落点推出猫站立点的射程；合法落点随后冻结进 AttemptSnapshot。
	if (FVector::Distance(RangeOrigin, Water.WaterSurfaceWorldPoint) > MaxRange)
	{
		Result.Command.Error = ECatFishingCommandError::InvalidWaterTarget;
		return Finish(Result);
	}
	FGuid SessionId = FGuid::NewGuid();
	FGuid CastAttemptId = FGuid::NewGuid();
	while (CastAttemptId == SessionId) CastAttemptId = FGuid::NewGuid();
	DependencyStage = TEXT("EquipmentReservation");
	const FCatFishingUseFreezeResult Reserved = Equipment->BeginFishingUse(SessionId,
		RodState.ItemInstanceId, Loadout.BaitItemInstanceId, Loadout.FloatItemInstanceId,
		RodState.RodItemId, Loadout.BaitItemId,
		Loadout.FloatItemId, Loadout.Revision, RodEquipment->ResolveOwnerInventoryComponent());
	if (Reserved.Error != ECatDomainCommandError::None)
	{
		EquipmentError = Reserved.Error;
		Result.Command.Error = Reserved.Error == ECatDomainCommandError::RevisionConflict
			? ECatFishingCommandError::EquipmentRevisionConflict : ECatFishingCommandError::DependencyUnavailable;
		return Finish(Result);
	}
	// Begin 已推进装备版本；失败必须回传 Release 后版本，客户端才能观察到鱼竿使用权解除。
	const TWeakObjectPtr<UCatEquipmentComponent> CastingEquipment = Equipment;
	const auto ReleaseFishingUseAndFinish = [CastingEquipment, &Finish, &Result, SessionId](
		const ECatFishingCommandError Error)
	{
		if (CastingEquipment.IsValid())
		{
			const FCatFishingUseOperationResult Released = CastingEquipment->ReleaseFishingUse(SessionId);
			Result.Command.EquipmentRevision = Released.EquipmentRevision;
		}
		Result.Command.Error = Error;
		return Finish(Result);
	};
	DependencyStage = TEXT("EquipmentReservationPublication");
	const ACatfishingGameModeBase* CurrentGameMode = World->GetAuthGameMode<ACatfishingGameModeBase>();
	// 预留广播可能同步触发 UnPossess/关局；此时 Session 尚未登记，退出扫描无法替本事务收尾。
	// 重新校验相同宿主及原命令 gate，不沿新 Pawn/装备替换已经冻结的资源归属。
	if (!IsValid(Character) || !IsValid(Rod) || !CastingEquipment.IsValid() || !RodEquipment.IsValid()
		|| !IsValid(FisherController) || FisherController->GetPawn() != Character
		|| Character->GetEquipmentComponent() != CastingEquipment.Get()
		|| !bCommandsOpen || !CurrentGameMode || !CurrentGameMode->CanAcceptFishingCommand(FisherController)
		|| !CanControllerStartFishingAction(FisherController)
		|| (!bPreservedRod && (!IsValid(RodOwnerCharacter) || RodOwnerCharacter->GetWorld() != World
			|| RodOwnerCharacter->GetPlayerState() != RodState.OwnerPlayerState
			|| RodOwnerCharacter->GetEquipmentComponent() != RodEquipment.Get()
			|| RodOwnerCharacter->GetController() != InitialRodOwnerController
			|| (InitialRodOwnerController && (!IsValid(InitialRodOwnerController)
				|| InitialRodOwnerController->GetPawn() != RodOwnerCharacter))))
		|| (bPreservedRod && (!PreservedRodEquipment.Contains(RodState.RodActorId)
			|| PreservedRodEquipment.FindChecked(RodState.RodActorId) != RodEquipment))
		|| !CastingEquipment->IsFishingUseActive(SessionId))
	{
		return ReleaseFishingUseAndFinish(ECatFishingCommandError::DependencyUnavailable);
	}
	if (Rod->GetPresentationState().RodActorRevision != RodState.RodActorRevision)
	{
		return ReleaseFishingUseAndFinish(ECatFishingCommandError::RodActorRevisionConflict);
	}
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=begin_cast_equipment_bound RequestId=%s SessionId=%s RodActorId=%s RodItemInstanceId=%s CastEquipment=%s RodEquipment=%s EquipmentRevision=%lld RodEquipmentRevision=%lld Borrowed=%d World=%s NetMode=%d Authority=%d LocalRole=%d"),
		*Command.RequestId.ToString(), *SessionId.ToString(), *RodState.RodActorId.ToString(), *RodState.ItemInstanceId.ToString(),
		*GetPathNameSafe(Equipment), *GetPathNameSafe(RodEquipment.Get()), Reserved.EquipmentRevision,
		RodEquipment->GetSnapshot().Revision, RodEquipment.Get() != Equipment, *GetNameSafe(World),
		static_cast<int32>(World->GetNetMode()), Character->HasAuthority(), static_cast<int32>(Character->GetLocalRole()));
	DependencyStage = TEXT("HookClass");
	const UCatFishingPresentationSettings* Presentation = GetDefault<UCatFishingPresentationSettings>();
	UClass* HookClass = Presentation ? Presentation->HookActorClass.LoadSynchronous() : nullptr;
	if (!HookClass || !HookClass->IsChildOf(ACatFishingHookActor::StaticClass()))
	{
		return ReleaseFishingUseAndFinish(ECatFishingCommandError::DependencyUnavailable);
	}
	const FTransform HookTransform(Rod->GetRodTipWorldTransform().GetRotation(),
		Rod->GetRodTipWorldTransform().GetLocation());
	DependencyStage = TEXT("HookSpawn");
	ACatFishingHookActor* Hook = World->SpawnActorDeferred<ACatFishingHookActor>(HookClass, HookTransform,
		Rod, Character, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Hook)
	{
		return ReleaseFishingUseAndFinish(ECatFishingCommandError::DependencyUnavailable);
	}
	Hook->DeferInitialPresentationFromAuthority();
	DependencyStage = TEXT("HookIdentity");
	if (!Hook->InitializeAuthoritativeIdentity(SessionId, CastAttemptId))
	{
		Hook->Destroy();
		return ReleaseFishingUseAndFinish(ECatFishingCommandError::DependencyUnavailable);
	}
	Hook->FinishSpawning(HookTransform);
	DependencyStage = TEXT("HookFlight");
	if (!Hook->BeginAuthoritativeFlight(Water.WaterSurfaceWorldPoint))
	{
		Hook->Destroy();
		return ReleaseFishingUseAndFinish(ECatFishingCommandError::DependencyUnavailable);
	}
	DependencyStage = TEXT("SessionPreparation");
	ACatFishingSession* Session = World->SpawnActorDeferred<ACatFishingSession>(ACatFishingSession::StaticClass(),
		Character->GetActorTransform(), FisherController, Character, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	FCatFishingAttemptSnapshot Attempt;
	Attempt.RequestId = Command.RequestId;
	Attempt.FishingSessionId = SessionId;
	Attempt.CastAttemptId = CastAttemptId;
	Attempt.FisherPlayerState = PlayerState;
	Attempt.RodActor = Rod;
	Attempt.RodItemInstanceId = RodState.ItemInstanceId;
	Attempt.RodItemId = RodState.RodItemId;
	Attempt.FloatItemId = Loadout.FloatItemId;
	Attempt.BaitItemId = Loadout.BaitItemId;
	Attempt.EquipmentReservationRevision = Reserved.EquipmentRevision;
	Attempt.RodActorRevision = RodState.RodActorRevision;
	Attempt.ServerCorrectedLandingWorldPoint = Water.WaterSurfaceWorldPoint;
	Attempt.WaterRegion = Water.WaterRegion;
	Attempt.ServerRandomSeed = static_cast<uint64>(GetTypeHash(FGuid::NewGuid()));
	if (!Session || !Session->PrepareSessionFromAuthority(Attempt, FisherController, Character, Hook))
	{
		if (Session) Session->Destroy();
		Hook->Destroy();
		return ReleaseFishingUseAndFinish(ECatFishingCommandError::DependencyUnavailable);
	}
	Session->FinishSpawning(Character->GetActorTransform());
	DependencyStage = TEXT("SessionStart");
	if (!Session->StartPreparedSessionLogicFromAuthority())
	{
		Session->AbortPreparedSessionFromAuthority();
		Hook->Destroy();
		return ReleaseFishingUseAndFinish(ECatFishingCommandError::DependencyUnavailable);
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
	return Frozen;
}

FCatFishingCommandResult UCatFishingService::PlaceRod(AController* Controller, const FCatPlaceRodCommand& Command)
{
	FCatFishingCommandResult Result;
	Result.CommandType = ECatFishingCommandType::PlaceRod;
	Result.RequestId = Command.RequestId;
	// 放竿从玩家身上同时读取正式库存、旧钓具选择投影和身份事实：库存版本先写入回包并保护实例离包，装备版本只保护当前选中竿与皮肤。
	// Use 成功后鱼竿实例已经临时离开背包，服务再按该实例定义生成 Actor 并注册部署事实；后续任一步失败都要回滚同一实例。
	// 回滚后的回包必须重新读取两套版本，调用方才能知道背包内容和旧装备投影最终停在哪个事实点。
	UWorld* World = GetWorld();
	ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr;
	APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
	UCatEquipmentComponent* Equipment = Character ? Character->GetEquipmentComponent() : nullptr;
	UCatInventoryComponent* OwnerInventory = Character ? Character->GetInventoryComponent() : nullptr;
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
	if (!World || !Character || !PlayerState || !Equipment || !OwnerInventory)
	{
		Result.Error = ECatFishingCommandError::DependencyUnavailable;
		return Result;
	}
	Result.EquipmentRevision = Equipment->GetSnapshot().Revision;
	if (ACatFishingRodActor* OperatedRod = FindRodOperatedBy(PlayerState))
	{
		Result.Error = ECatFishingCommandError::ActiveSessionExists;
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fishing_rod_place_rejected RequestId=%s Reason=AlreadyOperatingRod RodActorId=%s DeployedRodCount=%d MaximumDeployedRods=%d World=%s NetMode=%d Authority=true LocalRole=%d %s"),
			*Command.RequestId.ToString(), *OperatedRod->GetPresentationState().RodActorId.ToString(),
			GetDeployedRodCount(PlayerState), GetDefault<UCatFishingSettings>()->GetMaximumDeployedRodsPerPlayer(), *GetNameSafe(World),
			static_cast<int32>(World->GetNetMode()), static_cast<int32>(Controller->GetLocalRole()),
			*CatLogContext::BuildControllerFields(Controller));
		return Result;
	}
	if (GetDeployedRodCount(PlayerState) >= GetDefault<UCatFishingSettings>()->GetMaximumDeployedRodsPerPlayer())
	{
		Result.Error = ECatFishingCommandError::RodDeploymentLimitReached;
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fishing_rod_place_rejected RequestId=%s Reason=DeploymentLimitReached DeployedRodCount=%d MaximumDeployedRods=%d World=%s NetMode=%d Authority=true LocalRole=%d %s"),
			*Command.RequestId.ToString(), GetDeployedRodCount(PlayerState), GetDefault<UCatFishingSettings>()->GetMaximumDeployedRodsPerPlayer(),
			*GetNameSafe(World), static_cast<int32>(World->GetNetMode()), static_cast<int32>(Controller->GetLocalRole()),
			*CatLogContext::BuildControllerFields(Controller));
		return Result;
	}
	// 精确实例 Use 只信任命令携带的鱼竿实例；旧入口仍用装备投影版本保护当前装备视图，不把本地快捷栏选择复制到服务器。
	const FCatEquipmentLoadoutSnapshot Loadout = Equipment->GetSnapshot();
	if (!Command.RequestedRodItemInstanceId.IsValid() && Loadout.Revision != Command.ExpectedEquipmentRevision)
	{
		Result.Error = ECatFishingCommandError::EquipmentRevisionConflict;
		return Result;
	}
	FCatInventoryEntry InventoryRod;
	if (Command.RequestedRodItemInstanceId.IsValid())
	{
		const int32 RequestedSlot = OwnerInventory->FindInventorySlotIndexFromInstanceId(Command.RequestedRodItemInstanceId);
		const FCatInventoryEntry* RequestedEntry = OwnerInventory->GetInventoryEntryAtSlot(RequestedSlot);
		if (RequestedEntry)
		{
			InventoryRod = *RequestedEntry;
		}
	}
	else
	{
		Equipment->TryGetInventoryRodForDeployment(InventoryRod);
	}
	const UCatEquipmentItemDefinition* RequestedDefinition = InventoryRod.Instance
		? Cast<UCatEquipmentItemDefinition>(InventoryRod.Instance->GetItemDefinition()) : nullptr;
	if (!InventoryRod.Instance || InventoryRod.StackCount != 1 || !RequestedDefinition
		|| !RequestedDefinition->CanServeFishingRod()
		|| (Command.RequestedRodItemInstanceId.IsValid()
			&& InventoryRod.Instance->GetItemInstanceId() != Command.RequestedRodItemInstanceId))
	{
		Result.Error = ECatFishingCommandError::NoRod;
		Result.EquipmentRevision = Loadout.Revision;
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fishing_rod_place_rejected RequestId=%s Reason=NoUsableInventoryRod DeployedRodCount=%d MaximumDeployedRods=%d EquipmentRevision=%lld World=%s NetMode=%d Authority=true LocalRole=%d %s"),
			*Command.RequestId.ToString(), GetDeployedRodCount(PlayerState), GetDefault<UCatFishingSettings>()->GetMaximumDeployedRodsPerPlayer(),
			Loadout.Revision, *GetNameSafe(World), static_cast<int32>(World->GetNetMode()),
			static_cast<int32>(Controller->GetLocalRole()), *CatLogContext::BuildControllerFields(Controller));
		return Result;
	}
	const auto RefreshResultRevisions = [OwnerInventory, Equipment, &Result]()
	{
		// Use 成功后的失败路径会先改背包再回滚，装备投影也可能随回滚递增；回包必须返回两套最新版本，避免上层继续拿旧事实重试。
		Result.EquipmentRevision = Equipment ? Equipment->GetSnapshot().Revision : 0;
	};
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
		Equipment->Use(Command.RequestId, Command.ExpectedEquipmentRevision, InventoryRod.Instance->GetItemInstanceId(),
			1);
	const auto* UsedRodInstance = Cast<UCatEquipmentInventoryItemInstance>(UseResult.Item.Instance);
	if (UseResult.Error != ECatDomainCommandError::None)
	{
		Result.Error = MapRodInventoryUseError(UseResult.Error);
		// 定义侧以 InvalidPhase 拒绝坏竿；此处按实际实例解释，不能把它误报成仍有场景鱼竿占用。
		if (UseResult.Error == ECatDomainCommandError::InvalidPhase && UsedRodInstance != nullptr
			&& ((UsedRodInstance && UsedRodInstance->IsRodBroken()) || !FMath::IsFinite((UsedRodInstance ? UsedRodInstance->GetRodDurability() : 0.0))
				|| (UsedRodInstance ? UsedRodInstance->GetRodDurability() : 0.0) <= 0.0))
		{
			Result.Error = ECatFishingCommandError::RodBroken;
		}
		Result.EquipmentRevision = Equipment->GetSnapshot().Revision;
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fishing_rod_place_rejected RequestId=%s Definition=%s RodItemInstanceId=%s Durability=%.3f Broken=%s InventoryError=%s Error=%s EquipmentRevision=%lld World=%s %s"),
			*Command.RequestId.ToString(), *FString::FromInt(InventoryRod.Instance->GetItemId()), *InventoryRod.Instance->GetItemInstanceId().ToString(),
			(UsedRodInstance ? UsedRodInstance->GetRodDurability() : 0.0), (UsedRodInstance && UsedRodInstance->IsRodBroken()) ? TEXT("true") : TEXT("false"),
			*UEnum::GetValueAsString(UseResult.Error), *UEnum::GetValueAsString(Result.Error),
			Result.EquipmentRevision, *GetNameSafe(World),
			*CatLogContext::BuildControllerFields(Controller));
		return Result;
	}
	const UCatEquipmentItemDefinition* UsedRodDefinition =
		GetDefault<UCatInventorySettings>()->FindRuntimeDefinition<UCatEquipmentItemDefinition>(UseResult.Item.Instance->GetItemId());
	if (!UsedRodDefinition || !UsedRodDefinition->CanServeFishingRod()
		|| UseResult.Item.Instance->GetItemId() != InventoryRod.Instance->GetItemId())
	{
		Equipment->UnUse(FGuid::NewGuid(), UseResult.Item.Instance->GetItemInstanceId());
		Result.Error = ECatFishingCommandError::DependencyUnavailable;
		RefreshResultRevisions();
		return Result;
	}
	// 鱼竿 Actor 类在 Use 成功后按被移出的实例定义重读；正式 InventoryComponent 已完成借出，Equipment 这里只是旧投影适配层。
	// 表现类型仍由钓鱼服务按鱼竿规则裁决，不能让旧装备快照重新拥有库存事实。
	UClass* RodClass = UsedRodDefinition->GetEquipmentDefinition()->ActorClass.LoadSynchronous();
	if (!RodClass || !RodClass->IsChildOf(ACatFishingRodActor::StaticClass()))
	{
		Equipment->UnUse(FGuid::NewGuid(), UseResult.Item.Instance->GetItemInstanceId());
		Result.Error = ECatFishingCommandError::DependencyUnavailable;
		RefreshResultRevisions();
		return Result;
	}
	const auto RollbackUsedRod = [Equipment, &UseResult]()
	{
		// Actor 还没正式成为场景事实时，回滚只处理库存实例；回滚失败只写诊断，避免掩盖原始放杆失败原因。
		const FCatInventoryItemUseResult Rollback =
			Equipment->UnUse(FGuid::NewGuid(), UseResult.Item.Instance->GetItemInstanceId());
		if (Rollback.Error != ECatDomainCommandError::None
			&& Rollback.Error != ECatDomainCommandError::AlreadyResolved)
		{
			UE_LOG(LogCatFishing, Warning,
				TEXT("Event=fishing_rod_use_rollback_failed Reason=%s ItemInstance=%s EquipmentRevision=%lld"),
				*UEnum::GetValueAsString(Rollback.Error),
				*UseResult.Item.Instance->GetItemInstanceId().ToString(EGuidFormats::DigitsWithHyphens),
				Equipment->GetSnapshot().Revision);
		}
	};
	const FTransform SpawnTransform(Character->GetActorRotation(), GroundHit.ImpactPoint);
	const UCatFishingSettings* PhysicalSettings = GetDefault<UCatFishingSettings>();
	ACatFishingRodActor* Rod = World->SpawnActorDeferred<ACatFishingRodActor>(RodClass, SpawnTransform,
		Controller, Character, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	const FGuid RodActorId = FGuid::NewGuid();
	if (!Rod || !Rod->ConfigureCanonicalAnchorsFromAuthority(UsedRodDefinition->FindFragment<UCatEquipmentFragment_Rod>()->RodTipLocalTransform,
		UsedRodDefinition->FindFragment<UCatEquipmentFragment_Rod>()->StandLocalTransform, UsedRodDefinition->FindFragment<UCatEquipmentFragment_Rod>()->GripLocalTransform)
		|| !Rod->InitializeAuthoritativeIdentity(RodActorId, UseResult.Item.Instance->GetItemInstanceId(),
			UseResult.Item.Instance->GetItemId(), Loadout.RodSkinDefinitionId, PlayerState, PlayerState, true,
			(UsedRodInstance && UsedRodInstance->IsRodBroken()))
		|| !FMath::IsFinite(PhysicalSettings->HeldRodMaximumAngularSpeedDegreesPerSecond)
		|| PhysicalSettings->HeldRodMaximumAngularSpeedDegreesPerSecond <= 0.0
		|| !FMath::IsFinite(PhysicalSettings->HeldRodAngularResistanceResponseSeconds)
		|| PhysicalSettings->HeldRodAngularResistanceResponseSeconds <= 0.0)
	{
		if (Rod) Rod->Destroy();
		RollbackUsedRod();
		Result.Error = ECatFishingCommandError::DependencyUnavailable;
		RefreshResultRevisions();
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fishing_rod_place_rejected RequestId=%s RodActorId=%s Stage=PrepareHeldRod Error=DependencyUnavailable World=%s Authority=true LocalRole=%d %s"),
			*Command.RequestId.ToString(), *RodActorId.ToString(), *GetNameSafe(World),
			static_cast<int32>(Controller->GetLocalRole()), *CatLogContext::BuildControllerFields(Controller));
		return Result;
	}
	// Initialize the receiver, then align this new rod to an actual free paw and create the same physical grip.
	Rod->FinishSpawning(SpawnTransform);
	if (!RegisterDeployedRod(PlayerState, Rod))
	{
		Rod->Destroy();
		RollbackUsedRod();
		Result.Error = ECatFishingCommandError::ActiveSessionExists;
		RefreshResultRevisions();
		return Result;
	}
	if (!Rod->IsUsingPhysicalRod() || !Rod->BeginPhysicalHoldFromAuthority(PlayerState, true)
		|| !Rod->SetPrimaryOperatorFromAuthority(PlayerState, Rod->GetPresentationState().RodActorRevision)
		|| !Rod->GetPhysicalRodComponent()->CommitPrimaryHold(PlayerState))
	{
		Rod->Destroy();
		RollbackUsedRod();
		Result.Error = ECatFishingCommandError::DependencyUnavailable;
		RefreshResultRevisions();
		UE_LOG(LogCatFishing, Warning, TEXT("Event=fishing_rod_place_rejected RequestId=%s RodActorId=%s Stage=PhysicalGrip Result=RolledBack"),
			*Command.RequestId.ToString(), *RodActorId.ToString());
		return Result;
	}
	// 只有库存实例部署与操作位事务都成功，才提交拥有者握持，避免留下无库存来源的持竿状态。
	Result.bCommitted = true;
	Result.Error = ECatFishingCommandError::None;
	Result.RodActorId = RodActorId;
	Result.RodActorRevision = Rod->GetPresentationState().RodActorRevision;
	Result.EquipmentRevision = Equipment->GetSnapshot().Revision;
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_rod_placed RequestId=%s Rod=%s RodActorId=%s ItemInstance=%s Definition=%s Pose=Held Holder=%s OperatorCount=%d RodActorRevision=%lld EquipmentRevision=%lld DeployedRodCount=%d MaximumDeployedRods=%d World=%s NetMode=%d Authority=true LocalRole=%d %s"),
		*Command.RequestId.ToString(), *GetNameSafe(Rod), *RodActorId.ToString(),
		*UseResult.Item.Instance->GetItemInstanceId().ToString(EGuidFormats::DigitsWithHyphens),
		*FString::FromInt(UseResult.Item.Instance->GetItemId()), *GetNameSafe(Rod->GetPresentationState().HolderPlayerState),
		Rod->GetOperatorCount(), Rod->GetPresentationState().RodActorRevision, Equipment->GetSnapshot().Revision,
		GetDeployedRodCount(PlayerState), GetDefault<UCatFishingSettings>()->GetMaximumDeployedRodsPerPlayer(),
		*GetNameSafe(World), static_cast<int32>(World->GetNetMode()), static_cast<int32>(Controller->GetLocalRole()),
		*CatLogContext::BuildControllerFields(Controller));
	return Result;
}

FCatFishingCommandResult UCatFishingService::AcquireRodIntoQuickbar(AController* Controller, const FCatOperateRodCommand& Command)
{
	FCatFishingCommandResult Result;
	Result.CommandType = ECatFishingCommandType::OperateRod;
	Result.RequestId = Command.Context.RequestId;
	Result.RodActorId = Command.Context.RodActorId;
	Result.Error = ECatFishingCommandError::DependencyUnavailable;
	auto* PC = Cast<ACatfishingPlayerController>(Controller);
	auto* Character = PC ? Cast<ACatCharacter>(PC->GetPawn()) : nullptr;
	auto* TargetEquipment = Character ? Character->GetEquipmentComponent() : nullptr;
	auto* TargetInventory = Character ? Cast<UCatBackPackComponent>(Character->GetInventoryComponent()) : nullptr;
	auto* Rod = FindDeployedRodById(Command.Context.RodActorId);
	auto* SourceEquipment = ResolveRodEquipmentFromAuthority(Rod);
	auto* SourceInventory = SourceEquipment ? SourceEquipment->ResolveOwnerInventoryComponent() : nullptr;
	const FGuid ItemId = Rod ? Rod->GetPresentationState().ItemInstanceId : FGuid();
	const auto* Entry = SourceInventory ? SourceInventory->FindHeldInventoryEntryFromAuthority(ItemId) : nullptr;
	int32 SlotIndex = INDEX_NONE;
	if (PC && PC->HasAuthority() && TargetEquipment && TargetInventory && Entry && Entry->Instance)
	{
		for (int32 Index = 0; Index < TargetInventory->GetInventorySlotCount(); ++Index)
			if (!TargetInventory->HasItemAtSlot(Index) && TargetInventory->CanAcceptInventoryEntryAtSlot(*Entry, Index))
			{ SlotIndex = Index; break; }
	}
	if (SlotIndex == INDEX_NONE)
	{
		UE_LOG(LogCatFishing, Warning, TEXT("Event=quickbar_rod_acquire_rejected RequestId=%s RodActorId=%s ItemId=%s Reason=NoInventorySlot %s"),
			*Result.RequestId.ToString(), *Result.RodActorId.ToString(), *ItemId.ToString(), *CatLogContext::BuildControllerFields(Controller));
		return Result;
	}
	Result = OperateRod(Controller, Command);
	if (!Result.bCommitted) return Result;
	const bool bTransfer = SourceEquipment != TargetEquipment;
	const bool bMoved = !bTransfer || SourceEquipment->MoveFishingResourcesToCustodian(TargetEquipment, {}, {ItemId});
	if (bMoved) PreservedRodEquipment.Add(Result.RodActorId, TargetEquipment);
	const bool bReserved = bMoved && TargetInventory->ReserveExistingHeldQuickbarSlotFromAuthority(SlotIndex, ItemId);
	if (!bReserved)
	{
		const bool bRolledBack = !bTransfer || !bMoved || TargetEquipment->MoveFishingResourcesToCustodian(SourceEquipment, {}, {ItemId});
		PreservedRodEquipment.Add(Result.RodActorId, bRolledBack ? SourceEquipment : TargetEquipment);
		FCatLeaveRodCommand Leave;
		Leave.Context.RequestId = FGuid::NewGuid(); Leave.Context.RodActorId = Result.RodActorId;
		Leave.Context.ExpectedRodActorRevision = Rod->GetPresentationState().RodActorRevision;
		LeaveRod(Controller, Leave);
		Result.bCommitted = false;
		Result.Error = ECatFishingCommandError::DependencyUnavailable;
		UE_LOG(LogCatFishing, Warning, TEXT("Event=quickbar_rod_acquire_rejected RequestId=%s RodActorId=%s ItemId=%s Reason=InventoryCommitRejected RolledBack=%d %s"),
			*Result.RequestId.ToString(), *Result.RodActorId.ToString(), *ItemId.ToString(), bRolledBack, *CatLogContext::BuildControllerFields(Controller));
		return Result;
	}
	if (auto* OldBackPack = Cast<UCatBackPackComponent>(SourceInventory); bTransfer && OldBackPack
		&& OldBackPack->GetQuickbarHeldSlot().ItemInstanceId == ItemId) OldBackPack->ClearQuickbarHeldSlotFromAuthority();
	const auto* Session = FindActiveSessionByRod(Rod);
	TargetInventory->SetQuickbarHeldSlotInUseFromAuthority(Session != nullptr);
	if (bTransfer) { SourceEquipment->PublishSnapshot(); TargetEquipment->PublishSnapshot(); }
	PC->SelectAcquiredRodSlotFromAuthority(Result.RequestId);
	UE_LOG(LogCatFishing, Log, TEXT("Event=quickbar_rod_acquired RequestId=%s RodActorId=%s ItemId=%s SessionId=%s Slot=%d Transferred=%d %s"),
		*Result.RequestId.ToString(), *Result.RodActorId.ToString(), *ItemId.ToString(),
		Session ? *Session->GetSnapshot().FishingSessionId.ToString() : TEXT("None"), SlotIndex, bTransfer, *CatLogContext::BuildControllerFields(Controller));
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
	// 显式交互接管空闲竿；物品实例不变，普通抓握不授予主控。
	ACatFishingRodActor* Rod = FindDeployedRodById(Command.Context.RodActorId);
	if (!Rod || !Character)
	{
		Result.Error = ECatFishingCommandError::NoRod;
		return Result;
	}
	if (Rod->IsUsingPhysicalRod())
	{
		// 换人接手是「竿上已有主控」这道拒绝的**唯一**例外（多人钓鱼附篇 §2.4）：
		// 主钓手得先挂出换人请求，接手者确认后才能接过一根有人的竿（09-13 裁决④）。
		// 没有请求就抢不走——这不是「谁先按谁得」的抢竿，是一次双方同意的交接。
		ECatFishingCommandError HandoffError = ECatFishingCommandError::HandoffNotRequested;
		const bool bHandoffTakeover = Rod->GetPresentationState().OperatorPlayerState
			&& !Rod->IsPrimaryOperator(PlayerState)
			&& CanAcceptHandoffTakeover(FindActiveSessionByRod(Rod), Rod, Controller, HandoffError);
		if (Rod->GetPresentationState().OperatorPlayerState && !Rod->IsPrimaryOperator(PlayerState)
			&& !bHandoffTakeover)
		{
			Result.Error = ECatFishingCommandError::RodOccupied;
			UE_LOG(LogCatFishing, Warning, TEXT("Event=fishing_rod_operate_rejected RequestId=%s RodActorId=%s Reason=AlreadyControlled Handoff=%s World=%s %s"),
				*Command.Context.RequestId.ToString(), *Command.Context.RodActorId.ToString(),
				*UEnum::GetValueAsString(HandoffError), *GetNameSafe(World),
				*CatLogContext::BuildControllerFields(Controller));
			return Result;
		}
		if (Command.Context.ExpectedRodActorRevision != Rod->GetPresentationState().RodActorRevision)
		{
			Result.Error = ECatFishingCommandError::RevisionConflict;
			Result.RodActorRevision = Rod->GetPresentationState().RodActorRevision;
			UE_LOG(LogCatFishing, Warning, TEXT("Event=fishing_rod_operate_rejected RequestId=%s RodActorId=%s ExpectedRevision=%lld CurrentRevision=%lld Reason=RevisionConflict %s"),
				*Command.Context.RequestId.ToString(), *Command.Context.RodActorId.ToString(), Command.Context.ExpectedRodActorRevision,
				Result.RodActorRevision, *CatLogContext::BuildControllerFields(Controller));
			return Result;
		}
		if (ACatFishingRodActor* AlreadyOperated = FindRodOperatedBy(PlayerState); AlreadyOperated && AlreadyOperated != Rod)
		{
			Result.Error = ECatFishingCommandError::RodOccupied;
			UE_LOG(LogCatFishing, Warning, TEXT("Event=fishing_rod_operate_rejected RequestId=%s RodActorId=%s Reason=AlreadyOperatingRod %s"),
				*Command.Context.RequestId.ToString(), *Command.Context.RodActorId.ToString(), *CatLogContext::BuildControllerFields(Controller));
			return Result;
		}
		if (!Rod->GetPresentationState().bDeployed || Rod->GetPresentationState().bBroken
			|| FVector::DistSquared(Character->GetActorLocation(), Rod->GetGripWorldTransform().GetLocation()) > FMath::Square(250.0))
		{
			Result.Error = ECatFishingCommandError::RodOccupied;
			UE_LOG(LogCatFishing, Warning, TEXT("Event=fishing_rod_operate_rejected RequestId=%s RodActorId=%s Reason=UnavailableOrOutOfPickupRange %s"),
				*Command.Context.RequestId.ToString(), *Command.Context.RodActorId.ToString(), *CatLogContext::BuildControllerFields(Controller));
			return Result;
		}
		const FTransform PreviousParkedPose = Rod->GetPhysicalRodComponent()->GetBody()->GetComponentTransform();
		if (bHandoffTakeover)
		{
			// 交接瞬间完成：先把上一任从主控位和主持上摘下来，下面那套「取得主控」的链条才走得通
			// （SetPrimaryOperatorFromAuthority 不允许在已有主控时直接改写成另一个人）。
			// 被换下者按搏斗外速率恢复，他仍可以用左右手物理抓着这根竿当帮手——那不是主控位。
			// 中途失败的兜底是下面那条既有回滚：这根竿会退回无人值守（线随鱼放、没人扣体力），
			// 而不是回到上一任手里——无人值守是设计里已经定义过的状态，不是新造的半成品。
			APlayerState* PreviousPrimary = Rod->GetPresentationState().OperatorPlayerState;
			Rod->ReleasePhysicalPrimaryHoldFromAuthority(PreviousPrimary, TEXT("FishingHandoff"));
			Rod->SetPrimaryOperatorFromAuthority(nullptr, Rod->GetPresentationState().RodActorRevision);
			if (ACatFishingSession* HandoffSession = FindActiveSessionByRod(Rod))
			{
				HandoffSession->RefreshPrimaryControlFromAuthority();
			}
		}
		if (!Command.Context.RequestId.IsValid()
			|| !Rod->BeginPhysicalHoldFromAuthority(PlayerState, true))
		{
			Result.Error = ECatFishingCommandError::RodOccupied;
			UE_LOG(LogCatFishing, Warning, TEXT("Event=fishing_rod_operate_rejected RequestId=%s RodActorId=%s Reason=PhysicalHoldRequired %s"),
				*Command.Context.RequestId.ToString(), *Command.Context.RodActorId.ToString(), *CatLogContext::BuildControllerFields(Controller));
			return Result;
		}
		const bool bAlreadyPrimary = Rod->IsPrimaryOperator(PlayerState);
		Result.bCommitted = Rod->SetPrimaryOperatorFromAuthority(PlayerState, Rod->GetPresentationState().RodActorRevision);
		if (Result.bCommitted)
		{
			if (ACatFishingSession* Existing = FindActiveSessionByRod(Rod))
			{
				Result.bCommitted = ResumeSessionControl(Existing, Controller);
				if (!Result.bCommitted)
				{
					if (!bAlreadyPrimary) Rod->SetPrimaryOperatorFromAuthority(nullptr, Rod->GetPresentationState().RodActorRevision);
					UE_LOG(LogCatFishing, Warning, TEXT("Event=fishing_rod_operate_rolled_back RequestId=%s SessionId=%s RodActorId=%s Reason=SessionResumeRejected %s"),
						*Command.Context.RequestId.ToString(), *Existing->GetSnapshot().FishingSessionId.ToString(),
						*Command.Context.RodActorId.ToString(), *CatLogContext::BuildControllerFields(Controller));
				}
			}
		}
		if (Result.bCommitted && !Rod->GetPhysicalRodComponent()->CommitPrimaryHold(PlayerState))
		{
			Result.bCommitted = false;
			if (!bAlreadyPrimary)
			{
				Rod->SetPrimaryOperatorFromAuthority(nullptr, Rod->GetPresentationState().RodActorRevision);
				if (ACatFishingSession* Existing = FindActiveSessionByRod(Rod)) Existing->RefreshPrimaryControlFromAuthority();
			}
			UE_LOG(LogCatFishing, Warning, TEXT("Event=fishing_rod_operate_rolled_back RequestId=%s RodActorId=%s Reason=ExplicitHoldCommitRejected %s"),
				*Command.Context.RequestId.ToString(), *Command.Context.RodActorId.ToString(), *CatLogContext::BuildControllerFields(Controller));
		}
		if (!Result.bCommitted && !bAlreadyPrimary)
		{
			Rod->ReleasePhysicalPrimaryHoldFromAuthority(PlayerState, TEXT("PickupRolledBack"));
			Rod->GetPhysicalRodComponent()->GetBody()->SetWorldTransform(PreviousParkedPose, false, nullptr, ETeleportType::TeleportPhysics);
			Rod->GetPhysicalRodComponent()->RefreshObservedPose();
		}
		Result.Error = Result.bCommitted ? ECatFishingCommandError::None : ECatFishingCommandError::DependencyUnavailable;
		Result.RodActorId = Command.Context.RodActorId;
		Result.RodActorRevision = Rod->GetPresentationState().RodActorRevision;
		if (Result.bCommitted)
		{
			const ACatFishingSession* Active = FindActiveSessionByRod(Rod);
			UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_rod_operated RequestId=%s RodActorId=%s ItemInstanceId=%s SessionId=%s ControlEpoch=%u OperatorPlayerId=%d World=%s Result=SharedItemControlled %s"),
				*Command.Context.RequestId.ToString(), *Result.RodActorId.ToString(), *Rod->GetPresentationState().ItemInstanceId.ToString(),
				Active ? *Active->GetSnapshot().FishingSessionId.ToString() : TEXT("None"), Rod->GetControlEpoch(), PlayerState->GetPlayerId(),
				*GetNameSafe(World), *CatLogContext::BuildControllerFields(Controller));
		}
		return Result;
	}
	Result.Error = ECatFishingCommandError::DependencyUnavailable;
	UE_LOG(LogCatFishing, Warning, TEXT("Event=fishing_rod_operate_rejected RequestId=%s RodActorId=%s Reason=PhysicalReceiverUnavailable %s"),
		*Command.Context.RequestId.ToString(), *Command.Context.RodActorId.ToString(), *CatLogContext::BuildControllerFields(Controller));
	return Result;
}
FCatFishingCommandResult UCatFishingService::LeaveRod(AController* Controller, const FCatLeaveRodCommand& Command)
{
	FCatFishingCommandResult Result;
	Result.CommandType = ECatFishingCommandType::LeaveRod;
	Result.RequestId = Command.Context.RequestId;
	APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
	// 仅显式主控可以放下当前竿；其它物理抓握不属于操作位。
	ACatFishingRodActor* Rod = FindRodOperatedBy(PlayerState);
	if (!Rod || Rod->GetPresentationState().RodActorId != Command.Context.RodActorId)
	{
		Result.Error = ECatFishingCommandError::NoRod;
		return Result;
	}
	const FCatFishingRodPresentationState State = Rod->GetPresentationState();
	const int32 LeavingSlotIndex = Rod->GetOperatorSlotIndex(PlayerState);
	if (ACatFishingSession* Session = FindActiveSessionByRod(Rod); Session && Session->IsFixedStepMutationBoundaryActive())
	{
		Result.Error = ECatFishingCommandError::DependencyUnavailable;
		UE_LOG(LogCatFishing, Warning, TEXT("Event=fishing_membership_busy RequestId=%s SessionId=%s RodActorId=%s Action=Leave Retryable=true World=%s Authority=true LocalRole=%d %s"),
			*Command.Context.RequestId.ToString(), *Session->GetSnapshot().FishingSessionId.ToString(),
			*Rod->GetPresentationState().RodActorId.ToString(), *GetNameSafe(GetWorld()), int32(Rod->GetLocalRole()),
			*CatLogContext::BuildControllerFields(Controller));
		return Result;
	}
	if (LeavingSlotIndex == INDEX_NONE || State.RodActorRevision != Command.Context.ExpectedRodActorRevision)
	{
		Result.Error = ECatFishingCommandError::RodActorRevisionConflict;
		return Result;
	}
	if (!RemoveOperatorAndReconcileSession(Rod, PlayerState, Command.Context.ExpectedRodActorRevision,
		Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr, TEXT("RequestedLeave")))
	{
		Result.Error = ECatFishingCommandError::RodActorRevisionConflict;
		return Result;
	}
	Result.bCommitted = true;
	Result.Error = ECatFishingCommandError::None;
	Result.RodActorId = Command.Context.RodActorId;
	Result.RodActorRevision = Rod->GetPresentationState().RodActorRevision;
	return Result;
}

UCatEquipmentComponent* UCatFishingService::ResolveRodEquipmentFromAuthority(const ACatFishingRodActor* Rod) const
{
	if (!Rod || !Rod->HasAuthority() || Rod->GetWorld() != GetWorld()) return nullptr;
	const auto& State = Rod->GetPresentationState();
	if (const auto* Preserved = PreservedRodEquipment.Find(State.RodActorId); Preserved && Preserved->IsValid())
		return Preserved->Get();
	const ACatCharacter* Owner = Cast<ACatCharacter>(Rod->GetInstigator());
	if (!Owner && State.OwnerPlayerState) Owner = Cast<ACatCharacter>(State.OwnerPlayerState->GetPawn());
	return IsValid(Owner) && Owner->GetWorld() == GetWorld() ? Owner->GetEquipmentComponent() : nullptr;
}

FCatFishingCommandResult UCatFishingService::PackRod(AController* Controller, const FCatPackRodCommand& Command)
{
	FCatFishingCommandResult Result;
	Result.CommandType = ECatFishingCommandType::PackRod;
	Result.RequestId = Command.Context.RequestId;
	APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
	ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr;
	UCatEquipmentComponent* Equipment = Character ? Character->GetEquipmentComponent() : nullptr;
	// 世界鱼竿没有独占收纳权限；来源记录仅定位同一物品实例，不能重新生成一份竿。
	ACatFishingRodActor* Rod = FindDeployedRodById(Command.Context.RodActorId);
	if (!Rod || !Character || !Equipment)
	{
		Result.Error = ECatFishingCommandError::NoRod;
		return Result;
	}
	const FCatFishingRodPresentationState RodState = Rod->GetPresentationState();
	UCatEquipmentComponent* SourceEquipment = ResolveRodEquipmentFromAuthority(Rod);
	UCatInventoryComponent* SourceInventory = SourceEquipment
		? SourceEquipment->GetOwner()->FindComponentByClass<UCatInventoryComponent>() : nullptr;
	UCatInventoryComponent* TargetInventory = Character->GetInventoryComponent();
	if (!SourceInventory || !TargetInventory || !SourceInventory->FindHeldInventoryEntryFromAuthority(RodState.ItemInstanceId))
	{
		Result.Error = ECatFishingCommandError::DependencyUnavailable;
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fishing_rod_pack_rejected RequestId=%s RodActorId=%s Reason=ItemStorageUnavailable Source=%s World=%s NetMode=%d Authority=%s LocalRole=%d %s"),
			*Command.Context.RequestId.ToString(), *RodState.RodActorId.ToString(),
			*GetNameSafe(SourceEquipment), *GetNameSafe(GetWorld()),
			static_cast<int32>(GetWorld()->GetNetMode()), Rod->HasAuthority() ? TEXT("true") : TEXT("false"),
			static_cast<int32>(Rod->GetLocalRole()), *CatLogContext::BuildControllerFields(Controller));
		return Result;
	}
	Result.RodActorId = RodState.RodActorId;
	Result.RodActorRevision = RodState.RodActorRevision;
	Result.EquipmentRevision = Equipment->GetSnapshot().Revision;
	// 收回中的实例仍暂留服务索引，库存广播重入时只能观察这份已收起事实，不能再次归还或反向恢复。
	if (!RodState.bDeployed)
	{
		Result.Error = ECatFishingCommandError::AlreadyResolved;
		return Result;
	}
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
	// 先完成可逆的世界状态提交，再归还同一库存实例。UnUse 会广播库存变化，
	// 监听者可能推进 Actor Revision；不能在归还后再因旧 Revision 拒绝，并尝试 Use 一根已断的竿。
	if (!Rod->SetDeployedFromAuthority(false, Command.Context.ExpectedRodActorRevision))
	{
		Result.Error = ECatFishingCommandError::RodActorRevisionConflict;
		Result.RodActorId = RodState.RodActorId;
		Result.RodActorRevision = Rod->GetPresentationState().RodActorRevision;
		Result.EquipmentRevision = Equipment->GetSnapshot().Revision;
		return Result;
	}
	const bool bTransfer = SourceInventory != TargetInventory;
	if (bTransfer && !SourceInventory->MoveHeldInventoryEntriesToCustodianFromAuthority(TargetInventory, {RodState.ItemInstanceId}))
	{
		Rod->SetDeployedFromAuthority(true, Rod->GetPresentationState().RodActorRevision);
		Result.Error = ECatFishingCommandError::DependencyUnavailable;
		UE_LOG(LogCatFishing, Warning, TEXT("Event=fishing_rod_pack_rejected RequestId=%s RodActorId=%s Reason=ItemTransferRejected World=%s %s"),
			*Command.Context.RequestId.ToString(), *RodState.RodActorId.ToString(), *GetNameSafe(GetWorld()),
			*CatLogContext::BuildControllerFields(Controller));
		return Result;
	}
	const FCatInventoryItemUseResult UnUseResult =
		Equipment->UnUse(Command.Context.RequestId, RodState.ItemInstanceId);
	if (UnUseResult.Error != ECatDomainCommandError::None)
	{
		const bool bItemRestored = !bTransfer
			|| TargetInventory->MoveHeldInventoryEntriesToCustodianFromAuthority(SourceInventory, {RodState.ItemInstanceId});
		// 库存未接收实例时保留原 Use 记录，恢复部署即可；破损状态不参与该回滚。
		const bool bRestored = Rod->SetDeployedFromAuthority(true,
			Rod->GetPresentationState().RodActorRevision);
		Result.Error = bRestored && bItemRestored ? MapRodInventoryUseError(UnUseResult.Error)
			: ECatFishingCommandError::DependencyUnavailable;
		Result.RodActorId = RodState.RodActorId;
		Result.RodActorRevision = Rod->GetPresentationState().RodActorRevision;
		Result.EquipmentRevision = Equipment->GetSnapshot().Revision;
		UE_LOG(LogCatFishing, Warning,
			TEXT("Event=fishing_rod_pack_rejected RequestId=%s RodActorId=%s RodItemInstanceId=%s Reason=InventoryReturnRejected InventoryError=%s Restored=%s ItemRestored=%s Broken=%s RodActorRevision=%lld EquipmentRevision=%lld %s"),
			*Command.Context.RequestId.ToString(), *RodState.RodActorId.ToString(),
			*RodState.ItemInstanceId.ToString(), *UEnum::GetValueAsString(UnUseResult.Error),
			bRestored ? TEXT("true") : TEXT("false"), bItemRestored ? TEXT("true") : TEXT("false"), RodState.bBroken ? TEXT("true") : TEXT("false"),
			Result.RodActorRevision, Result.EquipmentRevision,
			*CatLogContext::BuildControllerFields(Controller));
		return Result;
	}
	UnregisterDeployedRod(RodState.OwnerPlayerState, Rod);
	PreservedRodEquipment.Remove(RodState.RodActorId);
	if (bTransfer) SourceEquipment->RefreshLoadoutFromInventoryComponentFromAuthority();
	Result.bCommitted = true;
	Result.Error = ECatFishingCommandError::None;
	Result.RodActorId = Command.Context.RodActorId;
	Result.RodActorRevision = Rod->GetPresentationState().RodActorRevision;
	Result.EquipmentRevision = Equipment->GetSnapshot().Revision;
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_rod_packed RequestId=%s Rod=%s RodActorId=%s ItemInstance=%s EquipmentRevision=%lld DeployedRodCount=%d MaximumDeployedRods=%d World=%s NetMode=%d Authority=%s LocalRole=%d %s"),
		*Command.Context.RequestId.ToString(), *GetNameSafe(Rod), *Command.Context.RodActorId.ToString(EGuidFormats::DigitsWithHyphens),
		*RodState.ItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), Equipment->GetSnapshot().Revision,
		GetDeployedRodCount(PlayerState), GetDefault<UCatFishingSettings>()->GetMaximumDeployedRodsPerPlayer(), *GetNameSafe(GetWorld()),
		static_cast<int32>(GetWorld()->GetNetMode()), Rod->HasAuthority() ? TEXT("true") : TEXT("false"),
		static_cast<int32>(Rod->GetLocalRole()),
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

// 身体失效与主动离队共用同一移除入口；会话和冻结装备结算继续由鱼竿承载。
void UCatFishingService::ReleaseFishingOperatorForCharacter(const ACatCharacter* Character)
{
	if (!Character) return;
	if (UCatPhysicalBodyComponent* Physical = Character->GetPhysicalBodyComponent())
		Physical->ReleaseConnectionsFromAuthority(TEXT("FishingCharacterUnavailable"));
	APlayerState* PlayerState = Character->GetPlayerState();
	if (ACatFishingRodActor* Rod = FindRodOperatedBy(PlayerState))
	{
		if (ACatFishingSession* Session = FindActiveSessionByRod(Rod); Session && Session->IsFixedStepMutationBoundaryActive())
		{
			const FGuid RodId = Rod->GetPresentationState().RodActorId;
			if (!DeferredOperatorRemovals.ContainsByPredicate([PlayerState, RodId](const FDeferredOperatorRemoval& Pending)
				{ return Pending.PlayerState.Get(true) == PlayerState && Pending.RodActorId == RodId; }))
			{
				DeferredOperatorRemovals.Add({RodId, PlayerState, const_cast<ACatCharacter*>(Character)});
				UE_LOG(LogCatFishing, Log,
					TEXT("Event=fishing_operator_removal_deferred RodActorId=%s SessionId=%s PlayerId=%d World=%s NetMode=%d Authority=true LocalRole=%d Result=QueuedAfterFixedStep"),
					*RodId.ToString(), *Session->GetSnapshot().FishingSessionId.ToString(), PlayerState->GetPlayerId(),
					*GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()), int32(Character->GetLocalRole()));
			}
			return;
		}
		RemoveOperatorAndReconcileSession(Rod, PlayerState, Rod->GetPresentationState().RodActorRevision,
			Character, TEXT("CharacterUnavailable"));
	}
}

void UCatFishingService::FlushDeferredOperatorRemovalsFromAuthority()
{
	TArray<FDeferredOperatorRemoval> Pending = MoveTemp(DeferredOperatorRemovals);
	DeferredOperatorRemovals.Reset();
	for (const FDeferredOperatorRemoval& Removal : Pending)
	{
		ACatFishingRodActor* Rod = FindDeployedRodById(Removal.RodActorId);
		APlayerState* Player = Removal.PlayerState.Get(true);
		if (!Rod || !Player || Rod->GetOperatorSlotIndex(Player) == INDEX_NONE) continue;
		if (ACatFishingSession* Session = FindActiveSessionByRod(Rod); Session && Session->IsFixedStepMutationBoundaryActive())
		{
			DeferredOperatorRemovals.Add(Removal);
			continue;
		}
		RemoveOperatorAndReconcileSession(Rod, Player, Rod->GetPresentationState().RodActorRevision,
			Removal.Character.Get(true), TEXT("DeferredCharacterUnavailable"));
	}
	const auto PendingRosters = MoveTemp(DeferredPrimaryControlChecks);
	DeferredPrimaryControlChecks.Reset();
	for (const auto& Rod : PendingRosters) if (Rod.IsValid()) Rod->RefreshPrimaryControlFromAuthority();
}

bool UCatFishingService::PreserveFishingResourcesForEquipmentShutdown(UCatEquipmentComponent* Equipment)
{
	UWorld* World = GetWorld();
	if (!bCommandsOpen || !World || World->bIsTearingDown || !Equipment || !Equipment->GetOwner()
		|| !Equipment->GetOwner()->HasAuthority() || Equipment->GetWorld() != World
		|| Equipment->GetOwner()->IsA<ACatFishingResourceCustodian>()) return false;
	TArray<FGuid> SessionIds;
	TArray<ACatFishingSession*> ReboundSessions;
	TArray<FGuid> RodItemIds;
	TArray<ACatFishingRodActor*> ReboundRods;
	for (const auto& Pair : Sessions)
	{
		ACatFishingSession* Session = Pair.Value.Get();
		if (Session && !Session->IsTerminal() && Session->CastEquipment.Get(true) == Equipment)
		{
			SessionIds.Add(Pair.Key);
			ReboundSessions.Add(Session);
		}
	}
	for (const auto& Pair : DeployedRodsByPlayerState)
	{
		ACatFishingRodActor* Rod = Pair.Value.Get();
		if (!Rod) continue;
		const FGuid ItemId = Rod->GetPresentationState().ItemInstanceId;
		const UCatInventoryComponent* Inventory = Equipment->ResolveOwnerInventoryComponent();
		if (Inventory && Inventory->FindHeldInventoryEntryFromAuthority(ItemId))
		{
			bool bPendingSessionRegistration = false;
			for (TObjectIterator<UCatEquipmentComponent> It; It; ++It)
			{
				if (It->GetWorld() != World) continue;
				for (const auto& Use : It->FishingUseRecords)
					if (!Use.Value.bReleased && Use.Value.RodInventory.Get(true) == Inventory
						&& Use.Value.RodItemInstanceId == ItemId && !FindSession(Use.Key))
						bPendingSessionRegistration = true;
			}
			if (bPendingSessionRegistration) continue;
			RodItemIds.AddUnique(ItemId);
			ReboundRods.AddUnique(Rod);
		}
	}
	if (SessionIds.IsEmpty() && RodItemIds.IsEmpty()) return false;
	const FString OriginalId = Equipment->FishingResourceOwnerStableId;
	FActorSpawnParameters Spawn;
	Spawn.ObjectFlags |= RF_Transient;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACatFishingResourceCustodian* Custodian = !OriginalId.IsEmpty()
		? World->SpawnActor<ACatFishingResourceCustodian>(Spawn) : nullptr;
	UCatEquipmentComponent* Target = Custodian ? Custodian->GetEquipment() : nullptr;
	if (!Target || !Equipment->MoveFishingResourcesToCustodian(Target, SessionIds, RodItemIds))
	{
		if (Custodian) Custodian->Destroy();
		UE_LOG(LogCatFishing, Error,
			TEXT("Event=fishing_resource_custody_rejected Sessions=%d Rods=%d Reason=IdentityOrExactRecordUnavailable World=%s NetMode=%d Authority=true Owner=%s"),
			SessionIds.Num(), RodItemIds.Num(), *GetNameSafe(World), static_cast<int32>(World->GetNetMode()), *GetNameSafe(Equipment->GetOwner()));
		return false;
	}
	Custodian->InitializeOriginalOwner(OriginalId);
	Target->FishingResourceOwnerStableId = OriginalId;
	ResourceCustodians.Add(Custodian);
	for (ACatFishingSession* Session : ReboundSessions) Session->CastEquipment = Target;
	for (ACatFishingRodActor* Rod : ReboundRods)
		PreservedRodEquipment.Add(Rod->GetPresentationState().RodActorId, Target);
	UE_LOG(LogCatFishing, Display,
		TEXT("Event=fishing_resource_custody_committed Sessions=%d Rods=%d Custodian=%s World=%s NetMode=%d Authority=true LocalRole=%d Result=MovedExactRecords"),
		SessionIds.Num(), RodItemIds.Num(), *GetNameSafe(Custodian), *GetNameSafe(World),
		static_cast<int32>(World->GetNetMode()), static_cast<int32>(Custodian->GetLocalRole()));
	for (const FGuid SessionId : SessionIds)
		UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_resource_session_rebound SessionId=%s Custodian=%s World=%s NetMode=%d Authority=true LocalRole=%d"),
			*SessionId.ToString(), *GetNameSafe(Custodian), *GetNameSafe(World), static_cast<int32>(World->GetNetMode()), static_cast<int32>(Custodian->GetLocalRole()));
	for (ACatFishingRodActor* Rod : ReboundRods)
		UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_resource_rod_rebound RodActorId=%s RodItemInstanceId=%s Custodian=%s World=%s NetMode=%d Authority=true LocalRole=%d"),
			*Rod->GetPresentationState().RodActorId.ToString(), *Rod->GetPresentationState().ItemInstanceId.ToString(),
			*GetNameSafe(Custodian), *GetNameSafe(World), static_cast<int32>(World->GetNetMode()), static_cast<int32>(Custodian->GetLocalRole()));
	// 跨组件锁、Session 和竿查询均已切换；通知重入只会看到转移完成后的单一记录。
	Equipment->PublishSnapshot();
	Target->PublishSnapshot();
	return true;
}

void UCatFishingService::RefreshBiteAvailabilityFromAuthority()
{
	TArray<TWeakObjectPtr<ACatFishingSession>> PendingSessions;
	Sessions.GenerateValueArray(PendingSessions);
	for (const TWeakObjectPtr<ACatFishingSession>& Pending : PendingSessions)
	{
		if (ACatFishingSession* Session = Pending.Get(); Session && Session->HasAuthority() && !Session->IsTerminal())
		{
			Session->RefreshBiteAvailabilityFromAuthority();
			const auto& Snapshot = Session->GetSnapshot();
			const auto* Mode = GetWorld()->GetAuthGameMode<ACatfishingGameModeBase>();
			UE_LOG(LogCatFishing, Log,
				TEXT("Event=fishing_run_bite_gate_refreshed SessionId=%s CastAttemptId=%s World=%s NetMode=%d Authority=1 LocalRole=%d Actor=%s NewBitesAllowed=%d Phase=%s Result=%s"),
				*Snapshot.FishingSessionId.ToString(), *Snapshot.CastAttemptId.ToString(), *GetNameSafe(GetWorld()),
				int32(GetWorld()->GetNetMode()), int32(Session->GetLocalRole()), *Session->GetName(),
				Mode && Mode->CanGenerateNewFishingBites(), *UEnum::GetValueAsString(Snapshot.Phase),
				Session->IsTerminal() ? TEXT("SessionTerminated") : TEXT("SessionPreserved"));
		}
	}
}

// 启动失败、献祭强制中断、翻天及终局：终止 Session（含全部等待计时）再释放竿位；普通入夜仍只刷新新咬钩准入。
void UCatFishingService::SuspendFishingAndReleaseOperators()
{
	TerminateAllSessionsAndReleaseOperators(TEXT("Run unavailable"));
}

// Teardown 流程：永久关闭新入口，并让每个存活会话进入 Terminated；随后让全部手持鱼竿落地。
void UCatFishingService::CloseCommandsAndTerminateAll()
{
	bCommandsOpen = false;
	TerminateAllSessionsAndReleaseOperators(TEXT("Run teardown"));
	for (ACatFishingResourceCustodian* Custodian : ResourceCustodians)
		if (IsValid(Custodian)) Custodian->Destroy();
	ResourceCustodians.Reset();
	PreservedRodEquipment.Reset();
	DeferredOperatorRemovals.Reset();
}

void UCatFishingService::TerminateAllSessionsAndReleaseOperators(const TCHAR* DiagnosticReason)
{
	// Terminal publication invokes gameplay/UI callbacks. A callback may compact the registry through a query.
	TArray<TWeakObjectPtr<ACatFishingSession>> PendingSessions;
	Sessions.GenerateValueArray(PendingSessions);
	for (const TWeakObjectPtr<ACatFishingSession>& Pending : PendingSessions)
	{
		if (ACatFishingSession* Session = Pending.Get())
		{
			Session->TerminateSession(ECatFishingOutcome::Invalidated, DiagnosticReason);
		}
	}
	CompactSessions();
	ReleaseAllRodOperators();
}

bool UCatFishingService::RemoveOperatorAndReconcileSession(ACatFishingRodActor* Rod,
	APlayerState* PlayerState, const int64 ExpectedRevision, const ACatCharacter* LeavingCharacter,
	const TCHAR* Reason)
{
	if (!Rod || !PlayerState || !Rod->IsPrimaryOperator(PlayerState)
		|| ExpectedRevision != Rod->GetPresentationState().RodActorRevision) return false;
	if (!Rod->SetPrimaryOperatorFromAuthority(nullptr, ExpectedRevision)) return false;
	// Explicit release removes this operator's hands from this shaft only. Other cats keep their grips.
	Rod->ReleasePhysicalPrimaryHoldFromAuthority(PlayerState, FName(Reason));
	if (ACatFishingSession* Session = FindActiveSessionByRod(Rod))
	{
		Session->SuspendOperatorFromAuthority();
		// 主位空了，挂着的换人请求随之失效：那是上一任主钓手挂的牌子，而这根竿现在是无人值守——
		// 谁想接直接按 R 拾起就行（钓鱼规则 §6.3 无人值守放线），不需要也不应该再走「接手」那条门槛。
		Session->ClearHandoffRequestFromAuthority(TEXT("PrimaryReleased"));
	}
	if (auto* PC = Cast<ACatfishingPlayerController>(FindControllerForPlayerState(GetWorld(), PlayerState)))
		if (auto* Commands = PC->GetFishingCommandComponent()) Commands->ClearHeldFightInputForControlTransferFromAuthority();
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_primary_released RodActorId=%s PlayerId=%d Reason=%s World=%s NetMode=%d Authority=1 LocalRole=%d Result=Unattended"),
		*Rod->GetPresentationState().RodActorId.ToString(), PlayerState->GetPlayerId(), Reason, *GetNameSafe(GetWorld()),
		int32(GetWorld()->GetNetMode()), int32(Rod->GetLocalRole()));
	return true;
}
void UCatFishingService::ReleaseAllRodOperators()
{
	CompactDeployedRods();
	TSet<ACatFishingRodActor*> ProcessedRods;
	for (const TPair<TWeakObjectPtr<APlayerState>, TWeakObjectPtr<ACatFishingRodActor>>& Pair : DeployedRodsByPlayerState)
	{
		ACatFishingRodActor* Rod = Pair.Value.Get();
		if (!Rod || ProcessedRods.Contains(Rod))
		{
			continue;
		}
		ProcessedRods.Add(Rod);
		ReleaseRodOperators(Rod);
	}
}

void UCatFishingService::ReleaseRodOperators(ACatFishingRodActor* Rod)
{
	if (!Rod) return;
	if (APlayerState* Primary = Rod->GetPresentationState().OperatorPlayerState)
		RemoveOperatorAndReconcileSession(Rod, Primary, Rod->GetPresentationState().RodActorRevision,
			Cast<ACatCharacter>(Primary->GetPawn()), TEXT("FishingWindowClosed"));
}

bool UCatFishingService::ReconcilePrimaryControlFromPhysicalGrip(ACatFishingRodActor* Rod)
{
	if (!Rod || !Rod->HasAuthority() || !Rod->IsUsingPhysicalRod()) return false;
	APlayerState* Primary = Rod->GetPresentationState().OperatorPlayerState;
	if (!Primary) return true; // A grip can revoke existing control, never grant it.
	if (ACatFishingSession* Session = FindActiveSessionByRod(Rod); Session && Session->IsFixedStepMutationBoundaryActive())
	{
		DeferredPrimaryControlChecks.Add(Rod);
		return false;
	}
	const ACatCharacter* Character = Cast<ACatCharacter>(Primary->GetPawn());
	const auto* Physical = Character ? Character->GetPhysicalBodyComponent() : nullptr;
	if (bCommandsOpen && Rod->GetPresentationState().bDeployed && !Rod->GetPresentationState().bBroken
		&& Physical && Physical->IsLocomotionEnabled() && CanControllerStartFishingAction(Character->GetController())
		&& Rod->GetPhysicalRodComponent()->IsHeldBy(Primary)) return true;
	return RemoveOperatorAndReconcileSession(Rod, Primary, Rod->GetPresentationState().RodActorRevision,
		Character, TEXT("PrimaryPhysicalHoldLost"));
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

// 主动道具闸门流程：先按主操作位取本人当前会话，再看阶段是否落在「咬钩成立 → 本竿结局落定」这段封闭区间内。
// 区间起点取真咬而不是试探期：试探期提竿必空竿、不损饵，本竿还没成立（钓鱼规则 §3.3、§3.4）。
// 区间终点是 Resolved/Terminated——会话一旦终态，TryGetActiveSessionForController 自己就不再返回它。
// 不在竿上、不是主控、或本人这根竿还在飞行/等口，都不受闸门约束。
bool UCatFishingService::IsActiveItemUseBlockedForController(const AController* Controller)
{
	FGuid FishingSessionId;
	FCatFishingSessionSnapshot Snapshot;
	if (!TryGetActiveSessionForController(Controller, FishingSessionId, Snapshot))
	{
		return false;
	}
	switch (Snapshot.Phase)
	{
	case ECatFishingPhase::TrueBiteWindow:
	case ECatFishingPhase::HookedFight:
	case ECatFishingPhase::NearShore:
	case ECatFishingPhase::AutoHauling:
	case ECatFishingPhase::ExhaustedReel:
		return true;
	default:
		return false;
	}
}

// 鱼竿查询流程：先移除双端任一失效的弱条目，再按服务器 PlayerState 身份只读查找。
ACatFishingRodActor* UCatFishingService::FindDeployedRod(const APlayerState* PlayerState)
{
	CompactDeployedRods();
	if (!PlayerState)
	{
		return nullptr;
	}
	for (const auto& Pair : DeployedRodsByPlayerState)
	{
		if (Pair.Key.Get() == PlayerState) return Pair.Value.Get();
	}
	return nullptr;
}

int32 UCatFishingService::GetDeployedRodCount(const APlayerState* PlayerState) const
{
	if (!IsValid(PlayerState)) return 0;
	int32 Count = 0;
	for (const auto& Pair : DeployedRodsByPlayerState)
	{
		if (Pair.Key.Get() == PlayerState && Pair.Value.IsValid()) ++Count;
	}
	return Count;
}

ACatFishingRodActor* UCatFishingService::FindNearestPackableRod(const APlayerState* PlayerState,
	const FVector& WorldLocation, const double MaxDistanceCentimeters)
{
	CompactDeployedRods();
	if (!IsValid(PlayerState) || WorldLocation.ContainsNaN()
		|| !FMath::IsFinite(MaxDistanceCentimeters) || MaxDistanceCentimeters < 0.0) return nullptr;
	ACatFishingRodActor* Best = nullptr;
	double BestDistanceSquared = FMath::Square(MaxDistanceCentimeters);
	for (const auto& Pair : DeployedRodsByPlayerState)
	{
		ACatFishingRodActor* Rod = Pair.Value.Get();
		if (!Rod || !Rod->GetPresentationState().bDeployed
			|| Rod->GetOperatorCount() != 0 || FindActiveSessionByRod(Rod)) continue;
		const double DistanceSquared = FVector::DistSquared(WorldLocation, Rod->GetActorLocation());
		if (DistanceSquared <= BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			Best = Rod;
		}
	}
	return Best;
}

// 接手对象查找流程：只认「挂着换人请求」的竿，按 grip 距离取最近；不接受客户端指定目标。
// 与 FindNearestOperableRod 的区别正是这一条：那边要求竿上没人，这边要求竿上有人且那个人已经喊过「谁来接一下」。
ACatFishingRodActor* UCatFishingService::FindNearestRodAwaitingHandoff(const FVector& WorldLocation,
	const double MaxDistanceCentimeters)
{
	CompactDeployedRods();
	if (WorldLocation.ContainsNaN() || !FMath::IsFinite(MaxDistanceCentimeters) || MaxDistanceCentimeters < 0.0)
	{
		return nullptr;
	}
	ACatFishingRodActor* Best = nullptr;
	double BestDistanceSquared = FMath::Square(MaxDistanceCentimeters);
	for (const auto& Pair : DeployedRodsByPlayerState)
	{
		ACatFishingRodActor* Rod = Pair.Value.Get();
		const ACatFishingSession* Session = Rod ? FindActiveSessionByRod(Rod) : nullptr;
		if (!Rod || !Rod->GetPresentationState().bDeployed || Rod->GetPresentationState().bBroken
			|| !Session || !Session->IsHandoffRequested())
		{
			continue;
		}
		const double DistanceSquared = FVector::DistSquared(WorldLocation, Rod->GetGripWorldTransform().GetLocation());
		if (DistanceSquared <= BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			Best = Rod;
		}
	}
	return Best;
}

// 接手资格流程：先要求这根竿上真的挂着请求、请求人仍是当前主控（他中途被换掉或自己取消了，牌子就不算数），
// 墓碑（2026-09-14）：删除 50% 体力准入；Knowledge/Design/设计修改记录.md 2026-09-13 裁决④⑥。
// 保留握手及接手者依赖就绪检查，双段零体力也允许接手。
bool UCatFishingService::CanAcceptHandoffTakeover(const ACatFishingSession* Session, const ACatFishingRodActor* Rod,
	const AController* Controller, ECatFishingCommandError& OutError) const
{
	OutError = ECatFishingCommandError::HandoffNotRequested;
	APlayerState* Requester = Session ? Session->GetHandoffRequesterPlayerState() : nullptr;
	if (!Session || !Rod || !Requester || Requester != Rod->GetPresentationState().OperatorPlayerState)
	{
		return false;
	}
	const ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr;
	const UCatAbilitySystemComponent* ASC = Character ? Character->GetCatAbilitySystemComponent() : nullptr;
	if (!ASC)
	{
		OutError = ECatFishingCommandError::DependencyUnavailable;
		return false;
	}
	OutError = ECatFishingCommandError::None;
	return true;
}

// 换人握手流程：按发起者当时的身份分派，主钓手挂牌/摘牌，替补接手。
// 接手本身复用 OperateRod 那条已经写好的接管链（物理持竿 → 主控位 → 会话接管 → 提交持竿），
// 不另写一套并行的转让代码；OperateRod 里唯一为换人放开的是「竿上已有主控」那道拒绝。
FCatFishingCommandResult UCatFishingService::SubmitFishingHandoff(AController* Controller,
	const FCatRodCommandContext& Context)
{
	FCatFishingCommandResult Result;
	Result.CommandType = ECatFishingCommandType::RequestHandoff;
	Result.RequestId = Context.RequestId;
	APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
	const ACatCharacter* Character = Controller ? Cast<ACatCharacter>(Controller->GetPawn()) : nullptr;
	UWorld* World = GetWorld();
	const ACatfishingGameModeBase* GameMode = World ? World->GetAuthGameMode<ACatfishingGameModeBase>() : nullptr;
	if (!bCommandsOpen || !GameMode || !GameMode->CanAcceptFishingCommand(Controller)
		|| !CanControllerStartFishingAction(Controller) || !PlayerState || !Character)
	{
		Result.Error = ECatFishingCommandError::CommandsClosed;
		return Result;
	}
	// 分支一：自己就是主钓手 → 挂出或撤回换人请求。目标竿以服务器登记的主控竿为准，不读客户端给的 RodActorId。
	if (ACatFishingRodActor* OperatedRod = FindRodOperatedBy(PlayerState))
	{
		ACatFishingSession* Session = FindActiveSessionByRod(OperatedRod);
		Result.RodActorId = OperatedRod->GetPresentationState().RodActorId;
		Result.RodActorRevision = OperatedRod->GetPresentationState().RodActorRevision;
		if (!Session)
		{
			// 手里有竿但没有活动会话（还没抛、或这一竿已经结了）：没有可交接的对象。
			Result.Error = ECatFishingCommandError::SessionNotFound;
			return Result;
		}
		Result.FishingSessionId = Session->GetSnapshot().FishingSessionId;
		Result.bCommitted = Session->ToggleHandoffRequestFromAuthority(Controller);
		Result.Error = Result.bCommitted ? ECatFishingCommandError::None : ECatFishingCommandError::NotFisher;
		Result.Revision = Session->GetSnapshot().Revision;
		Result.SnapshotSequence = Session->GetSnapshot().SnapshotSequence;
		Result.PhaseEpoch = Session->GetSnapshot().PhaseEpoch;
		Result.CastAttemptId = Session->GetSnapshot().CastAttemptId;
		return Result;
	}
	// 分支二：岸上替补 → 接手最近一根挂着请求的竿。范围与 R 接管同一口径（grip 250cm）。
	ACatFishingRodActor* TargetRod = FindNearestRodAwaitingHandoff(Character->GetActorLocation(), 250.0);
	const ACatFishingSession* TargetSession = TargetRod ? FindActiveSessionByRod(TargetRod) : nullptr;
	if (!TargetRod || !TargetSession)
	{
		Result.Error = ECatFishingCommandError::HandoffNotRequested;
		return Result;
	}
	Result.RodActorId = TargetRod->GetPresentationState().RodActorId;
	Result.RodActorRevision = TargetRod->GetPresentationState().RodActorRevision;
	Result.FishingSessionId = TargetSession->GetSnapshot().FishingSessionId;
	ECatFishingCommandError TakeoverError = ECatFishingCommandError::HandoffNotRequested;
	if (!CanAcceptHandoffTakeover(TargetSession, TargetRod, Controller, TakeoverError))
	{
		Result.Error = TakeoverError;
		UE_LOG(LogCatFishing, Log,
			TEXT("Event=fishing_handoff_accept_rejected SessionId=%s Reason=%s %s"),
			*Result.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
			*UEnum::GetValueAsString(TakeoverError), *CatLogContext::BuildControllerFields(Controller));
		return Result;
	}
	const FGuid TakeoverSessionId = Result.FishingSessionId;
	FCatOperateRodCommand TakeoverCommand;
	TakeoverCommand.Context.RequestId = Context.RequestId;
	TakeoverCommand.Context.RodActorId = TargetRod->GetPresentationState().RodActorId;
	TakeoverCommand.Context.ExpectedRodActorRevision = TargetRod->GetPresentationState().RodActorRevision;
	Result = OperateRod(Controller, TakeoverCommand);
	// OperateRod 回的是「接管鱼竿」的结果，这里把它改标成换人，并补回会话身份——
	// 玩家按的是换人键，回执上的命令类型和会话必须是他按的那件事。
	Result.CommandType = ECatFishingCommandType::RequestHandoff;
	Result.FishingSessionId = TakeoverSessionId;
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_handoff_accepted SessionId=%s Committed=%s Error=%s %s"),
		*Result.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
		Result.bCommitted ? TEXT("true") : TEXT("false"), *UEnum::GetValueAsString(Result.Error),
		*CatLogContext::BuildControllerFields(Controller));
	return Result;
}

ACatFishingRodActor* UCatFishingService::FindNearestOperableRod(const APlayerState* PlayerState,
	const FVector& WorldLocation, const double MaxDistanceCentimeters)
{
	CompactDeployedRods();
	if (!IsValid(PlayerState) || WorldLocation.ContainsNaN()
		|| !FMath::IsFinite(MaxDistanceCentimeters) || MaxDistanceCentimeters < 0.0) return nullptr;
	ACatFishingRodActor* Best = nullptr;
	double BestDistanceSquared = FMath::Square(MaxDistanceCentimeters);
	for (const auto& Pair : DeployedRodsByPlayerState)
	{
		ACatFishingRodActor* Rod = Pair.Value.Get();
		if (!Rod || !Rod->GetPresentationState().bDeployed
			|| Rod->GetOperatorCount() != 0 || Rod->GetPresentationState().bBroken) continue;
		const double DistanceSquared = FVector::DistSquared(WorldLocation, Rod->GetGripWorldTransform().GetLocation());
		if (DistanceSquared <= BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			Best = Rod;
		}
	}
	return Best;
}

// 多人竿共享查询组：都先压缩失效登记再线性扫描（部署竿数量=玩家数量级，线性可接受）。
ACatFishingRodActor* UCatFishingService::FindDeployedRodById(const FGuid RodActorId)
{
	CompactDeployedRods();
	if (!RodActorId.IsValid()) return nullptr;
	for (const TPair<TWeakObjectPtr<APlayerState>, TWeakObjectPtr<ACatFishingRodActor>>& Pair : DeployedRodsByPlayerState)
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
	for (const TPair<TWeakObjectPtr<APlayerState>, TWeakObjectPtr<ACatFishingRodActor>>& Pair : DeployedRodsByPlayerState)
	{
		ACatFishingRodActor* Rod = Pair.Value.Get();
		if (Rod && Rod->GetOperatorSlotIndex(const_cast<APlayerState*>(PlayerState)) != INDEX_NONE) return Rod;
	}
	return nullptr;
}

ACatFishingRodActor* UCatFishingService::FindNearestUnattendedSessionRod(const FVector& WorldLocation,
	const double MaxDistanceCentimeters)
{
	CompactDeployedRods();
	CompactSessions();
	ACatFishingRodActor* Best = nullptr;
	double BestDistanceSquared = FMath::Square(FMath::Max(0.0, MaxDistanceCentimeters));
	for (const TPair<TWeakObjectPtr<APlayerState>, TWeakObjectPtr<ACatFishingRodActor>>& Pair
		: DeployedRodsByPlayerState)
	{
		ACatFishingRodActor* Rod = Pair.Value.Get();
		if (!Rod || !Rod->GetPresentationState().bDeployed
			|| Rod->GetPresentationState().PoseMode != ECatFishingRodPoseMode::Grounded
			|| Rod->GetOperatorCount() != 0 || !FindActiveSessionByRod(Rod))
		{
			continue;
		}
		const double DistanceSquared = FVector::DistSquared(WorldLocation, Rod->GetActorLocation());
		if (DistanceSquared <= BestDistanceSquared)
		{
			BestDistanceSquared = DistanceSquared;
			Best = Rod;
		}
	}
	return Best;
}

ACatFishingSession* UCatFishingService::FindActiveSessionByRod(const ACatFishingRodActor* RodActor) const
{
	// A domain read may run inside batch termination; it must not mutate the registry being traversed.
	if (!RodActor) return nullptr;
	for (const TPair<FGuid, TWeakObjectPtr<ACatFishingSession>>& Pair : Sessions)
	{
		ACatFishingSession* Session = Pair.Value.Get();
		if (Session && !Session->IsTerminal() && Session->GetSnapshot().RodActor == RodActor) return Session;
	}
	return nullptr;
}

// 墓碑（2026-09-14，T15；钓鱼规则 §5.5）：FindNearestScoopableSession 已无消费者，F 改传准星目标身份。


// 显式接管编排：等口恢复当前钓手身份，HookedFight 同时重绑当前钓手的 Runner。
bool UCatFishingService::ResumeSessionControl(ACatFishingSession* Session, AController* NewFisherController)
{
	CompactSessions();
	const FString NewFisherId = ResolveStableNetId(NewFisherController);
	if (!Session || Session->IsTerminal() || NewFisherId.IsEmpty()) return false;
	if (Session->GetFisherStableNetIdForAuthority() == NewFisherId) return true;
	if (!Session->ResumePrimaryControlFromAuthority(NewFisherController)) return false;
	// 成功事件由 Session 的唯一状态写口记录完整且脱敏的 Controller 上下文，服务层不再重复输出原始 StableNetId。
	return true;
}

// 鱼竿登记流程：每个实体 Actor 只登记一次；本人最多两根，操作位仍不能跨竿重复占用。
bool UCatFishingService::RegisterDeployedRod(APlayerState* PlayerState, ACatFishingRodActor* RodActor)
{
	CompactDeployedRods();
	if (!IsValid(PlayerState) || !IsValid(RodActor))
	{
		return false;
	}
	const TWeakObjectPtr<APlayerState> PlayerKey(PlayerState);
	for (const auto& Pair : DeployedRodsByPlayerState)
	{
		if (Pair.Value.Get() == RodActor) return Pair.Key == PlayerKey;
		if (RodActor->GetPresentationState().RodActorId.IsValid()
			&& Pair.Value->GetPresentationState().RodActorId == RodActor->GetPresentationState().RodActorId)
		{
			return false;
		}
	}
	const FCatFishingRodPresentationState& State = RodActor->GetPresentationState();
	if ((State.OwnerPlayerState && State.OwnerPlayerState != PlayerState)
		|| GetDeployedRodCount(PlayerState) >= GetDefault<UCatFishingSettings>()->GetMaximumDeployedRodsPerPlayer()) return false;
	for (APlayerState* Operator : State.OperatorPlayerStates)
	{
		if (FindRodOperatedBy(Operator)) return false;
	}
	DeployedRodsByPlayerState.Add(PlayerKey, RodActor);
	if (const ACatCharacter* OwnerCharacter = Cast<ACatCharacter>(RodActor->GetInstigator()))
	{
		if (UCatEquipmentComponent* Equipment = OwnerCharacter->GetEquipmentComponent();
			Equipment && Equipment->FishingResourceOwnerStableId.IsEmpty() && PlayerState->GetUniqueId().IsValid())
			Equipment->FishingResourceOwnerStableId = PlayerState->GetUniqueId()->ToString();
	}
	return true;
}

// 报废鱼竿撤场流程：先松开所有仍抓着它的爪子，再从服务索引里摘掉，最后销毁 Actor。
// 顺序不能反：先销毁会让后两步拿不到有效指针，操作位和索引就会留下悬空记录。
void UCatFishingService::RetireDeployedRodActorFromAuthority(ACatFishingRodActor* RodActor)
{
	if (!IsValid(RodActor))
	{
		return;
	}
	ReleaseRodOperators(RodActor);
	const FGuid RodActorId = RodActor->GetPresentationState().RodActorId;
	UnregisterDeployedRod(RodActor->GetPresentationState().OwnerPlayerState, RodActor);
	PreservedRodEquipment.Remove(RodActorId);
	const bool bDestroyed = RodActor->Destroy();
	// 控制权交接过的竿，OwnerPlayerState 可能已经不是当初登记的那个键，上面那次注销就会落空。
	// 销毁之后再补一次压缩：弱引用此刻已失效，残留的索引项在这里一并清掉，不留悬空记录。
	UnregisterDeployedRod(nullptr, nullptr);
	UE_LOG(LogCatFishing, Log,
		TEXT("Event=fishing_broken_rod_actor_retired RodActorId=%s Destroyed=%s World=%s"),
		*RodActorId.ToString(EGuidFormats::DigitsWithHyphens), bDestroyed ? TEXT("true") : TEXT("false"),
		*GetNameSafe(GetWorld()));
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
	// EndPlay 时普通 Weak.Get() 已可能返回空；允许 pending-kill 读取只用于和调用方 Actor 做同一性校验及最终补偿。
	ACatFishingRodActor* ExistingRod = nullptr;
	for (const auto& Pair : DeployedRodsByPlayerState)
	{
		if (Pair.Key == PlayerKey && Pair.Value.Get(true) == ExpectedRodActor)
		{
			ExistingRod = Pair.Value.Get(true);
			break;
		}
	}
	if (ExistingRod)
	{
		// EndPlay/异常销毁也从这里注销；先释放该竿全部操作位与牵引，再清理服务自己的弱索引。
		ReleaseRodOperators(ExistingRod);
		DeployedRodsByPlayerState.RemoveSingle(PlayerKey, TWeakObjectPtr<ACatFishingRodActor>(ExistingRod));
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
		: DeployedRodsByPlayerState)
	{
		if (Pair.Value.IsValid() && (Pair.Key.IsValid()
			|| PreservedRodEquipment.Contains(Pair.Value->GetPresentationState().RodActorId)))
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

// 已部署鱼竿弱索引压缩流程：任一弱端失效即删除整条登记，不保留可阻塞后续 Place 的旧槽位。
void UCatFishingService::CompactDeployedRods()
{
	for (auto It = DeployedRodsByPlayerState.CreateIterator(); It; ++It)
	{
		const ACatFishingRodActor* Rod = It.Value().Get();
		if (!Rod || (!It.Key().IsValid() && !PreservedRodEquipment.Contains(Rod->GetPresentationState().RodActorId)))
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
	const UCatAbilitySystemComponent* ASC = Character ? Character->GetCatAbilitySystemComponent() : nullptr;
	const FString StableNetId = ResolveStableNetId(Controller);
	if (!World || !GameMode || !Character || !Conditions || !ASC || StableNetId.IsEmpty()
		|| !GameMode->CanAcceptGameplayCommand(Controller) || Conditions->GetSnapshot().bDowned)
	{
		return false;
	}
	const double Strength = ASC->GetNumericAttribute(UCatSurvivalAttributeSet::GetFishingStrengthAttribute());
	// 墓碑（2026-09-14）：合力/抽鱼能力不再只认绿段；设计修改记录 2026-09-13 裁决②。
	const double FightStamina = ASC->GetTotalFightStamina();
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
