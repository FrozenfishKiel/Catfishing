#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Fishing/Integration/CatFishingCommandTypes.h"
#include "CatFishingCommandComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FCatFishingCommandResultReceived,
	const FCatFishingCommandResult&, Result);

USTRUCT(BlueprintType)
struct FCatFishingInputEdge
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FGuid RequestId;

	UPROPERTY(BlueprintReadOnly)
	FGuid ActivationCorrelationId;

	UPROPERTY(BlueprintReadOnly)
	int64 InputSequence = 0;
};

UCLASS(ClassGroup=(Catfishing), meta=(BlueprintSpawnableComponent))
class CATFISHING_API UCatFishingCommandComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCatFishingCommandComponent();
	void DeliverResultFromAuthority(const FCatFishingCommandResult& Result);
	void DeliverPlaceChumResultFromAuthority(const FCatPlaceChumResult& Result);

	UFUNCTION(BlueprintCallable, Category="Catfishing|Chum")
	void SubmitPlaceChum(const FCatPlaceChumCommand& Command);

	UFUNCTION(BlueprintCallable, Category="Catfishing|Chum")
	bool TryGetPlaceChumResult(FGuid RequestId, FCatPlaceChumResult& OutResult) const;

	UFUNCTION(BlueprintCallable)
	bool TryGetResult(FGuid RequestId, FCatFishingCommandResult& OutResult) const;

	UFUNCTION(BlueprintCallable)
	void ConsumeResult(FGuid RequestId);

	void ResetTransientCommandState();
	FCatFishingInputEdge SubmitRodInteract();
	FCatFishingInputEdge SubmitPrimaryPressed();
	FCatFishingInputEdge SubmitPrimaryReleased();
	FCatFishingInputEdge SubmitCancel();
	FCatFishingInputEdge SubmitScoop();
	FCatFishingInputEdge SubmitChum();
	void ForwardLegacyStart(FGuid RequestId);
	void ForwardLegacyAssist(FGuid FishingSessionId, FGuid RequestId, int64 ExpectedRevision);
	void ForwardLegacyScoop(FGuid FishingSessionId, FCatScoopCommand Command);

	UPROPERTY(BlueprintAssignable)
	FCatFishingCommandResultReceived OnResultReceived;

private:
	UFUNCTION(Client, Reliable)
	void ClientReceiveFishingCommandResult(const FCatFishingCommandResult& Result);

	UFUNCTION(Client, Reliable)
	void ClientReceivePlaceChumResult(const FCatPlaceChumResult& Result);

	UFUNCTION(Server, Reliable)
	void ServerSubmitPlaceChum(const FCatPlaceChumCommand& Command);

	UFUNCTION(Server, Reliable)
	void ServerSubmitFishingAbilityCommand(ECatFishingCommandType CommandType, FCatFishingInputEdge Edge);

	static constexpr int32 MaxStoredResults = 32;

	bool IsSupportedOwner() const;
	void ReceiveResultLocally(const FCatFishingCommandResult& Result);
	FCatFishingInputEdge MakeDiscreteEdge();
	void DispatchAbilityCommand(ECatFishingCommandType CommandType, const FCatFishingInputEdge& Edge);
	void HandleAbilityCommandFromAuthority(ECatFishingCommandType CommandType, const FCatFishingInputEdge& Edge);
	void ReceivePlaceChumResultLocally(const FCatPlaceChumResult& Result);

	TMap<FGuid, FCatFishingCommandResult> ResultsByRequestId;
	TArray<FGuid> ResultOrder;
	FGuid PrimaryActivationCorrelationId;
	int64 NextInputSequence = 0;
	TMap<FGuid, FCatPlaceChumResult> PlaceChumResultsByRequestId;
	TArray<FGuid> PlaceChumResultOrder;
};
