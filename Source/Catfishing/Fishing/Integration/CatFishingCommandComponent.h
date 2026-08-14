#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Fishing/Integration/CatFishingCommandTypes.h"
#include "CatFishingCommandComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FCatFishingCommandResultReceived,
	const FCatFishingCommandResult&, Result);

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

	UPROPERTY(BlueprintAssignable)
	FCatFishingCommandResultReceived OnResultReceived;

private:
	UFUNCTION(Client, Reliable)
	void ClientReceiveFishingCommandResult(const FCatFishingCommandResult& Result);

	static constexpr int32 MaxStoredResults = 32;

	bool IsSupportedOwner() const;
	void ReceiveResultLocally(const FCatFishingCommandResult& Result);

	TMap<FGuid, FCatFishingCommandResult> ResultsByRequestId;
	TArray<FGuid> ResultOrder;
};
