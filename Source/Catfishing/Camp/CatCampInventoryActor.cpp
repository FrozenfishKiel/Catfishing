#include "Camp/CatCampInventoryActor.h"
#include "Equipment/Inventory/CatInventoryTransferService.h"
#include "Inventory/CatInventoryMutationScope.h"
#include "Misc/ScopeExit.h"

#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/LocalPlayer.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentInventoryItemInstance.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Equipment/CatRunInventorySlotOperations.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/CatInteractionSettings.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "Inventory/CatInventorySettings.h"
#include "Inventory/CatInventoryStatics.h"
#include "Logging/CatLog.h"
#include "Net/UnrealNetwork.h"
#include "UI/CatLocalPlayerUISubsystem.h"
#include "UI/Inventory/CatCampInventoryWidget.h"

// 构造流程：公共仓库是关卡里的服务器权威 Actor；创建根节点、交互碰撞、默认独立 WBP 软路径、开启复制并关闭 Tick，库存变化只由显式命令提交。
ACatCampInventoryActor::ACatCampInventoryActor()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	InteractionCollision = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionCollision"));
	InteractionCollision->SetupAttachment(SceneRoot);
	InteractionCollision->SetSphereRadius(100.0f);
	InteractionCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionCollision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractionCollision->SetGenerateOverlapEvents(false);
	InventoryComponent = CreateDefaultSubobject<UCatInventoryComponent>(TEXT("InventoryComponent"));
	InventoryViewClass = TSoftClassPtr<UCatCampInventoryWidget>(
		FSoftClassPath(TEXT("/Game/UI/Inventory/WBP_CatCampInventory.WBP_CatCampInventory_C")));
	InteractionPrompt = NSLOCTEXT("Catfishing", "CampInventoryInteractionPrompt", "打开营地库存");
}

// 复制注册流程：只复制公共仓库快照；终态缓存留在服务器内存，避免客户端拿缓存当权限事实。
void ACatCampInventoryActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, Snapshot);
}

// BeginPlay 流程：读取项目交互设置并把公共仓库命中球对齐到同一 Trace 通道；服务器和客户端都只调整碰撞响应，不在这里改库存。
void ACatCampInventoryActor::BeginPlay()
{
	Super::BeginPlay();
	if (const UCatInteractionSettings* Settings = GetDefault<UCatInteractionSettings>(); Settings && InteractionCollision)
	{
		InteractionCollision->SetCollisionResponseToChannel(Settings->TargetingTraceChannel, ECR_Block);
	}
	if (HasAuthority() && InventoryComponent)
	{
		InventoryComponent->SetInventorySlotCountFromAuthority(InventorySlotCapacity);
	}
}

// 可交互判断流程：只要求交互开关开启且请求来自玩家 Controller；具体距离由准星扫描和服务器取用 RPC 再复核。
bool ACatCampInventoryActor::CanInteract_Implementation(AController* RequestingController) const
{
	return bInteractionEnabled && Cast<APlayerController>(RequestingController) != nullptr;
}

// 提示文本流程：交互关闭时返回空文本；打开时使用编辑器配置文本，让提示和实际入口保持同一个开关。
FText ACatCampInventoryActor::GetInteractionPrompt_Implementation() const
{
	return bInteractionEnabled ? InteractionPrompt : FText::GetEmpty();
}

// 距离读取流程：把编辑器配置的厘米值裁成非负有限数；异常值按 0 处理，让服务器距离复核保守失败。
double ACatCampInventoryActor::GetInteractionRadius_Implementation() const
{
	return FMath::IsFinite(InteractionRadiusCentimeters)
		? FMath::Max(0.0, InteractionRadiusCentimeters) : 0.0;
}

// 交互流程：
// 1. 先校验请求 ID、玩家 Controller 和交互开关；缺任一项都返回失败并记录日志。
// 2. 只有本地 Controller 会打开库存 UI；远端或服务器代理不会创建本地页面。
// 3. 打开时把本 Actor 和它配置的独立 WBP 类交给 LocalPlayer UI，后续取用仍由服务器 RPC 重读公共仓库和玩家随身库存。
bool ACatCampInventoryActor::Interact_Implementation(AController* RequestingController, const FGuid RequestId)
{
	APlayerController* PlayerController = Cast<APlayerController>(RequestingController);
	if (!RequestId.IsValid() || !CanInteract_Implementation(RequestingController) || !PlayerController)
	{
		UE_LOG(LogCatUI, Warning,
			TEXT("Event=camp_inventory_interaction_rejected Reason=DependencyUnavailable Controller=%s Inventory=%s"),
			*GetNameSafe(PlayerController), *GetNameSafe(this));
		return false;
	}
	if (!PlayerController->IsLocalController())
	{
		return false;
	}
	ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
	UCatLocalPlayerUISubsystem* UISubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<UCatLocalPlayerUISubsystem>() : nullptr;
	const bool bOpened = UISubsystem && UISubsystem->OpenCampInventory(this, LoadInventoryViewClass());
	UE_LOG(LogCatUI, Log, TEXT("Event=camp_inventory_interaction_opened Inventory=%s Opened=%s"),
		*GetNameSafe(this), bOpened ? TEXT("true") : TEXT("false"));
	return bOpened;
}

// 快照读取流程：返回公共仓库当前读模型；调用方只能显示 Revision 和 Slots，不能绕过入库/取用写口。
const FCatCampInventorySnapshot& ACatCampInventoryActor::GetSnapshot() const
{
	return Snapshot;
}

// Inventory 读取流程：直接返回构造期正式库存组件；公共仓库命令和 UI 展示都以它作为库存事实源。
UCatInventoryComponent* ACatCampInventoryActor::GetInventoryComponent() const
{
	return InventoryComponent;
}

// 公共仓库恢复预检流程：
// 1. 先确认当前 Actor 是 authority 且保存格数没有超过现行容量。
// 2. 再逐格检查空格残留、运行定义、堆叠上限、实例唯一性和鱼竿专属状态。
// 3. 此处不写 Snapshot，Save 用它先完成所有领域预检，防止营地先恢复而其他容器失败。
bool ACatCampInventoryActor::CanRestoreSnapshotFromAuthority(const FCatCampInventorySnapshot& RestoredSnapshot,
	FText& OutFailure) const
{
	OutFailure = FText::GetEmpty();
	if (!HasAuthority() || RestoredSnapshot.InventorySlots.Num() > GetConfiguredSlotCapacity())
	{
		OutFailure = FText::FromString(TEXT("营地仓库恢复不是服务器上下文或保存格数超过容量。"));
		return false;
	}
	TSet<FGuid> SeenInstanceIds;
	for (const FCatRunInventorySlot& Slot : RestoredSnapshot.InventorySlots)
	{
		const bool bOccupied = CatRunInventorySlotOperations::IsInventorySlotOccupied(Slot);
		if (!bOccupied)
		{
			if (!Slot.DefinitionId.IsNone() || Slot.ItemInstanceId.IsValid() || Slot.Quantity != 0
				|| Slot.RodDurability != 0.0 || Slot.bRodBroken)
			{
				OutFailure = FText::FromString(TEXT("营地仓库空格携带了残留运行状态。"));
				return false;
			}
			continue;
		}
		const UCatEquipmentDefinition* Definition = ResolveEquipmentDefinitionForLegacyInventory(Slot.DefinitionId);
		if (!Slot.ItemInstanceId.IsValid() || SeenInstanceIds.Contains(Slot.ItemInstanceId) || !Definition
			|| !Definition->IsRuntimeDefinitionReady() || Slot.Quantity <= 0
			|| Slot.Quantity > GetInventoryStackLimit(*Definition))
		{
			OutFailure = FText::FromString(TEXT("营地仓库含有无效定义、数量或重复实例。"));
			return false;
		}
		if (Definition->Kind == ECatEquipmentKind::Rod)
		{
			if (!FMath::IsFinite(Slot.RodDurability) || Slot.RodDurability < 0.0
				|| Slot.RodDurability > Definition->MaximumRodDurability
				|| (Slot.bRodBroken && Slot.RodDurability != 0.0))
			{
				OutFailure = FText::FromString(TEXT("营地仓库鱼竿耐久与定义约束不一致。"));
				return false;
			}
		}
		else if (Slot.RodDurability != 0.0 || Slot.bRodBroken)
		{
			OutFailure = FText::FromString(TEXT("营地仓库非鱼竿格含有鱼竿状态。"));
			return false;
		}
		SeenInstanceIds.Add(Slot.ItemInstanceId);
	}
	return true;
}

// 公共仓库恢复提交流程：先重复预检并把旧槽位重建为正式库存实例；两边都成功后才替换快照、清终态缓存并发布。
bool ACatCampInventoryActor::RestoreSnapshotFromAuthority(const FCatCampInventorySnapshot& RestoredSnapshot)
{
	FText Failure;
	if (!CanRestoreSnapshotFromAuthority(RestoredSnapshot, Failure))
	{
		return false;
	}
	TArray<FCatInventoryEntry> RestoredEntries;
	if (!BuildFormalEntriesFromLegacySnapshot(RestoredSnapshot, RestoredEntries)
		|| !InventoryComponent
		|| !InventoryComponent->ReplaceInventoryEntriesFromAuthority(RestoredEntries, GetConfiguredSlotCapacity()))
	{
		return false;
	}
	Snapshot = RestoredSnapshot;
	Snapshot.Revision = FMath::Max<int64>(1, Snapshot.Revision + 1);
	TerminalCache.Reset();
	TerminalPayloadByKey.Reset();
	PublishSnapshot();
	return true;
}

