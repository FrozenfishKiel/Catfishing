#pragma once

#include "CoreMinimal.h"
#include "Collection/CatImprintTypes.h"
#include "Fishing/CatFishingTypes.h"
#include "Framework/Core/CatDomainCommandTypes.h"
#include "Framework/Core/CatProfileContracts.h"
#include "Framework/Core/CatSacrificeContracts.h"
#include "GameFramework/PlayerController.h"
#include "Items/CatItemTypes.h"
#include "ShopEconomy/CatShopEconomyTypes.h"
#include "Social/CatSocialTypes.h"
#include "CatfishingPlayerController.generated.h"

class ACatCampHubActor;
class ACatCharacter;

/**
 * Lake owning-client 与服务器之间的网络适配器：本项目所有玩家意图 RPC 的唯一入口。
 *
 * 它拥有的只有"转发"这件事。每条 RPC 的实现体都是同一个形状——过 CanForwardGameplayCommand 这道门、
 * 从本机 PlayerState 重建服务器身份（客户端上报的身份一律被覆盖）、调对应领域服务或协调器的写口、打一行终态日志。
 * 所以本文件的 include 面基本等于"所有 RPC 参数 DTO"，而不是领域实现。
 *
 * 它不拥有任何领域真相：鱼、装备、社交协议、商店账本、Run 阶段各归各的服务；它也不做玩法裁决，
 * 距离、主人、版本前提、重放判定全部在服务器侧的领域写口里完成。跨聚合的事务同理不写在这里：
 * 投窝、直接吃鱼分别由 `Integration/` 下的 UCatChumContributionCoordinator、UCatFishConsumptionCoordinator 推进，
 * 献祭和商店订单则各有自己的协调器。
 *
 * 唯一还留在本类的跨聚合链是取用团队装备（ServerTakeTeamEquipment），它的两个聚合都在 Equipment 领域内，
 * 落点需要单独裁决，因此本轮没有一起搬走；不要把它当成新写口的模板照抄。
 */
