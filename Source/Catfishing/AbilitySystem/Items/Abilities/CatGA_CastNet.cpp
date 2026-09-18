#include "AbilitySystem/Items/Abilities/CatGA_CastNet.h"
#include "Inventory/Fragments/CatItemUseFragment.h"
#include "Inventory/Fragments/CatItemDropFragment.h"
#include "Inventory/CatInventoryComponent.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "Data/CatFishCatalogSettings.h"
#include "Data/CatFishDefinition.h"
#include "Fishing/Presentation/CatFishPresentationDefinition.h"
#include "Environment/CatChumFieldSubsystem.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/Actors/CatCastNetCatchEmitter.h"
#include "Components/PrimitiveComponent.h"
#include "EngineUtils.h"
#include "Fishing/Integration/CatFishingAimLibrary.h"
#include "Framework/Game/CatfishingGameState.h"
#include "Collection/CatRunFishCollectionComponent.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Logging/CatLog.h"
#include "Misc/ScopeExit.h"

// 配置检查流程：渔网只消耗一件，数量和空间参数必须有效；效果走每条鱼的捕获结算而非自用 GE。
bool UCatGA_CastNet::ValidateUseConfiguration(const UCatItemUseFragment& Config, FText& Error) const
{
	const bool bValid = Config.ConsumeCount == 1 && Config.ResourceCapacity == 0 && Config.Effects.IsEmpty()
		&& MinimumFish > 0 && MaximumFish >= MinimumFish && FMath::IsFinite(CastRange) && CastRange > 0
		&& FMath::IsFinite(FishEmissionInterval) && FishEmissionInterval >= 0.05f
		&& FMath::IsFinite(SuppressionRadius) && SuppressionRadius > 0 && FMath::IsFinite(SuppressionSeconds) && SuppressionSeconds > 0;
	if (!bValid) Error = NSLOCTEXT("CatItem", "NetConfig", "渔网需一次性消耗一件、有效鱼数区间和正的范围与时长。");
	return bValid;
}
// 水面检查流程：从冻结视线求交，按真实身体位置核射程，再阻挡穿墙与重复覆盖禁生区。
FCatWaterSpatialResult UCatGA_CastNet::ResolveWater() const
{
	FVector Origin, Direction;
	if (!ResolveUseRay(Origin, Direction)) return {};
	auto* Water = GetWorld()->GetSubsystem<UCatWaterQuerySubsystem>();
	const auto Region = UCatFishingAimLibrary::FindNearestWaterRegion(GetAvatarActorFromActorInfo(), Origin);
	auto Result = Water ? Water->ResolveRayToWater(Origin, Direction, Region) : FCatWaterSpatialResult{};
	if (!Result.bSucceeded || Result.Containment == ECatWaterContainment::Outside
		|| FVector::Dist(GetAvatarActorFromActorInfo()->GetActorLocation(), Result.WaterSurfaceWorldPoint) > CastRange
		|| Water->IsFishSpawnSuppressed(Result.WaterSurfaceWorldPoint, Result.WaterRegion)) return {};
	FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(CastNet), false, GetAvatarActorFromActorInfo());
	if (GetWorld()->LineTraceSingleByChannel(Hit, CastChecked<APawn>(GetAvatarActorFromActorInfo())->GetPawnViewLocation(), Result.WaterSurfaceWorldPoint, ECC_Visibility, Query)
		&& FVector::DistSquared(Hit.ImpactPoint, Result.WaterSurfaceWorldPoint) > 100.0) return {};
	return Result;
}
// 能力预检流程：来源和身体合法后核水面；鱼种与表现依赖在提交阶段整批准备。
bool UCatGA_CastNet::ValidateUse() const { return Super::ValidateUse() && ResolveWater().bSucceeded; }