// 容量读取流程：返回公共仓库当前配置容量的安全值；UI 用它展示空格，提交逻辑仍由服务器重新检查容量和版本。
int32 ACatCampInventoryActor::GetInventorySlotCapacityForView() const
{
	return GetConfiguredSlotCapacity();
}

// 入库预检流程：
// 1. 先从库存目录读取定义并验证 RequestId、authority、定义和数量；失败不改公共仓库格子。
// 2. 已有同 RequestId 终态时先比对版本、定义和数量签名，只有同一批货才放行合法重放。
// 3. 首次预检要求调用方看到的仓库版本仍是当前版本，再按公共仓库容量和堆叠规则判断整批物品能否一次放完。
ECatDomainCommandError ACatCampInventoryActor::ValidateAddItemFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const FName DefinitionId, const int32 Quantity) const
{
	TArray<FCatCampInventoryAddItemRequest> Items;
	FCatCampInventoryAddItemRequest& Item = Items.AddDefaulted_GetRef();
	Item.DefinitionId = DefinitionId;
	Item.Quantity = Quantity;
	FCatInventoryReceiveBatch ReceiveBatch;
	if (!HasAuthority() || !RequestId.IsValid() || Quantity <= 0
		|| !BuildFormalReceiveBatchForLegacyInventory(Items, ReceiveBatch))
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	const FString Key = MakeTerminalKey(TEXT("AddItem"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("ExpectedRevision=%lld|Definition=%s|Quantity=%d"),
		ExpectedRevision, *DefinitionId.ToString(), Quantity);
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		const FString* CachedPayload = TerminalPayloadByKey.Find(Key);
		if (!CachedPayload || *CachedPayload != PayloadSignature)
		{
			return ECatDomainCommandError::InvalidPayload;
		}
		return Cached->Error == ECatDomainCommandError::None ? ECatDomainCommandError::None : Cached->Error;
	}
	if (Snapshot.Revision != ExpectedRevision)
	{
		return ECatDomainCommandError::RevisionConflict;
	}
	return InventoryComponent && InventoryComponent->CanFullyAcceptInventoryBatch(ReceiveBatch, GetConfiguredSlotCapacity())
		? ECatDomainCommandError::None : ECatDomainCommandError::CapacityExceeded;
}

// 入库提交流程：
// 1. 先用 RequestId 和载荷签名处理幂等重放，防止同一次购买换 DefinitionId 或数量。
// 2. 首次提交复用扣款前预检，再检查公共仓库 Revision 是否仍匹配调用方看到的事实。
// 3. 批次写入和旧 Snapshot 投影都成功后才递增 Revision、复制并缓存终态；预检或写入失败只返回当前仓库版本。
FCatDomainCommandResult ACatCampInventoryActor::AddItemFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const FName DefinitionId, const int32 Quantity)
{

	FCatInventoryMutationScope InventoryMutation(InventoryComponent);
	const bool bWasDeferring = bDeferringPublication;
	bDeferringPublication = true;
	ON_SCOPE_EXIT
	{
		bDeferringPublication = bWasDeferring;
		if (!bWasDeferring && bPublicationPending)
		{
			bPublicationPending = false;
			PublishSnapshot();
		}
	};
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (!RequestId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		Result.Revision = Snapshot.Revision;
		return Result;
	}
	const FString Key = MakeTerminalKey(TEXT("AddItem"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("ExpectedRevision=%lld|Definition=%s|Quantity=%d"),
		ExpectedRevision, *DefinitionId.ToString(), Quantity);
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		const FString* CachedPayload = TerminalPayloadByKey.Find(Key);
		if (!CachedPayload || *CachedPayload != PayloadSignature)
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
			Result.Revision = Snapshot.Revision;
			return Result;
		}
		Result = *Cached;
		if (Cached->bCommitted && Cached->Error == ECatDomainCommandError::None)
		{
			MarkCommandReplayed(Result);
		}
		return Result;
	}

	TArray<FCatCampInventoryAddItemRequest> Items;
	FCatCampInventoryAddItemRequest& Item = Items.AddDefaulted_GetRef();
	Item.DefinitionId = DefinitionId;
	Item.Quantity = Quantity;
	FCatInventoryReceiveBatch ReceiveBatch;
	const bool bBatchReady = BuildFormalReceiveBatchForLegacyInventory(Items, ReceiveBatch);
	const ECatDomainCommandError Rejection =
		ValidateAddItemFromAuthority(RequestId, ExpectedRevision, DefinitionId, Quantity);
	if (Rejection != ECatDomainCommandError::None)
	{
		Result.Error = Rejection;
	}
	else if (bBatchReady && AddPreparedBatchToFormalInventory(ReceiveBatch)
		&& SyncLegacySnapshotFromInventoryComponent())
	{
		++Snapshot.Revision;
		PublishSnapshot();
		Result.bCommitted = true;
		Result.Error = ECatDomainCommandError::None;
	}
	else
	{
		Result.Error = ECatDomainCommandError::CapacityExceeded;
	}
	Result.Revision = Snapshot.Revision;
	TerminalCache.Add(Key, Result);
	TerminalPayloadByKey.Add(Key, PayloadSignature);
	return Result;
}

// 整批入库预检流程：
// 1. 先验证 RequestId、服务器身份和整批载荷签名；重复 DefinitionId 会合并，行顺序不会制造另一批货。
// 2. 已有同身份同 RequestId 成功终态时只允许同一批货重放，成功终态继续放行给 AddItemsFromAuthority 返回 AlreadyResolved。
// 3. 首次预检要求仓库版本仍匹配，然后让正式库存组件模拟整批入库，任何一行放不下都拒绝整批。
ECatDomainCommandError ACatCampInventoryActor::ValidateAddItemsFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const FString& StableNetId,
	const TArray<FCatCampInventoryAddItemRequest>& Items) const
{
	if (!HasAuthority() || !RequestId.IsValid() || StableNetId.IsEmpty())
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	FString PayloadSignature;
	TArray<FCatCampInventoryAddItemRequest> NormalizedItems;
	if (!BuildAddItemsPayloadSignature(Items, PayloadSignature, NormalizedItems))
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	const FString Key = MakeTerminalKey(TEXT("AddItems"), StableNetId, RequestId);
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		const FString* CachedPayload = TerminalPayloadByKey.Find(Key);
		if (!CachedPayload || *CachedPayload != PayloadSignature)
		{
			return ECatDomainCommandError::InvalidPayload;
		}
		return Cached->Error == ECatDomainCommandError::None ? ECatDomainCommandError::None : Cached->Error;
	}
	if (Snapshot.Revision != ExpectedRevision)
	{
		return ECatDomainCommandError::RevisionConflict;
	}
	return CanStoreItems(NormalizedItems) ? ECatDomainCommandError::None : ECatDomainCommandError::CapacityExceeded;
}

// 整批入库提交流程：
// 1. 用服务器身份、RequestId 和整批载荷签名处理成功终态重放；失败不写终态缓存，调用方重读状态后仍可重新提交。
// 2. 首次提交复用整批预检，再把批次完整写入正式库存组件。
// 3. 批次写入和旧 Snapshot 投影都成功后才推进 Revision、广播并缓存；预检或写入失败不写终态缓存。
FCatDomainCommandResult ACatCampInventoryActor::AddItemsFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const FString& StableNetId,
	const TArray<FCatCampInventoryAddItemRequest>& Items)
{

	FCatInventoryMutationScope InventoryMutation(InventoryComponent);
	const bool bWasDeferring = bDeferringPublication;
	bDeferringPublication = true;
	ON_SCOPE_EXIT
	{
		bDeferringPublication = bWasDeferring;
		if (!bWasDeferring && bPublicationPending)
		{
			bPublicationPending = false;
			PublishSnapshot();
		}
	};
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (!RequestId.IsValid() || StableNetId.IsEmpty())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		Result.Revision = Snapshot.Revision;
		return Result;
	}
	FString PayloadSignature;
	TArray<FCatCampInventoryAddItemRequest> NormalizedItems;
	if (!BuildAddItemsPayloadSignature(Items, PayloadSignature, NormalizedItems))
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		Result.Revision = Snapshot.Revision;
		return Result;
	}
	const FString Key = MakeTerminalKey(TEXT("AddItems"), StableNetId, RequestId);
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		const FString* CachedPayload = TerminalPayloadByKey.Find(Key);
		if (!CachedPayload || *CachedPayload != PayloadSignature)
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
			Result.Revision = Snapshot.Revision;
			return Result;
		}
		Result = *Cached;
		if (Cached->bCommitted && Cached->Error == ECatDomainCommandError::None)
		{
			MarkCommandReplayed(Result);
		}
		return Result;
	}

	const ECatDomainCommandError Rejection =
		ValidateAddItemsFromAuthority(RequestId, ExpectedRevision, StableNetId, NormalizedItems);
	if (Rejection != ECatDomainCommandError::None)
	{
		Result.Error = Rejection;
	}
	else
	{
		FCatInventoryReceiveBatch ReceiveBatch;
		if (BuildFormalReceiveBatchForLegacyInventory(NormalizedItems, ReceiveBatch)
			&& InventoryComponent
			&& AddPreparedBatchToFormalInventory(ReceiveBatch)
			&& SyncLegacySnapshotFromInventoryComponent())
		{
			++Snapshot.Revision;
			PublishSnapshot();
			Result.bCommitted = true;
			Result.Error = ECatDomainCommandError::None;
		}
		else
		{
			Result.Error = ECatDomainCommandError::CapacityExceeded;
		}
	}
	Result.Revision = Snapshot.Revision;
	if (Result.bCommitted && Result.Error == ECatDomainCommandError::None)
	{
		TerminalCache.Add(Key, Result);
		TerminalPayloadByKey.Add(Key, PayloadSignature);
	}
	return Result;
}

