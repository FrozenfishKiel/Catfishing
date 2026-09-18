#pragma once
#include "GameplayCueNotify_Actor.h"
#include "CatGC_ItemSplash.generated.h"
class UParticleSystem;
class UParticleSystemComponent;
class USoundBase;

/** 水枪与假鱼共用的湿身 GC；特效与声音由美术资产配置，服务器只持有 GE 状态。 */
UCLASS()
class CATFISHING_API ACatGC_ItemSplash : public AGameplayCueNotify_Actor
{
	GENERATED_BODY()
public:
	/** 固定 GC 标签，效果移除时回收实例；同一目标重复喷水允许重复爆发。 */
	ACatGC_ItemSplash();
	/** 喷中瞬间的粒子；美术可留空，空值不影响湿身状态。 */
	UPROPERTY(EditDefaultsOnly, Category="湿身表现") TObjectPtr<UParticleSystem> BurstEffect;
	/** 湿身期间附着的持续粒子；结束时停止，仅属于该 GC。 */
	UPROPERTY(EditDefaultsOnly, Category="湿身表现") TObjectPtr<UParticleSystem> WetEffect;
	/** 喷中瞬间音效；客户端 GC 播放，不触发额外业务。 */
	UPROPERTY(EditDefaultsOnly, Category="湿身表现") TObjectPtr<USoundBase> SplashSound;
	/** 喷中时播放一次爆发和音效，允许重复命中反馈。 */
	virtual bool OnActive_Implementation(AActor* Target, const FGameplayCueParameters& Parameters) override;
	/** 首次可见或网络相关性恢复时挂持续效果，已存在时不重复创建。 */
	virtual bool WhileActive_Implementation(AActor* Target, const FGameplayCueParameters& Parameters) override;
	/** GE 结束时销毁本 GC 的持续粒子，重置引用以支持引擎池复用。 */
	virtual bool OnRemove_Implementation(AActor* Target, const FGameplayCueParameters& Parameters) override;
private:
	/** 本实例创建的持续粒子；不接管目标已有组件，移除时只清理这一份。 */
	UPROPERTY(Transient) TObjectPtr<UParticleSystemComponent> ActiveWetEffect;
};
