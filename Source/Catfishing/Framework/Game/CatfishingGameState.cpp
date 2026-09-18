#include "Framework/Game/CatfishingGameState.h"

#include "AbilitySystem/Attributes/CatEconomyAttributeSet.h"
#include "UI/Items/CatHornMessageWidget.h"
#include "GameFramework/PlayerController.h"
#include "AbilitySystem/Attributes/CatRunAttributeSet.h"
#include "AbilitySystem/Attributes/CatRunModifierAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Collection/CatRunFishCollectionComponent.h"
#include "Engine/World.h"
#include "Environment/CatChumFieldReplicationComponent.h"
#include "Logging/CatLog.h"
#include "Net/UnrealNetwork.h"
#include "ShopEconomy/CatShopEconomySettings.h"

// 广播消费流程：每个本地玩家创建自己的顶部显示，专用服务器只记录事件；不把文本写入玩法状态或日志。
void ACatfishingGameState::Multicast_HornAnnouncement_Implementation(const FString& Speaker, const FString& Message, FGuid RequestId)
{
	if (!RequestId.IsValid() || Message.IsEmpty() || Message.Len() > 120) return;
	for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		if (APlayerController* Controller = It->Get(); Controller && Controller->IsLocalController())
			if (auto* Widget = CreateWidget<UCatHornAnnouncementWidget>(Controller))
			{
				Widget->Message = FText::FromString(Speaker + TEXT("：") + Message);
				Widget->AddToPlayerScreen(90);
			}
	UE_LOG(LogCatfishing, Log, TEXT("Event=horn_announcement RequestId=%s Characters=%d World=%s NetMode=%d Authority=%d"),
		*RequestId.ToString(), Message.Len(), *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority());
}

// 构造流程：先创建 ChumField 公开复制组件，再创建 GameState 自己拥有的 Run ASC、最终供品/世界进度集和来源倍率集；
// ASC 开启复制并采用 Lyra 口径的 Mixed 模式；两套Run属性和独立经济属性共用此ASC，商店不再持有另一份可写余额。
ACatfishingGameState::ACatfishingGameState()
{
	RunFishCollection = CreateDefaultSubobject<UCatRunFishCollectionComponent>(TEXT("RunFishCollection"));
	ChumFieldReplication = CreateDefaultSubobject<UCatChumFieldReplicationComponent>(TEXT("ChumFieldReplication"));
	RunAbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("RunAbilitySystemComponent"));
	RunAbilitySystemComponent->SetIsReplicated(true);
	RunAbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
	RunAttributes = CreateDefaultSubobject<UCatRunAttributeSet>(TEXT("RunAttributes"));
	RunAbilitySystemComponent->AddAttributeSetSubobject(RunAttributes.Get());
	RunModifiers = CreateDefaultSubobject<UCatRunModifierAttributeSet>(TEXT("RunModifiers"));
	RunAbilitySystemComponent->AddAttributeSetSubobject(RunModifiers.Get());
	EconomyAttributes = CreateDefaultSubobject<UCatEconomyAttributeSet>(TEXT("EconomyAttributes"));
	RunAbilitySystemComponent->AddAttributeSetSubobject(EconomyAttributes.Get());
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

// 经济属性查询流程：返回与 Run 共用同一 ASC 的只读属性集，避免商店服务另存一份可写余额。
const UCatEconomyAttributeSet* ACatfishingGameState::GetEconomyAttributeSet() const
{
	return EconomyAttributes;
}

// 权威经济属性查询流程：只给服务器交易服务提供目标属性集，客户端读取余额仍走 GAS 复制或公开快照。
UCatEconomyAttributeSet* ACatfishingGameState::GetEconomyAttributeSetFromAuthority() const
{
	return HasAuthority() ? EconomyAttributes : nullptr;
}

