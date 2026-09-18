#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CatCastNetCatchEmitter.generated.h"

class ACatFishPickupActor;

/** 同一执行者的渔网出鱼对象；接收连续撒网的鱼，独立于使用 GA 和库存丢弃队列逐条公开。 */
UCLASS()
class CATFISHING_API ACatCastNetCatchEmitter : public AActor
{
	GENERATED_BODY()
public:
	/** 建立不复制的服务器出鱼宿主；真正的鱼仍走自己的复制。 */
	ACatCastNetCatchEmitter();
	/** 接过本次已扣网的鱼队列并启动；跟随执行者位置，执行者消失后沿最后位置完成。 */
	void StartFromAuthority(AActor* Executor, const TArray<ACatFishPickupActor*>& Fish, float IntervalSeconds);
protected:
	/** 世界或宿主退出时清理未公开候选；已喷出的鱼不再归本对象所有。 */
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	/** 等待喷出的已结算鱼；强引用防止排队期间被回收，出队即移交世界。 */
	UPROPERTY() TArray<TObjectPtr<ACatFishPickupActor>> PendingFish;
	/** 出鱼位置的跟随对象，不持有其生命周期，也不监听其动作。 */
	TWeakObjectPtr<AActor> Source;
	/** 后续出鱼间隔，单位秒；每次追加鱼时读取本次渔网配置，不改已安排的下一次执行时间。 */
	float Interval = 0.2f;
	/** 仅驱动当前宿主的渔网出鱼队列；能力结束不会清掉它。 */
	FTimerHandle EmissionTimer;
	/** 从队首取一条，按当前执行者位置求岸侧出口，再公开并物理抛出；无有效岸线时沿角色前方，队空后销毁宿主。 */
	void EmitNextFish();
};
