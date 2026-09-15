#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CatFishingResolutionSubsystem.generated.h"

class AController;
/** 仅钓鱼规则 §5.4 三类事件的优先级，不是全局玩法调度。 */
enum class ECatFishingResolution : uint8 { Catch, Revival, Water };

UCLASS()
class CATFISHING_API UCatFishingResolutionSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	/** 入队时记录服务器收到指令的时间与房间席位；回调不得保存裸 UObject 指针。 */
	void Enqueue(ECatFishingResolution Phase, AController* Requester, FGuid RequestId, TFunction<void()> Resolve);
	void Flush(UWorld* World, ELevelTick TickType, float DeltaSeconds);
private:
	struct FPending
	{
		ECatFishingResolution Phase;
		double ReceivedTime;
		int64 Seat;
		uint64 Sequence;
		FGuid RequestId;
		TFunction<void()> Resolve;
	};
	TArray<FPending> Pending;
	uint64 NextSequence = 0;
	FDelegateHandle PostTickHandle;
};
