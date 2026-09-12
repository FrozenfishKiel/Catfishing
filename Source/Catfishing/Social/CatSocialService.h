#pragma once

#include "CoreMinimal.h"
#include "Social/CatSocialTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "CatSocialService.generated.h"

class ACatCharacter;
class AController;

/** 一局服务器 Social 深模块；拥有恶作剧权限、防骚扰牌与求助信号，不拥有拿鱼、Character 救援或 Fishing 阶段状态。
 *  拿鱼不在这里：它是客观的库存移动，规则（够得着、鱼护在地面、一嘴一条）在库存链上，Social 不判断动机或归属（2026-09-11 拍）。 */
UCLASS()
class CATFISHING_API UCatSocialService : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 只在 authority Game World 创建；客户端从 GameState/Actor 复制和表现事件观察。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** World 销毁时清一局权限缓存与牌子索引；Social 不再持有任何可逆的鱼事务，没有要返还的东西。 */
	virtual void Deinitialize() override;

	/** Host teardown 时永久关闭全部新 Social 命令；Social 只持权限与信号，关门之后没有待收口的实物事务。 */
	void CloseCommands();

	/** 幂等检查普通恶作剧权限、冷却与 ProtectionSign；成功只表示 Social 允许，上层玩法仍负责自身命中/演出。 */
	FCatDomainCommandResult RequestMischief(AController* InstigatorController, AController* TargetController,
		FGuid RequestId, FVector InteractionLocation);

	/** 玩家幂等地在自身附近放置或移动唯一 ProtectionSign；显式范围未裁时拒绝。
	 *  牌子目前只裁决恶作剧。「立牌＝完整免打扰」按「拿鱼」重述后应当覆盖「别人从你的地面鱼护拿鱼」，
	 *  但那条豁免的几何口径（保护的是牌子范围内的鱼护，还是鱼的归属者）09-11 仍挂在李前臻名下未裁，
	 *  所以拿鱼路径暂时不读牌子；没裁的部分不在代码里先补一个半成品（联机社交册 §3.1.5、09-11 回填清单 L215）。 */
	FCatDomainCommandResult PlaceProtectionSign(AController* RequestingController, FGuid RequestId,
		FVector SignLocation);

	/** 玩家幂等地发布普通钓鱼/倒地求助；同请求不重复 Revision，信号保持 nearby 且不自动分配任务。 */
	FCatDomainCommandResult RequestManualHelp(AController* RequestingController, FGuid RequestId,
		ECatHelpSignalKind Kind);

	/** Giant FishingSession 建立后发布唯一系统全局提示；普通鱼不得调用该入口。 */
	void BroadcastGiantFishingPrompt(AController* FisherController, FGuid FishingSessionId);

private:
	/** 组合服务器身份、操作名与 RequestId 为一局终态键；同一请求即使载荷变化也只能重放首次裁决。 */
	static FString MakeTerminalKey(const FString& StableNetId, const TCHAR* Operation, FGuid RequestId);

	/** 从 Controller PlayerState::UniqueId 解析服务器私有身份；无效返回空。 */
	static FString ResolveStableNetId(const AController* Controller);

	/** 判断项目 Character 当前可参与 Social 交互：角色/Condition 有效且未倒地。 */
	static bool IsCharacterSociallyActive(const ACatCharacter* Character);

	/** 玩家身份到上次普通恶作剧服务器时间；未裁冷却不会写入。 */
	TMap<FString, double> LastMischiefTimeByPlayer;

	/** 玩家身份到上次手动求助服务器时间；Giant 系统提示不占用该冷却。 */
	TMap<FString, double> LastManualHelpTimeByPlayer;

	/** 身份+操作+RequestId 到普通 Social 命令首次终态；成功重试不会再次移动牌子、发信号或消耗冷却。 */
	TMap<FString, FCatDomainCommandResult> CommandTerminalCache;

	/** GameState 求助信号单调 Revision。 */
	int64 HelpSignalRevision = 0;

	/** 玩家 StableNetId 到其当前唯一防骚扰牌子弱引用；重放移动同一 Actor，不叠加多个保护区。 */
	TMap<FString, TWeakObjectPtr<class ACatProtectionSignActor>> ProtectionSignByPlayer;

	/** 一局 Social 新命令门；Host teardown 后永久置 false。 */
	bool bCommandsOpen = true;
};
