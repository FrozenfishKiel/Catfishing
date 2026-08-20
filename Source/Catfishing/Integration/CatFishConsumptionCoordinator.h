#pragma once

#include "CoreMinimal.h"
#include "Items/CatItemTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "CatFishConsumptionCoordinator.generated.h"

class ACatCharacter;

/**
 * 把"从容器里拿掉一条鱼"和"这只猫结算这次进食"串成一条链的服务器协调器：空间与身体 preflight → Items 不可逆移除 → Condition 结算食用后果。
 *
 * 它存在的理由和献祭、商店订单那两条链一样：鱼是 Items 容器的事实，中毒与倒地是角色 Condition 的事实，
 * 两边都不该反过来调对方，所以需要第三个地方按固定顺序推这条链；这条链还要读 Camp 的交互距离和 Data 的鱼定义，
 * 跨的领域比前两条都多，因此落在 `Integration/` 而不是任何单个领域目录里。
 *
 * 它拥有的只有这条链的首次终态缓存。它不拥有鱼、不拥有中毒数值，也不判断"这个 Controller 现在能不能发玩法命令"——
 * 那道门由 GameMode 的 gate 回答，调用方必须先过门再进来；"客户端报的进食者是不是它自己的 Pawn"同理由 RPC 层判。
 *
 * 失败回退边界：整条链只有一个不可逆点，就是 Items 真的把鱼从容器里删掉那一刻，它之前的每一步失败都不改任何事实。
 * 越过它之后不存在回退分支——Condition 结算失败也不会把鱼放回容器，因为那等于凭空多出一条鱼；
 * 这种情况下 Items 那一段的成功终态照常返回，Condition 自己的终态缓存负责让同一 RequestId 不被结算第二次。
 *
 * 它不负责偷来的鱼。赃物有自己的 escrow 协议（追回窗口、逃离前提、归还分支都在 Social），
 * 走的是 Items 的另一个写口 `CommitStolenFishConsumption`，准入条件与本链没有一条相同，硬合成一个入口只会得到一张开关矩阵。
 */
UCLASS()
class CATFISHING_API UCatFishConsumptionCoordinator : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 只在服务器 Game World 创建；客户端没有这条链，也不能本地推进进食。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/**
	 * 声明：按一条已经带上服务器身份的进食意图跑完整条链，返回 Items 侧的终态（含被吃掉的那条鱼）。
	 *       EatingCharacter 是进食者本人的身体，由 RPC 层从它自己的 Pawn 取好交进来；本函数不再复核它属于谁。
	 * 实现：先按"身份 + 容器 + RequestId"查首次终态，命中且载荷一致就原样重放；确认是首次之后依次校验依赖、源容器、
	 *       是否倒地、容器宿主、共享缸距离、目标鱼与鱼定义、身体 preflight，全部通过才让 Items 不可逆移除，
	 *       移除确实提交后才让 Condition 结算食用后果；最后把终态和载荷签名一起写进缓存。
	 * 边界：RequestId 无效或身份为空时构造不出幂等键，只能稳定拒绝且不进缓存。其余终态成功与失败都进缓存，
	 *       与 `UCatConditionComponent::ConsumeCommittedFish` 同一口径——同 RequestId 只有一次结算机会，重试必须换新 RequestId。
	 *       每一处拒绝都记一行 Event=fish_consume_command_rejected 并带上原因，这是服务器侧"为什么没吃成"的唯一可观察点。
	 */
	FCatFishConsumeResult ConsumeFishFromContainer(const FCatFishConsumeCommand& Command, ACatCharacter* EatingCharacter);

private:
	/** 组合服务器身份、源容器与 RequestId 的幂等键；换个容器就是另一次意图，两名玩家的同一 RequestId 也互不干扰。 */
	static FString MakeTerminalKey(const FString& StableNetId, const FGuid& ContainerId, const FGuid& RequestId);

	/** 进食链的首次终态；覆盖 preflight、Items 移除和 Condition 结算三段，重放不会再吃掉第二条鱼。 */
	TMap<FString, FCatFishConsumeResult> TerminalCache;

	/** 首次终态对应的业务载荷签名；同 RequestId 换目标鱼或换容器版本前提会被拒绝。 */
	TMap<FString, FString> TerminalPayloadByKey;
};