// 撒网提交流程：冻结环境和中性倍率，按同一鱼目录抽取非巨物；整批生成、初始化成功前不扣网。
// 准备完鱼后复用该执行者的出鱼宿主，没有才新建；扣网成功即发布禁生区并逐条结算奖励。
// 最后将隐藏鱼追加到独立出鱼队列并结束能力；准备或扣网失败只清本次候选及本次新建宿主，不动已有队列。
void UCatGA_CastNet::CommitUse()
{
	if (!IsActive() || bUseCommitted || !CurrentActorInfo || !CurrentActorInfo->IsNetAuthority()) return;
	IncrementListLock(); ON_SCOPE_EXIT { DecrementListLock(); };
	TArray<ACatFishPickupActor*> Fish;
	ACatCastNetCatchEmitter* Emitter = nullptr;
	bool bCreatedEmitter = false;
	ON_SCOPE_EXIT { if (!bUseCommitted) { for (auto* Pickup : Fish) if (IsValid(Pickup)) Pickup->Destroy(); if (bCreatedEmitter) Emitter->Destroy(); } };
	const auto Fail = [&]() { EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, true); };
	if (!ValidateUse()) { Fail(); return; }
	const auto WaterHit = ResolveWater();
	auto* World = GetWorld(); auto* GameState = World->GetGameState<ACatfishingGameState>();
	auto* Chum = World->GetSubsystem<UCatChumFieldSubsystem>(); auto* Fishing = World->GetSubsystem<UCatFishingService>();
	auto* Controller = CurrentActorInfo->PlayerController.Get(); auto* Player = Controller ? Controller->PlayerState.Get() : nullptr;
	if (!GameState || !Chum || !Fishing || !Player || !Player->GetUniqueId().IsValid()) { Fail(); return; }
	const FString ExecutorId = Player->GetUniqueId()->ToString();
	FCatFishSelectionContext Selection; Selection.WaterRegion = WaterHit.WaterRegion;
	Selection.ChumSample = Chum->SampleChumAtPoint(WaterHit.WaterSurfaceWorldPoint, WaterHit.WaterRegion, World->GetTimeSeconds());
	Selection.TimeOfDay = GameState->GetRunPublicState().Environment.TimeOfDay;
	Selection.Weather = GameState->GetRunPublicState().Environment.Weather;
	Selection.bExcludeGiant = true;
	Fishing->BuildFightCapabilitySnapshot(Selection.ActivePlayerCount, Selection.CombinedFishingStrength, Selection.CombinedFightStamina);
	FCatCaptureConditionSnapshot Condition; Condition.RegionId = Selection.WaterRegion.RegionId;
	Condition.TimeOfDayId = FName(*UEnum::GetValueAsString(Selection.TimeOfDay)); Condition.WeatherId = FName(*UEnum::GetValueAsString(Selection.Weather));
	FRandomStream Random(GetTypeHash(UseTarget.RequestId)); const int32 Count = Random.RandRange(MinimumFish, MaximumFish);
	const auto* Catalog = GetDefault<UCatFishCatalogSettings>();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Selection.RandomSeed = Random.GetUnsignedInt(); const auto Selected = Catalog->SelectRuntimeDefinition(Selection);
		auto* Definition = Selected.bSelected ? Catalog->FindRuntimeDefinition(Selected.ItemId) : nullptr;
		const auto* Presentation = Definition ? Definition->LoadRuntimePresentationDefinition() : nullptr;
		UClass* Class = Definition ? Definition->WorldActorClass.LoadSynchronous() : nullptr;
		if (!Presentation || !Class || !Class->IsChildOf(ACatFishPickupActor::StaticClass())) { Fail(); return; }
		FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector Location = GetAvatarActorFromActorInfo()->GetActorLocation() + FVector(30, (Index - Count / 2) * 15, 30);
		auto* Pickup = World->SpawnActor<ACatFishPickupActor>(Class, Location, FRotator::ZeroRotator, Params);
		if (Pickup) Fish.Add(Pickup);
		if (!Pickup || !Pickup->InitializeFromAuthority(UseTarget.RequestId, FGuid::NewGuid(), Definition, Selected.WeightKilograms,
			Presentation->ComputeUniformVisualScale(Selected.WeightKilograms), Condition, ExecutorId, {ExecutorId})) { Fail(); return; }
		Pickup->SetActorHiddenInGame(true); Pickup->SetActorEnableCollision(false);
		Pickup->SetReplicates(false);
		if (auto* Body = Cast<UPrimitiveComponent>(Pickup->GetRootComponent())) Body->SetSimulatePhysics(false);
	}
	// 同一执行者尚未喷完时追加到其渔网队列；不与库存丢弃互相阻塞。
	for (TActorIterator<ACatCastNetCatchEmitter> It(World); It; ++It)
		if (!It->IsActorBeingDestroyed() && It->GetOwner() == GetAvatarActorFromActorInfo()) { Emitter = *It; break; }
	if (!Emitter)
	{
		Emitter = World->SpawnActor<ACatCastNetCatchEmitter>(GetAvatarActorFromActorInfo()->GetActorLocation(), GetAvatarActorFromActorInfo()->GetActorRotation());
		bCreatedEmitter = Emitter != nullptr;
		if (Emitter) Emitter->SetOwner(GetAvatarActorFromActorInfo());
	}
	if (!Emitter) { Fail(); return; }
	CommittedSource = ResolveSourceItem();
	if (!CommitAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo) || !bResourceCommitted) { Fail(); return; }
	bUseCommitted = true;
	World->GetSubsystem<UCatWaterQuerySubsystem>()->AddFishSpawnSuppressionFromAuthority(WaterHit.WaterSurfaceWorldPoint, WaterHit.WaterRegion, SuppressionRadius, SuppressionSeconds);
	for (auto* Pickup : Fish)
	{
		const auto& State = Pickup->GetPresentationState();
		if (auto* Collection = GameState->GetRunFishCollection()) Collection->RecordCaptureFromAuthority(State.FishInstanceId, State.ItemId, ExecutorId);
		if (const auto* Drops = Pickup->GetFishDefinition()->FindFragment<UCatItemDropFragment>())
			Drops->AwardCaptureDropsFromAuthority(GetAvatarActorFromActorInfo(), State.FishInstanceId);
	}
	Emitter->StartFromAuthority(GetAvatarActorFromActorInfo(), Fish, FishEmissionInterval);
	UseTarget.Inventory->BroadcastInventoryChange();
	UE_LOG(LogCatFishing, Log, TEXT("Event=cast_net_completed RequestId=%s Executor=%s FishCount=%d Region=%s World=%s NetMode=%d Authority=1"),
		*UseTarget.RequestId.ToString(), *GetNameSafe(GetAvatarActorFromActorInfo()), Fish.Num(), *Selection.WaterRegion.RegionId.ToString(), *GetNameSafe(World), World->GetNetMode());
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}
