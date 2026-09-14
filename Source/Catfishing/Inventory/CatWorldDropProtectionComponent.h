#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CatWorldDropProtectionComponent.generated.h"

/** 服务器抛落轨迹防护；仅保存上一安全物理姿态，物品身份和数量始终归原 Actor/库存。 */
UCLASS()
class CATFISHING_API UCatWorldDropProtectionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCatWorldDropProtectionComponent();
	/** 正式 Drop/Place 完成变换后启用；再次丢出复用同一组件，重新设置本次安全起点。 */
	static void ArmFromAuthority(AActor* Actor);
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	FTransform LastSafeTransform = FTransform::Identity;
	FVector LastSafeCenter = FVector::ZeroVector;
};
