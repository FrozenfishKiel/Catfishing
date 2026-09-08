#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CatFishingResourceCustodian.generated.h"

class UCatEquipmentComponent;

/** 原角色离场后仅保存其场上竿和预约饵；服务器私有，生命周期止于当前 Run/World。 */
UCLASS(NotBlueprintable, Transient)
class CATFISHING_API ACatFishingResourceCustodian : public AActor
{
	GENERATED_BODY()
public:
	ACatFishingResourceCustodian();
	UCatEquipmentComponent* GetEquipment() const { return Equipment; }
	void InitializeOriginalOwner(const FString& StableId) { OriginalOwnerStableId = StableId; }
	const FString& GetOriginalOwnerStableId() const { return OriginalOwnerStableId; }
private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCatEquipmentComponent> Equipment;
	/** 仅服务器归属键，不复制、不原样写日志，也不赋予接力者领取权限。 */
	FString OriginalOwnerStableId;
};
