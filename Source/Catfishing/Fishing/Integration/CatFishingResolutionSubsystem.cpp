#include "Fishing/Integration/CatFishingResolutionSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "Social/CatRoomOwnerService.h"
#include "Logging/CatLog.h"

bool UCatFishingResolutionSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}
void UCatFishingResolutionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UCatRoomOwnerService>();
	// 墓碑（2026-09-14，T15；钓鱼规则 §5.4）：不能用 RPC/Timer 回调先后代替收鱼→苏醒→落水。
	// 引擎 PostActorTick 在 TimerManager 和 PostUpdateWork 之后，同 tick 的生产者已提交完毕。
	PostTickHandle = FWorldDelegates::OnWorldPostActorTick.AddUObject(this, &ThisClass::Flush);
}
void UCatFishingResolutionSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldPostActorTick.Remove(PostTickHandle);
	Pending.Reset();
	Super::Deinitialize();
}
void UCatFishingResolutionSubsystem::Enqueue(ECatFishingResolution Phase, AController* Requester,
	FGuid RequestId, TFunction<void()> Resolve)
{
	const UCatRoomOwnerService* Room = GetWorld()->GetSubsystem<UCatRoomOwnerService>();
	const int64 Seat = Room ? Room->GetJoinSequence(Requester) : MAX_int64;
	Pending.Add({Phase, GetWorld()->GetTimeSeconds(), Seat, NextSequence++, RequestId, MoveTemp(Resolve)});
	UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_resolution_queued RequestId=%s Phase=%d ReceivedTime=%.9f Seat=%lld World=%s NetMode=%d Authority=1"),
		*RequestId.ToString(), int32(Phase), GetWorld()->GetTimeSeconds(), Seat, *GetNameSafe(GetWorld()), int32(GetWorld()->GetNetMode()));
}
void UCatFishingResolutionSubsystem::Flush(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	if (World != GetWorld() || Pending.IsEmpty()) return;
	TArray<FPending> Batch = MoveTemp(Pending);
	Pending.Reset();
	Batch.Sort([](const FPending& A, const FPending& B)
	{
		if (A.Phase != B.Phase) return A.Phase < B.Phase;
		if (A.ReceivedTime != B.ReceivedTime) return A.ReceivedTime < B.ReceivedTime;
		if (A.Seat != B.Seat) return A.Seat < B.Seat;
		return A.Sequence < B.Sequence; // 仅同席位同时间请求保持自身顺序。
	});
	for (FPending& Event : Batch)
	{
		UE_LOG(LogCatFishing, Log, TEXT("Event=fishing_resolution_applied RequestId=%s Phase=%d ReceivedTime=%.9f Seat=%lld World=%s NetMode=%d Authority=1"),
			*Event.RequestId.ToString(), int32(Event.Phase), Event.ReceivedTime, Event.Seat, *GetNameSafe(World), int32(World->GetNetMode()));
		Event.Resolve();
	}
}
