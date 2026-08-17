#pragma once

#include "CoreMinimal.h"
#include "Environment/CatChumFieldTypes.h"
#include "Subsystems/WorldSubsystem.h"

#include "CatChumFieldSubsystem.generated.h"

struct FCatChumFieldCommitToken
{
	FGuid Value;
	bool IsValid() const { return Value.IsValid(); }
	bool operator==(const FCatChumFieldCommitToken& Other) const { return Value == Other.Value; }
};

struct FCatPrepareChumFieldResult
{
	bool bPrepared = false;
	ECatChumFieldError Error = ECatChumFieldError::DependencyUnavailable;
	FCatChumFieldCommitToken CommitToken;
	FGuid FieldId;
	FCatWaterRegionHandle WaterRegion;
	FVector CorrectedCenter = FVector::ZeroVector;
	double StartServerTime = 0.0;
	double ExpireServerTime = 0.0;
};

struct FCatChumRequestKey
{
	FString StableNetId;
	FGuid RequestId;
	bool operator==(const FCatChumRequestKey& Other) const
	{
		return StableNetId == Other.StableNetId && RequestId == Other.RequestId;
	}
	friend uint32 GetTypeHash(const FCatChumRequestKey& Key)
	{
		return HashCombine(GetTypeHash(Key.StableNetId), GetTypeHash(Key.RequestId));
	}
};

struct FCatPrepareChumFieldRequest
{
	FString StableNetId;
	FCatPlaceChumCommand Command;
	FVector ServerCorrectedCenter = FVector::ZeroVector;
	FCatChumInfluenceSpec Influence;
	ECatChumFieldSource Source = ECatChumFieldSource::Player;
	double ServerTime = 0.0;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FCatChumFieldActivated, FGuid);
DECLARE_MULTICAST_DELEGATE_OneParam(FCatChumFieldRemoved, FGuid);

UCLASS()
class CATFISHING_API UCatChumFieldSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	FCatPrepareChumFieldResult PrepareField(const FCatPrepareChumFieldRequest& Request);
	FCatPlaceChumResult ActivatePreparedFieldDeferred(FCatChumFieldCommitToken Token, int64 EquipmentRevision);
	void PublishActivatedField(FGuid FieldId);
	void AbortPreparedField(FCatChumFieldCommitToken Token);
	bool TryGetTerminalResult(const FString& StableNetId, FGuid RequestId, FCatPlaceChumResult& OutResult) const;
	void StoreTerminalResult(const FString& StableNetId, const FCatPlaceChumResult& Result);
	FCatChumSample SampleChumAtPoint(const FVector& WorldPoint,
		const FCatWaterRegionHandle& ExpectedHandle, double ServerTime) const;
	int32 CleanupExpiredFields(double ServerTime);

	FCatChumFieldActivated OnFieldActivated;
	FCatChumFieldRemoved OnFieldRemoved;

private:
	struct FCatPendingChumField
	{
		FCatChumRequestKey RequestKey;
		FCatChumFieldState State;
		double RawContribution = 0.0;
	};

	struct FCatChumBudgetState
	{
		int32 ActiveCount = 0;
		int32 PendingCount = 0;
		double ReservedRawContribution = 0.0;
	};

	bool IsAuthorityRuntimeReady() const;
	void EnsureCleanupTimer();
	void HandleCleanupTimer();
	static FCatPrepareChumFieldResult MakePrepareError(ECatChumFieldError Error);
	static ECatChumFieldError MapWaterError(ECatWaterQueryError Error);

	TMap<FGuid, FCatChumFieldState> FieldsById;
	TMap<FName, TArray<FGuid>> FieldIdsByRegion;
	TMap<FName, int64> FieldSetRevisionByRegion;
	TMap<FGuid, FCatPendingChumField> PendingByToken;
	TMap<FCatChumRequestKey, FGuid> PendingTokenByRequest;
	TMap<FCatChumRequestKey, FCatPlaceChumResult> TerminalByIdentityAndRequest;
	TMap<FName, FCatChumBudgetState> BudgetByRegion;
	FTimerHandle CleanupTimerHandle;
};
