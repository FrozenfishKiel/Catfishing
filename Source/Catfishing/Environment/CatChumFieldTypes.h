#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "Environment/CatWaterTypes.h"
#include "UObject/SoftObjectPtr.h"

#include "CatChumFieldTypes.generated.h"

class AActor;
class UCurveFloat;

UENUM(BlueprintType)
enum class ECatChumFieldSource : uint8
{
	Player,
	NaturalEvent
};

UENUM(BlueprintType)
enum class ECatChumFieldError : uint8
{
	None,
	FeatureDisabled,
	CommandsClosed,
	InvalidIdentity,
	InvalidPayload,
	DefinitionUnavailable,
	InvalidWaterTarget,
	StaleGeometry,
	PlacementOutOfRange,
	PlacementOccluded,
	EquipmentRevisionConflict,
	EquipmentUnavailable,
	FieldCapacityExceeded,
	AlreadyResolved,
	DependencyUnavailable
};

struct FCatChumRuntimeInfluence;

USTRUCT(BlueprintType)
struct FCatChumInfluenceSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	double RadiusCentimeters = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	double DurationSeconds = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FCatChumVector BaseContribution;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<UCurveFloat> DistanceFalloffCurve;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<UCurveFloat> TimeFalloffCurve;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 MaximumQuantityPerPlacement = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName PresentationId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSoftClassPtr<AActor> PresentationClass;

	bool IsRuntimeReady() const;
	bool IsUnconfigured() const;
	bool BuildRuntimeInfluence(int32 Quantity, FCatChumRuntimeInfluence& OutRuntime) const;
};

struct FCatChumFalloffTable
{
	static constexpr int32 SampleCount = 65;
	TStaticArray<double, SampleCount> Samples{};

	double Evaluate(double NormalizedInput) const;
	bool IsRuntimeReady() const;
};

struct FCatChumRuntimeInfluence
{
	double RadiusCentimeters = 0.0;
	double DurationSeconds = 0.0;
	FCatChumVector BaseContribution;
	FCatChumFalloffTable DistanceFalloff;
	FCatChumFalloffTable TimeFalloff;
	FName PresentationId = NAME_None;
};

USTRUCT(BlueprintType)
struct FCatPlaceChumCommand
{
	GENERATED_BODY()

	/** 本次打窝请求的幂等身份；输入组件生成，打窝终态缓存用它防止 RPC 重放造成重复落点或重复扣量。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid RequestId;

	/** 客户端预测命中的水域版本；服务器用它复查落点是否仍在同一片有效水面上。 */
	UPROPERTY(BlueprintReadWrite)
	FCatWaterRegionHandle ExpectedWaterRegionHandle;

	/** 正式随身库存的内容版本快照，表示提交方组装打窝命令时看到的背包并发点；命令发送方写入，打窝服务读取它拒绝过期扣量，0 表示新字段未随命令提供并触发旧字段回退。 */
	UPROPERTY(BlueprintReadWrite)
	int64 ExpectedInventoryRevision = 0;

	/** 旧打窝协议留下的随身物品版本槽位；迁移期只在 ExpectedInventoryRevision 缺省时被服务端回退读取，新调用方必须同步同一库存版本，避免旧蓝图和正式库存形成两套版本事实。 */
	UPROPERTY(BlueprintReadWrite)
	int64 ExpectedEquipmentRevision = 0;

	/** 玩家实际要消耗的窝料物品实例；PlaceChum 用它锁定正式库存中的那一格，避免同类多堆窝料被误扣。 */
	UPROPERTY(BlueprintReadWrite)
	FGuid ChumItemInstanceId;

	/** 窝料定义身份；玩家投放时由服务器按 ChumItemInstanceId 复核和覆盖，自然事件直接写它来生成场地影响。 */
	UPROPERTY(BlueprintReadWrite)
	FName ChumDefinitionId = NAME_None;

	/** 本次投放消耗的窝料份数；库存扣量提交会从 ChumItemInstanceId 对应数量栈扣除这份数量。 */
	UPROPERTY(BlueprintReadWrite)
	int32 Quantity = 0;

	/** 客户端预测的候选落点；服务器只把它当输入重新吸附到水面，不直接信任最终坐标。 */
	UPROPERTY(BlueprintReadWrite)
	FVector ClientCandidateWorldPoint = FVector::ZeroVector;

