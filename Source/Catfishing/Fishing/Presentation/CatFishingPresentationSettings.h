#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CatFishingPresentationSettings.generated.h"

class ACatChumFieldPresentationActor;
class ACatFishEncounterActor;
class ACatFishingHookActor;
class ACatFishingRodActor;
class UAnimMontage;
class UCatRodSkinDefinition;

UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Catfishing Fishing Presentation"))
class CATFISHING_API UCatFishingPresentationSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	const UCatRodSkinDefinition* FindRuntimeRodSkin(FName RodSkinDefinitionId, FName RodDefinitionId) const;
	UPROPERTY(Config, EditAnywhere) TSoftClassPtr<ACatFishingRodActor> RodActorClass;
	UPROPERTY(Config, EditAnywhere) TSoftClassPtr<ACatFishingHookActor> HookActorClass;
	UPROPERTY(Config, EditAnywhere) TSoftClassPtr<ACatFishEncounterActor> FishEncounterActorClass;
	/**
	 * 服务器确认抛竿并让 Hook 进入 CastFlight 后，由每台客户端在抛竿者角色上本地播放的 Montage。
	 * Montage 本身不复制；复制的是 CastFlight 这个玩法事实，因此主机、发起客户端和旁观客户端走同一触发条件。
	 */
	UPROPERTY(Config, EditAnywhere, Category="Animation")
	TSoftObjectPtr<UAnimMontage> CastMontage;
	/** 客户端窝点表现 Actor 类；留空则用原生基类（无任何可见表现）。 */
	UPROPERTY(Config, EditAnywhere) TSoftClassPtr<ACatChumFieldPresentationActor> ChumFieldPresentationClass;
	UPROPERTY(Config, EditAnywhere) TArray<TSoftObjectPtr<UCatRodSkinDefinition>> RodSkinCatalog;
};
