#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CatFishGuardActor.generated.h"

class UCatContainerReplicationComponent;
class UCatFishGuardInteractionComponent;
class USceneComponent;

/** 世界里的鱼护箱子宿主；它承载一份 Items 鱼容器，可被玩家交互打开，不挂在 Character 或 PlayerState 身上。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API ACatFishGuardActor : public AActor
{
	GENERATED_BODY()

public:
	/** 创建鱼护的场景根、容器复制出口和交互入口；容量和容器 ID 等到服务器 BeginPlay 时写入。 */
	ACatFishGuardActor();

	/** 注册鱼护 Actor 自身的容器 ID 复制字段；鱼槽内容仍由容器复制组件负责发送。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 返回本鱼护箱子的一局稳定容器 ID；未 BeginPlay 注册时无效，调用方必须继续让 Items 校验容器存在和 Revision。 */
	FGuid GetGuardContainerId() const;

	/** 返回鱼护公开快照的复制组件；UI 只读它，任何写入仍必须走 Items Service。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Items")
	UCatContainerReplicationComponent* GetContainerReplicationComponent() const;

	/** 鱼护箱子暴露给交互扫描的入口组件；关卡蓝图和验收可读取它确认接线，真实库存写入仍必须走 Items 服务。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Items")
	UCatFishGuardInteractionComponent* GetGuardInteraction() const;

protected:
	/** authority 进入 World 时生成容器 ID 并注册 FishGuard 容器；客户端只等待 ID 与组件快照复制。 */
	virtual void BeginPlay() override;

	/** 鱼护箱子离开 World 前注销它在 Items 中的复制宿主；已经提交的局内鱼不会迁移到 Character。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 服务器把本箱子注册进 Items；重复调用保持幂等，失败时鱼护保留为空容器对象。 */
	bool RegisterContainerFromAuthority();

	/** 鱼护的独立世界根；蓝图可在它下面挂网兜、箱体或其他表现组件，不影响容器真相。 */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> GuardRoot;

	/** 本局鱼护箱子容器 ID；服务器生成后复制，客户端和命令层用它指向同一个 Items 聚合。 */
	UPROPERTY(Replicated)
	FGuid GuardContainerId;

	/** Items 提交后唯一对外复制的鱼护箱子快照组件；鱼护 Actor 不保存第二份鱼数组。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Items", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatContainerReplicationComponent> ContainerReplication;

	/** 鱼护的通用交互入口；玩家确认时把该鱼护箱子作为外部容器打开。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Items", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatFishGuardInteractionComponent> GuardInteraction;

	/** 服务器是否已经把本鱼护注册进 Items；EndPlay 只在注册后注销，避免误删不存在的容器。 */
	bool bRegisteredWithItems = false;
};
