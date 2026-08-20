#pragma once

#include "CoreMinimal.h"
#include "Social/CatSocialTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "CatSocialService.generated.h"

class ACatCharacter;
class AController;
class APlayerState;

/** 一局服务器 Social 深模块；拥有权限/求助/偷鱼协议，不拥有 Character 救援或 Fishing 阶段状态。 */
UCLASS()
class CATFISHING_API UCatSocialService : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** 只在 authority Game World 创建；客户端从 GameState/Actor 复制和表现事件观察。 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** 服务建立时把 Settings 里的偷取/恶作剧默认权限抄进本局运行期策略，并给出策略首个版本号；此后 Settings 不再参与权限裁决。 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** World 销毁时清计时器并返还所有仍在追回窗口的鱼；随后清一局协议。 */
	virtual void Deinitialize() override;

	/** Host teardown 时先关闭全部新 Social 命令，再清计时器并经 Items 返还所有 theft escrow；全部协议清空才返回 true。 */
	bool CloseCommandsAndResolveAll();

	/** 开始最多一条鱼的偷取；按身份/操作/RequestId 重放，首提要先过权限、鱼主人防骚扰牌与 Items escrow，才开启唯一进食计时器。 */
	FCatTheftResult BeginTheft(AController* ThiefController, const FCatTheftCommand& Command);

	/** 用服务器 ProtocolId 让真实受害者或已裁共享缸成员在窗口内追回；成功按身份重放，距离/状态失败可在窗口内重新尝试。 */
	FCatTheftResult CatchTheft(AController* CatchingController, FGuid TheftProtocolId);

	/**
	 * 偷鱼售出入口；成功时用同一 RequestId 串起 Items 准备态、团队钱包入账和 escrow drain，重放只读取首次售出事实。
	 * 成交价不是参数：服务器拿这条鱼在捕获时就冻结下来的重量，现场向 ShopEconomy 要一个价，客户端既报不了价也改不了价。
	 * 收鱼价还没被裁定、或者这条鱼的重量在体重轴上查不到档位时，整笔售出被拒，不会退而求其次给个兜底价。
	 * ExpectedWalletRevision 仍由调用方带进来，它表达的是"我读到的公款版本"这个并发前提，与定价无关。
	 */
	FCatTheftResult SellStolenFish(AController* ThiefController, FGuid TheftProtocolId,
		FGuid RequestId, int64 ExpectedWalletRevision);

	/** 幂等检查普通恶作剧权限、双方距离与目标 ProtectionSign；不做频率限制，成功只表示 Social 允许，上层玩法仍负责自身命中/演出。 */
	FCatDomainCommandResult RequestMischief(AController* InstigatorController, AController* TargetController,
		FGuid RequestId);

	/** 玩家幂等地在自身附近放置或移动唯一 ProtectionSign；只看牌子自身的显式范围，不受恶作剧权限开关影响。放下的牌子同时挡恶作剧和偷窃。 */
	FCatDomainCommandResult PlaceProtectionSign(AController* RequestingController, FGuid RequestId,
		FVector SignLocation);

	/** 玩家幂等地发布普通钓鱼/倒地求助；同请求不重复 Revision，信号保持 nearby 且不自动分配任务。 */
	FCatDomainCommandResult RequestManualHelp(AController* RequestingController, FGuid RequestId,
		ECatHelpSignalKind Kind);

	/** Giant FishingSession 建立后发布唯一系统全局提示；普通鱼不得调用该入口。 */
	void BroadcastGiantFishingPrompt(AController* FisherController, FGuid FishingSessionId);

	/** Character 失去占有/倒地销毁前返还其作为小偷持有的鱼；受害者掉线不自动吃掉。 */
	void CancelTheftsForCharacter(const ACatCharacter* Character);

	/**
	 * 当前场上所有"嘴里叼着一条赃物"的猫，由服务器从活跃偷鱼协议整体派生，内容里不含受害者身份与服务器协议 ID。
	 * 它是给全场看的那份读模型的权威来源：受害者和旁观者要靠它认出该追谁，追回窗口才成立。
	 * 需要说明的是，Social 只负责生产和维护这份列表，把它复制到客户端的那一步在本模块之外（GameState 侧），
	 * 所以在复制出口接上之前，这里的内容只有服务器读得到。
	 */
	const TArray<FCatStolenFishCarrySnapshot>& GetStolenFishCarriers() const;

	/** 当前本局偷取与恶作剧权限；BeginTheft 和 RequestMischief 都只读它，不再读 Settings。 */
	const FCatSocialPolicySnapshot& GetSocialPolicy() const;

	/**
	 * 登记本局局主身份；只有登记过的那名玩家才能改运行期 Social 权限。传空指针表示局主未知，此时任何人都改不了策略，
	 * 权限保持开局默认值。谁是局主由会话成员关系决定，本服务不自行推断。
	 */
	void SetHostAuthority(const AController* HostController);

	/** 局主在运行期整体改写偷取与恶作剧权限；非局主一律拒绝，权限真的发生变化才推进策略版本。 */
	FCatDomainCommandResult SetSocialPolicy(AController* RequestingController, FGuid RequestId,
		ECatDomainPolicy NewTheftPermission, ECatDomainPolicy NewMischiefPermission);