// 取用预检流程：
// 1. 先验证 authority、请求身份、目标 Inventory 宿主和数量，让公共仓库取用必须有明确正式库存接收方。
// 2. 源格、定义、实例和容量全部从双方 InventoryComponent 读取并预演；本函数不写任何格子。
// 3. 任一正式库存组件缺失都直接拒绝，避免旧 Snapshot 或 Equipment 投影重新成为库存写事实。
ECatDomainCommandError ACatCampInventoryActor::ValidateWithdrawToInventory(const FGuid RequestId,
	const int32 SourceSlotIndex, const int32 Quantity, UCatInventoryComponent* TargetInventory) const
{
	const AActor* TargetOwner = TargetInventory ? TargetInventory->GetOwner() : nullptr;
	if (!HasAuthority() || !RequestId.IsValid() || !TargetInventory || Quantity <= 0
		|| !TargetOwner || !TargetOwner->HasAuthority() || SourceSlotIndex < 0)
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	if (InventoryComponent && TargetInventory)
	{
		const FCatInventoryEntry* SourceEntry = InventoryComponent->GetInventoryEntryAtSlot(SourceSlotIndex);
		UCatInventoryItemDefinition* InventoryDefinition =
			SourceEntry && SourceEntry->Instance ? SourceEntry->Instance->GetItemDefinition() : nullptr;
		UCatEquipmentDefinition* Definition = Cast<UCatEquipmentDefinition>(InventoryDefinition);
		if (SourceEntry == nullptr || SourceEntry->Instance == nullptr || SourceEntry->StackCount <= 0
			|| !Definition || InventoryDefinition->GetInventoryDefinitionId().IsNone()
			|| SourceEntry->StackCount < Quantity)
		{
			return ECatDomainCommandError::InvalidPayload;
		}
		if (!Definition->bRunConsumable && Quantity != 1)
		{
			return ECatDomainCommandError::InvalidPayload;
		}
		FCatInventoryReceiveBatch ReceiveBatch;
		if (Quantity == SourceEntry->StackCount)
		{
			FCatInventoryInstanceEntry& InstanceEntry = ReceiveBatch.InstanceEntries.AddDefaulted_GetRef();
			InstanceEntry.ItemInstance = SourceEntry->Instance;
			InstanceEntry.Count = Quantity;
		}
		else
		{
			FCatInventoryDefinitionEntry& DefinitionEntry = ReceiveBatch.DefinitionEntries.AddDefaulted_GetRef();
			DefinitionEntry.ItemDefinition = Definition;
			DefinitionEntry.ItemInstanceClass = SourceEntry->Instance->GetClass();
			DefinitionEntry.Count = Quantity;
		}
		return TargetInventory->CanFullyAcceptInventoryBatch(ReceiveBatch)
			? ECatDomainCommandError::None : ECatDomainCommandError::CapacityExceeded;
	}
	return ECatDomainCommandError::DependencyUnavailable;
}

// 旧取用预检流程：只把 Equipment 宿主翻译成 Owner 上的正式随身库存，随后复用新的 Inventory-first 预检；旧 Snapshot 不参与可取性裁决。
ECatDomainCommandError ACatCampInventoryActor::ValidateWithdrawToEquipment(const FGuid RequestId,
	const int32 SourceSlotIndex, const int32 Quantity, UCatEquipmentComponent* TargetEquipment) const
{
	return ValidateWithdrawToInventory(RequestId, SourceSlotIndex, Quantity,
		ResolveFormalInventoryFromEquipment(TargetEquipment));
}

// 取用提交流程：
// 1. 先用 RequestId、源槽、源物定义/实例、数量和双方版本签名处理幂等；源物身份只从正式 InventoryComponent 读取。
// 2. 正式库存可用时优先验证营地公开版本、玩家正式库存版本、容量和源格数量；只有旧 wrapper 开启兼容开关时才允许旧投影版本回退。
// 3. 满栈取用保留原实例身份，拆栈取用按定义生成接收批次；玩家接收、营地扣减或可选旧投影刷新失败都会恢复双方正式 entries 和已存在的旧快照。
// 4. 任一正式库存组件缺失都返回依赖不可用，不再用旧 Equipment 或 Snapshot 写入库存。
FCatDomainCommandResult ACatCampInventoryActor::WithdrawToInventoryFromAuthority(const FGuid RequestId,
	const int64 ExpectedCampRevision, const int32 SourceSlotIndex, const int32 Quantity,
	UCatInventoryComponent* TargetInventory, const int64 ExpectedInventoryRevision,
	UCatEquipmentComponent* LegacyProjectionEquipment, const bool bAllowLegacyProjectionRevisionFallback)
{

	FCatInventoryMutationScope CampMutation(InventoryComponent);
	FCatInventoryMutationScope PlayerMutation(TargetInventory);
	const bool bWasDeferring = bDeferringPublication;
	const bool bEquipmentWasDeferring = LegacyProjectionEquipment && LegacyProjectionEquipment->bDeferringSnapshotPublication;
	bDeferringPublication = true;
	if (LegacyProjectionEquipment) LegacyProjectionEquipment->bDeferringSnapshotPublication = true;
	ON_SCOPE_EXIT
	{
		bDeferringPublication = bWasDeferring;
		if (LegacyProjectionEquipment)
		{
			LegacyProjectionEquipment->bDeferringSnapshotPublication = bEquipmentWasDeferring;
			if (!bEquipmentWasDeferring && LegacyProjectionEquipment->bSnapshotPublicationPending)
			{
				LegacyProjectionEquipment->bSnapshotPublicationPending = false;
				LegacyProjectionEquipment->PublishSnapshot();
			}
		}
		if (!bWasDeferring && bPublicationPending)
		{
			bPublicationPending = false;
			PublishSnapshot();
		}
	};
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	if (!RequestId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		Result.Revision = Snapshot.Revision;
		return Result;
	}
	const FString Key = MakeTerminalKey(TEXT("WithdrawToInventory"), GetPathNameSafe(TargetInventory), RequestId);
	const FString PayloadSignature = FString::Printf(
		TEXT("ExpectedCamp=%lld|Slot=%d|Quantity=%d|TargetInventory=%s|ExpectedInventory=%lld"),
		ExpectedCampRevision, SourceSlotIndex, Quantity,
		*GetPathNameSafe(TargetInventory), ExpectedInventoryRevision);
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		const FString* CachedPayload = TerminalPayloadByKey.Find(Key);
		if (!CachedPayload || *CachedPayload != PayloadSignature)
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
			Result.Revision = Snapshot.Revision;
			return Result;
		}
		Result = *Cached;
		MarkCommandReplayed(Result);
		return Result;
	}

	if (InventoryComponent && TargetInventory)
	{
		AActor* TargetOwner = TargetInventory->GetOwner();
		if (!HasAuthority() || !TargetOwner || !TargetOwner->HasAuthority()
			|| !RequestId.IsValid() || Quantity <= 0)
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
		}
		else if (Snapshot.Revision != ExpectedCampRevision
			|| !DoesPlayerInventoryRevisionMatch(TargetInventory, ExpectedInventoryRevision,
				LegacyProjectionEquipment, bAllowLegacyProjectionRevisionFallback))
		{
			Result.Error = ECatDomainCommandError::RevisionConflict;
		}
		else
		{
			// 右键取入也先对齐正式容量；兼容读模型仍按快照看空格，正式预检不能因为组件尚未补格而误报满包。
			InventoryComponent->SetInventorySlotCountFromAuthority(GetConfiguredSlotCapacity());
			const int32 TargetMinimumSlotCount = LegacyProjectionEquipment
				? LegacyProjectionEquipment->GetConfiguredInventorySlotCapacity()
				: TargetInventory->GetInventorySlotCount();
			TargetInventory->SetInventorySlotCountFromAuthority(TargetMinimumSlotCount);
			if (!InventoryComponent->IsValidInventorySlotIndex(SourceSlotIndex))
			{
				Result.Error = ECatDomainCommandError::InvalidPayload;
			}
			else
			{
				const FCatInventoryEntry* SourceEntry = InventoryComponent->GetInventoryEntryAtSlot(SourceSlotIndex);
				UCatInventoryItemDefinition* InventoryDefinition =
					SourceEntry && SourceEntry->Instance ? SourceEntry->Instance->GetItemDefinition() : nullptr;
				UCatEquipmentDefinition* Definition = Cast<UCatEquipmentDefinition>(InventoryDefinition);
				const FName FormalDefinitionId =
					InventoryDefinition ? InventoryDefinition->GetInventoryDefinitionId() : NAME_None;
				if (SourceEntry == nullptr || SourceEntry->Instance == nullptr || SourceEntry->StackCount <= 0
					|| !Definition || FormalDefinitionId.IsNone() || Quantity > SourceEntry->StackCount)
				{
					Result.Error = ECatDomainCommandError::InvalidPayload;
				}
				else if (!Definition->bRunConsumable && Quantity != 1)
				{
					Result.Error = ECatDomainCommandError::InvalidPayload;
				}
				else
				{
					FCatInventoryReceiveBatch ReceiveBatch;
					if (Quantity == SourceEntry->StackCount)
					{
						FCatInventoryInstanceEntry& InstanceEntry =
							ReceiveBatch.InstanceEntries.AddDefaulted_GetRef();
						InstanceEntry.ItemInstance = SourceEntry->Instance;
						InstanceEntry.Count = Quantity;
					}
					else
					{
						FCatInventoryDefinitionEntry& DefinitionEntry =
							ReceiveBatch.DefinitionEntries.AddDefaulted_GetRef();
						DefinitionEntry.ItemDefinition = Definition;
						DefinitionEntry.ItemInstanceClass = SourceEntry->Instance->GetClass();
						DefinitionEntry.Count = Quantity;
					}

					if (!TargetInventory->CanFullyAcceptInventoryBatch(ReceiveBatch))
					{
						Result.Error = ECatDomainCommandError::CapacityExceeded;
					}
					else
					{
						const TArray<FCatInventoryEntry> SavedCampEntries = InventoryComponent->GetInventoryEntries();
						const TArray<FCatInventoryEntry> SavedTargetEntries = TargetInventory->GetInventoryEntries();
						const FCatCampInventorySnapshot SavedCampSnapshot = Snapshot;
						const FCatEquipmentLoadoutSnapshot SavedEquipmentSnapshot = LegacyProjectionEquipment
							? LegacyProjectionEquipment->Snapshot : FCatEquipmentLoadoutSnapshot();
						const bool bAccepted = TargetInventory->TryAddInventoryBatch(ReceiveBatch);
						const bool bConsumed =
							bAccepted && InventoryComponent->ConsumeItemAtSlot(SourceSlotIndex, Quantity);
						if (!bAccepted || !bConsumed
							|| !SyncLegacyViewsFromFormalInventories(
								LegacyProjectionEquipment, Definition, FormalDefinitionId))
						{
							InventoryComponent->ReplaceInventoryEntriesFromAuthority(
								SavedCampEntries, GetConfiguredSlotCapacity());
							TargetInventory->ReplaceInventoryEntriesFromAuthority(
								SavedTargetEntries, TargetMinimumSlotCount);
							Snapshot = SavedCampSnapshot;
							if (LegacyProjectionEquipment)
							{
								LegacyProjectionEquipment->Snapshot = SavedEquipmentSnapshot;
							}
							Result.Error = ECatDomainCommandError::DependencyUnavailable;
						}
						else
						{
							++Snapshot.Revision;
							PublishSnapshot();
							Result.bCommitted = true;
							Result.Error = ECatDomainCommandError::None;
						}
					}
				}
			}
		}
		Result.Revision = Snapshot.Revision;
		TerminalCache.Add(Key, Result);
		TerminalPayloadByKey.Add(Key, PayloadSignature);
		return Result;
	}

	Result.Error = ECatDomainCommandError::DependencyUnavailable;
	Result.Revision = Snapshot.Revision;
	TerminalCache.Add(Key, Result);
	TerminalPayloadByKey.Add(Key, PayloadSignature);
	return Result;
}

