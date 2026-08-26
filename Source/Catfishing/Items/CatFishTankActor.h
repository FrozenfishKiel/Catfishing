#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CatFishTankActor.generated.h"

class UCatContainerReplicationComponent;
class UCatFishTankInteractionComponent;
class USceneComponent;

/** 固定营地的一局共享鱼缸宿主；只适配 Items 容器和复制，不拥有转移、献祭或偷取规则。 */
UCLASS()
class CATFISHING_API ACatFishTankActor : public AActor
{
	GENERATED_BODY()

public:
	/** 建立共享鱼缸唯一复制宿主并关闭无关 Tick；容量和鱼内容仍由 Settings/Items 注入，Actor 不生成第二份容器真相。 */
	ACatFishTankActor();

	/** 注册共享容器 ID 的复制；鱼数组由组件独立复制。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** 返回 authority 生成并复制的一局鱼缸容器 ID；未 BeginPlay 时无效。 */
	FGuid GetTankContainerId() const;

	/** 该鱼缸暴露给世界交互扫描的能力组件；人工验收和蓝图只读取它确认交互接线，不通过它拿容器写权。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Items")
	UCatFishTankInteractionComponent* GetTankInteraction() const;

protected:
	/** authority 进入 World 时以显式 TankId/配置容量注册共享容器；失败保持空读模型。 */
	virtual void BeginPlay() override;

	/** World 退出前按精确组件从 Items 注销；局末鱼随 World 清空。 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** 共享鱼缸的固定场景根；关卡用它摆放位置，运行时不把该坐标当成容器真相。 */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> TankRoot;

	/** 共享鱼缸在本局运行时使用的稳定容器 ID；服务器 BeginPlay 生成后复制给客户端，调用者用它向 Items 查询同一容器。 */
	UPROPERTY(Replicated)
	FGuid TankContainerId;

	/** Items 提交后唯一对外复制的容器快照组件。 */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCatContainerReplicationComponent> ContainerReplication;

	/** 鱼缸的本地交互入口；它只把共享容器作为背包外部上下文打开，真实移动仍由 Drop 后的服务器 Items 事务决定。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Catfishing|Items", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCatFishTankInteractionComponent> TankInteraction;
};