private:
	/** 一条服务器私有偷鱼协议；鱼实体在 Items escrow，Social 只持计时器、参与者和公开结果。 */
	struct FActiveTheft
	{
		/** 客户端最初 Begin 意图的 RequestId；只用于身份作用域终态重放。 */
		FGuid ClientRequestId;
		/** 偷取者服务器私有 StableNetId。 */
		FString ThiefStableNetId;
		/** 鱼的原主人服务器私有 StableNetId。 */
		FString VictimStableNetId;
		/** 偷取者当前 Character 弱引用；失效时返还鱼。 */
		TWeakObjectPtr<ACatCharacter> ThiefCharacter;
		/** 源容器种类，用于共享缸追回策略。 */
		ECatContainerKind SourceKind = ECatContainerKind::Unknown;
		/** Items escrow 返回的不可变鱼事实。 */
		FCatFishInstance Fish;
		/** 当前可重放公开结果。 */
		FCatTheftResult Result;
		/** 进食窗口的唯一 Timer；追回/取消/销毁必须清除。 */
		FTimerHandle EatingWindowTimer;
	};

	/** 进食窗口到期回调；依赖完整时先由 Items 不可逆吃掉，再向小偷 Character 提交食用效果。 */
	void HandleTheftWindowExpired(FGuid TheftProtocolId);

	/** 让 Items 按 ProtocolId 原位返还并在成功后移除 Social 协议；依赖失败时保留活跃记录，调用方可继续有界收口且不会伪造 returned。 */
	FCatTheftResult ReturnActiveTheft(FGuid TheftProtocolId);

	/** 被抓终态成功后提交双方印记候选，并批量先建齐两人的 Planned 记录再投递；事件 ID 未配置时安全跳过。 */
	void SubmitCaughtImprint(const FActiveTheft& Theft, const FString& CatcherStableNetId);

	/** 组合服务器身份、操作名与 RequestId 为一局终态键；只有业务载荷一致时才允许读取首次裁决。 */
	static FString MakeTerminalKey(const FString& StableNetId, const TCHAR* Operation, FGuid RequestId);

	/** 判断项目 Character 当前可参与 Social 交互：角色/Condition 有效且未倒地。 */
	static bool IsCharacterSociallyActive(const ACatCharacter* Character);

	/**
	 * 判断指定玩家在给定世界位置是否被任一防骚扰牌子罩住；恶作剧和偷窃共用这一次查询，不各自实现半径判定。目标
	 * PlayerState 为空或 World 已失效时返回 false，由调用方自己的前置检查负责这两种情况。
	 */
	bool IsPlayerProtectedAt(const APlayerState* TargetPlayerState, const FVector& InteractionLocation) const;

	/** 按当前活跃协议整体重建公开叼鱼列表并推进一次版本。所有增删活跃协议的地方都要调用它，公开事实因此不可能与 escrow 事实分叉。 */
	void RefreshStolenFishCarriers();

	/** 判断小偷此刻是否站在某个商店锚点的售出距离内；锚点标签或距离未登记时返回 false，让售出保持不可达。 */
	bool IsAtShopAnchor(const ACatCharacter* ThiefCharacter) const;

	/** 判断小偷此刻是否已经跑离受害者足够远、可以把鱼吃掉；距离未登记、受害者当前不在场因而无从判定时都返回 false。 */
	bool HasEscapedVictimForConsumption(const FActiveTheft& Theft) const;

	/** 服务器 TheftProtocolId 到活跃偷鱼协议；同一鱼只由 Items escrow 保证单一实体。 */
	TMap<FGuid, FActiveTheft> ActiveThefts;

	/** 偷取者 StableNetId 到唯一活跃 ProtocolId；保证最大负面影响为一条鱼。 */
	TMap<FString, FGuid> ActiveTheftByThief;

	/** 从 ActiveThefts 派生的公开叼鱼列表；它是发给全场的读模型，返还/吃掉/售出让协议离开活跃表时自然清空。 */
	TArray<FCatStolenFishCarrySnapshot> PublicStolenFishCarriers;

	/** 公开叼鱼列表的单调版本；每次重建递增一次，并写进本次发布的每个条目。调用点全部位于活跃协议真的发生增删之后，所以它不会空转。 */
	int64 StolenFishCarryRevision = 0;

	/** 本局偷取与恶作剧权限的唯一运行期真相；开局从 Settings 抄一次，之后只由局主改写。 */
	FCatSocialPolicySnapshot RuntimePolicy;

	/** 已登记的局主服务器私有身份；为空表示局主未知，此时没有人能改运行期权限。 */
	FString HostStableNetId;

	/** 玩家身份到上次手动求助服务器时间；Giant 系统提示不占用该冷却。 */
	TMap<FString, double> LastManualHelpTimeByPlayer;

	/** 身份+操作+RequestId 到普通 Social 命令首次终态；成功重试不会再次移动牌子、发信号或消耗冷却。 */
	TMap<FString, FCatDomainCommandResult> CommandTerminalCache;

	/** 普通 Social 命令首次受理时的业务载荷签名；同 RequestId 不能换目标、位置或求助类型。 */
	TMap<FString, FString> CommandPayloadByKey;

	/** 身份+操作+关联键到偷鱼协议结果；Begin 用客户端 RequestId 更新最终 returned/consumed，Catch 用 ProtocolId 只缓存成功追回。 */
	TMap<FString, FCatTheftResult> TheftTerminalCache;

	/**
	 * 上表每个终态键第一次受理时，调用方那份意图的签名，与终态缓存必须成对写入、成对存在。
	 * 它挡的是"同一个 RequestId 被拿去表达两件事"：偷鱼不能换鱼或换容器，售鱼不能换协议或换公款版本前提，
	 * 追回只有协议本身可换而它已经在键里。少写一半的后果不是少一道校验，而是共享模板把缺签名读成载荷冲突，
	 * 把本该正常重放的重试判成 InvalidPayload。
	 */
	TMap<FString, FString> TheftPayloadByKey;

	/** GameState 求助信号单调 Revision。 */
	int64 HelpSignalRevision = 0;

	/** 玩家 StableNetId 到其当前唯一防骚扰牌子弱引用；重放移动同一 Actor，不叠加多个保护区。 */
	TMap<FString, TWeakObjectPtr<class ACatProtectionSignActor>> ProtectionSignByPlayer;

	/** 一局 Social 新命令门；Host teardown 在 Items 关闭前永久置 false，内部返还辅助仍可完成既有协议。 */
	bool bCommandsOpen = true;
};
