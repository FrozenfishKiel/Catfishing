#include "Environment/CatChumFieldReplicationComponent.h"

#include "Engine/World.h"
#include "Environment/Presentation/CatChumFieldPresentationActor.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

void FCatChumFieldPublicItem::PreReplicatedRemove(const FCatChumFieldPublicArray& ArraySerializer)
{
	if (ArraySerializer.Owner) ArraySerializer.Owner->HandleReplicatedRemove(FieldId);
}

void FCatChumFieldPublicItem::PostReplicatedAdd(const FCatChumFieldPublicArray& ArraySerializer)
{
	if (ArraySerializer.Owner) ArraySerializer.Owner->HandleReplicatedAdd(*this);
}

void FCatChumFieldPublicItem::PostReplicatedChange(const FCatChumFieldPublicArray& ArraySerializer)
{
	if (ArraySerializer.Owner) ArraySerializer.Owner->HandleReplicatedChange(*this);
}

UCatChumFieldReplicationComponent::UCatChumFieldReplicationComponent()
{
	SetIsReplicatedByDefault(true);
	PublicFields.Owner = this;
}

void UCatChumFieldReplicationComponent::BeginPlay()
{
	Super::BeginPlay();
	PublicFields.Owner = this;
	OnRep_PublicFields();
}

void UCatChumFieldReplicationComponent::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, PublicFields);
}

void UCatChumFieldReplicationComponent::ReconcileFieldFromAuthority(const FCatChumFieldState& Field)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Field.FieldId.IsValid()) return;
	FCatChumFieldPublicItem* Item = PublicFields.Items.FindByPredicate(
		[&Field](const FCatChumFieldPublicItem& Candidate) { return Candidate.FieldId == Field.FieldId; });
	const bool bAdded = Item == nullptr;
	if (bAdded) Item = &PublicFields.Items.AddDefaulted_GetRef();
	Item->FieldId = Field.FieldId;
	Item->WaterRegion = Field.WaterRegion;
	Item->ChumDefinitionId = Field.ChumDefinitionId;
	Item->CenterWorldPoint = Field.CenterWorldPoint;
	Item->RadiusCentimeters = Field.Influence.RadiusCentimeters;
	Item->StartServerTime = Field.StartServerTime;
	Item->ExpireServerTime = Field.ExpireServerTime;
	Item->Source = Field.Source;
	Item->PresentationId = Field.Influence.PresentationId;
	PublicFields.MarkItemDirty(*Item);
	ReconcilePresentation(*Item, bAdded);
	if (bAdded) OnFieldAdded.Broadcast(*Item); else OnFieldChanged.Broadcast(*Item);
	GetOwner()->ForceNetUpdate();
}

void UCatChumFieldReplicationComponent::RemoveFieldFromAuthority(const FGuid FieldId)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !FieldId.IsValid()) return;
	const int32 Index = PublicFields.Items.IndexOfByPredicate(
		[FieldId](const FCatChumFieldPublicItem& Item) { return Item.FieldId == FieldId; });
	if (Index == INDEX_NONE) return;
	PublicFields.Items.RemoveAt(Index);
	PublicFields.MarkArrayDirty();
	HandleReplicatedRemove(FieldId);
	GetOwner()->ForceNetUpdate();
}

void UCatChumFieldReplicationComponent::OnRep_PublicFields()
{
	PublicFields.Owner = this;
}

void UCatChumFieldReplicationComponent::HandleReplicatedAdd(const FCatChumFieldPublicItem& Item)
{
	ReconcilePresentation(Item, true);
	OnFieldAdded.Broadcast(Item);
}

void UCatChumFieldReplicationComponent::HandleReplicatedChange(const FCatChumFieldPublicItem& Item)
{
	ReconcilePresentation(Item, false);
	OnFieldChanged.Broadcast(Item);
}

void UCatChumFieldReplicationComponent::HandleReplicatedRemove(const FGuid FieldId)
{
	if (TObjectPtr<ACatChumFieldPresentationActor>* Actor = PresentationActors.Find(FieldId))
	{
		if (*Actor)
		{
			(*Actor)->NotifyFieldRemoved();
			(*Actor)->Destroy();
		}
		PresentationActors.Remove(FieldId);
	}
	OnFieldRemoved.Broadcast(FieldId);
}

void UCatChumFieldReplicationComponent::ReconcilePresentation(
	const FCatChumFieldPublicItem& Item, const bool bAdded)
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer) return;
	ACatChumFieldPresentationActor* Actor = PresentationActors.FindRef(Item.FieldId);
	if (!IsValid(Actor))
	{
		Actor = World->SpawnActor<ACatChumFieldPresentationActor>(
			ACatChumFieldPresentationActor::StaticClass(), Item.CenterWorldPoint, FRotator::ZeroRotator);
		if (!Actor) return;
		PresentationActors.Add(Item.FieldId, Actor);
		Actor->ApplyPublicState(Item, true);
		return;
	}
	Actor->ApplyPublicState(Item, bAdded);
}