// 旧取用提交流程：旧函数只负责把 Equipment 翻译成正式库存和可选投影刷新者，ExpectedInventoryRevision 也先交给 Inventory-first 入口按正式版本优先校验。
FCatDomainCommandResult ACatCampInventoryActor::WithdrawToEquipmentFromAuthority(const FGuid RequestId,
	const int64 ExpectedCampRevision, const int32 SourceSlotIndex, const int32 Quantity,
	UCatEquipmentComponent* TargetEquipment, const int64 ExpectedEquipmentRevision)
{
	FCatInventoryTransferRequest Request;
	Request.RequestId = RequestId;
	Request.Initiator = TargetEquipment ? TargetEquipment->GetOwner() : nullptr;
	Request.Source.Host = this;
	Request.Target.Host = TargetEquipment;
	Request.ExpectedSourceRevision = ExpectedCampRevision;
	Request.ExpectedTargetRevision = ExpectedEquipmentRevision;
	Request.SourceSlotIndex = SourceSlotIndex;
	Request.Quantity = Quantity;
	UCatInventoryTransferService* Transfers = GetWorld() ? GetWorld()->GetSubsystem<UCatInventoryTransferService>() : nullptr;
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	Result.Revision = Snapshot.Revision;
	if (!Transfers) { Result.Error = ECatDomainCommandError::DependencyUnavailable; return Result; }
	const FCatInventoryTransferResult Transferred = Transfers->TransferFromAuthority(Request);
	Result.bCommitted = Transferred.bCommitted;
	Result.Error = Transferred.Error;
	Result.Revision = Transferred.SourceRevision;
	return Result;
}

// 公共仓库整理流程：
// 1. 用 RequestId、Revision 和源/目标下标处理幂等重放；同 RequestId 换格子会被拒绝。
// 2. 首次请求要求服务器 authority、公开 Snapshot 版本匹配且正式库存槽位有效，再让 InventoryComponent 裁决移动、合并或交换。
// 3. 正式库存变化后才刷新旧 Snapshot 并推进公共仓库公开 Revision；目标格已满这类无变化结果只返回 AlreadyResolved。
FCatDomainCommandResult ACatCampInventoryActor::MoveInventorySlotFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const int32 SourceSlotIndex, const int32 TargetSlotIndex)
{

	FCatInventoryMutationScope InventoryMutation(InventoryComponent);
	const bool bWasDeferring = bDeferringPublication;
	bDeferringPublication = true;
	ON_SCOPE_EXIT
	{
		bDeferringPublication = bWasDeferring;
		if (!bWasDeferring && bPublicationPending)
		{
			bPublicationPending = false;
			PublishSnapshot();
		}
	};
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString Key = MakeTerminalKey(TEXT("MoveInventorySlot"), RequestId);
	const FString PayloadSignature = FString::Printf(TEXT("ExpectedRevision=%lld|Source=%d|Target=%d"),
		ExpectedRevision, SourceSlotIndex, TargetSlotIndex);
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		const FString* CachedPayload = TerminalPayloadByKey.Find(Key);
		if (!CachedPayload || *CachedPayload != PayloadSignature)
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
			Result.Revision = Snapshot.Revision;
			return Result;
		}
		Result = *Cached;
		MarkCommandReplayed(Result);
		return Result;
	}
	if (!HasAuthority() || !RequestId.IsValid() || SourceSlotIndex < 0 || TargetSlotIndex < 0
		|| SourceSlotIndex == TargetSlotIndex)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (Snapshot.Revision != ExpectedRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
	}
	else if (!InventoryComponent)
	{
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else
	{
		InventoryComponent->SetInventorySlotCountFromAuthority(GetConfiguredSlotCapacity());
		const FCatInventoryEntry* SourceEntry = InventoryComponent->GetInventoryEntryAtSlot(SourceSlotIndex);
		if (!InventoryComponent->IsValidInventorySlotIndex(SourceSlotIndex)
			|| !InventoryComponent->IsValidInventorySlotIndex(TargetSlotIndex))
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
		}
		else if (SourceEntry == nullptr || SourceEntry->Instance == nullptr || SourceEntry->StackCount <= 0)
		{
			Result.Error = ECatDomainCommandError::NotFound;
		}
		else if (!UCatInventoryComponent::CanExecuteExchangeRequest(
			InventoryComponent, SourceSlotIndex, InventoryComponent, TargetSlotIndex))
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
		}
		else
		{
			const TArray<FCatInventoryEntry> SavedEntries = InventoryComponent->GetInventoryEntries();
			const FCatCampInventorySnapshot SavedSnapshot = Snapshot;
			const bool bFormalChanged = UCatInventoryComponent::ExecuteExchangeRequestOnAuthority(
				InventoryComponent, SourceSlotIndex, InventoryComponent, TargetSlotIndex);
			if (!bFormalChanged)
			{
				Result.Error = ECatDomainCommandError::AlreadyResolved;
			}
			else if (!SyncLegacyViewsFromFormalInventories(nullptr, nullptr, NAME_None))
			{
				Result.Error = ECatDomainCommandError::InvalidPayload;
				Result.bCommitted = false;
				InventoryComponent->ReplaceInventoryEntriesFromAuthority(SavedEntries, GetConfiguredSlotCapacity());
				Snapshot = SavedSnapshot;
			}
			else
			{
				Result.bCommitted = true;
				Result.Error = ECatDomainCommandError::None;
			}
		}
	}
	if (Result.bCommitted)
	{
		++Snapshot.Revision;
		PublishSnapshot();
	}
	Result.Revision = Snapshot.Revision;
	TerminalCache.Add(Key, Result);
	TerminalPayloadByKey.Add(Key, PayloadSignature);
	return Result;
}

