#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/BodyAction/Core/CatBodyActionCommandComponent.h"
#include "CatCampBodyActionCommandComponent.generated.h"

class ACatCampHubActor;
class ACatCharacter;

/** Camp 身体动作命令组件；只负责创建营地类 BodyAction 载荷并复用 Core 的 GameplayEvent 投递。 */
UCLASS(ClassGroup=(Catfishing), meta=(BlueprintSpawnableComponent))
class CATFISHING_API UCatCampBodyActionCommandComponent : public UCatBodyActionCommandComponent
{
	GENERATED_BODY()

public:
	/** 提交营地休息身体动作请求；返回值只表示是否有正式 Ability 接管该 GameplayEvent。 */
	bool SubmitCampRest(ACatCampHubActor* Camp, FGuid RequestId) const;

	/** 提交篝火回看身体动作请求；CapturePlan 与结算夜规则仍由 Camp 入口裁决。 */
	bool SubmitCampfirePlayback(ACatCampHubActor* Camp, FGuid RequestId) const;

	/** 提交搬运救援身体动作请求；目标倒地事实和最终落点仍由 Camp/Condition 裁决。 */
	bool SubmitRescueCharacterToCamp(ACatCampHubActor* Camp, ACatCharacter* TargetCharacter, FGuid RequestId) const;
};
