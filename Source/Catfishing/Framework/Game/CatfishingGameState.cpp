#include "Framework/Game/CatfishingGameState.h"

#include "AbilitySystem/Attributes/CatRunAttributeSet.h"
#include "AbilitySystem/Attributes/CatRunModifierAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "Environment/CatChumFieldReplicationComponent.h"
#include "Logging/CatLog.h"
#include "Net/UnrealNetwork.h"

// 构造流程：先创建 ChumField 公开复制组件，再创建 GameState 自己拥有的 Run ASC、最终供品/世界进度集和来源倍率集；
// ASC 立即开启复制并采用 Lyra 口径的 Mixed 模式，最后把两套属性集稳定挂到同一 ASC；GameMode 的数值写入只通过 GE 提交。
ACatfishingGameState::ACatfishingGameState()
{
	ChumFieldReplication = CreateDefaultSubobject<UCatChumFieldReplicationComponent>(TEXT("ChumFieldReplication"));
	RunAbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("RunAbilitySystemComponent"));
	RunAbilitySystemComponent->SetIsReplicated(true);
	RunAbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
	RunAttributes = CreateDefaultSubobject<UCatRunAttributeSet>(TEXT("RunAttributes"));
	RunAbilitySystemComponent->AddAttributeSetSubobject(RunAttributes.Get());
	RunModifiers = CreateDefaultSubobject<UCatRunModifierAttributeSet>(TEXT("RunModifiers"));
	RunAbilitySystemComponent->AddAttributeSetSubobject(RunModifiers.Get());
}

// ASC 查询流程：直接返回构造期唯一 Run 组件，避免 GameMode、UI 或协调器从全局服务重新查找第二份公共数值宿主。
UAbilitySystemComponent* ACatfishingGameState::GetAbilitySystemComponent() const
{
	return RunAbilitySystemComponent;
}

// Run ASC 查询流程：提供明确的 Run 域入口给 GameMode 创建 GE Spec；返回值不授予调用方直接改属性的权限。
UAbilitySystemComponent* ACatfishingGameState::GetRunAbilitySystemComponent() const
{
	return RunAbilitySystemComponent;
}

// 权威 Run ASC 查询流程：先检查 GameState authority，再返回同一组件；客户端得到空以阻止复制回调和 UI 形成旁路写口。
UAbilitySystemComponent* ACatfishingGameState::GetRunAbilitySystemComponentFromAuthority() const
{
	return HasAuthority() ? RunAbilitySystemComponent : nullptr;
}

// GameState 组件初始化流程：按 Lyra 的 GameState ASC 口径先完成父类组件初始化，再把唯一 Run ASC 的 Owner/Avatar 都绑定为本 GameState；失败只记录依赖缺口，不运行替代公式。
void ACatfishingGameState::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	if (!RunAbilitySystemComponent || !RunAttributes || !RunModifiers)
	{
		UE_LOG(LogCatRun, Error, TEXT("Event=RunASCInitialized Result=Failed Reason=ComponentOrAttributeMissing World=%s"),
			GetWorld() ? *GetWorld()->GetName() : TEXT("None"));
		return;
	}
	RunAbilitySystemComponent->InitAbilityActorInfo(this, this);
	UE_LOG(LogCatRun, Display, TEXT("Event=RunASCInitialized Result=Success World=%s NetMode=%d Authority=%s Owner=%s Avatar=%s"),
		GetWorld() ? *GetWorld()->GetName() : TEXT("None"), GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : -1,
		HasAuthority() ? TEXT("true") : TEXT("false"), *GetName(), *GetName());
}

// GameState 开始流程：只记录实际类和运行 World，Run ASC 已在组件初始化阶段完成绑定；这里不补算 Run、Social 或商店快照，避免绕过 GameMode 的唯一写口。
void ACatfishingGameState::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogCatfishing, Log, TEXT("Event=gamestate_beginplay Class=%s World=%s"),
		*GetClass()->GetName(), GetWorld() ? *GetWorld()->GetName() : TEXT("None"));
}

// GameState 复制注册流程：先保留父类网络字段，再注册 Run、Help 和 Shop 三份公开快照；客户端只经对应 RepNotify 重读完整事实。
void ACatfishingGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, RunPublicState);
	DOREPLIFETIME(ThisClass, LastHelpSignal);
	DOREPLIFETIME(ThisClass, ShopEconomySnapshot);
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

// 商店经济快照写入流程：只接受服务器 authority，整体替换团队余额、货架和公开交易记录；写入后立即请求网络更新并广播本机 UI 重读事件。
void ACatfishingGameState::SetShopEconomySnapshotFromAuthority(
	const FCatShopPublicEconomySnapshot& NewSnapshot)
{
	if (!HasAuthority())
	{
		return;
	}
	ShopEconomySnapshot = NewSnapshot;
	ForceNetUpdate();
	OnShopEconomySnapshotChanged.Broadcast();
}

// 商店经济快照读取流程：返回服务器最终值或客户端最近复制值；调用方只能展示，不通过该引用确认交付或修改团队余额。
const FCatShopPublicEconomySnapshot& ACatfishingGameState::GetShopEconomySnapshot() const
{
	return ShopEconomySnapshot;
}

// ChumField 复制组件写口读取流程：仅 authority 返回可写组件，客户端得到空指针，防止表现层绕过环境/窝点服务发布公共窝点。
UCatChumFieldReplicationComponent* ACatfishingGameState::GetChumFieldReplicationFromAuthority()
{
	return HasAuthority() ? ChumFieldReplication : nullptr;
}

// Run 快照复制回调流程：只记录新 Revision/Phase 供诊断；UI 与玩法继续通过 getter 读取，不在客户端推进 StateTree。
void ACatfishingGameState::OnRep_RunPublicState()
{
	OnRunPublicStateChanged.Broadcast();
	UE_LOG(LogCatRun, Log, TEXT("Event=run_snapshot_received RunId=%s Revision=%lld Phase=%s Day=%d"),
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

// 商店经济复制回调流程：客户端收到整份公开快照后只广播重读通知并写诊断日志；不在 RepNotify 中确认订单交付或推导余额变化。
void ACatfishingGameState::OnRep_ShopEconomySnapshot()
{
	OnShopEconomySnapshotChanged.Broadcast();
	UE_LOG(LogCatfishing, Verbose,
		TEXT("Event=shop_economy_snapshot_received Balance=%d WalletRevision=%lld Stocks=%d Transactions=%d"),
		ShopEconomySnapshot.Balance, ShopEconomySnapshot.WalletRevision,
		ShopEconomySnapshot.Stocks.Num(),
		ShopEconomySnapshot.Transactions.Num());
}