	/** 返回本命令参与服务端库存并发校验的版本；PlaceChum 先读新字段，只有新字段仍为 0 才把旧字段当迁移输入，返回值不重新授权 Equipment Snapshot 裁决数量。 */
	int64 GetExpectedInventoryRevision() const
	{
		// 这里把 0 当“新字段未写”的哨兵；正式库存提交后版本至少为 1，因此不会和有效库存版本冲突。
		return ExpectedInventoryRevision != 0 ? ExpectedInventoryRevision : ExpectedEquipmentRevision;
	}
};

USTRUCT(BlueprintType)
struct FCatPlaceChumResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FGuid RequestId;

	UPROPERTY(BlueprintReadOnly)
	bool bCommitted = false;

	UPROPERTY(BlueprintReadOnly)
	ECatChumFieldError Error = ECatChumFieldError::DependencyUnavailable;

	UPROPERTY(BlueprintReadOnly)
	FGuid FieldId;

	UPROPERTY(BlueprintReadOnly)
	FCatWaterRegionHandle WaterRegion;

	UPROPERTY(BlueprintReadOnly)
	FVector ServerCorrectedCenter = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly)
	double StartServerTime = 0.0;

	UPROPERTY(BlueprintReadOnly)
	double ExpireServerTime = 0.0;

	/** 本次打窝回执里的正式库存内容版本，表示服务端扣量链结束后背包达到的并发点；SetInventoryRevision 写入，UI 和通用结果投影读取，失败结果保留提交阶段已知值。 */
	UPROPERTY(BlueprintReadOnly)
	int64 InventoryRevision = 0;

	/** 旧回执协议留下的随身物品版本槽位；迁移期只承载与 InventoryRevision 相同的正式库存版本，旧 UI 和旧测试读取它时不再得到 Equipment Snapshot 权威版本。 */
	UPROPERTY(BlueprintReadOnly)
	int64 EquipmentRevision = 0;

	/** 写入打窝回执的正式库存版本；服务端激活窝料场时调用，先更新新字段再同步旧槽位，让迁移期新旧消费者读取同一背包版本。 */
	void SetInventoryRevision(const int64 InInventoryRevision)
	{
		InventoryRevision = InInventoryRevision;
		EquipmentRevision = InInventoryRevision;
	}

	/** 读取本回执表达的正式库存版本；新字段优先，旧字段只在手写旧回执还没迁移时提供兼容值，供通用结果缓存继续投影同一库存并发点。 */
	int64 GetInventoryRevision() const
	{
		// 0 仍表示新字段没有被写入；旧对象直接写 EquipmentRevision 时，回退读取能保留迁移期测试和蓝图的版本值。
		return InventoryRevision != 0 ? InventoryRevision : EquipmentRevision;
	}

	UPROPERTY(BlueprintReadOnly)
	int64 ChumFieldSetRevision = 0;
};

struct FCatChumFieldState
{
	FGuid FieldId;
	FCatWaterRegionHandle WaterRegion;
	FName ChumDefinitionId = NAME_None;
	FVector CenterWorldPoint = FVector::ZeroVector;
	FCatChumRuntimeInfluence Influence;
	double StartServerTime = 0.0;
	double ExpireServerTime = 0.0;
	ECatChumFieldSource Source = ECatChumFieldSource::Player;
	FString OwnerStableNetId;
	bool bPublicationFlushed = false;
};

USTRUCT(BlueprintType)
struct FCatChumSample
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	bool bSucceeded = false;

	UPROPERTY(BlueprintReadOnly)
	ECatChumFieldError Error = ECatChumFieldError::DependencyUnavailable;

	UPROPERTY(BlueprintReadOnly)
	FCatWaterRegionHandle WaterRegion;

	UPROPERTY(BlueprintReadOnly)
	int64 ChumFieldSetRevision = 0;

	UPROPERTY(BlueprintReadOnly)
	double SampleServerTime = 0.0;

	UPROPERTY(BlueprintReadOnly)
	FCatChumVector EffectiveChumVector;

	UPROPERTY(BlueprintReadOnly)
	int32 ContributingFieldCount = 0;

	TArray<FGuid> ContributingFieldIds;
};
