#include "Framework/Game/CatfishingGameState.h"

#include "Logging/CatLog.h"
#include "Net/UnrealNetwork.h"

// GameState 开始流程：先完成父类注册，再记录实际类型；Run 快照只由 authority GameMode setter 写入。
void ACatfishingGameState::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogCatfishing, Log, TEXT("Event=gamestate_beginplay Class=%s"), *GetClass()->GetName());
}

// GameState 复制注册流程：先保留父类网络字段，再注册整结构 RunPublicState 与最近 HelpSignal；两者各带 Revision，客户
// 端只经对应 RepNotify 重读完整快照。
void ACatfishingGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, RunPublicState);
	DOREPLIFETIME(ThisClass, LastHelpSignal);
	DOREPLIFETIME(ThisClass, StolenFishCarriers);
	DOREPLIFETIME(ThisClass, SocialPolicy);
	DOREPLIFETIME(ThisClass, ShopEconomySnapshot);
	DOREPLIFETIME(ThisClass, TeamEquipmentLibrary);
}

// Run 快照写入流程：只接受 authority 实例，把 GameMode 提供的完整 DTO 一次替换并请求立即网络更新；客户端调用不会改本地副本。
void ACatfishingGameState::SetRunPublicStateFromAuthority(const FCatRunPublicState& NewState)
{
	if (!HasAuthority())
	{
		return;
	}
	RunPublicState = NewState;
	ForceNetUpdate();
	OnRunPublicStateChanged.Broadcast();
}

// Run 快照读取流程：返回当前本机观察到的只读组合事实，不补算时间或预测下一阶段。
const FCatRunPublicState& ACatfishingGameState::GetRunPublicState() const
{
	return RunPublicState;
}

// 求助发布流程：只接受 authority，复制完整信号并强制网络更新；GameState 不解释附近范围或自动生成任务。
void ACatfishingGameState::SetHelpSignalFromAuthority(const FCatHelpSignalSnapshot& NewSignal)
{
	if (!HasAuthority())
	{
		return;
	}
	LastHelpSignal = NewSignal;
	ForceNetUpdate();
	OnHelpSignalChanged.Broadcast();
}

// 求助读取流程：返回服务器最终值或客户端最近复制值；消费者自行按全局/范围做表现过滤。
const FCatHelpSignalSnapshot& ACatfishingGameState::GetLastHelpSignal() const
{
	return LastHelpSignal;
}

// 叼鱼列表发布流程：只接受 authority，整体替换后强制网络更新；GameState 不判断谁该追、也不给追回计时。
void ACatfishingGameState::SetStolenFishCarriersFromAuthority(const TArray<FCatStolenFishCarrySnapshot>& NewCarriers)
{
	if (!HasAuthority())
	{
		return;
	}
	StolenFishCarriers = NewCarriers;
	ForceNetUpdate();
	OnStolenFishCarriersChanged.Broadcast();
}

// 叼鱼列表读取流程：返回服务器最终值或客户端最近复制值；空列表表示此刻场上没有赃物，而不是"还没同步到"。
const TArray<FCatStolenFishCarrySnapshot>& ACatfishingGameState::GetStolenFishCarriers() const
{
	return StolenFishCarriers;
}

// Social 权限发布流程：只接受 authority；权限值本身由 Social 裁决，GameState 只负责把结果送出去。
void ACatfishingGameState::SetSocialPolicyFromAuthority(const FCatSocialPolicySnapshot& NewPolicy)
{
	if (!HasAuthority())
	{
		return;
	}
	SocialPolicy = NewPolicy;
	ForceNetUpdate();
	OnSocialPolicyChanged.Broadcast();
}

// Social 权限读取流程：返回本机观察到的开关现状；具体一次偷取能否成立仍要由服务器完整判定。
const FCatSocialPolicySnapshot& ACatfishingGameState::GetSocialPolicy() const
{
	return SocialPolicy;
}

// 团队经济发布流程：只接受 authority，整份快照一次替换；条目里的操作者已由 GameMode 解析完成。
void ACatfishingGameState::SetShopEconomySnapshotFromAuthority(const FCatShopPublicEconomySnapshot& NewSnapshot)
{
	if (!HasAuthority())
	{
		return;
	}
	ShopEconomySnapshot = NewSnapshot;
	ForceNetUpdate();
	OnShopEconomySnapshotChanged.Broadcast();
}