UCLASS()
class CATFISHING_API ACatfishingPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	/** 控制器接管 Pawn 后记录装配结果；不缓存 Pawn 或创建第二条 Online 旅行入口。 */
	virtual void OnPossess(APawn* InPawn) override;
	/** 把客户端翻天确认转发给 authority GameMode；GameMode 负责资格、Revision 与幂等裁决。 */
	UFUNCTION(Server, Reliable)
	void ServerSetNextDayReady(FGuid RequestId, int64 ExpectedRevision, bool bReady);

	/** 结算夜请求检查本局成像终态与 Grant ACK；只有归档已收口才向 Run StateTree 发送 SettlementComplete。 */
	UFUNCTION(Server, Reliable)
	void ServerRequestSettlementCompletion(FGuid RequestId, int64 ExpectedRevision);

	/** 服务器向 owning client 投递不可变永久 Grant；客户端只有 durable Profile 完成后才发 ACK。 */
	UFUNCTION(Client, Reliable)
	void ClientReceiveProfileGrant(const FCatProfileGrant& Grant);

	/** 客户端在本地 Journal 完整落盘后确认 GrantId；服务器重建身份，并在真实 ACK 后复核 Host exit 有界等待。 */
	UFUNCTION(Server, Reliable)
	void ServerAcknowledgeProfileGrant(FGuid GrantId);

	/** 服务器向 owning client 投递独立 CapturePlan；本 RPC 不表示图片或 Grant 已成功。 */
	UFUNCTION(Client, Reliable)
	void ClientReceiveImprintCapturePlan(const FCatCapturePlan& Plan);

	/** 外部本地成像桥回报真实结果；成功必须携带 durable ImprintId，服务器随后才生成 Grant。 */
	UFUNCTION(Server, Reliable)
	void ServerReportImprintCaptureResult(FGuid CapturePlanId, bool bSucceeded, FGuid ImprintId);

	/** 请求服务器按当前 Run/Environment/Water 与可参战协作能力快照开始一次钓鱼会话；RequestId 只用于重放，不参与抽鱼。 */
	UFUNCTION(Server, Reliable)
	void ServerStartFishingSession(FGuid RequestId);

	/** 把 Giant HookedFight 的手动协作意图转给指定 FishingSession。 */
	UFUNCTION(Server, Reliable)
	void ServerAssistFishingSession(FGuid FishingSessionId, FGuid RequestId, int64 ExpectedRevision);

	/** 把 NearShore 抢抄意图转给指定 FishingSession；服务器覆盖本人的鱼护 ID，首个合法提交者由 Compare-and-Commit 决定。 */
	UFUNCTION(Server, Reliable)
	void ServerRequestScoop(FGuid FishingSessionId, FCatScoopCommand Command);

	/**
	 * 上报本人当前按住的遛鱼操作（左键拖 / 右键松 / 都没按）；它是按键状态不是一次性命令，所以没有 RequestId，服务器
	 * 按本人身份找活跃会话。真咬期第一次"拖"就是提竿。
	 */
	UFUNCTION(Server, Reliable)
	void ServerSetFishingFightIntent(ECatFishingFightIntent Intent);

	/** 把唯一献祭命令转给 SacrificeCoordinator；Controller 不直接删鱼或增加 Run 额度。 */
	UFUNCTION(Server, Reliable)
	void ServerRequestSacrifice(FCatSacrificeCommand Command);

	/** 在固定营地范围请求本人休息；Camp/Condition 负责位置和身体裁决。 */
	UFUNCTION(Server, Reliable)
	void ServerRequestCampRest(ACatCampHubActor* Camp, FGuid RequestId);

	/** 在固定营地请求可跳过的篝火回看；结算夜全员在场时同时建立本局封面 CapturePlan。 */
	UFUNCTION(Server, Reliable)
	void ServerRequestCampfirePlayback(ACatCampHubActor* Camp, FGuid RequestId);

	/** 在固定营地把本人鱼护的一条鱼原子转入共享鱼缸。 */
	UFUNCTION(Server, Reliable)
	void ServerTransferFishToTank(ACatCampHubActor* Camp, FGuid RequestId, FGuid FishInstanceId,
		int64 ExpectedGuardRevision, int64 ExpectedTankRevision);

	/** 伙伴把倒地目标送到固定营地 RescuePoint；没有死亡/重生旁路。 */
	UFUNCTION(Server, Reliable)
	void ServerRescueCharacterToCamp(ACatCampHubActor* Camp, ACatCharacter* TargetCharacter, FGuid RequestId);

	/** 提交三个功能装备 ID；服务器目录与可信解锁证明共同通过后才允许首次装配，客户端 Profile 选择本身不授予权限。 */
	UFUNCTION(Server, Reliable)
	void ServerConfigureEquipment(FGuid RequestId, int64 ExpectedRevision, FName RodDefinitionId,
		FName BaitDefinitionId, FName FloatDefinitionId);

	/**
	 * 从团队装备库按实例取走一件实物并装到本人对应槽位上（买到的竿从这里才真的装上）。
	 * 只传实例 ID、幂等 ID 和两边的版本前提：拿谁、装到谁身上都由服务器按本 Controller 的 Pawn 决定，客户端不能替别人取用。
	 */
	UFUNCTION(Server, Reliable)
	void ServerTakeTeamEquipment(FGuid InstanceId, FGuid RequestId, int64 ExpectedLibraryRevision,
		int64 ExpectedEquipmentRevision);


	/**
	 * 从本人鱼护或共享缸直接吃一条鱼；Items 移除成功后才按 FishDefinition 结算食用后果：Toxic 鱼累加 Poison，Safe 鱼
	 * 不改任何 Attribute，两者都只是确认这次消费已结算完成。
	 */
	UFUNCTION(Server, Reliable)
	void ServerConsumeFish(ACatCharacter* EatingCharacter, FCatFishConsumeCommand Command);

	/** 开始一条鱼的偷取与追回窗口；Social 覆盖客户端身份并保证每个小偷最多一条。 */
	UFUNCTION(Server, Reliable)
	void ServerBeginTheft(FCatTheftCommand Command);

	/** 服务器把 Begin/Catch/Sell 的首次或重放结果发回 owning client；ProtocolId 和售出终态只能通过该权威结果取得。 */
	UFUNCTION(Client, Reliable)
	void ClientReceiveTheftResult(const FCatTheftResult& Result);

	/** 提供本机最近收到的偷鱼协议结果供 UI 读取；它不授权客户端直接访问 Social、Items 或 ShopEconomy 写口。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Social")
	FCatTheftResult GetLastTheftResult() const;

	/** 在进食窗口内按服务器返回的 ProtocolId 追回；Social 按权威主人、状态、距离与共享缸策略授权。 */
	UFUNCTION(Server, Reliable)
	void ServerCatchTheft(FGuid TheftProtocolId);

	/** 在追回窗口内请求售出本人持有的偷鱼 escrow；当前 Social 在可恢复经济事务缺失时稳定拒绝，不删除鱼也不入账。 */
	UFUNCTION(Server, Reliable)
	void ServerSellStolenFish(FGuid TheftProtocolId, FGuid RequestId, int64 ExpectedWalletRevision);

	/** 手动发布普通钓鱼或倒地求助；普通信号不会升级为全局任务。 */
	UFUNCTION(Server, Reliable)
	void ServerRequestManualHelp(FGuid RequestId, ECatHelpSignalKind Kind);

	/**
	 * 请求一次普通恶作剧许可；Social 重新验证目标 Controller、双方权威距离与 ProtectionSign，距离一律读服务器上双方
	 * Pawn 的权威位置，客户端不上报交互坐标。
	 */
	UFUNCTION(Server, Reliable)
	void ServerRequestMischief(APlayerState* TargetPlayerState, FGuid RequestId);

	/** 在本人附近放置或移动唯一防骚扰牌子；Social 用显式范围配置保护普通恶作剧。 */
	UFUNCTION(Server, Reliable)
	void ServerPlaceProtectionSign(FGuid RequestId, FVector SignLocation);

	/**
	 * 局主在运行期整体改写本局偷取与恶作剧权限。
	 * 这里只传两个开关值和幂等 ID：谁是局主由服务器按会话成员关系判定，客户端说自己是局主不算数。
	 */
	UFUNCTION(Server, Reliable)
	void ServerSetSocialPolicy(FGuid RequestId, ECatDomainPolicy NewTheftPermission, ECatDomainPolicy NewMischiefPermission);

	/**
	 * 房主请求把某个成员踢出本局。
	 * 只传目标的服务器身份键；请求者身份由服务器从本 Controller 的 PlayerState 重建，伪造别人的房主资格无效。
	 */
	UFUNCTION(Server, Reliable)
	void ServerRequestKickPlayer(const FString& TargetStableNetId);

	/**
	 * 花团队公款买下一条商店目录项。
	 * 只传目录项 ID、幂等 ID 和钱包版本：价格与库存是服务器目录事实，客户端报价既没有意义也不会被读取。
	 */
	UFUNCTION(Server, Reliable)
	void ServerSubmitShopPurchase(FName EntryId, FGuid RequestId, int64 ExpectedWalletRevision);

	/** 免费自取一条显式标记为免费的目录项（普通饵、1 级保底竿）；哪些条目免费由服务器配置决定。 */
	UFUNCTION(Server, Reliable)
	void ServerClaimFreeShopEntry(FName EntryId, FGuid RequestId, int64 ExpectedWalletRevision);

	/**
	 * 把自己鱼护或共用鱼缸里的一条鱼卖给商人猫。
	 * 只传鱼实例、它所在的容器和来源类别：重量取自服务器上的鱼实例，价格由服务器查体重轴自己算，两者都不接受客户端填写。
	 * 容器 ID 允许由客户端指定，是因为 Items 会按服务器记录复核这条鱼是否真在该容器、该容器是否对请求者开放；
	 * 指错容器只会被拒，不会让人卖掉别人的鱼。
	 */
	UFUNCTION(Server, Reliable)
	void ServerSellFish(FGuid FishInstanceId, FGuid ContainerId, int64 ExpectedContainerRevision,
		ECatShopFishSaleSource SourceKind, FGuid RequestId, int64 ExpectedWalletRevision);

	/** 本人完成抖水表现后请求清除 Wet；该入口只改表现状态，不恢复生存数值或提供玩法增益。 */
	UFUNCTION(Server, Reliable)
	void ServerCompleteShakeDry(FGuid RequestId);

	/**
	 * 倒地者单人请求野外自救；Condition 只核对请求者拥有当前 Pawn，随后清毒解除倒地。这是飞书"倒地者可爬回营地休息自
	 * 愈、保证不卡死"红线的兜底入口，不需要营地范围或伙伴。
	 */
	UFUNCTION(Server, Reliable)
	void ServerRequestFieldSelfRecovery(FGuid RequestId);

	/** 消费一份正式 Chum 并把窝料投到指定世界坐标；服务端先预留耗材，窝点写入成功后才提交消耗，客户端不能自报贡献。 */
	UFUNCTION(Server, Reliable)
	void ServerContributeChum(FVector DropLocation, FGuid RequestId, int64 ExpectedEquipmentRevision,
		int64 ExpectedChumRevision, FName ChumDefinitionId);

	/** Online Client 在 DestroySession 前通知服务器这是主动离局；GameMode 不把它误判为连接故障。 */
	UFUNCTION(Server, Reliable)
	void ServerMarkVoluntaryLeave();

	/** Host exit 通知远端在本地执行统一 Online Destroy/Frontend 链；不把它标成玩家主动离局。 */
	UFUNCTION(Client, Reliable)
	void ClientPrepareForHostExit(FGuid RequestId);

	/** 远端本地 DestroySession 成功后向 Host 回 ACK；GameMode 只接受当前 Active Controller 与同 RequestId。 */
	UFUNCTION(Server, Reliable)
	void ServerAcknowledgeHostExit(FGuid RequestId);

	/** 服务器登录完成后让 owning client 从 durable Profile 刷新公开图鉴摘要和装备解锁清单；两份都是只读投影，一起上报省一次往返。 */
	UFUNCTION(Client, Reliable)
	void ClientRefreshPublicFishCollection();

	/** owning client 只提交鱼图鉴记录；PlayerState 在服务器验证唯一 ID、数量和数值后整体发布。 */
	UFUNCTION(Server, Reliable)
	void ServerPublishPublicFishCollection(const TArray<FCatFishCollectionRecord>& Records);

	/** owning client 只提交自己的跨局装备解锁 ID 清单；PlayerState 在服务器校验条数/非空/唯一后整体持有并复制，只用于装配 gate。 */
	UFUNCTION(Server, Reliable)
	void ServerPublishEquipmentUnlocks(const TArray<FName>& UnlockIds);

