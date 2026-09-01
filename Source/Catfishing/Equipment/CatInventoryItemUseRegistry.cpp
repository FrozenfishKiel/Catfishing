#include "Equipment/CatInventoryItemUseRegistry.h"

#include "Engine/World.h"
#include "Equipment/CatInventoryItemWorldActor.h"
#include "Logging/CatLog.h"

// 创建条件流程：只让服务器 Game World 持有物品实例占用索引；客户端不能把本地表现 Actor 当成库存裁决事实。
bool UCatInventoryItemUseRegistry::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}

// 反初始化流程：清掉本 World 的全部弱登记；此处不调用 UnUse，避免 World teardown 阶段反向写已经销毁的库存组件。
void UCatInventoryItemUseRegistry::Deinitialize()
{
	ActorByItemInstanceId.Reset();
	Super::Deinitialize();
}

bool UCatInventoryItemUseRegistry::RegisterWorldItemActor(AActor* Actor)
{
	// 场景物品登记流程：
	// 1. 先压缩旧弱引用，再要求 Actor 存活、属于当前 World、在服务器端，并实现库存实例 Actor 合同。
	// 2. 从 Actor 自己读取 ItemInstanceId 和 DefinitionId，登记器不接受外部另传一份身份，避免调用方把鱼竿 A 登成鱼竿 B。
	// 3. 同一实例若已有存活 Actor，只允许同一个 Actor 幂等重放；不同 Actor 直接拒绝，调用方必须回滚 Use。
	Compact();
	const ICatInventoryItemWorldActor* ItemActor = Cast<ICatInventoryItemWorldActor>(Actor);
	const FGuid ItemInstanceId = ItemActor ? ItemActor->GetInventoryItemInstanceIdForRegistry() : FGuid();
	const FName DefinitionId = ItemActor ? ItemActor->GetInventoryItemDefinitionIdForRegistry() : NAME_None;
	if (!IsValid(Actor) || Actor->GetWorld() != GetWorld() || !Actor->HasAuthority() || !ItemActor
		|| !ItemInstanceId.IsValid() || DefinitionId.IsNone())
	{
		UE_LOG(LogCatItems, Warning,
			TEXT("Event=world_item_actor_register_rejected Actor=%s ItemInstance=%s Definition=%s"),
			*GetNameSafe(Actor), *ItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
			*DefinitionId.ToString());
		return false;
	}
	if (TWeakObjectPtr<AActor>* Existing = ActorByItemInstanceId.Find(ItemInstanceId))
	{
		if (AActor* ExistingActor = Existing->Get())
		{
			if (ExistingActor == Actor)
			{
				return true;
			}
			UE_LOG(LogCatItems, Warning,
				TEXT("Event=world_item_actor_register_rejected Reason=DuplicateItemInstance Actor=%s ExistingActor=%s ItemInstance=%s Definition=%s"),
				*GetNameSafe(Actor), *GetNameSafe(ExistingActor),
				*ItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), *DefinitionId.ToString());
			return false;
		}
		ActorByItemInstanceId.Remove(ItemInstanceId);
	}
	ActorByItemInstanceId.Add(ItemInstanceId, Actor);
	UE_LOG(LogCatItems, Log, TEXT("Event=world_item_actor_registered Actor=%s ItemInstance=%s Definition=%s"),
		*GetNameSafe(Actor), *ItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), *DefinitionId.ToString());
	return true;
}

void UCatInventoryItemUseRegistry::UnregisterWorldItemActor(AActor* ExpectedActor)
{
	// 场景物品注销流程：
	// 1. 优先从 Actor 合同读取 ItemInstanceId，命中时只在当前登记仍指向同一个 Actor 才删除。
	// 2. 如果 Actor 身份已不可读，再退化为扫描弱表删除同一个 Actor；这只处理 EndPlay 边界，不建立新身份事实。
	// 3. 不匹配的新 Actor 登记保持不动，避免旧 Actor 的迟到 EndPlay 清掉后来生成的同实例替代物。
	if (!ExpectedActor)
	{
		Compact();
		return;
	}
	if (const ICatInventoryItemWorldActor* ItemActor = Cast<ICatInventoryItemWorldActor>(ExpectedActor))
	{
		const FGuid ItemInstanceId = ItemActor->GetInventoryItemInstanceIdForRegistry();
		if (TWeakObjectPtr<AActor>* Existing = ActorByItemInstanceId.Find(ItemInstanceId))
		{
			AActor* ExistingActor = Existing->Get(true);
			if (!ExistingActor || ExistingActor == ExpectedActor)
			{
				ActorByItemInstanceId.Remove(ItemInstanceId);
				UE_LOG(LogCatItems, Log, TEXT("Event=world_item_actor_unregistered Actor=%s ItemInstance=%s"),
					*GetNameSafe(ExpectedActor), *ItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens));
				return;
			}
		}
	}
	for (auto It = ActorByItemInstanceId.CreateIterator(); It; ++It)
	{
		AActor* ExistingActor = It.Value().Get(true);
		if (!ExistingActor || ExistingActor == ExpectedActor)
		{
			It.RemoveCurrent();
		}
	}
}

AActor* UCatInventoryItemUseRegistry::FindWorldItemActor(const FGuid ItemInstanceId)
{
	// 场景物品查询流程：先压缩登记，再按实例 ID 取弱引用；返回前再次要求 Actor 合同仍读出同一个实例，错位时删除并返回空。
	Compact();
	if (!ItemInstanceId.IsValid())
	{
		return nullptr;
	}
	TWeakObjectPtr<AActor>* Existing = ActorByItemInstanceId.Find(ItemInstanceId);
	AActor* Actor = Existing ? Existing->Get() : nullptr;
	const ICatInventoryItemWorldActor* ItemActor = Cast<ICatInventoryItemWorldActor>(Actor);
	if (!Actor || !ItemActor || ItemActor->GetInventoryItemInstanceIdForRegistry() != ItemInstanceId)
	{
		ActorByItemInstanceId.Remove(ItemInstanceId);
		return nullptr;
	}
	return Actor;
}

bool UCatInventoryItemUseRegistry::IsItemInstanceInWorld(const FGuid ItemInstanceId)
{
	// 占用判断流程：复用 FindWorldItemActor 的压缩和身份复核；调用方只需要布尔结果，不拿 Actor 做玩法操作。
	return FindWorldItemActor(ItemInstanceId) != nullptr;
}

void UCatInventoryItemUseRegistry::Compact()
{
	// 弱登记压缩流程：删除 Actor 已失效、接口消失或接口读出的 ItemInstanceId 与 Map Key 不一致的条目；不因 DefinitionId 变化重写键。
	for (auto It = ActorByItemInstanceId.CreateIterator(); It; ++It)
	{
		AActor* Actor = It.Value().Get();
		const ICatInventoryItemWorldActor* ItemActor = Cast<ICatInventoryItemWorldActor>(Actor);
		if (!Actor || !ItemActor || ItemActor->GetInventoryItemInstanceIdForRegistry() != It.Key())
		{
			It.RemoveCurrent();
		}
	}
}
