#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/CatFishDefinition.h"
#include "CatFishThrowEffectActor.generated.h"

class USphereComponent;
class ACatCharacter;

/** 投掷命中后的限时状态区域；服务器拥有碰撞和到期，客户端消费同一效果配置与目标。 */
UCLASS()
class CATFISHING_API ACatFishThrowEffectActor : public AActor
{
	GENERATED_BODY()
public:
	ACatFishThrowEffectActor();
	bool InitializeFromAuthority(const FCatFishThrowEffect& Effect, ACatCharacter* Target, FGuid FishInstanceId);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
	virtual void EndPlay(EEndPlayReason::Type Reason) override;
private:
	friend class FCatFishThrowReceiverTest;
	UFUNCTION() void OnRep_Effect();
	void PlayReactionLocally(ACatCharacter* Character);
	UFUNCTION() void HandleBarrierHit(UPrimitiveComponent* Component, AActor* Other,
		UPrimitiveComponent* OtherComponent, FVector Impulse, const FHitResult& Hit);
	UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Barrier;
	UPROPERTY(ReplicatedUsing=OnRep_Effect) FCatFishThrowEffect ActiveEffect;
	UPROPERTY(ReplicatedUsing=OnRep_Effect) TObjectPtr<ACatCharacter> HitTarget;
	UPROPERTY(Replicated) FGuid SourceFishInstanceId;
	UPROPERTY(Replicated) double EndsServerTime = 0.0;
	UPROPERTY(ReplicatedUsing=OnRep_Effect) TArray<TObjectPtr<ACatCharacter>> ReactedCharacters;
	TSet<TWeakObjectPtr<ACatCharacter>> LocalReactedCharacters;
};
