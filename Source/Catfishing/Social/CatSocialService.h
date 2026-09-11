#pragma once

#include "CoreMinimal.h"
#include "Social/CatSocialTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "CatSocialService.generated.h"

class ACatCharacter;
class AController;
class UCatFishInventoryItemInstance;
class UCatInventoryComponent;

/** 一局服务器 Social 深模块；拥有权限/求助/偷鱼协议，不拥有 Character 救援或 Fishing 阶段状态。 */
UCLASS()
class CATFISHING_API UCatSocialService : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 只在 authority Game World 创建；客户端从 GameState/Actor 复制和表现事件观察。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** World 销毁时清计时器并返还所有仍在追回窗口的鱼；随后清一局协议。 */
	virtual void Deinitialize() override;

	/** Host teardown 时先关闭全部新 Social 命令，再清计时器并经来源库存返还所有被偷鱼实例；全部协议清空才返回 true。 */
	bool CloseCommandsAndResolveAll();

	/** 开始最多一条鱼的偷取；按身份/操作/RequestId 重放，首提通过权限与来源库存扣除后开启唯一进食计时器。 */
	FCatTheftResult BeginTheft(AController* ThiefController, const FCatTheftCommand& Command);

	/** 用服务器 ProtocolId 让真实受害者或已裁共享缸成员在窗口内追回；成功按身份重放，距离/状态失败可在窗口内重新尝试。 */
	FCatTheftResult CatchTheft(AController* CatchingController, FGuid TheftProtocolId);

	/** 幂等检查普通恶作剧权限、冷却与 ProtectionSign；成功只表示 Social 允许，上层玩法仍负责自身命中/演出。 */
	FCatDomainCommandResult RequestMischief(AController* InstigatorController, AController* TargetController,
		FGuid RequestId, FVector InteractionLocation);

	/** 玩家幂等地在自身附近放置或移动唯一 ProtectionSign；显式范围未裁时拒绝，不影响偷鱼权限。 */
	FCatDomainCommandResult PlaceProtectionSign(AController* RequestingController, FGuid RequestId,
		FVector SignLocation);

	/** 玩家幂等地发布普通钓鱼/倒地求助；同请求不重复 Revision，信号保持 nearby 且不自动分配任务。 */
	FCatDomainCommandResult RequestManualHelp(AController* RequestingController, FGuid RequestId,
		ECatHelpSignalKind Kind);

	/** Giant FishingSession 建立后发布唯一系统全局提示；普通鱼不得调用该入口。 */
	void BroadcastGiantFishingPrompt(AController* FisherController, FGuid FishingSessionId);

	/** Character 失去占有/倒地销毁前返还其作为小偷持有的鱼；受害者掉线不自动吃掉。 */
	void CancelTheftsForCharacter(const ACatCharacter* Character);

