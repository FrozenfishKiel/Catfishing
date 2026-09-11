#pragma once

#include "CoreMinimal.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "CatFishGuardInventoryItemInstance.generated.h"

class ACatFishGuardActor;

/** 鱼护的背包身份；内部鱼继续由关联 Actor 的正式库存持有，本实例不复制第二份鱼列表，也不负责存档。 */
UCLASS()
class CATFISHING_API UCatFishGuardInventoryItemInstance : public UCatInventoryItemInstance
{
	GENERATED_BODY()

public:
	/** 关联承载内部库存的鱼护；地面首次拾取或从定义生成时调用，重复搬运继续使用同一载体。 */
	void SetGuardFromAuthority(ACatFishGuardActor* InGuard);

	/** 读取仍有效的原鱼护供通用落地入口复用；未生成的新鱼护返回空。 */
	virtual AActor* GetWorldActor() const override;

	/** 库存接收或转移后同步载体归属；背包中的鱼护关闭世界交互，回到自身 Actor 时恢复地面状态。 */
	virtual void SetRuntimeOwnerActor(AActor* InRuntimeOwnerActor) override;

private:
	/** 本物品保管的鱼护载体；服务器生成/拾取时设置，随实例保活，内部库存始终只在该 Actor 上。 */
	UPROPERTY(Transient)
	TObjectPtr<ACatFishGuardActor> Guard;
};