// 背包存入公共仓库流程：
// 1. 用 RequestId 和双方版本/槽位做幂等签名；同请求重放只返回首次终态，不重复移动任何格子。
// 2. 正式库存可用时把玩家正式库存作为源、营地正式库存作为目标，Equipment 只作为可选旧读模型刷新者。
// 3. 目标营地格有不同物品时，把玩家将收到的定义传给旧投影刷新，保留旧钓具自动选择体验。
// 4. 正式交换失败或可选投影刷新失败时由共享 helper 回滚双方正式 entries 和已存在的旧快照；缺正式库存组件时直接拒绝。
// 5. 成功后公共仓库公开 Revision 由营地推进，玩家 Inventory/Equipment 的版本变化由玩家侧组件自己的发布入口维护。
FCatDomainCommandResult ACatCampInventoryActor::DepositFromInventorySlotFromAuthority(const FGuid RequestId,
	const int64 ExpectedCampRevision, const int32 TargetCampSlotIndex, UCatInventoryComponent* SourceInventory,
	const int64 ExpectedInventoryRevision, const int32 SourceInventorySlotIndex,
	UCatEquipmentComponent* LegacyProjectionEquipment, const bool bAllowLegacyProjectionRevisionFallback)
{

	FCatInventoryMutationScope CampMutation(InventoryComponent);
	FCatInventoryMutationScope PlayerMutation(SourceInventory);
	const bool bWasDeferring = bDeferringPublication;
	const bool bEquipmentWasDeferring = LegacyProjectionEquipment && LegacyProjectionEquipment->bDeferringSnapshotPublication;
	bDeferringPublication = true;
	if (LegacyProjectionEquipment) LegacyProjectionEquipment->bDeferringSnapshotPublication = true;
	ON_SCOPE_EXIT
	{
		bDeferringPublication = bWasDeferring;
		if (LegacyProjectionEquipment)
		{
			LegacyProjectionEquipment->bDeferringSnapshotPublication = bEquipmentWasDeferring;
			if (!bEquipmentWasDeferring && LegacyProjectionEquipment->bSnapshotPublicationPending)
			{
				LegacyProjectionEquipment->bSnapshotPublicationPending = false;
				LegacyProjectionEquipment->PublishSnapshot();
			}
		}
		if (!bWasDeferring && bPublicationPending)
		{
			bPublicationPending = false;
			PublishSnapshot();
		}
	};
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString Key = MakeTerminalKey(TEXT("DepositFromInventorySlot"), GetPathNameSafe(SourceInventory), RequestId);
	const FString PayloadSignature = FString::Printf(
		TEXT("ExpectedCamp=%lld|TargetCamp=%d|ExpectedInventory=%lld|SourceInventory=%s|SourceSlot=%d"),
		ExpectedCampRevision, TargetCampSlotIndex, ExpectedInventoryRevision,
		*GetPathNameSafe(SourceInventory), SourceInventorySlotIndex);
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		const FString* CachedPayload = TerminalPayloadByKey.Find(Key);
		if (!CachedPayload || *CachedPayload != PayloadSignature)
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
			Result.Revision = Snapshot.Revision;
			return Result;
		}
		Result = *Cached;
		MarkCommandReplayed(Result);
		return Result;
	}

	if (SourceInventory && InventoryComponent)
	{
		const FCatInventoryEntry* SourceEntry =
			SourceInventory->GetInventoryEntryAtSlot(SourceInventorySlotIndex);
		const FCatInventoryEntry* CampTargetEntry =
			InventoryComponent->GetInventoryEntryAtSlot(TargetCampSlotIndex);
		const UCatInventoryItemDefinition* SourceDefinition =
			SourceEntry && SourceEntry->Instance ? SourceEntry->Instance->GetItemDefinition() : nullptr;
		const UCatInventoryItemDefinition* CampTargetDefinition =
			CampTargetEntry && CampTargetEntry->Instance ? CampTargetEntry->Instance->GetItemDefinition() : nullptr;
		const FName SourceDefinitionId =
			SourceDefinition ? SourceDefinition->GetInventoryDefinitionId() : NAME_None;
		const FName EquipmentReceivedDefinitionId =
			CampTargetDefinition ? CampTargetDefinition->GetInventoryDefinitionId() : NAME_None;
		const UCatEquipmentDefinition* EquipmentReceivedDefinition =
			Cast<UCatEquipmentDefinition>(CampTargetDefinition);
		const bool bEquipmentReceivesCampSlot = EquipmentReceivedDefinition != nullptr
			&& !EquipmentReceivedDefinitionId.IsNone()
			&& EquipmentReceivedDefinitionId != SourceDefinitionId;
		Result = ExecuteFormalPlayerCampSlotExchangeFromAuthority(RequestId, ExpectedCampRevision,
			SourceInventory, ExpectedInventoryRevision, LegacyProjectionEquipment,
			SourceInventory, SourceInventorySlotIndex, InventoryComponent, TargetCampSlotIndex,
			bEquipmentReceivesCampSlot ? EquipmentReceivedDefinition : nullptr,
			bEquipmentReceivesCampSlot ? EquipmentReceivedDefinitionId : NAME_None,
			bAllowLegacyProjectionRevisionFallback);
		TerminalCache.Add(Key, Result);
		TerminalPayloadByKey.Add(Key, PayloadSignature);
		return Result;
	}

	Result.Error = ECatDomainCommandError::DependencyUnavailable;
	Result.Revision = Snapshot.Revision;
	TerminalCache.Add(Key, Result);
	TerminalPayloadByKey.Add(Key, PayloadSignature);
	return Result;
}

// 旧背包存入公共仓库流程：旧函数只从 Equipment Owner 解析玩家正式库存并转发；传入的旧槽位下标被当作正式库存下标兼容，不再让 Equipment 写库存。
FCatDomainCommandResult ACatCampInventoryActor::DepositFromEquipmentSlotFromAuthority(const FGuid RequestId,
	const int64 ExpectedCampRevision, const int32 TargetCampSlotIndex, UCatEquipmentComponent* SourceEquipment,
	const int64 ExpectedEquipmentRevision, const int32 SourceEquipmentSlotIndex)
{
	FCatInventoryTransferRequest Request;
	Request.RequestId = RequestId;
	Request.Initiator = SourceEquipment ? SourceEquipment->GetOwner() : nullptr;
	Request.Source.Host = SourceEquipment;
	Request.Target.Host = this;
	Request.ExpectedSourceRevision = ExpectedEquipmentRevision;
	Request.ExpectedTargetRevision = ExpectedCampRevision;
	Request.SourceSlotIndex = SourceEquipmentSlotIndex;
	Request.TargetSlotIndex = TargetCampSlotIndex;
	Request.Mode = ECatInventoryTransferMode::DragToSlot;
	UCatInventoryTransferService* Transfers = GetWorld() ? GetWorld()->GetSubsystem<UCatInventoryTransferService>() : nullptr;
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	Result.Revision = Snapshot.Revision;
	if (!Transfers) { Result.Error = ECatDomainCommandError::DependencyUnavailable; return Result; }
	const FCatInventoryTransferResult Transferred = Transfers->TransferFromAuthority(Request);
	Result.bCommitted = Transferred.bCommitted;
	Result.Error = Transferred.Error;
	Result.Revision = Transferred.TargetRevision;
	return Result;
}

// 公共仓库拖入背包流程：
// 1. 用 RequestId 和双方版本/槽位做幂等签名；重放只返回首次终态，不重复扣公共仓库或发背包。
// 2. 正式库存可用时把营地正式库存作为源、玩家正式库存作为目标，客户端目标格仍只是候选输入。
// 3. 公共仓库源物会作为玩家收到的新增定义传给旧投影刷新，让拖入背包后钓具选择仍保持旧规则体验。
// 4. 正式交换失败或可选投影刷新失败时由共享 helper 回滚双方正式 entries 和已存在的旧快照；缺正式库存组件时直接拒绝。
// 5. 成功后营地只推进自己的公开 Revision，玩家随身正式库存和 Equipment 投影由玩家组件发布各自变化。
FCatDomainCommandResult ACatCampInventoryActor::WithdrawToInventorySlotFromAuthority(const FGuid RequestId,
	const int64 ExpectedCampRevision, const int32 SourceCampSlotIndex, UCatInventoryComponent* TargetInventory,
	const int64 ExpectedInventoryRevision, const int32 TargetInventorySlotIndex,
	UCatEquipmentComponent* LegacyProjectionEquipment, const bool bAllowLegacyProjectionRevisionFallback)
{

	FCatInventoryMutationScope CampMutation(InventoryComponent);
	FCatInventoryMutationScope PlayerMutation(TargetInventory);
	const bool bWasDeferring = bDeferringPublication;
	const bool bEquipmentWasDeferring = LegacyProjectionEquipment && LegacyProjectionEquipment->bDeferringSnapshotPublication;
	bDeferringPublication = true;
	if (LegacyProjectionEquipment) LegacyProjectionEquipment->bDeferringSnapshotPublication = true;
	ON_SCOPE_EXIT
	{
		bDeferringPublication = bWasDeferring;
		if (LegacyProjectionEquipment)
		{
			LegacyProjectionEquipment->bDeferringSnapshotPublication = bEquipmentWasDeferring;
			if (!bEquipmentWasDeferring && LegacyProjectionEquipment->bSnapshotPublicationPending)
			{
				LegacyProjectionEquipment->bSnapshotPublicationPending = false;
				LegacyProjectionEquipment->PublishSnapshot();
			}
		}
		if (!bWasDeferring && bPublicationPending)
		{
			bPublicationPending = false;
			PublishSnapshot();
		}
	};
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString Key = MakeTerminalKey(TEXT("WithdrawToInventorySlot"), GetPathNameSafe(TargetInventory), RequestId);
	const FString PayloadSignature = FString::Printf(
		TEXT("ExpectedCamp=%lld|SourceCamp=%d|ExpectedInventory=%lld|TargetInventory=%s|TargetSlot=%d"),
		ExpectedCampRevision, SourceCampSlotIndex, ExpectedInventoryRevision,
		*GetPathNameSafe(TargetInventory), TargetInventorySlotIndex);
	if (const FCatDomainCommandResult* Cached = TerminalCache.Find(Key))
	{
		const FString* CachedPayload = TerminalPayloadByKey.Find(Key);
		if (!CachedPayload || *CachedPayload != PayloadSignature)
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
			Result.Revision = Snapshot.Revision;
			return Result;
		}
		Result = *Cached;
		MarkCommandReplayed(Result);
		return Result;
	}

	if (InventoryComponent && TargetInventory)
	{
		const FCatInventoryEntry* CampSourceEntry =
			InventoryComponent->GetInventoryEntryAtSlot(SourceCampSlotIndex);
		const UCatInventoryItemDefinition* CampSourceDefinition =
			CampSourceEntry && CampSourceEntry->Instance ? CampSourceEntry->Instance->GetItemDefinition() : nullptr;
		const UCatEquipmentDefinition* GrantedDefinition = Cast<UCatEquipmentDefinition>(CampSourceDefinition);
		const FName GrantedDefinitionId =
			CampSourceDefinition ? CampSourceDefinition->GetInventoryDefinitionId() : NAME_None;
		Result = ExecuteFormalPlayerCampSlotExchangeFromAuthority(RequestId, ExpectedCampRevision,
			TargetInventory, ExpectedInventoryRevision, LegacyProjectionEquipment,
			InventoryComponent, SourceCampSlotIndex, TargetInventory, TargetInventorySlotIndex,
			GrantedDefinition, GrantedDefinitionId, bAllowLegacyProjectionRevisionFallback);
		TerminalCache.Add(Key, Result);
		TerminalPayloadByKey.Add(Key, PayloadSignature);
		return Result;
	}

	Result.Error = ECatDomainCommandError::DependencyUnavailable;
	Result.Revision = Snapshot.Revision;
	TerminalCache.Add(Key, Result);
	TerminalPayloadByKey.Add(Key, PayloadSignature);
	return Result;
}