private:
	/** 一条服务器私有偷鱼协议；鱼实体已经离开来源库存，Social 只持同一实例、计时器、参与者和公开结果。 */
	struct FActiveTheft
	{
		/** Social 分配并贯穿库存返还、Timer 和追回的服务器唯一协议 ID。 */
		FGuid TheftProtocolId;
		/** 客户端最初 Begin 意图的 RequestId；只用于身份作用域终态重放。 */
		FGuid ClientRequestId;
		/** 偷取者服务器私有 StableNetId。 */
		FString ThiefStableNetId;
		/** 鱼实例原捕获者的服务器私有 StableNetId。 */
		FString VictimStableNetId;
		/** 偷取者当前 Character 弱引用；失效时返还鱼。 */
		TWeakObjectPtr<ACatCharacter> ThiefCharacter;
		/** 被偷前持有鱼实例的库存组件；追回和 teardown 用它把同一实例放回可见库存。 */
		TWeakObjectPtr<UCatInventoryComponent> SourceInventory;
		/** 被偷前的来源槽位；日志和未来 UI 回放使用，返还时不伪造旧槽仍然可用。 */
		int32 SourceInventorySlotIndex = INDEX_NONE;
		/** 被偷的鱼物品实例；开始偷取后它不再位于任何库存槽，但仍是同一个 UObject。 */
		TWeakObjectPtr<UCatFishInventoryItemInstance> FishItem;
		/** 被偷鱼实例的稳定 ID；公开结果、日志和印记候选用它，不重新从可能失效的实例猜测。 */
		FGuid FishItemInstanceId;
		/** 被偷鱼的定义 ID；被抓印记需要这个稳定鱼种，而不依赖追回时实例仍可访问。 */
		FName FishDefinitionId = NAME_None;
		/** 当前可重放公开结果。 */
		FCatTheftResult Result;
		/** 进食窗口的唯一 Timer；追回/取消/销毁必须清除。 */
		FTimerHandle EatingWindowTimer;
	};

	/** 进食窗口到期回调；依赖完整时直接用已移出的鱼实例向小偷 Character 提交食用效果并回送最终协议。 */
	void HandleTheftWindowExpired(FGuid TheftProtocolId);

	/** 把已移出的鱼实例放回来源库存并在成功后移除 Social 协议；依赖失败时保留活跃记录，调用方可继续有界收口且不会伪造 returned。 */
	FCatTheftResult ReturnActiveTheft(FGuid TheftProtocolId);

	/** 被抓终态成功后提交双方印记候选，并批量先建齐两人的 Planned 记录再投递；事件 ID 未配置时安全跳过。 */
	void SubmitCaughtImprint(const FActiveTheft& Theft, const FString& CatcherStableNetId);

	/** 组合服务器身份、操作名与 RequestId 为一局终态键；同一请求即使载荷变化也只能重放首次裁决。 */
	static FString MakeTerminalKey(const FString& StableNetId, const TCHAR* Operation, FGuid RequestId);

	/** 从 Controller PlayerState::UniqueId 解析服务器私有身份；无效返回空。 */
	static FString ResolveStableNetId(const AController* Controller);

	/** 在当前 World 按 APlayerState::UniqueId 定位精确活动 Controller；未知鱼实例捕获者或断线时返回空并让 Social fail-closed。 */
	AController* FindControllerByStableNetId(const FString& StableNetId) const;

	/** 判断项目 Character 当前可参与 Social 交互：角色/Condition 有效且未倒地。 */
	static bool IsCharacterSociallyActive(const ACatCharacter* Character);

	/** 服务器 TheftProtocolId 到活跃偷鱼协议；同一鱼只会离开一个来源库存并由这个协议持有。 */
	TMap<FGuid, FActiveTheft> ActiveThefts;

	/** 偷取者 StableNetId 到唯一活跃 ProtocolId；保证最大负面影响为一条鱼。 */
	TMap<FString, FGuid> ActiveTheftByThief;

	/** 玩家身份到上次普通恶作剧服务器时间；未裁冷却不会写入。 */
	TMap<FString, double> LastMischiefTimeByPlayer;

	/** 玩家身份到上次手动求助服务器时间；Giant 系统提示不占用该冷却。 */
	TMap<FString, double> LastManualHelpTimeByPlayer;

	/** 身份+操作+RequestId 到普通 Social 命令首次终态；成功重试不会再次移动牌子、发信号或消耗冷却。 */
	TMap<FString, FCatDomainCommandResult> CommandTerminalCache;

	/** 身份+操作+关联键到偷鱼协议结果；Begin 用客户端 RequestId 更新最终 returned/consumed，Catch 用 ProtocolId 只缓存成功追回。 */
	TMap<FString, FCatTheftResult> TheftTerminalCache;

	/** GameState 求助信号单调 Revision。 */
	int64 HelpSignalRevision = 0;

	/** 玩家 StableNetId 到其当前唯一防骚扰牌子弱引用；重放移动同一 Actor，不叠加多个保护区。 */
	TMap<FString, TWeakObjectPtr<class ACatProtectionSignActor>> ProtectionSignByPlayer;

	/** 一局 Social 新命令门；Host teardown 后永久置 false，内部返还辅助仍可完成既有协议。 */
	bool bCommandsOpen = true;
};
