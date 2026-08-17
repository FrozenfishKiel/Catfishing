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
	void DeliverBeginCastResultFromAuthority(const FCatBeginCastResult& Result);
	void DeliverPlaceChumResultFromAuthority(const FCatPlaceChumResult& Result);

	UFUNCTION(BlueprintCallable, Category="Catfishing|Chum")
	void SubmitPlaceChum(const FCatPlaceChumCommand& Command);
	UFUNCTION(BlueprintCallable, Category="Catfishing|Fishing") void SubmitBeginCast(const FCatBeginCastCommand& Command);
	UFUNCTION(BlueprintCallable, Category="Catfishing|Fishing") void SubmitPlaceRod(const FCatPlaceRodCommand& Command);
	UFUNCTION(BlueprintCallable, Category="Catfishing|Fishing") void SubmitOperateRod(const FCatOperateRodCommand& Command);
	UFUNCTION(BlueprintCallable, Category="Catfishing|Fishing") void SubmitLeaveRod(const FCatLeaveRodCommand& Command);
	UFUNCTION(BlueprintCallable, Category="Catfishing|Fishing") void SubmitPackRod(const FCatPackRodCommand& Command);
	UFUNCTION(BlueprintCallable, Category="Catfishing|Fishing")
	bool TryGetBeginCastResult(FGuid RequestId, FCatBeginCastResult& OutResult) const;

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
	void ForwardLegacyAssist(FGuid FishingSessionId, FGuid RequestId, int64 ExpectedRevision);
	void ForwardLegacyStart(FGuid RequestId);
	void ForwardLegacyScoop(FGuid FishingSessionId, FCatScoopCommand Command);

	UPROPERTY(BlueprintAssignable)
	FCatFishingCommandResultReceived OnResultReceived;

private:
	UFUNCTION(Client, Reliable)
	void ClientReceiveFishingCommandResult(const FCatFishingCommandResult& Result);

	UFUNCTION(Client, Reliable)
	void ClientReceivePlaceChumResult(const FCatPlaceChumResult& Result);
	UFUNCTION(Client, Reliable) void ClientReceiveBeginCastResult(const FCatBeginCastResult& Result);

	UFUNCTION(Server, Reliable)
	void ServerSubmitPlaceChum(const FCatPlaceChumCommand& Command);
	UFUNCTION(Server, Reliable) void ServerSubmitBeginCast(const FCatBeginCastCommand& Command);
	UFUNCTION(Server, Reliable) void ServerSubmitPlaceRod(const FCatPlaceRodCommand& Command);
	UFUNCTION(Server, Reliable) void ServerSubmitOperateRod(const FCatOperateRodCommand& Command);
	UFUNCTION(Server, Reliable) void ServerSubmitLeaveRod(const FCatLeaveRodCommand& Command);
	UFUNCTION(Server, Reliable) void ServerSubmitPackRod(const FCatPackRodCommand& Command);

	UFUNCTION(Server, Reliable)
	void ServerSubmitFishingAbilityCommand(ECatFishingCommandType CommandType, FCatFishingInputEdge Edge);

	static constexpr int32 MaxStoredResults = 32;

	bool IsSupportedOwner() const;
	void ReceiveResultLocally(const FCatFishingCommandResult& Result);
	FCatFishingInputEdge MakeDiscreteEdge();
	void DispatchAbilityCommand(ECatFishingCommandType CommandType, const FCatFishingInputEdge& Edge);
	void HandleAbilityCommandFromAuthority(ECatFishingCommandType CommandType, const FCatFishingInputEdge& Edge);
	void ReceivePlaceChumResultLocally(const FCatPlaceChumResult& Result);
	void ReceiveBeginCastResultLocally(const FCatBeginCastResult& Result);

	TMap<FGuid, FCatFishingCommandResult> ResultsByRequestId;
	TArray<FGuid> ResultOrder;
	FGuid PrimaryActivationCorrelationId;
	int64 NextInputSequence = 0;
	TMap<FGuid, FCatPlaceChumResult> PlaceChumResultsByRequestId;
	TArray<FGuid> PlaceChumResultOrder;
	TMap<FGuid, FCatBeginCastResult> BeginCastResultsByRequestId;
	TArray<FGuid> BeginCastResultOrder;
};
