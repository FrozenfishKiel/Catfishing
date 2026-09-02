#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "CatEnvironmentPresentationSubsystem.generated.h"

class ACatEnvironmentPresentationActor;

/** 环境表现本地保底入口；它只确保 World 里有表现消费者，不保存或推进任何 Run/Environment 状态。 */
UCLASS()
class CATFISHING_API UCatEnvironmentPresentationSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 限定只在游戏和 PIE World 创建；编辑器预览世界不应自动生成运行时表现 Actor。 */
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	/** World 开始游戏时检查表现消费者是否存在；专用服务器和已有 Actor 的关卡都不会重复生成。 */
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	/** 查找已有表现 Actor，找不到时生成一个本地非复制实例；该流程不读取也不写入 Run 公共状态。 */
	void EnsurePresentationActor(UWorld& World);

	/** 本 Subsystem 自动生成的表现 Actor；只用于避免重复生成，不代表任何环境玩法事实。 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ACatEnvironmentPresentationActor> SpawnedPresentationActor;
};