// 旧公共仓库拖入背包流程：旧函数只从 Equipment Owner 解析目标正式库存并转发；传入的旧槽位下标被当作正式库存下标兼容，不再让 Equipment 写库存。
FCatDomainCommandResult ACatCampInventoryActor::WithdrawToEquipmentSlotFromAuthority(const FGuid RequestId,
	const int64 ExpectedCampRevision, const int32 SourceCampSlotIndex, UCatEquipmentComponent* TargetEquipment,
	const int64 ExpectedEquipmentRevision, const int32 TargetEquipmentSlotIndex)
{
	FCatInventoryTransferRequest Request;
	Request.RequestId = RequestId;
	Request.Initiator = TargetEquipment ? TargetEquipment->GetOwner() : nullptr;
	Request.Source.Host = this;
	Request.Target.Host = TargetEquipment;
	Request.ExpectedSourceRevision = ExpectedCampRevision;
	Request.ExpectedTargetRevision = ExpectedEquipmentRevision;
	Request.SourceSlotIndex = SourceCampSlotIndex;
	Request.TargetSlotIndex = TargetEquipmentSlotIndex;
	Request.Mode = ECatInventoryTransferMode::DragToSlot;
	UCatInventoryTransferService* Transfers = GetWorld() ? GetWorld()->GetSubsystem<UCatInventoryTransferService>() : nullptr;
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	Result.Revision = Snapshot.Revision;
	if (!Transfers) { Result.Error = ECatDomainCommandError::DependencyUnavailable; return Result; }
	const FCatInventoryTransferResult Transferred = Transfers->TransferFromAuthority(Request);
	Result.bCommitted = Transferred.bCommitted;
	Result.Error = Transferred.Error;
	Result.Revision = Transferred.SourceRevision;
	return Result;
}

// 复制回调流程：客户端拿到服务器公共仓库快照后广播读模型变化；提交、扣减和取用仍只能回服务器。
void ACatCampInventoryActor::OnRep_Snapshot()
{
	OnSnapshotChanged.Broadcast();
}

// 容量读取流程：公共仓库容量来自 Actor 配置，负值运行时夹到 0；0 表示仓库未配置，所有入库都会拒绝。
int32 ACatCampInventoryActor::GetConfiguredSlotCapacity() const
{
	return FMath::Max(0, InventorySlotCapacity);
}

// 堆叠上限流程：定义资产统一回答单格上限；公共仓库只消费库存定义规则，不重复解释全局堆叠配置。
int32 ACatCampInventoryActor::GetInventoryStackLimit(const UCatEquipmentDefinition& Definition) const
{
	return Definition.GetMaxStackCount();
}

// 旧仓库定义解析流程：正式目录已经把“物品是什么”收拢到库存定义；旧 Snapshot 还需要装备字段时，只在这里做一次类型适配和历史回退。
UCatEquipmentDefinition* ACatCampInventoryActor::ResolveEquipmentDefinitionForLegacyInventory(
	const FName DefinitionId) const
{
	if (DefinitionId.IsNone())
	{
		return nullptr;
	}

	const UCatInventorySettings* InventorySettings = GetDefault<UCatInventorySettings>();
	UCatInventoryItemDefinition* InventoryDefinition =
		InventorySettings ? InventorySettings->FindRuntimeDefinition(DefinitionId) : nullptr;
	if (UCatEquipmentDefinition* EquipmentDefinition = Cast<UCatEquipmentDefinition>(InventoryDefinition))
	{
		return EquipmentDefinition;
	}

	const UCatEquipmentSettings* EquipmentSettings = GetDefault<UCatEquipmentSettings>();
	return EquipmentSettings ? EquipmentSettings->FindRuntimeDefinition(DefinitionId) : nullptr;
}

// 正式批次构建流程：旧请求只给 DefinitionId 和数量；这里统一解析成库存定义资产，让容量预检和正式写入都走新库存组件。
bool ACatCampInventoryActor::BuildFormalReceiveBatchForLegacyInventory(
	const TArray<FCatCampInventoryAddItemRequest>& Items, FCatInventoryReceiveBatch& OutReceiveBatch) const
{
	OutReceiveBatch = FCatInventoryReceiveBatch();
	if (Items.IsEmpty())
	{
		return false;
	}

	OutReceiveBatch.DefinitionEntries.Reserve(Items.Num());
	for (const FCatCampInventoryAddItemRequest& Item : Items)
	{
		UCatEquipmentDefinition* Definition = ResolveEquipmentDefinitionForLegacyInventory(Item.DefinitionId);
		if (Definition == nullptr || Item.Quantity <= 0)
		{
			OutReceiveBatch = FCatInventoryReceiveBatch();
			return false;
		}

		FCatInventoryDefinitionEntry& DefinitionEntry = OutReceiveBatch.DefinitionEntries.AddDefaulted_GetRef();
		DefinitionEntry.ItemDefinition = Definition;
		DefinitionEntry.Count = Item.Quantity;
	}

	return !OutReceiveBatch.IsEmpty();
}

// 旧快照恢复建模流程：
// 1. 按旧槽位顺序创建正式库存 entries，空槽保留为空 entry，不让读档改变玩家整理过的位置。
// 2. 每个占用槽都恢复定义资产、实例 ID、堆叠数量和鱼竿状态，让后续商店发货可以继续从正式库存追加。
// 3. 任一旧槽无法投影成正式装备实例时整体失败，调用方因此不会写入半份新库存。
bool ACatCampInventoryActor::BuildFormalEntriesFromLegacySnapshot(
	const FCatCampInventorySnapshot& SourceSnapshot, TArray<FCatInventoryEntry>& OutEntries)
{
	OutEntries.Reset();
	if (!InventoryComponent)
	{
		return false;
	}

	OutEntries.Reserve(SourceSnapshot.InventorySlots.Num());
	for (const FCatRunInventorySlot& Slot : SourceSnapshot.InventorySlots)
	{
		FCatInventoryEntry& Entry = OutEntries.AddDefaulted_GetRef();
		Entry = FCatInventoryEntry(InventoryComponent);
		if (!CatRunInventorySlotOperations::IsInventorySlotOccupied(Slot))
		{
			continue;
		}

		UCatEquipmentDefinition* Definition = ResolveEquipmentDefinitionForLegacyInventory(Slot.DefinitionId);
		const TSubclassOf<UCatInventoryItemInstance> InstanceClass =
			UCatInventoryItemDefinition::ResolveItemInstanceClass(Definition);
		if (Definition == nullptr || InstanceClass == nullptr || Slot.Quantity <= 0 || !Slot.ItemInstanceId.IsValid())
		{
			OutEntries.Reset();
			return false;
		}

		UCatInventoryItemInstance* Instance = NewObject<UCatInventoryItemInstance>(this, InstanceClass);
		if (Instance == nullptr)
		{
			OutEntries.Reset();
			return false;
		}
		Instance->SetItemDefinition(Definition);
		Instance->SetItemInstanceIdFromAuthority(Slot.ItemInstanceId);
		Instance->SetRuntimeOwnerActor(this);
		if (UCatEquipmentInventoryItemInstance* EquipmentInstance =
			Cast<UCatEquipmentInventoryItemInstance>(Instance))
		{
			EquipmentInstance->SetRodRuntimeStateFromAuthority(Slot.RodDurability, Slot.bRodBroken);
		}

		Entry.Instance = Instance;
		Entry.StackCount = Slot.Quantity;
		Entry.LastObservedCount = Slot.Quantity;
		Entry.SlotOwnerComponent = InventoryComponent;
	}

	return true;
}

