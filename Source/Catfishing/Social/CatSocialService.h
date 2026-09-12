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

	/**
	 * 幂等检查普通恶作剧的距离、目标身体状态与 ProtectionSign；成功只表示 Social 允许，上层玩法仍负责自身命中/演出。
	 *
	 * 这里没有频率上限也没有时机限制（联机社交 §3.1.4，熟人自治）：连着整同一只猫、在人家搏斗最紧张的时候整，
	 * 规则上都允许——被整正是戏。护栏只有两样：被整者立的防骚扰牌，和房主踢人。
	 * 拒绝项只有三类：够不着、目标倒地、目标在牌子保护内或正臭着（臭臭鱼 90 秒「请勿靠近」）。
	 */
	FCatDomainCommandResult RequestMischief(AController* InstigatorController, AController* TargetController,
		FGuid RequestId, FVector InteractionLocation);

	/**
	 * 玩家幂等地在自身附近放置或移动唯一 ProtectionSign；显式范围未裁时拒绝。
	 *
	 * 牌子只裁决恶作剧，**不挡拿鱼**（2026-09-12 裁决③，联机社交 §3.1.4／§3.1.5）：
	 * 08-16 那句「立牌＝完整免打扰」随「恶作剧权限开关」这层概念一并退役，代码一直就是这么做的、此前是文档说错了。
	 * 牌子本身保留、现状不变、待重新设计——那时候从零开始比从一句悬空的话开始干净。
	 */
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

	// 墓碑（2026-09-12）：这里原有 `TMap<FString, double> LastMischiefTimeByPlayer`，配 MischiefCooldownSeconds
	// 给每名玩家记一次恶作剧时间戳做系统级频率上限。联机社交 §3.1.4 明写不设这道闸，整张表随之删除。
	// 手动求助的冷却（LastManualHelpTimeByPlayer）是另一回事：那是防信号刷屏，设计没要求取消。

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