// GameState 初始化流程：父类完成后把唯一ASC的Owner/Avatar绑定自身；服务器再从经济设置播种初始余额，经济关闭时写零，客户端只等复制。
// 任一必要组件缺失只记录错误，不创建替代属性集或公式；后续购买、售鱼都经经济GE修改余额。
void ACatfishingGameState::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	if (!RunAbilitySystemComponent || !RunAttributes || !RunModifiers || !EconomyAttributes)
	{
		UE_LOG(LogCatRun, Error, TEXT("Event=RunASCInitialized Result=Failed Reason=ComponentOrAttributeMissing World=%s"),
			GetWorld() ? *GetWorld()->GetName() : TEXT("None"));
		return;
	}
	RunAbilitySystemComponent->InitAbilityActorInfo(this, this);
	if (HasAuthority())
	{
		const UCatShopEconomySettings* Settings = GetDefault<UCatShopEconomySettings>();
		EconomyAttributes->InitTeamWalletBalance(Settings && Settings->IsRuntimeEnabled()
			? static_cast<float>(Settings->StartingTeamWalletBalance) : 0.0f);
	}
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
	DOREPLIFETIME(ThisClass, LastFishSpeciesDiscovery);
}

// Run 快照写入流程：只接受 authority 实例，把 GameMode 提供的完整 DTO 一次替换并请求立即网络更新；客户端调用不会改本地副本。
void ACatfishingGameState::SetRunPublicStateFromAuthority(const FCatRunPublicState& NewState)
{
	if (!HasAuthority())
	{
		return;
	}
	RunPublicState = NewState;
	RunFishCollection->SynchronizeRunFromAuthority(NewState);
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

// 新鱼种广播写入流程：只接受服务器 authority 且必须带有效 AnnouncementId 与鱼种 ID；整体替换最近一条后立即请求网络更新。
// 它不写任何人的图鉴——图鉴是每个人自己的 durable Profile，服务器只把「谁第一次记录到什么」这件公开事实说出去。
void ACatfishingGameState::PublishFishSpeciesDiscoveryFromAuthority(const FCatFishSpeciesDiscoveryAnnouncement& Announcement)
{
	if (!HasAuthority() || !Announcement.AnnouncementId.IsValid() || (Announcement.ItemId == 0))
	{
		return;
	}
	LastFishSpeciesDiscovery = Announcement;
	ForceNetUpdate();
	OnFishSpeciesDiscoveryChanged.Broadcast();
	UE_LOG(LogCatfishing, Log,
		TEXT("Event=fish_species_discovery_published AnnouncementId=%s PlayerId=%d ItemId=%s"),
		*Announcement.AnnouncementId.ToString(EGuidFormats::DigitsWithHyphens),
		Announcement.DiscovererPlayerId, *FString::FromInt(Announcement.ItemId));
}

// 新鱼种广播读取流程：返回服务器最终值或客户端最近复制值；调用方只能展示提示，不据它推进自己的图鉴。
const FCatFishSpeciesDiscoveryAnnouncement& ACatfishingGameState::GetLastFishSpeciesDiscovery() const
{
	return LastFishSpeciesDiscovery;
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

// 新鱼种广播复制回调流程：只广播本机重读通知；提示是否要弹、弹给谁由 UI 自己按 PlayerId 判断。
void ACatfishingGameState::OnRep_LastFishSpeciesDiscovery()
{
	OnFishSpeciesDiscoveryChanged.Broadcast();
	UE_LOG(LogCatfishing, Verbose,
		TEXT("Event=fish_species_discovery_received AnnouncementId=%s PlayerId=%d ItemId=%s"),
		*LastFishSpeciesDiscovery.AnnouncementId.ToString(EGuidFormats::DigitsWithHyphens),
		LastFishSpeciesDiscovery.DiscovererPlayerId, *FString::FromInt(LastFishSpeciesDiscovery.ItemId));
}

// 全场钓鱼信号投递流程：服务器与每个客户端都会执行本体；这里只把标签和位置转成本机广播，不写任何玩法状态。
// 它是「不受距离衰减」的承载点——GameState 对所有客户端恒相关，投递不受发声者的网络相关性影响；
// 声音本身衰减不衰减由表现层挂的音效资产决定，服务器不替它做这个判断。
void ACatfishingGameState::Multicast_PlayWorldwideFishingSignal_Implementation(
	const FGameplayTag SignalTag, const FVector WorldLocation)
{
	if (!SignalTag.IsValid())
	{
		return;
	}
	OnWorldwideFishingSignal.Broadcast(SignalTag, WorldLocation);
	UE_LOG(LogCatfishing, Verbose,
		TEXT("Event=worldwide_fishing_signal_received Signal=%s Location=%s NetMode=%d"),
		*SignalTag.ToString(), *WorldLocation.ToCompactString(), static_cast<int32>(GetNetMode()));
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