// 整批容量预检流程：把旧发货载荷转成正式库存批次后交给 InventoryComponent 模拟，保证扣款前检查和提交使用同一套堆叠规则。
bool ACatCampInventoryActor::CanStoreItems(const TArray<FCatCampInventoryAddItemRequest>& Items) const
{
	FCatInventoryReceiveBatch ReceiveBatch;
	return InventoryComponent
		&& BuildFormalReceiveBatchForLegacyInventory(Items, ReceiveBatch)
		&& InventoryComponent->CanFullyAcceptInventoryBatch(ReceiveBatch, GetConfiguredSlotCapacity());
}

// 旧快照投影流程：正式库存组件是公共仓库写事实；迁移期把它重建成旧槽位数组，供存档和旧只读消费者读取。
bool ACatCampInventoryActor::SyncLegacySnapshotFromInventoryComponent()
{
	if (!InventoryComponent)
	{
		return false;
	}

	const TArray<FCatInventoryEntry> FormalEntries = InventoryComponent->GetInventoryEntries();
	const int32 SnapshotSlotCount = FMath::Max(GetConfiguredSlotCapacity(), FormalEntries.Num());
	TArray<FCatRunInventorySlot> ProjectedSlots;
	ProjectedSlots.SetNum(SnapshotSlotCount);
	for (int32 SlotIndex = 0; SlotIndex < FormalEntries.Num(); ++SlotIndex)
	{
		const FCatInventoryEntry& Entry = FormalEntries[SlotIndex];
		if (Entry.Instance == nullptr || Entry.StackCount <= 0)
		{
			continue;
		}

		const UCatEquipmentInventoryItemInstance* EquipmentInstance =
			Cast<UCatEquipmentInventoryItemInstance>(Entry.Instance);
		if (EquipmentInstance == nullptr
			|| !EquipmentInstance->BuildLegacyRunInventorySlot(Entry.StackCount, ProjectedSlots[SlotIndex]))
		{
			return false;
		}
	}

	Snapshot.InventorySlots = MoveTemp(ProjectedSlots);
	return true;
}

// 玩家正式库存解析流程：从 Equipment 的 Owner 取 InventoryComponent；Equipment 只提供兼容投影入口，不再直接承担背包格事实。
UCatInventoryComponent* ACatCampInventoryActor::ResolveFormalInventoryFromEquipment(
	UCatEquipmentComponent* Equipment) const
{
	AActor* EquipmentOwner = Equipment ? Equipment->GetOwner() : nullptr;
	return EquipmentOwner ? EquipmentOwner->FindComponentByClass<UCatInventoryComponent>() : nullptr;
}

// 旧视图同步流程：
// 1. 先从营地正式库存重建公共仓库 Snapshot，保证存档和迁移期旧只读消费者看到正式提交后的格位。
// 2. 再刷新玩家 Equipment 旧投影；传入新增定义时允许它按旧规则修正钓具选择。
// 3. 任一投影失败都返回 false，调用方负责回滚正式库存，避免旧读者和正式事实分叉。
bool ACatCampInventoryActor::SyncLegacyViewsFromFormalInventories(UCatEquipmentComponent* Equipment,
	const UCatEquipmentDefinition* GrantedDefinition, const FName GrantedDefinitionId)
{
	if (!SyncLegacySnapshotFromInventoryComponent())
	{
		return false;
	}

	return Equipment == nullptr
		|| Equipment->RefreshInventoryProjectionFromInventoryComponentFromAuthority(
			GrantedDefinition, GrantedDefinitionId);
}

// 玩家版本兼容流程：
// 1. 正式 UI 提交 InventoryComponent 的内容版本时直接放行，这是随身库存新的并发事实。
// 2. 旧测试和过渡期内部调用必须同时传入 Equipment 投影并显式开启兼容开关，才允许用旧 Snapshot 版本回退。
// 3. 两个版本都不匹配才视为陈旧请求，调用方保持无副作用返回 RevisionConflict。
bool ACatCampInventoryActor::DoesPlayerInventoryRevisionMatch(const UCatInventoryComponent* PlayerInventory,
	const int64 ExpectedInventoryRevision, const UCatEquipmentComponent* LegacyProjectionEquipment,
	const bool bAllowLegacyProjectionRevisionFallback) const
{
	if (PlayerInventory != nullptr && PlayerInventory->GetInventoryRevision() == ExpectedInventoryRevision)
	{
		return true;
	}
	return bAllowLegacyProjectionRevisionFallback && LegacyProjectionEquipment != nullptr
		&& LegacyProjectionEquipment->Snapshot.Revision == ExpectedInventoryRevision;
}

// 玩家/营地正式格交换流程：
// 1. 先验证营地公开版本、玩家正式库存版本和双方 authority；客户端提交的下标只作为候选。
// 2. 再按两边配置补齐正式库存空格，避免旧快照有容量而正式 FastArray 尚未初始化时误拒绝。
// 3. 然后确认源格确有装备/耗材定义，目标非空时也必须能投影回旧读模型，防止正式库存和兼容读模型分叉。
// 4. 正式交换成功后刷新营地 Snapshot，并在传入旧 Equipment 时同步玩家投影；刷新失败时恢复两份正式 entries 和已存在的旧快照。
// 5. 最后只推进营地公开 Revision；玩家 Inventory 的 Revision 由库存组件推进，旧投影 Revision 由 Equipment 自己维护。
FCatDomainCommandResult ACatCampInventoryActor::ExecuteFormalPlayerCampSlotExchangeFromAuthority(
	const FGuid RequestId, const int64 ExpectedCampRevision, UCatInventoryComponent* PlayerInventory,
	const int64 ExpectedInventoryRevision, UCatEquipmentComponent* LegacyProjectionEquipment,
	UCatInventoryComponent* SourceInventory, const int32 SourceSlotIndex,
	UCatInventoryComponent* TargetInventory, const int32 TargetSlotIndex,
	const UCatEquipmentDefinition* GrantedDefinition, const FName GrantedDefinitionId,
	const bool bAllowLegacyProjectionRevisionFallback)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	AActor* PlayerInventoryOwner = PlayerInventory ? PlayerInventory->GetOwner() : nullptr;
	if (!HasAuthority() || !RequestId.IsValid() || !InventoryComponent || !PlayerInventoryOwner
		|| !PlayerInventoryOwner->HasAuthority() || !PlayerInventory || !SourceInventory || !TargetInventory
		|| SourceSlotIndex < 0 || TargetSlotIndex < 0)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		Result.Revision = Snapshot.Revision;
		return Result;
	}
	if (Snapshot.Revision != ExpectedCampRevision
		|| !DoesPlayerInventoryRevisionMatch(PlayerInventory, ExpectedInventoryRevision, LegacyProjectionEquipment,
			bAllowLegacyProjectionRevisionFallback))
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
		Result.Revision = Snapshot.Revision;
		return Result;
	}
	// 这里属于迁移期桥接：外部仍按旧 Snapshot 看到容量，正式库存写入前必须把空格补齐到同一容量口径。
	InventoryComponent->SetInventorySlotCountFromAuthority(GetConfiguredSlotCapacity());
	const int32 PlayerMinimumSlotCount = LegacyProjectionEquipment
		? LegacyProjectionEquipment->GetConfiguredInventorySlotCapacity()
		: PlayerInventory->GetInventorySlotCount();
	PlayerInventory->SetInventorySlotCountFromAuthority(PlayerMinimumSlotCount);
	if (!SourceInventory->IsValidInventorySlotIndex(SourceSlotIndex)
		|| !TargetInventory->IsValidInventorySlotIndex(TargetSlotIndex))
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		Result.Revision = Snapshot.Revision;
		return Result;
	}

	const FCatInventoryEntry* SourceEntry = SourceInventory->GetInventoryEntryAtSlot(SourceSlotIndex);
	const FCatInventoryEntry* TargetEntry = TargetInventory->GetInventoryEntryAtSlot(TargetSlotIndex);
	UCatInventoryItemDefinition* SourceDefinition =
		SourceEntry && SourceEntry->Instance ? SourceEntry->Instance->GetItemDefinition() : nullptr;
	UCatInventoryItemDefinition* TargetDefinition =
		TargetEntry && TargetEntry->Instance ? TargetEntry->Instance->GetItemDefinition() : nullptr;
	if (SourceEntry == nullptr || SourceEntry->Instance == nullptr || SourceEntry->StackCount <= 0
		|| Cast<UCatEquipmentDefinition>(SourceDefinition) == nullptr)
	{
		Result.Error = ECatDomainCommandError::NotFound;
		Result.Revision = Snapshot.Revision;
		return Result;
	}
	if (TargetEntry != nullptr && TargetEntry->Instance != nullptr && TargetEntry->StackCount > 0
		&& Cast<UCatEquipmentDefinition>(TargetDefinition) == nullptr)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		Result.Revision = Snapshot.Revision;
		return Result;
	}
	if (!UCatInventoryComponent::CanExecuteExchangeRequest(
		SourceInventory, SourceSlotIndex, TargetInventory, TargetSlotIndex))
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		Result.Revision = Snapshot.Revision;
		return Result;
	}

	const TArray<FCatInventoryEntry> SavedCampEntries = InventoryComponent->GetInventoryEntries();
	const TArray<FCatInventoryEntry> SavedPlayerEntries = PlayerInventory->GetInventoryEntries();
	const FCatCampInventorySnapshot SavedCampSnapshot = Snapshot;
	const FCatEquipmentLoadoutSnapshot SavedEquipmentSnapshot = LegacyProjectionEquipment
		? LegacyProjectionEquipment->Snapshot : FCatEquipmentLoadoutSnapshot();
	const bool bFormalChanged = UCatInventoryComponent::ExecuteExchangeRequestOnAuthority(
		SourceInventory, SourceSlotIndex, TargetInventory, TargetSlotIndex);
	if (!bFormalChanged)
	{
		Result.Error = ECatDomainCommandError::AlreadyResolved;
		Result.Revision = Snapshot.Revision;
		return Result;
	}
	if (!SyncLegacyViewsFromFormalInventories(LegacyProjectionEquipment, GrantedDefinition, GrantedDefinitionId))
	{
		InventoryComponent->ReplaceInventoryEntriesFromAuthority(SavedCampEntries, GetConfiguredSlotCapacity());
		PlayerInventory->ReplaceInventoryEntriesFromAuthority(SavedPlayerEntries, PlayerMinimumSlotCount);
		Snapshot = SavedCampSnapshot;
		if (LegacyProjectionEquipment)
		{
			LegacyProjectionEquipment->Snapshot = SavedEquipmentSnapshot;
		}
		Result.Error = ECatDomainCommandError::DependencyUnavailable;
		Result.Revision = Snapshot.Revision;
		return Result;
	}

	++Snapshot.Revision;
	PublishSnapshot();
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	Result.Revision = Snapshot.Revision;
	return Result;
}

