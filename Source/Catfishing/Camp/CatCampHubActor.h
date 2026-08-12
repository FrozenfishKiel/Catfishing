#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Items/CatItemTypes.h"
#include "CatCampHubActor.generated.h"

class ACatCharacter;
class ACatFishTankActor;
class USceneComponent;

/** 篝火公共回看请求；它只启动可跳过表现，不参与普通夜晚 ready 或 StateTree 转移。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatCampfirePlaybackRequested, FGuid);

/** Lake 中唯一固定营地宿主；提供休息、救援落点、鱼缸转移和可选回看，不支持建造/装饰/搬迁。 */
UCLASS()
class CATFISHING_API ACatCampHubActor : public AActor
{
	GENERATED_BODY()

public:
	/** 建立关卡摆放营地所需的固定根节点、救援落点和复制属性；布局仍由关卡资产负责，运行时不会生成第二套营地真相。 */
	ACatCampHubActor();

	/** 本人位于营地范围时请求快速休息；Character ConditionComponent 拥有最终身体写入。 */
	FCatDomainCommandResult RequestRest(AController* RequestingController, FGuid RequestId);

	/** 伙伴把倒地目标送到固定 RescuePoint；Teleport 成功后才提交 CarriedToCamp 事实。 */
	FCatDomainCommandResult RescueToCamp(AController* HelpingController, ACatCharacter* TargetCharacter, FGuid RequestId);

	/** 把本人鱼护的一条鱼原子转入固定共享鱼缸；双 Revision 与权限仍由 Items 唯一裁决。 */
	FCatDomainCommandResult TransferFishToTank(AController* RequestingController, FGuid RequestId,
		FGuid FishInstanceId, int64 ExpectedGuardRevision, int64 ExpectedTankRevision);

	/** 幂等请求可跳过的篝火回看；结算封面先为全体在场玩家批量建齐 Planned 事实，成功才广播一次表现意图，且不写 Run ready。 */
	FCatDomainCommandResult RequestCampfirePlayback(AController* RequestingController, FGuid RequestId);

	/** 只读判断 Controller 当前 Character 是否位于固定营地交互范围；供修竿等其他领域适配，不产生回看或休息副作用。 */
	bool IsControllerInCamp(AController* Controller) const;

	/** 篝火表现订阅入口；表现结束/跳过无需回写 Run。 */
	FCatCampfirePlaybackRequested OnCampfirePlaybackRequested;

private:
	/** 验证 Controller 当前 Character 是否处于固定营地交互范围；不接受客户端位置参数。 */
	ACatCharacter* ResolveCharacterInCamp(AController* Controller) const;

	/** 固定营地 Actor 根；关卡摆放后不会被玩家移动。 */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> CampRoot;

	/** 倒地搬运的固定落点；没有动态建造或玩家自定义坐标。 */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> RescuePoint;

	/** 关卡显式关联的共享鱼缸；空引用时转移 fail-closed，不在命令中临时 Spawn。 */
	UPROPERTY(EditInstanceOnly, Category = "Camp")
	TObjectPtr<ACatFishTankActor> SharedFishTank;

	/** 救援者身份、命令类别与 RequestId 到首次成功终态；网络重试先重放，避免重复 Teleport 同一倒地目标。 */
	TMap<FString, FCatDomainCommandResult> RescueTerminalCache;

	/** 玩家身份+RequestId 到篝火回看首次终态；成功重试只重放结果，不再次广播表现或创建成像计划。 */
	TMap<FString, FCatDomainCommandResult> CampfirePlaybackTerminalCache;
};