// 团队经济读取流程：返回公款余额与公开流水；货架剩余量不在其中，库存仍要单独查询。
const FCatShopPublicEconomySnapshot& ACatfishingGameState::GetShopEconomySnapshot() const
{
	return ShopEconomySnapshot;
}

// 团队装备库发布流程：只接受 authority，整份快照一次替换。
void ACatfishingGameState::SetTeamEquipmentLibraryFromAuthority(const FCatTeamEquipmentLibrarySnapshot& NewSnapshot)
{
	if (!HasAuthority())
	{
		return;
	}
	TeamEquipmentLibrary = NewSnapshot;
	ForceNetUpdate();
	OnTeamEquipmentLibraryChanged.Broadcast();
}

// 团队装备库读取流程：返回全队共有装备清单；能否装备某件东西仍由各自 Equipment 组件裁决。
const FCatTeamEquipmentLibrarySnapshot& ACatfishingGameState::GetTeamEquipmentLibrary() const
{
	return TeamEquipmentLibrary;
}

// 叼鱼列表复制回调流程：只通知表现重读；客户端不据此发起追回，也不本地推算窗口剩余时间。
void ACatfishingGameState::OnRep_StolenFishCarriers()
{
	OnStolenFishCarriersChanged.Broadcast();
	UE_LOG(LogCatSocial, Verbose, TEXT("Event=stolen_fish_carriers_received Count=%d Revision=%lld"),
		StolenFishCarriers.Num(), StolenFishCarriers.Num() > 0 ? StolenFishCarriers[0].Revision : 0);
}

// Social 权限复制回调流程：只通知表现重读；本地不把它缓存成"我已获授权"。
void ACatfishingGameState::OnRep_SocialPolicy()
{
	OnSocialPolicyChanged.Broadcast();
	UE_LOG(LogCatSocial, Verbose, TEXT("Event=social_policy_received Theft=%s Mischief=%s Revision=%lld"),
		*UEnum::GetValueAsString(SocialPolicy.TheftPermission),
		*UEnum::GetValueAsString(SocialPolicy.MischiefPermission), SocialPolicy.Revision);
}

// 团队经济复制回调流程：只通知表现重读；余额和流水都不在客户端二次计算。
void ACatfishingGameState::OnRep_ShopEconomySnapshot()
{
	OnShopEconomySnapshotChanged.Broadcast();
	UE_LOG(LogCatfishing, Verbose, TEXT("Event=shop_economy_snapshot_received Balance=%d WalletRevision=%lld Transactions=%d"),
		ShopEconomySnapshot.Balance, ShopEconomySnapshot.WalletRevision, ShopEconomySnapshot.Transactions.Num());
}

// 团队装备库复制回调流程：只通知表现重读。
void ACatfishingGameState::OnRep_TeamEquipmentLibrary()
{
	OnTeamEquipmentLibraryChanged.Broadcast();
	UE_LOG(LogCatfishing, Verbose, TEXT("Event=team_equipment_library_received Revision=%lld Instances=%d"),
		TeamEquipmentLibrary.Revision, TeamEquipmentLibrary.Instances.Num());
}

// Run 快照复制回调流程：只记录新 Revision/Phase 供诊断；UI 与玩法继续通过 getter 读取，不在客户端推进 StateTree。
void ACatfishingGameState::OnRep_RunPublicState()
{
	OnRunPublicStateChanged.Broadcast();
	UE_LOG(LogCatRun, Verbose, TEXT("Event=run_snapshot_received RunId=%s Revision=%lld Phase=%s Day=%d"),
		*RunPublicState.Phase.RunId.ToString(EGuidFormats::DigitsWithHyphens), RunPublicState.Revision,
		*UEnum::GetValueAsString(RunPublicState.Phase.Phase), RunPublicState.Phase.DayIndex);
}

// 求助复制回调流程：只记录结构化诊断；客户端不自动进入 Fishing、救援或任务状态。
void ACatfishingGameState::OnRep_HelpSignal()
{
	OnHelpSignalChanged.Broadcast();
	UE_LOG(LogCatfishing, Verbose, TEXT("Event=help_signal_received Kind=%s Revision=%lld Global=%s"),
		*UEnum::GetValueAsString(LastHelpSignal.Kind), LastHelpSignal.Revision,
		LastHelpSignal.bGlobal ? TEXT("true") : TEXT("false"));
}
