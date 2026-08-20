#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Items/CatItemTypes.h"
#include "CatCampHubActor.generated.h"

class ACatCharacter;
class ACatFishTankActor;
class USceneComponent;

/** 篝火公共回看请求；每个夜晚点亮时广播一次，它只启动可跳过表现，不参与普通夜晚 ready 或 StateTree 转移，也不要求玩家围坐。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FCatCampfirePlaybackRequested, FGuid);

/** Lake 中唯一固定营地宿主；提供休息、救援落点、鱼缸转移和可选回看，不支持建造/装饰/搬迁。 */
UCLASS()
class CATFISHING_API ACatCampHubActor : public AActor
{
	GENERATED_BODY()

public:
	/** 建立关卡摆放营地所需的固定根节点、救援落点和复制属性；布局仍由关卡资产负责，运行时不会生成第二套营地真相。 */
	ACatCampHubActor();

	/** 本人位于营地范围时请求休息；同 RequestId 重试先重放首次终态，真正的解除倒地（清 Poison）仍由 Character ConditionComponent 拥有。 */
	FCatDomainCommandResult RequestRest(AController* RequestingController, FGuid RequestId);

	/** 伙伴把倒地目标送到固定 RescuePoint；同 RequestId 绑定首次目标身份，Teleport 成功后才提交 CarriedToCamp 事实。 */
	FCatDomainCommandResult RescueToCamp(AController* HelpingController, ACatCharacter* TargetCharacter, FGuid RequestId);

	/** 把本人鱼护的一条鱼原子转入固定共享鱼缸；同 RequestId 绑定鱼、鱼护、鱼缸和双 Revision，原样重试不再读取已变化的鱼护。 */
	FCatDomainCommandResult TransferFishToTank(AController* RequestingController, FGuid RequestId,
		FGuid FishInstanceId, int64 ExpectedGuardRevision, int64 ExpectedTankRevision);

	/**
	 * 幂等请求可跳过的篝火回看；只在普通夜和结算夜开放，白天与局末拒绝。结算封面按当晚实际围坐者批量建齐 Planned 事
	 * 实，不在营地的玩家只是不入镜、不阻塞命令；成功才广播一次表现意图，且不写 Run ready。
	 */
	FCatDomainCommandResult RequestCampfirePlayback(AController* RequestingController, FGuid RequestId);

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

	/** 玩家身份+RequestId 到休息首次终态；休息重试必须先读旧结果，再看角色是否仍在营地范围内。 */
	TMap<FString, FCatDomainCommandResult> RestTerminalCache;

	/** 救援者身份、命令类别与 RequestId 到首次终态；网络重试先重放，避免目标状态变化后复活旧命令。 */
	TMap<FString, FCatDomainCommandResult> RescueTerminalCache;

	/** 救援终态缓存对应的目标身份签名；同 RequestId 不能换另一个倒地目标继续执行。 */
	TMap<FString, FString> RescuePayloadByKey;

	/** 玩家身份+RequestId 到入缸首次终态；Camp 必须先重放再读鱼护，否则成功后的鱼已离开鱼护会把合法重试误判成新失败。 */
	TMap<FString, FCatDomainCommandResult> TankTransferTerminalCache;

	/** 入缸命令首次终态对应的鱼、鱼护、鱼缸和双 Revision；同 RequestId 不能换鱼或换前提继续执行。 */
	TMap<FString, FString> TankTransferPayloadByKey;

	/** 玩家身份+RequestId 到篝火回看首次终态；成功重试只重放结果，不再次广播表现或创建成像计划。
	 *  这里刻意没有配套的 PayloadSignature 表，也就用不上共享的 CatQueryTerminalReplay：
	 *  篝火回看命令除了发起人身份和 RequestId 之外没有任何业务载荷，而这两项已经全部编进终态键，
	 *  同一个键不可能对应两种不同意图，再存一份签名只是空转的仪式。 */
	TMap<FString, FCatDomainCommandResult> CampfirePlaybackTerminalCache;
};
