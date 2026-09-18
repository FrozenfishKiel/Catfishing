#include "AbilitySystem/Items/Abilities/CatGA_Horn.h"
#include "Inventory/Fragments/CatItemUseFragment.h"
#include "Framework/Game/CatfishingGameState.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Engine/World.h"

// 检查流程：库存与阶段通过后再验证文本及广播依赖；空白和控制字符均在扣费前拒绝。
bool UCatGA_Horn::ValidateUse() const
{
	if (!Super::ValidateUse() || UseTarget.Message.TrimStartAndEnd().IsEmpty() || UseTarget.Message.Len() > 120
		|| !GetWorld()->GetGameState<ACatfishingGameState>()) return false;
	for (const TCHAR Character : UseTarget.Message) if (Character < 32 || Character == 127) return false;
	return true;
}
// 配置检查流程：次数是唯一成本，最后一次消费本体，避免一声同时扣整件和次数。
bool UCatGA_Horn::ValidateUseConfiguration(const UCatItemUseFragment& Config, FText& Error) const
{
	const bool bValid = Config.ConsumeCount == 0 && Config.ResourceCapacity > 0 && Config.bConsumeWhenEmpty && Config.Effects.IsEmpty();
	if (!bValid) Error = NSLOCTEXT("CatItem", "HornConfig", "响响筒需有限次数、耗尽消耗本体，件数消耗为零且无自用效果。");
	return bValid;
}
// 广播流程：服务器使用能力内冻结的文字，公开显示名来自当前 PlayerState；GameState 覆盖全队网络相关性。
void UCatGA_Horn::OnUseCommitted(UCatInventoryItemInstance* ConsumedItem)
{
	auto* State = GetWorld()->GetGameState<ACatfishingGameState>();
	auto* Controller = CurrentActorInfo->PlayerController.Get();
	const FString Name = Controller && Controller->PlayerState ? Controller->PlayerState->GetPlayerName() : TEXT("队友");
	if (State) State->Multicast_HornAnnouncement(Name, UseTarget.Message.TrimStartAndEnd(), UseTarget.RequestId);
}
