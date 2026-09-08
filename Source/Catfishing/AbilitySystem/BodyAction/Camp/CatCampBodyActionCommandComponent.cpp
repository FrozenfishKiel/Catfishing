#include "AbilitySystem/BodyAction/Camp/CatCampBodyActionCommandComponent.h"

#include "AbilitySystem/BodyAction/Camp/CatCampBodyActionAbilities.h"
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"

bool UCatCampBodyActionCommandComponent::SubmitCampRest(ACatCampHubActor* Camp, const FGuid RequestId) const
{
	// 休息载荷流程：组件创建休息专用请求并投给 ASC；Controller 不再了解请求类和 Ability 触发细节。
	// Submit 方法保持 const，是因为它不写组件状态；NewObject 需要非 const Outer 只是为了让瞬时载荷挂在组件生命周期下等待 GameplayEvent 消费。
	UCatBodyActionRequestCampRest* Payload = NewObject<UCatBodyActionRequestCampRest>(
		const_cast<UCatCampBodyActionCommandComponent*>(this));
	Payload->Camp = Camp;
	Payload->RequestId = RequestId;
	return SubmitPayload(Payload, CatFishingAbilityTags::AbilityEvent_Body_CampRest);
}

bool UCatCampBodyActionCommandComponent::SubmitCampfirePlayback(ACatCampHubActor* Camp,
	const FGuid RequestId) const
{
	// 回看载荷流程：组件创建回看专用请求并投给 ASC；夜晚结算和 CapturePlan 仍在 Camp 写口完成。
	// Submit 方法保持 const，是因为它不写组件状态；NewObject 需要非 const Outer 只是为了让瞬时载荷挂在组件生命周期下等待 GameplayEvent 消费。
	UCatBodyActionRequestCampfirePlayback* Payload = NewObject<UCatBodyActionRequestCampfirePlayback>(
		const_cast<UCatCampBodyActionCommandComponent*>(this));
	Payload->Camp = Camp;
	Payload->RequestId = RequestId;
	return SubmitPayload(Payload, CatFishingAbilityTags::AbilityEvent_Body_CampfirePlayback);
}

bool UCatCampBodyActionCommandComponent::SubmitRescueCharacterToCamp(ACatCampHubActor* Camp,
	ACatCharacter* TargetCharacter, const FGuid RequestId) const
{
	// 救援载荷流程：组件创建救援专用请求并投给 ASC；Ability 前摇结束后才进入 Camp/Condition 的权威裁决。
	// Submit 方法保持 const，是因为它不写组件状态；NewObject 需要非 const Outer 只是为了让瞬时载荷挂在组件生命周期下等待 GameplayEvent 消费。
	UCatBodyActionRequestRescueCharacterToCamp* Payload = NewObject<UCatBodyActionRequestRescueCharacterToCamp>(
		const_cast<UCatCampBodyActionCommandComponent*>(this));
	Payload->Camp = Camp;
	Payload->TargetCharacter = TargetCharacter;
	Payload->RequestId = RequestId;
	return SubmitPayload(Payload, CatFishingAbilityTags::AbilityEvent_Body_RescueCharacterToCamp);
}
