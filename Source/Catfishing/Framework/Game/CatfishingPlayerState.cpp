#include "Framework/Game/CatfishingPlayerState.h"

#include "Logging/CatLog.h"
#include "Net/UnrealNetwork.h"
#include "Online/CatOnlineSettings.h"

// PlayerState 开始流程：先完成父类注册，再记录继承 UniqueId 的有效性和策略允许的日志表示；不复制第二份 StableNetId 或恢复白名单。
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

// PlayerState 复制注册流程：保留父类 UniqueId 等身份字段，再注册个人 ready、主动公开鱼图鉴摘要与装备解锁清单；三者都
// 不包含 Profile 私有记录或全员转移判断。
void ACatfishingPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, bReadyForNextDay);
	DOREPLIFETIME(ThisClass, PublicFishCollection);
	DOREPLIFETIME(ThisClass, AuthorizedEquipmentUnlockIds);
}

// 个人 ready 写入流程：仅 authority 可修改；值变化后强制网络更新，使本人的最终确认及时到达客户端。
void ACatfishingPlayerState::SetNextDayReadyFromAuthority(const bool bNewReady)
{
	if (!HasAuthority() || bReadyForNextDay == bNewReady)
	{
		return;
	}
	bReadyForNextDay = bNewReady;
	ForceNetUpdate();
}

// 个人 ready 读取流程：返回服务器最终值或客户端最近复制值，不推导本人是否仍在本夜资格集合。
bool ACatfishingPlayerState::IsReadyForNextDay() const
{
	return bReadyForNextDay;
}

// 公开图鉴写入流程：仅 authority 接受有限数量、唯一非空鱼种和有限非负数值；验证全部通过后整体替换并强制网络更新。
// 记录改成两轨制后，State=Unknown 不再等于"这条记录是空的"：只抄网命中、从没自己钓起的鱼种就停在 Unknown 而 ScoopedCount>0。
// 因此这里不能再整体拒绝 Unknown，只拒绝两轨都为空的行——否则该玩家一旦持有这种记录，整份摘要会被判非法，公开图鉴投影就永久停在上一版。
bool ACatfishingPlayerState::SetPublicFishCollectionFromAuthority(const TArray<FCatFishCollectionRecord>& Records)
{
	if (!HasAuthority() || Records.Num() > 512)
	{
		return false;
	}
	TSet<FName> UniqueFishIds;
	for (const FCatFishCollectionRecord& Record : Records)
	{
		const bool bHasAnyTrack = Record.State != ECatFishCollectionState::Unknown || Record.ScoopedCount > 0;
		if (Record.FishDefinitionId.IsNone() || !bHasAnyTrack
			|| !FMath::IsFinite(Record.BestWeightKilograms) || Record.BestWeightKilograms < 0.0
			|| Record.EncounterCount < 0 || Record.ScoopedCount < 0
			|| UniqueFishIds.Contains(Record.FishDefinitionId))
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

// 解锁清单写入流程：仅 authority 接受不超过 256 条、逐条非空且唯一的清单；全部通过后整体替换并强制网络更新，任一条不合法整份拒绝、保留上一份。
bool ACatfishingPlayerState::SetAuthorizedEquipmentUnlocksFromAuthority(const TArray<FName>& UnlockIds)
{
	if (!HasAuthority() || UnlockIds.Num() > 256)
	{
		return false;
	}
	TSet<FName> Unique;
	for (const FName& UnlockId : UnlockIds)
	{
		if (UnlockId.IsNone() || Unique.Contains(UnlockId))
		{
			return false;
		}
		Unique.Add(UnlockId);
	}
	AuthorizedEquipmentUnlockIds = UnlockIds;
	ForceNetUpdate();
	return true;
}

// 解锁清单读取流程：返回服务器持有值或客户端最近复制值；不从 Profile 拼接第二份。
const TArray<FName>& ACatfishingPlayerState::GetAuthorizedEquipmentUnlocks() const
{
	return AuthorizedEquipmentUnlockIds;
}

// 装备解锁证明读取流程：None 表示定义明确声明 starter；非空 UnlockId 只在服务器持有的解锁清单里查，查不到就拒绝。清单
// 来源见 SetAuthorizedEquipmentUnlocksFromAuthority，不读客户端本地 SaveGame。
bool ACatfishingPlayerState::HasServerAuthorizedEquipmentUnlock(const FName UnlockId) const
{
	return UnlockId.IsNone() || AuthorizedEquipmentUnlockIds.Contains(UnlockId);
}

// 个人 ready 复制回调流程：只记录最终布尔值；客户端不向 GameMode 回发确认，也不计算全员完成。
void ACatfishingPlayerState::OnRep_ReadyForNextDay()
{
	UE_LOG(LogCatRun, Verbose, TEXT("Event=next_day_ready_received Ready=%s"), bReadyForNextDay ? TEXT("true") : TEXT("false"));
}
