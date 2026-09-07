#include "AbilitySystem/BodyAction/Core/CatBodyActionCommandComponent.h"

#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Framework/Game/CatfishingPlayerController.h"

UCatBodyActionCommandComponent::UCatBodyActionCommandComponent()
{
	// 构造流程：BodyAction RPC 只需要临时 GameplayEvent 投递，不持有可复制状态，也不需要每帧轮询。
	PrimaryComponentTick.bCanEverTick = false;
}

bool UCatBodyActionCommandComponent::SubmitPayload(UObject* Payload, const FGameplayTag BodyActionEventTag) const
{
	// GameplayEvent 投递流程：
	// 1. 先确认拥有者是 Cat PlayerController，调用方给出了明确身体动作事件标签。
	// 2. 再从当前 Pawn 找 authority ASC，保证客户端本地不能绕过服务器 RPC 启动身体动作。
	// 3. 最后把专用载荷作为 OptionalObject 投给同标签 Ability；没有授予或标签不匹配时返回 false。
	ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(GetOwner());
	if (!Controller || !Payload || !BodyActionEventTag.IsValid())
	{
		return false;
	}
	UCatAbilitySystemComponent* AbilitySystem =
		UCatAbilitySystemComponent::FindCatAbilitySystemFromActor(Controller->GetPawn());
	if (!AbilitySystem || !AbilitySystem->IsOwnerActorAuthoritative())
	{
		return false;
	}
	FGameplayEventData EventData;
	EventData.EventTag = BodyActionEventTag;
	EventData.Instigator = Controller->GetPawn();
	EventData.Target = Controller->GetPawn();
	EventData.OptionalObject = Payload;
	return AbilitySystem->HandleGameplayEvent(BodyActionEventTag, &EventData) > 0;
}
