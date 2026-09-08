#include "AbilitySystem/BodyAction/Social/CatSocialBodyActionCommandComponent.h"

#include "AbilitySystem/BodyAction/Social/CatSocialBodyActionAbilities.h"
#include "AbilitySystem/Tags/CatFishingAbilityTags.h"

bool UCatSocialBodyActionCommandComponent::SubmitManualHelp(const FGuid RequestId,
	const ECatHelpSignalKind HelpKind) const
{
	// 求助载荷流程：组件创建普通求助请求并投给 ASC；系统全局提示不会从这条玩家身体动作入口伪造。
	// Submit 方法保持 const，是因为它不写组件状态；NewObject 需要非 const Outer 只是为了让瞬时载荷挂在组件生命周期下等待 GameplayEvent 消费。
	UCatBodyActionRequestManualHelp* Payload = NewObject<UCatBodyActionRequestManualHelp>(
		const_cast<UCatSocialBodyActionCommandComponent*>(this));
	Payload->RequestId = RequestId;
	Payload->HelpKind = HelpKind;
	return SubmitPayload(Payload, CatFishingAbilityTags::AbilityEvent_Body_RequestManualHelp);
}

bool UCatSocialBodyActionCommandComponent::SubmitMischief(APlayerState* TargetPlayerState,
	const FGuid RequestId, const FVector InteractionLocation) const
{
	// 恶作剧载荷流程：组件只冻结玩家请求参数；后续 Ability 会做当前 World 只读查找，权限和保护牌仍由 Social 裁决。
	// Submit 方法保持 const，是因为它不写组件状态；NewObject 需要非 const Outer 只是为了让瞬时载荷挂在组件生命周期下等待 GameplayEvent 消费。
	UCatBodyActionRequestMischief* Payload = NewObject<UCatBodyActionRequestMischief>(
		const_cast<UCatSocialBodyActionCommandComponent*>(this));
	Payload->TargetPlayerState = TargetPlayerState;
	Payload->RequestId = RequestId;
	Payload->InteractionLocation = InteractionLocation;
	return SubmitPayload(Payload, CatFishingAbilityTags::AbilityEvent_Body_RequestMischief);
}

bool UCatSocialBodyActionCommandComponent::SubmitPlaceProtectionSign(const FGuid RequestId,
	const FVector SignLocation) const
{
	// 放牌载荷流程：组件只冻结期望位置和请求键；唯一保护牌规则仍由 Social 服务维护。
	// Submit 方法保持 const，是因为它不写组件状态；NewObject 需要非 const Outer 只是为了让瞬时载荷挂在组件生命周期下等待 GameplayEvent 消费。
	UCatBodyActionRequestPlaceProtectionSign* Payload = NewObject<UCatBodyActionRequestPlaceProtectionSign>(
		const_cast<UCatSocialBodyActionCommandComponent*>(this));
	Payload->RequestId = RequestId;
	Payload->SignLocation = SignLocation;
	return SubmitPayload(Payload, CatFishingAbilityTags::AbilityEvent_Body_PlaceProtectionSign);
}