private:
	/** 统一向 authority GameMode 查询运行内玩法命令 gate；缺少 GameMode、非 Active 或 teardown 关门时返回 false。 */
	bool CanForwardGameplayCommand() const;

	/**
	 * 购买与免费自取共用的转发实现；bFreeClaim 决定交给订单协调器的哪个入口。
	 * 两条 RPC 除了这一个布尔之外完全相同：都要过玩法 gate、都要从本机 PlayerState 重建服务器身份、
	 * 都必须经协调器而不是直接调商店服务——绕过协调器的订单会付了钱却停在 Pending，永远拿不到东西。
	 */
	void SubmitShopOrder(FName EntryId, FGuid RequestId, int64 ExpectedWalletRevision, bool bFreeClaim);

	/** owning client 最近收到的 Social 协议读模型；由可靠结果 RPC 整体替换，不复制回服务器或作为权限事实。 */
	UPROPERTY(Transient)
	FCatTheftResult LastTheftResult;

	/** 本 Controller 的"取用团队装备"首次终态；覆盖装备库取走和本人装配两步，重放不会再取第二件或再换一次装。 */
	TMap<FGuid, FCatDomainCommandResult> TakeTeamEquipmentTerminalCache;

	/** 取用首次终态对应的业务载荷签名；同 RequestId 换实例或任一版本前提会被拒绝。 */
	TMap<FGuid, FString> TakeTeamEquipmentPayloadByRequest;
};
