#include "Framework/Game/CatfishingPlayerState.h"

#include "Logging/CatLog.h"
#include "Online/CatOnlineSettings.h"
#include "Net/UnrealNetwork.h"

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
}

// 公开图鉴写入流程：仅 authority 接受有限数量、唯一非空鱼种、合法状态和有限非负数值；验证全部通过后整体替换并强制网络更新。
bool ACatfishingPlayerState::SetPublicFishCollectionFromAuthority(const TArray<FCatFishCollectionRecord>& Records)
{
	if (!HasAuthority() || Records.Num() > 512)
	{
		return false;
	}
	TSet<FName> UniqueFishIds;
	for (const FCatFishCollectionRecord& Record : Records)
	{
		if (Record.FishDefinitionId.IsNone() || Record.State == ECatFishCollectionState::Unknown
			|| !FMath::IsFinite(Record.BestWeightKilograms) || Record.BestWeightKilograms < 0.0
			|| Record.EncounterCount < 0 || UniqueFishIds.Contains(Record.FishDefinitionId))
		{
			return false;
		}
		UniqueFishIds.Add(Record.FishDefinitionId);
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
