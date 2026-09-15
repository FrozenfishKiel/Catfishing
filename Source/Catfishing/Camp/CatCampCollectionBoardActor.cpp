#include "Camp/CatCampCollectionBoardActor.h"

#include "Collection/CatRunFishCollectionComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Framework/Game/CatfishingGameState.h"
#include "Logging/CatLog.h"

ACatCampCollectionBoardActor::ACatCampCollectionBoardActor()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = false;
	BoardMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BoardMesh"));
	SetRootComponent(BoardMesh);
	BoardMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
}

void ACatCampCollectionBoardActor::BeginPlay()
{
	Super::BeginPlay();
	GameStateSetHandle = GetWorld()->GameStateSetEvent.AddUObject(this, &ThisClass::BindGameState);
	BindGameState(GetWorld()->GetGameState());
}

void ACatCampCollectionBoardActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (BoundCollection.IsValid()) BoundCollection->OnCollectionChanged.RemoveDynamic(this, &ThisClass::RefreshPresentation);
	BoundCollection.Reset();
	if (GetWorld()) GetWorld()->GameStateSetEvent.Remove(GameStateSetHandle);
	Super::EndPlay(EndPlayReason);
}

void ACatCampCollectionBoardActor::BindGameState(AGameStateBase* GameState)
{
	if (BoundCollection.IsValid()) BoundCollection->OnCollectionChanged.RemoveDynamic(this, &ThisClass::RefreshPresentation);
	const ACatfishingGameState* CatGameState = Cast<ACatfishingGameState>(GameState);
	BoundCollection = CatGameState ? CatGameState->GetRunFishCollection() : nullptr;
	if (BoundCollection.IsValid()) BoundCollection->OnCollectionChanged.AddUniqueDynamic(this, &ThisClass::RefreshPresentation);
	RefreshPresentation();
}

FCatRunFishCollectionSnapshot ACatCampCollectionBoardActor::GetCollectionSnapshot() const
{
	const ACatfishingGameState* GameState = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
	return GameState && GameState->GetRunFishCollection()
		? GameState->GetRunFishCollection()->GetSnapshot() : FCatRunFishCollectionSnapshot();
}

void ACatCampCollectionBoardActor::RefreshPresentation()
{
	const FCatRunFishCollectionSnapshot Collection = GetCollectionSnapshot();
	BP_RefreshCollectionPresentation(Collection);
	UE_LOG(LogCatRun, Log,
		TEXT("Event=run_collection_board_refreshed World=%s NetMode=%d Authority=%d LocalRole=%d Actor=%s RunId=%s Revision=%lld Pages=%d Bound=%d"),
		*GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole(), *GetNameSafe(this),
		*Collection.RunId.ToString(), Collection.Revision, Collection.Pages.Num(), BoundCollection.IsValid());
}
