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
	void ForwardLegacyChum(class ACatWaterRegion* WaterRegion, FGuid RequestId,
		int64 ExpectedEquipmentRevision, int64 ExpectedAggregationRevision, FName ChumDefinitionId);

	UPROPERTY(BlueprintAssignable)
	FCatFishingCommandResultReceived OnResultReceived;

private:
	UFUNCTION(Client, Reliable)
	void ClientReceiveFishingCommandResult(const FCatFishingCommandResult& Result);

	UFUNCTION(Server, Reliable)
	void ServerSubmitFishingAbilityCommand(ECatFishingCommandType CommandType, FCatFishingInputEdge Edge);

	static constexpr int32 MaxStoredResults = 32;

	bool IsSupportedOwner() const;
	void ReceiveResultLocally(const FCatFishingCommandResult& Result);
	FCatFishingInputEdge MakeDiscreteEdge();
	void DispatchAbilityCommand(ECatFishingCommandType CommandType, const FCatFishingInputEdge& Edge);
	void HandleAbilityCommandFromAuthority(ECatFishingCommandType CommandType, const FCatFishingInputEdge& Edge);
	static FCatAggregationResult MakeInvalidChumResult(FGuid RequestId, const ACatWaterRegion* WaterRegion);

	TMap<FGuid, FCatFishingCommandResult> ResultsByRequestId;
	TArray<FGuid> ResultOrder;
	FGuid PrimaryActivationCorrelationId;
	int64 NextInputSequence = 0;
	TMap<FGuid, FCatAggregationResult> ChumTerminalCache;
};
