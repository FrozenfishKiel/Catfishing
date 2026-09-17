#include "Framework/Game/CatfishingPlayerState.h"

#include "Logging/CatLog.h"
#include "Online/CatOnlineSettings.h"
#include "Online/Voice/CatProximityVoiceComponent.h"
#include "Net/UnrealNetwork.h"

ACatfishingPlayerState::ACatfishingPlayerState()
{
	ProximityVoice = CreateDefaultSubobject<UCatProximityVoiceComponent>(TEXT("ProximityVoice"));
}

void ACatfishingPlayerState::OnSetUniqueId()
{
	Super::OnSetUniqueId();
	if (ProximityVoice) ProximityVoice->RefreshPlayerBinding();
}

void ACatfishingPlayerState::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// DestroyComponent 调用引擎的成对注销并清 bIsRegistered；只清静态表会让旧对象析构误删旅行后的新 Talker。
	if (IsValid(ProximityVoice)) ProximityVoice->DestroyComponent();
	Super::EndPlay(EndPlayReason);
}

// 玩家状态启动流程：先让父类完成 UniqueId 等引擎复制状态初始化，再按 Online 的暴露策略裁剪日志里的 StableNetId；
// 本日志只用于诊断 PlayerState 装配，不把身份字符串复制给其他系统或作为权限缓存。
void ACatfishingPlayerState::BeginPlay()
{
	Super::BeginPlay();
	const FString StableNetId = GetUniqueId().IsValid()
		&& GetDefault<UCatOnlineSettings>()->StableNetIdExposure == ECatPolicyDecision::Enabled
		? GetUniqueId()->ToString()
		: GetUniqueId().IsValid() ? TEXT("Valid(Redacted)") : TEXT("Invalid");
	UE_LOG(LogCatOnline, Log, TEXT("Event=playerstate_beginplay Class=%s StableNetId=%s Authority=%s"),
		*GetClass()->GetName(), *StableNetId, HasAuthority() ? TEXT("true") : TEXT("false"));
}

// PlayerState 复制注册流程：保留父类 UniqueId 等身份字段，再注册公开鱼图鉴摘要；该摘要不复制 Profile 私有记录。
void ACatfishingPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, PublicFishCollection);
	DOREPLIFETIME(ThisClass, bRoomOwner);
}

// 房主标记写入流程：只接受 authority 并在真实变化时强制网络更新。
// PlayerState 只是这条事实的复制载体：谁当房主、什么时候移交，全由 UCatRoomOwnerService 裁决，
// 它保证全场同时最多一个 true（换人时先清旧的再置新的）。
void ACatfishingPlayerState::SetRoomOwnerFromAuthority(const bool bNewRoomOwner)
{
	if (!HasAuthority() || bRoomOwner == bNewRoomOwner)
	{
		return;
	}
	bRoomOwner = bNewRoomOwner;
	ForceNetUpdate();
}

// 公开图鉴写入流程：仅 authority 接受有限数量、唯一非空鱼种、至少一层已解锁和有限非负数值；验证全部通过后整体替换并强制网络更新。
// 「至少一层已解锁」取代了原来的 State != Unknown：吃过但没钓到的鱼整页层级仍是 Unknown，
// 它是合法事实（图鉴 §3.1.4:124 知识层不要求先收集），不能让它把整份摘要判废。
bool ACatfishingPlayerState::SetPublicFishCollectionFromAuthority(const TArray<FCatFishCollectionRecord>& Records)
{
	if (!HasAuthority() || Records.Num() > 512)
	{
		return false;
	}
	TSet<int32> UniqueFishIds;
	for (const FCatFishCollectionRecord& Record : Records)
	{
		const bool bAnyLayerUnlocked = Record.bSilhouetteUnlocked || Record.bRecordedUnlocked || Record.bKnowledgeUnlocked;
		if ((Record.ItemId == 0) || !bAnyLayerUnlocked
			|| !FMath::IsFinite(Record.BestWeightKilograms) || Record.BestWeightKilograms < 0.0
			|| Record.EncounterCount < 0 || UniqueFishIds.Contains(Record.ItemId))
		{
			return false;
		}
		UniqueFishIds.Add(Record.ItemId);
	}
	PublicFishCollection = Records;
	ForceNetUpdate();
	return true;
}

// 公开图鉴读取流程：返回服务器最终值或客户端最近复制摘要；没有任何接口返回别人的相册或隐藏记录。
const TArray<FCatFishCollectionRecord>& ACatfishingPlayerState::GetPublicFishCollection() const
{
	return PublicFishCollection;
}

// 接管流程：先让父类建立 Pawn 所有权与输入链，再记录最终双方类型；不缓存 Pawn，也不从 Controller 复制 StableNetId 到 Character。
