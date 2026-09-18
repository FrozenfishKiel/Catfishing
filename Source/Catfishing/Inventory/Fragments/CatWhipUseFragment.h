#pragma once
#include "CoreMinimal.h"
#include "Inventory/Fragments/CatItemUseFragment.h"
#include "CatWhipUseFragment.generated.h"
class ACatWhipActor;

/** 复用通用物品来源与 GAS 生命周期；只增加鞭子的使用期 Actor 配置。 */
UCLASS(EditInlineNew, DefaultToInstanced)
class CATFISHING_API UCatWhipUseFragment : public UCatItemUseFragment
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, Category="Whip") TSubclassOf<ACatWhipActor> SwingActorClass;
};
