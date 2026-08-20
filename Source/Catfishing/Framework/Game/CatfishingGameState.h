#pragma once

#include "CoreMinimal.h"
#include "Equipment/CatEquipmentTypes.h"
#include "Framework/Core/CatRunContracts.h"
#include "GameFramework/GameStateBase.h"
#include "ShopEconomy/CatShopEconomyTypes.h"
#include "Social/CatSocialTypes.h"
#include "CatfishingGameState.generated.h"

/** GameState Run/Environment 完整公开快照变化通知；本机 UI 必须重新读取 GetRunPublicState。 */
DECLARE_MULTICAST_DELEGATE(FCatRunPublicStateChanged);

/** GameState 最近求助完整快照变化通知；本机 UI 必须重新读取 GetLastHelpSignal。 */
DECLARE_MULTICAST_DELEGATE(FCatHelpSignalChanged);

/**
 * GameState 其余公开快照的共用变化通知。
 * 上面两条各自成型是历史原因；本轮新增的四份快照（叼鱼列表、Social 权限、团队经济、团队装备库）
 * 通知语义完全一致——“这份快照换了，重读对应 getter”——所以共用一个类型，
 * 不再为每份快照造一个只有名字不同、内容完全一样的委托。
 */
DECLARE_MULTICAST_DELEGATE(FCatGameStateSnapshotChanged);

/**
 * Lake 一局的公开复制面：服务器已经裁决完的六份快照在这里落地并发给所有客户端。
 *
 * 它拥有的只是"传输和通知"这件事本身——六个 UPROPERTY、它们的 RepNotify，以及本机的六个变化委托。
 * 每份快照都是整结构替换而不是增量，客户端读到的永远是一个自洽的、带 Revision 的截面。
 *
 * 它不拥有任何一份快照的内容：Run 状态由 GameMode 写，求助信号、叼鱼列表和 Social 权限由 Social 服务写，
 * 团队经济与团队装备库由 GameMode 从对应服务重建后写。setter 一律带 FromAuthority 后缀，就是在提醒
 * "这里不是写口，只是出口"。客户端拿到快照只能渲染，任何据此发起的动作都得重新走服务器的领域写口。
 * 这个文件因此只依赖那几份快照 DTO 所在的头，不依赖任何领域服务。
 */
UCLASS()
class CATFISHING_API ACatfishingGameState : public AGameStateBase
{
	GENERATED_BODY()
public:
	/** 注册 RunPublicState 与 LastHelpSignal 两份整结构复制；客户分别经 RepNotify 消费各自 Revision 对齐的完整快照。 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	/** 仅允许 authority GameMode 写入组合公开事实；每次写入都会触发网络更新。 */
	void SetRunPublicStateFromAuthority(const FCatRunPublicState& NewState);
	/** 提供服务器最终值或客户端最近复制值，调用方据此渲染一局状态；返回 const 引用保证外部不能绕过 GameMode 写口推进 Run。 */
	const FCatRunPublicState& GetRunPublicState() const;
	/** 仅允许 authority Social 服务发布最近一次求助；它不启动任务或自动加入 Fishing。 */
	void SetHelpSignalFromAuthority(const FCatHelpSignalSnapshot& NewSignal);
	/** 提供最近一次服务器求助信号供表现去重；它不是任务分配或自动加入玩法的授权依据。 */
	const FCatHelpSignalSnapshot& GetLastHelpSignal() const;
	/** 本机 Run/Environment 完整快照变化通知；不授权订阅者推进 StateTree。 */
	FCatRunPublicStateChanged OnRunPublicStateChanged;
	/** 本机最近求助完整快照变化通知；不授权订阅者接受任务或改变 Social 权限。 */
	FCatHelpSignalChanged OnHelpSignalChanged;

