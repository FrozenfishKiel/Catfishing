#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CatEquippedDefinition.generated.h"

class UCatAbilitySet;
class UCatEquippedInstance;

/** 拿出装备后的能力和世界表现配置；库存身份、价格、重量和耐久归来源物品，不属于这份定义。 */
UCLASS(BlueprintType)
class CATFISHING_API UCatEquippedDefinition : public UDataAsset
{
	GENERATED_BODY()
public:
	/** 装备生效期间授予操作者的能力集合；装备实例负责记录句柄，并在卸下或换 Pawn 时从原 ASC 撤销。 */
	UPROPERTY(EditDefaultsOnly, Category="装备", meta=(DisplayName="装备能力集合"))
	TArray<TSoftObjectPtr<UCatAbilitySet>> AbilitySetsToGrant;
	/** 部署后的世界实体类；装备行为负责生成和附着，未配置表示没有独立世界实体。 */
	UPROPERTY(EditDefaultsOnly, Category="装备", meta=(DisplayName="世界表现类"))
	TSoftClassPtr<AActor> ActorClass;
};
