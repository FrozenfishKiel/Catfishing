#pragma once

#include "CoreMinimal.h"
#include "Social/CatSocialTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "CatSocialService.generated.h"

class ACatCharacter;
class AController;

/** 一局服务器 Social 深模块；拥有求助、恶作剧和保护牌权限，不拥有 Character 救援、Fishing 阶段或拿鱼叙事。 */
UCLASS()
class CATFISHING_API UCatSocialService : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 只在 authority Game World 创建；客户端从 GameState/Actor 复制和表现事件观察。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** World 销毁时关闭新 Social 命令并清一局缓存。 */
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

	/** 玩家幂等地在自身附近放置或移动唯一 ProtectionSign；显式范围未裁时拒绝。 */
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


	/** 玩家身份到上次手动求助服务器时间；Giant 系统提示不占用该冷却。 */
	TMap<FString, double> LastManualHelpTimeByPlayer;

	/** 身份+操作+RequestId 到普通 Social 命令首次终态；成功重试不会再次移动牌子、发信号或消耗冷却。 */
	TMap<FString, FCatDomainCommandResult> CommandTerminalCache;

	/** GameState 求助信号单调 Revision。 */
	int64 HelpSignalRevision = 0;

	/** 玩家 StableNetId 到其当前唯一防骚扰牌子弱引用；重放移动同一 Actor，不叠加多个保护区。 */
	TMap<FString, TWeakObjectPtr<class ACatProtectionSignActor>> ProtectionSignByPlayer;

	/** 一局 Social 是否接收新命令的开关；Host 拆场关闭后，求助、恶作剧和保护牌请求均被拒绝。 */
	bool bCommandsOpen = true;
};