	/** 仅允许 authority Social 服务发布当前场上叼着赃物的猫；列表整体替换，GameState 不追加也不过滤。 */
	void SetStolenFishCarriersFromAuthority(const TArray<FCatStolenFishCarrySnapshot>& NewCarriers);
	/** 提供受害者和旁观者据以认出“该追谁”的公开叼鱼事实；它不授权追回，追回仍要走 Social 的服务器写口。 */
	const TArray<FCatStolenFishCarrySnapshot>& GetStolenFishCarriers() const;
	/** 仅允许 authority Social 服务发布本局运行期偷取/恶作剧权限；客户端读到的只是开关现状，判定仍在服务器。 */
	void SetSocialPolicyFromAuthority(const FCatSocialPolicySnapshot& NewPolicy);
	/** 提供 UI 渲染当前 Social 权限开关所需的只读事实；它不是“我这次能不能偷”的答案，只是“这局开没开这条玩法”。 */
	const FCatSocialPolicySnapshot& GetSocialPolicy() const;
	/** 仅允许 authority GameMode 发布团队公款余额与公开流水；条目里的 ActorPlayerState 已在发布前由服务器解析完成。 */
	void SetShopEconomySnapshotFromAuthority(const FCatShopPublicEconomySnapshot& NewSnapshot);
	/** 提供全队常时可见的公款余额与流水；它不含货架剩余量，库存仍要单独查询。 */
	const FCatShopPublicEconomySnapshot& GetShopEconomySnapshot() const;
	/** 仅允许 authority GameMode 发布团队装备库的公开形态；库里没有服务器私有身份，整体原样复制。 */
	void SetTeamEquipmentLibraryFromAuthority(const FCatTeamEquipmentLibrarySnapshot& NewSnapshot);
	/** 提供团队共有装备的只读清单；能不能装备某件东西仍由各自 Equipment 组件裁决。 */
	const FCatTeamEquipmentLibrarySnapshot& GetTeamEquipmentLibrary() const;

	/** 本机公开叼鱼列表变化通知；订阅者只更新表现，不据此发起追回。 */
	FCatGameStateSnapshotChanged OnStolenFishCarriersChanged;
	/** 本机 Social 运行期权限变化通知；订阅者只更新开关显示，不据此放行玩法。 */
	FCatGameStateSnapshotChanged OnSocialPolicyChanged;
	/** 本机团队经济公开快照变化通知。 */
	FCatGameStateSnapshotChanged OnShopEconomySnapshotChanged;
	/** 本机团队装备库公开快照变化通知。 */
	FCatGameStateSnapshotChanged OnTeamEquipmentLibraryChanged;
protected:
	/** 实例进入 World 后记录实际类；不增加可写玩法状态。 */
	virtual void BeginPlay() override;
	/** 客户端收到新 Revision 后记录结构化诊断，UI/玩法只能继续读取复制快照。 */
	UFUNCTION()
	void OnRep_RunPublicState();
	/** 客户端收到新求助 Revision 后只记录/供表现读取，不自动执行互动。 */
	UFUNCTION()
	void OnRep_HelpSignal();
	/** 客户端收到新的叼鱼列表后只通知表现重读；追回窗口是否还开着由服务器裁决，客户端不自行计时。 */
	UFUNCTION()
	void OnRep_StolenFishCarriers();
	/** 客户端收到新的 Social 权限后只通知表现重读；本地不把它缓存成“我已获授权”。 */
	UFUNCTION()
	void OnRep_SocialPolicy();
	/** 客户端收到新的团队经济快照后只通知表现重读；余额和流水都不在客户端二次计算。 */
	UFUNCTION()
	void OnRep_ShopEconomySnapshot();
	/** 客户端收到新的团队装备库快照后只通知表现重读。 */
	UFUNCTION()
	void OnRep_TeamEquipmentLibrary();

private:
	/** Run 的唯一公开复制快照；服务器 GameMode 写，客户端 RepNotify 读。 */
	UPROPERTY(ReplicatedUsing = OnRep_RunPublicState)
	FCatRunPublicState RunPublicState;

	/** Social 在 authority 写入的最近一条手动/巨鱼信号；客户 OnRep 只通知表现，范围和全局标记始终由服务器裁决。 */
	UPROPERTY(ReplicatedUsing = OnRep_HelpSignal)
	FCatHelpSignalSnapshot LastHelpSignal;

	/** 当前场上叼着赃物的猫；由 Social 从活跃偷鱼协议整体派生后写入，条目里没有受害者身份和服务器协议 ID。 */
	UPROPERTY(ReplicatedUsing = OnRep_StolenFishCarriers)
	TArray<FCatStolenFishCarrySnapshot> StolenFishCarriers;

	/** 本局运行期的偷取与恶作剧权限现状；只由局主经 Social 改写，客户端读到的是结果不是授权。 */
	UPROPERTY(ReplicatedUsing = OnRep_SocialPolicy)
	FCatSocialPolicySnapshot SocialPolicy;

	/** 团队公款余额与公开流水；服务器每次经济写口成功后整体重建并替换。 */
	UPROPERTY(ReplicatedUsing = OnRep_ShopEconomySnapshot)
	FCatShopPublicEconomySnapshot ShopEconomySnapshot;

	/** 团队共有装备的公开清单；服务器每次入库成功后整体替换。 */
	UPROPERTY(ReplicatedUsing = OnRep_TeamEquipmentLibrary)
	FCatTeamEquipmentLibrarySnapshot TeamEquipmentLibrary;
};
