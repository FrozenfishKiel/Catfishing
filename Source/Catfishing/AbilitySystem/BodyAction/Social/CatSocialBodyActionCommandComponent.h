#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/BodyAction/Core/CatBodyActionCommandComponent.h"
#include "Social/CatSocialTypes.h"
#include "CatSocialBodyActionCommandComponent.generated.h"

class APlayerState;

/** Social 身体动作命令组件；只负责创建求助、恶作剧和保护牌载荷，不接触 Camp 或 Condition 动作。 */
UCLASS(ClassGroup=(Catfishing), meta=(BlueprintSpawnableComponent))
class CATFISHING_API UCatSocialBodyActionCommandComponent : public UCatBodyActionCommandComponent
{
	GENERATED_BODY()

public:
	/** 提交手动求助身体动作请求；普通信号类型是否合法仍由 Social 服务裁决。 */
	bool SubmitManualHelp(FGuid RequestId, ECatHelpSignalKind HelpKind) const;

	/** 提交恶作剧身体动作请求；组件只冻结目标 PlayerState，权限、冷却和保护牌规则仍由 Social 服务裁决。 */
	bool SubmitMischief(APlayerState* TargetPlayerState, FGuid RequestId, FVector InteractionLocation) const;

	/** 提交放置保护牌身体动作请求；唯一保护牌和距离规则仍由 Social 服务裁决。 */
	bool SubmitPlaceProtectionSign(FGuid RequestId, FVector SignLocation) const;
};
