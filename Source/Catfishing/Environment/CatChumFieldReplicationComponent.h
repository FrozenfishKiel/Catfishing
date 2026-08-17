#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Environment/CatChumFieldTypes.h"
#include "Net/Serialization/FastArraySerializer.h"

#include "CatChumFieldReplicationComponent.generated.h"

class ACatChumFieldPresentationActor;
class UCatChumFieldReplicationComponent;

USTRUCT(BlueprintType)
struct FCatChumFieldPublicItem : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FGuid FieldId;

	UPROPERTY(BlueprintReadOnly)
	FCatWaterRegionHandle WaterRegion;

	UPROPERTY(BlueprintReadOnly)
	FName ChumDefinitionId = NAME_None;

	UPROPERTY(BlueprintReadOnly)
	FVector CenterWorldPoint = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	double RadiusCentimeters = 0.0;

	UPROPERTY(BlueprintReadOnly)
	double StartServerTime = 0.0;

	UPROPERTY(BlueprintReadOnly)
	double ExpireServerTime = 0.0;

	UPROPERTY(BlueprintReadOnly)
	ECatChumFieldSource Source = ECatChumFieldSource::Player;

	UPROPERTY(BlueprintReadOnly)
	FName PresentationId = NAME_None;

	void PreReplicatedRemove(const struct FCatChumFieldPublicArray& ArraySerializer);
	void PostReplicatedAdd(const struct FCatChumFieldPublicArray& ArraySerializer);
	void PostReplicatedChange(const struct FCatChumFieldPublicArray& ArraySerializer);
};

USTRUCT()
struct FCatChumFieldPublicArray : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FCatChumFieldPublicItem> Items;

	UCatChumFieldReplicationComponent* Owner = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParameters)
	{
		return FastArrayDeltaSerialize<FCatChumFieldPublicItem, FCatChumFieldPublicArray>(
			Items, DeltaParameters, *this);
	}
};

template<>
struct TStructOpsTypeTraits<FCatChumFieldPublicArray> : TStructOpsTypeTraitsBase2<FCatChumFieldPublicArray>
{
	enum { WithNetDeltaSerializer = true };
};

DECLARE_MULTICAST_DELEGATE_OneParam(FCatChumFieldPublicChanged, const FCatChumFieldPublicItem&);
DECLARE_MULTICAST_DELEGATE_OneParam(FCatChumFieldPublicRemoved, FGuid);

UCLASS(ClassGroup=(Catfishing), meta=(BlueprintSpawnableComponent))
class CATFISHING_API UCatChumFieldReplicationComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	UCatChumFieldReplicationComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void ReconcileFieldFromAuthority(const FCatChumFieldState& Field);
	void RemoveFieldFromAuthority(FGuid FieldId);
	const TArray<FCatChumFieldPublicItem>& GetPublicFields() const { return PublicFields.Items; }

	FCatChumFieldPublicChanged OnFieldAdded;
	FCatChumFieldPublicChanged OnFieldChanged;
	FCatChumFieldPublicRemoved OnFieldRemoved;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_PublicFields();

private:
	friend struct FCatChumFieldPublicItem;
	void HandleReplicatedAdd(const FCatChumFieldPublicItem& Item);
	void HandleReplicatedChange(const FCatChumFieldPublicItem& Item);
	void HandleReplicatedRemove(FGuid FieldId);
	void ReconcilePresentation(const FCatChumFieldPublicItem& Item, bool bAdded);

	UPROPERTY(ReplicatedUsing=OnRep_PublicFields)
	FCatChumFieldPublicArray PublicFields;

	UPROPERTY(Transient)
	TMap<FGuid, TObjectPtr<ACatChumFieldPresentationActor>> PresentationActors;
};