// 页面类解析流程：同步加载营地仓库自身配置的库存 View，并确认它就是营地仓库页面类型；错配普通背包页时返回空，交互打开链路会记录拒绝。
TSubclassOf<UCatCampInventoryWidget> ACatCampInventoryActor::LoadInventoryViewClass() const
{
	UClass* LoadedClass = InventoryViewClass.LoadSynchronous();
	if (!LoadedClass || !LoadedClass->IsChildOf(UCatCampInventoryWidget::StaticClass()))
	{
		return nullptr;
	}
	return LoadedClass;
}

// 槽位补齐流程：只追加配置容量内缺失的空槽，不删除多余已有槽位；容量被调小时已有物品仍可显示和取走。
void ACatCampInventoryActor::EnsureInventorySlotArray()
{
	const int32 SlotCapacity = GetConfiguredSlotCapacity();
	if (Snapshot.InventorySlots.Num() < SlotCapacity)
	{
		Snapshot.InventorySlots.AddDefaulted(SlotCapacity - Snapshot.InventorySlots.Num());
	}
}

// 整批签名流程：
// 1. 先合并重复 DefinitionId 并拒绝空定义、非正数量和 int32 溢出。
// 2. 再按 DefinitionId 排序生成稳定载荷字符串，让同一批货不受购物车行顺序影响。
// 3. 签名故意不包含 ExpectedRevision：首轮提交仍检查版本，成功后的重放则应取回既有回执而不是被新版本挡住。
bool ACatCampInventoryActor::BuildAddItemsPayloadSignature(
	const TArray<FCatCampInventoryAddItemRequest>& Items, FString& OutPayloadSignature,
	TArray<FCatCampInventoryAddItemRequest>& OutNormalizedItems) const
{
	OutPayloadSignature.Reset();
	OutNormalizedItems.Reset();
	TMap<FName, int32> QuantitiesByDefinitionId;
	for (const FCatCampInventoryAddItemRequest& Item : Items)
	{
		if (Item.DefinitionId.IsNone() || Item.Quantity <= 0)
		{
			return false;
		}
		int32& Quantity = QuantitiesByDefinitionId.FindOrAdd(Item.DefinitionId);
		if (Item.Quantity > MAX_int32 - Quantity)
		{
			OutNormalizedItems.Reset();
			return false;
		}
		Quantity += Item.Quantity;
	}
	for (const TPair<FName, int32>& Pair : QuantitiesByDefinitionId)
	{
		FCatCampInventoryAddItemRequest& NormalizedItem = OutNormalizedItems.AddDefaulted_GetRef();
		NormalizedItem.DefinitionId = Pair.Key;
		NormalizedItem.Quantity = Pair.Value;
	}
	OutNormalizedItems.Sort([](const FCatCampInventoryAddItemRequest& Left,
		const FCatCampInventoryAddItemRequest& Right)
	{
		return Left.DefinitionId.ToString() < Right.DefinitionId.ToString();
	});
	if (OutNormalizedItems.IsEmpty())
	{
		return false;
	}
	TArray<FString> Parts;
	Parts.Reserve(OutNormalizedItems.Num());
	for (const FCatCampInventoryAddItemRequest& Item : OutNormalizedItems)
	{
		Parts.Add(FString::Printf(TEXT("%s:%d"), *Item.DefinitionId.ToString(), Item.Quantity));
	}
	OutPayloadSignature = FString::Join(Parts, TEXT(","));
	return true;
}

// 发布流程：服务器提交后请求复制并广播本机读模型变化；客户端复制回调只走 OnRep_Snapshot。
void ACatCampInventoryActor::PublishSnapshot()
{
	if (bDeferringPublication)
	{
		bPublicationPending = true;
		return;
	}
	ForceNetUpdate();
	OnSnapshotChanged.Broadcast();
}

// 幂等键流程：公共仓库按 Actor 生命周期隔离缓存，操作名和 RequestId 共同决定一条命令的唯一终态。
FString ACatCampInventoryActor::MakeTerminalKey(const TCHAR* Operation, const FGuid RequestId)
{
	return FString::Printf(TEXT("%s|%s"), Operation, *RequestId.ToString(EGuidFormats::DigitsWithHyphens));
}

// 身份幂等键流程：公共仓库是共享 Actor，但购物车发货来自某个服务器身份的订单；身份进 key 后，不同玩家同 RequestId 不会共享发货终态。
FString ACatCampInventoryActor::MakeTerminalKey(const TCHAR* Operation, const FString& StableNetId,
	const FGuid RequestId)
{
	return FString::Printf(TEXT("%s|%s|%s"), Operation, *StableNetId,
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
}

ECatDomainCommandError ACatCampInventoryActor::ReadInventoryTransferEndpoint(const FName Channel,
	const FGuid EntryId, FCatInventoryEndpointSnapshot& OutSnapshot) const
{
	OutSnapshot = FCatInventoryEndpointSnapshot{};
	OutSnapshot.Revision = Snapshot.Revision;
	if (Channel != TEXT("Stored") || EntryId.IsValid()) return ECatDomainCommandError::InvalidPayload;
	if (!InventoryComponent) return ECatDomainCommandError::DependencyUnavailable;
	const TArray<FCatInventoryEntry> Entries = InventoryComponent->GetInventoryEntries();
	OutSnapshot.Capacity = FMath::Max(GetConfiguredSlotCapacity(), Entries.Num());
	OutSnapshot.Slots.SetNum(OutSnapshot.Capacity);
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		if (!Entries[Index].Instance || Entries[Index].StackCount <= 0) continue;
		const UCatEquipmentInventoryItemInstance* Instance = Cast<UCatEquipmentInventoryItemInstance>(Entries[Index].Instance);
		if (!Instance || !Instance->BuildLegacyRunInventorySlot(Entries[Index].StackCount, OutSnapshot.Slots[Index]))
			return ECatDomainCommandError::InvalidPayload;
	}
	return ECatDomainCommandError::None;
}

int32 ACatCampInventoryActor::GetInventoryTransferStackLimit(const FName DefinitionId) const
{
	const UCatEquipmentDefinition* Definition = ResolveEquipmentDefinitionForLegacyInventory(DefinitionId);
	return Definition ? GetInventoryStackLimit(*Definition) : 0;
}

void ACatCampInventoryActor::ApplyInventoryTransferWritesSilently(
	const TConstArrayView<FCatInventoryEndpointWrite> Writes, const int64 NewRevision)
{
	for (const FCatInventoryEndpointWrite& Write : Writes)
	{
		InventoryComponent->ReplaceInventoryEntriesFromAuthority(Write.FormalEntries, Write.Slots.Num());
	}
	SyncLegacySnapshotFromInventoryComponent();
	Snapshot.Revision = NewRevision;
}

void ACatCampInventoryActor::PublishInventoryTransfer()
{
	PublishSnapshot();
}

bool ACatCampInventoryActor::AddPreparedBatchToFormalInventory(const FCatInventoryReceiveBatch& ReceiveBatch)
{
	if (!InventoryComponent || !InventoryComponent->CanFullyAcceptInventoryBatch(ReceiveBatch, GetConfiguredSlotCapacity())) return false;
	FCatInventoryMutationScope Mutation(InventoryComponent);
	InventoryComponent->SetInventorySlotCountFromAuthority(GetConfiguredSlotCapacity());
	return InventoryComponent->TryAddInventoryBatch(ReceiveBatch);
}
