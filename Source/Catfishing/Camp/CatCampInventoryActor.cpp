#include "Camp/CatCampInventoryActor.h"

#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/LocalPlayer.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Equipment/CatRunInventorySlotOperations.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/CatInteractionSettings.h"
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

// 容量读取流程：返回公共仓库当前配置容量的安全值；UI 用它展示空格，提交逻辑仍由服务器重新检查容量和版本。
int32 ACatCampInventoryActor::GetInventorySlotCapacityForView() const
{
	return GetConfiguredSlotCapacity();
}

// 入库预检流程：
// 1. 先读取装备定义并验证 RequestId、authority、定义和数量；失败不改公共仓库格子。
// 2. 已有同 RequestId 终态时先比对版本、定义和数量签名，只有同一批货才放行合法重放。
// 3. 首次预检要求调用方看到的仓库版本仍是当前版本，再按公共仓库容量和堆叠规则判断整批物品能否一次放完。
ECatDomainCommandError ACatCampInventoryActor::ValidateAddItemFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const FName DefinitionId, const int32 Quantity) const
{
	const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
	const UCatEquipmentDefinition* Definition = Settings ? Settings->FindRuntimeDefinition(DefinitionId) : nullptr;
	if (!HasAuthority() || !RequestId.IsValid() || !Definition || Quantity <= 0)
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
	return CanStoreItem(*Definition, DefinitionId, Quantity)
		? ECatDomainCommandError::None : ECatDomainCommandError::CapacityExceeded;
}

// 入库提交流程：
// 1. 先用 RequestId 和载荷签名处理幂等重放，防止同一次购买换 DefinitionId 或数量。
// 2. 首次提交复用扣款前预检，再检查公共仓库 Revision 是否仍匹配调用方看到的事实。
// 3. 通过后写入公共仓库格子、递增 Revision、复制并缓存终态；失败只返回当前仓库版本。
FCatDomainCommandResult ACatCampInventoryActor::AddItemFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const FName DefinitionId, const int32 Quantity)
{
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

	const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
	UCatEquipmentDefinition* Definition = Settings ? Settings->FindRuntimeDefinition(DefinitionId) : nullptr;
	const ECatDomainCommandError Rejection =
		ValidateAddItemFromAuthority(RequestId, ExpectedRevision, DefinitionId, Quantity);
	if (Rejection != ECatDomainCommandError::None)
	{
		Result.Error = Rejection;
	}
	else if (Definition && AddItemQuantity(*Definition, DefinitionId, Quantity))
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
// 3. 首次预检要求仓库版本仍匹配，然后用临时格子数组模拟整批入库，任何一行放不下都拒绝整批。
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
// 2. 首次提交复用整批预检，再把当前仓库格子复制到临时数组里完整写入。
// 3. 只有临时数组整批成功后才替换正式 Snapshot、推进一次 Revision、广播并缓存；失败时正式格子保持原样。
FCatDomainCommandResult ACatCampInventoryActor::AddItemsFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const FString& StableNetId,
	const TArray<FCatCampInventoryAddItemRequest>& Items)
{
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
		TArray<FCatRunInventorySlot> SimulatedSlots = Snapshot.InventorySlots;
		const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
		bool bStoredAll = true;
		for (const FCatCampInventoryAddItemRequest& Item : NormalizedItems)
		{
			const UCatEquipmentDefinition* Definition =
				Settings ? Settings->FindRuntimeDefinition(Item.DefinitionId) : nullptr;
			if (!Definition || !AddItemQuantityToSlots(SimulatedSlots, *Definition, Item.DefinitionId, Item.Quantity))
			{
				bStoredAll = false;
				break;
			}
		}
		if (bStoredAll)
		{
			Snapshot.InventorySlots = MoveTemp(SimulatedSlots);
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
// 1. 先验证公共仓库槽位、数量、authority 和目标玩家装备组件，确保取用有明确来源和接收方。
// 2. 再复制源格形成本次要取出的完整实例；消耗品取指定数量，装备型只允许一次取一件。
// 3. 目标玩家能原样接收该实例时才返回 None；本函数不修改公共仓库，也不调用玩家入库提交。
ECatDomainCommandError ACatCampInventoryActor::ValidateWithdrawToEquipment(const FGuid RequestId,
	const int32 SourceSlotIndex, const int32 Quantity, UCatEquipmentComponent* TargetEquipment) const
{
	const AActor* TargetOwner = TargetEquipment ? TargetEquipment->GetOwner() : nullptr;
	if (!HasAuthority() || !RequestId.IsValid() || !TargetEquipment || Quantity <= 0
		|| !TargetOwner || !TargetOwner->HasAuthority() || !Snapshot.InventorySlots.IsValidIndex(SourceSlotIndex))
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	if (TargetEquipment->HasActiveFishingUse() || TargetEquipment->HasActiveRunConsumableUse())
	{
		return ECatDomainCommandError::InvalidPhase;
	}
	const FCatRunInventorySlot& SourceSlot = Snapshot.InventorySlots[SourceSlotIndex];
	const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
	const UCatEquipmentDefinition* Definition =
		Settings ? Settings->FindRuntimeDefinition(SourceSlot.DefinitionId) : nullptr;
	if (!Definition || !CatRunInventorySlotOperations::IsInventorySlotOccupied(SourceSlot)
		|| !SourceSlot.ItemInstanceId.IsValid() || SourceSlot.Quantity < Quantity)
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	if (!Definition->bRunConsumable && Quantity != 1)
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	FCatRunInventorySlot WithdrawnItem = SourceSlot;
	WithdrawnItem.Quantity = Quantity;
	CatRunInventorySlotOperations::NormalizeStoredItemSlot(WithdrawnItem, *Definition);
	return TargetEquipment->CanStoreInventorySlot(*Definition, WithdrawnItem)
		? ECatDomainCommandError::None : ECatDomainCommandError::CapacityExceeded;
}

// 取用提交流程：
// 1. 先用 RequestId 和源槽/数量/双方版本签名处理幂等，重放不会重复扣公共仓库或重复发玩家随身库存。
// 2. 首次提交先做公共仓库和个人装备双侧预检，再检查公共仓库 Revision 是否仍是调用方看到的版本。
// 3. 扣公共仓库槽位前保存一份槽位快照；如果个人装备授予出现意外失败，恢复公共仓库，避免物品凭空消失。
// 4. 玩家随身装备授予成功后递增公共仓库 Revision、复制并缓存终态；随身库存的 Revision 由 UCatEquipmentComponent 自己返回。
FCatDomainCommandResult ACatCampInventoryActor::WithdrawToEquipmentFromAuthority(const FGuid RequestId,
	const int64 ExpectedCampRevision, const int32 SourceSlotIndex, const int32 Quantity,
	UCatEquipmentComponent* TargetEquipment, const int64 ExpectedEquipmentRevision)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	FName SourceDefinitionId = NAME_None;
	FGuid SourceItemInstanceId;
	if (Snapshot.InventorySlots.IsValidIndex(SourceSlotIndex))
	{
		SourceDefinitionId = Snapshot.InventorySlots[SourceSlotIndex].DefinitionId;
		SourceItemInstanceId = Snapshot.InventorySlots[SourceSlotIndex].ItemInstanceId;
	}
	if (!RequestId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		Result.Revision = Snapshot.Revision;
		return Result;
	}
	const FString Key = MakeTerminalKey(TEXT("WithdrawToEquipment"), RequestId);
	const FString PayloadSignature = FString::Printf(
		TEXT("ExpectedCamp=%lld|Slot=%d|Definition=%s|Instance=%s|Quantity=%d|ExpectedEquipment=%lld"),
		ExpectedCampRevision, SourceSlotIndex, *SourceDefinitionId.ToString(),
		*SourceItemInstanceId.ToString(EGuidFormats::DigitsWithHyphens), Quantity, ExpectedEquipmentRevision);
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

	const ECatDomainCommandError Rejection =
		ValidateWithdrawToEquipment(RequestId, SourceSlotIndex, Quantity, TargetEquipment);
	if (Rejection != ECatDomainCommandError::None)
	{
		Result.Error = Rejection;
		Result.Revision = Snapshot.Revision;
		TerminalCache.Add(Key, Result);
		TerminalPayloadByKey.Add(Key, PayloadSignature);
		return Result;
	}
	if (Snapshot.Revision != ExpectedCampRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
		Result.Revision = Snapshot.Revision;
		TerminalCache.Add(Key, Result);
		TerminalPayloadByKey.Add(Key, PayloadSignature);
		return Result;
	}

	const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
	const UCatEquipmentDefinition* Definition = Settings ? Settings->FindRuntimeDefinition(SourceDefinitionId) : nullptr;
	if (!Definition)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		Result.Revision = Snapshot.Revision;
		TerminalCache.Add(Key, Result);
		TerminalPayloadByKey.Add(Key, PayloadSignature);
		return Result;
	}

	TArray<FCatRunInventorySlot> SavedSlots = Snapshot.InventorySlots;
	FCatRunInventorySlot& SourceSlot = Snapshot.InventorySlots[SourceSlotIndex];
	FCatRunInventorySlot WithdrawnItem = SourceSlot;
	WithdrawnItem.Quantity = Quantity;
	CatRunInventorySlotOperations::NormalizeStoredItemSlot(WithdrawnItem, *Definition);
	if (Definition->bRunConsumable && Quantity < SourceSlot.Quantity)
	{
		WithdrawnItem.ItemInstanceId = FGuid::NewGuid();
	}
	SourceSlot.Quantity -= Quantity;
	if (SourceSlot.Quantity <= 0)
	{
		SourceSlot = FCatRunInventorySlot();
	}

	const FCatDomainCommandResult Grant =
		TargetEquipment->GrantInventorySlotFromAuthority(RequestId, ExpectedEquipmentRevision, WithdrawnItem);
	const bool bGrantStanding = Grant.bCommitted || Grant.Error == ECatDomainCommandError::AlreadyResolved;
	if (!bGrantStanding)
	{
		Snapshot.InventorySlots = MoveTemp(SavedSlots);
		Result = Grant;
		Result.RequestId = RequestId;
		Result.Revision = Snapshot.Revision;
		TerminalCache.Add(Key, Result);
		TerminalPayloadByKey.Add(Key, PayloadSignature);
		return Result;
	}

	++Snapshot.Revision;
	PublishSnapshot();
	Result.bCommitted = true;
	Result.Error = ECatDomainCommandError::None;
	Result.Revision = Snapshot.Revision;
	TerminalCache.Add(Key, Result);
	TerminalPayloadByKey.Add(Key, PayloadSignature);
	return Result;
}

// 公共仓库整理流程：
// 1. 用 RequestId、Revision 和源/目标下标处理幂等重放；同 RequestId 换格子会被拒绝。
// 2. 首次请求要求服务器 authority、版本匹配且容量数组已补齐，再复用运行库存格通用规则移动、合并或交换。
// 3. 只有格子数组真的变化时才推进公共仓库 Revision 并广播；目标格已满这类无变化结果不刷新库存。
FCatDomainCommandResult ACatCampInventoryActor::MoveInventorySlotFromAuthority(const FGuid RequestId,
	const int64 ExpectedRevision, const int32 SourceSlotIndex, const int32 TargetSlotIndex)
{
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
	else
	{
		EnsureInventorySlotArray();
		const auto ResolveStackLimit = [this](const FName DefinitionId)
		{
			const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
			const UCatEquipmentDefinition* Definition = Settings ? Settings->FindRuntimeDefinition(DefinitionId) : nullptr;
			return Definition ? GetInventoryStackLimit(*Definition) : 1;
		};
		const CatRunInventorySlotOperations::FMoveSlotsResult MoveResult =
			CatRunInventorySlotOperations::MoveItemBetweenSlots(
				Snapshot.InventorySlots, SourceSlotIndex, TargetSlotIndex, ResolveStackLimit);
		Result.bCommitted = MoveResult.bChanged;
		Result.Error = MoveResult.Error;
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
// 2. 首次提交同时验证公共仓库和玩家随身库存的 authority、阶段、Revision 和槽位，任一侧不成立都不改数据源。
// 3. 通过后用同一套运行库存格规则把背包源格移动、合并或交换到公共仓库目标格。
// 4. 只有不同物品交换让背包收到营地目标物时才修正背包选择；空格存入和同类合并不反向改选择。
// 5. 只有数组真的变化时才分别推进背包和公共仓库版本，再各自广播完整快照，让两边 UI 自己刷新。
FCatDomainCommandResult ACatCampInventoryActor::DepositFromEquipmentSlotFromAuthority(const FGuid RequestId,
	const int64 ExpectedCampRevision, const int32 TargetCampSlotIndex, UCatEquipmentComponent* SourceEquipment,
	const int64 ExpectedEquipmentRevision, const int32 SourceEquipmentSlotIndex)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString Key = MakeTerminalKey(TEXT("DepositFromEquipmentSlot"), RequestId);
	const FString PayloadSignature = FString::Printf(
		TEXT("ExpectedCamp=%lld|TargetCamp=%d|ExpectedEquipment=%lld|SourceEquipment=%d"),
		ExpectedCampRevision, TargetCampSlotIndex, ExpectedEquipmentRevision, SourceEquipmentSlotIndex);
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

	AActor* EquipmentOwner = SourceEquipment ? SourceEquipment->GetOwner() : nullptr;
	if (!HasAuthority() || !RequestId.IsValid() || !SourceEquipment || !EquipmentOwner
		|| !EquipmentOwner->HasAuthority() || SourceEquipmentSlotIndex < 0 || TargetCampSlotIndex < 0)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (SourceEquipment->HasActiveFishingUse() || SourceEquipment->HasActiveRunConsumableUse())
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
	}
	else if (Snapshot.Revision != ExpectedCampRevision
		|| SourceEquipment->Snapshot.Revision != ExpectedEquipmentRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
	}
	else
	{
		EnsureInventorySlotArray();
		SourceEquipment->EnsureInventorySlotArray();
		if (!Snapshot.InventorySlots.IsValidIndex(TargetCampSlotIndex)
			|| !SourceEquipment->Snapshot.InventorySlots.IsValidIndex(SourceEquipmentSlotIndex))
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
		}
		else
		{
			const FName EquipmentReceivedDefinitionId = Snapshot.InventorySlots[TargetCampSlotIndex].DefinitionId;
			const FName SourceDefinitionId =
				SourceEquipment->Snapshot.InventorySlots[SourceEquipmentSlotIndex].DefinitionId;
			const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
			const UCatEquipmentDefinition* SourceDefinition =
				Settings ? Settings->FindRuntimeDefinition(SourceDefinitionId) : nullptr;
			const bool bEquipmentReceivesCampSlot = !EquipmentReceivedDefinitionId.IsNone()
				&& Snapshot.InventorySlots[TargetCampSlotIndex].Quantity > 0
				&& EquipmentReceivedDefinitionId != SourceDefinitionId;
			const UCatEquipmentDefinition* EquipmentReceivedDefinition =
				Settings && !EquipmentReceivedDefinitionId.IsNone()
					? Settings->FindRuntimeDefinition(EquipmentReceivedDefinitionId) : nullptr;
			if (SourceDefinitionId.IsNone() || !SourceDefinition
				|| SourceEquipment->Snapshot.InventorySlots[SourceEquipmentSlotIndex].Quantity <= 0)
			{
				Result.Error = ECatDomainCommandError::InvalidPayload;
			}
			else if (!EquipmentReceivedDefinitionId.IsNone() && !EquipmentReceivedDefinition)
			{
				Result.Error = ECatDomainCommandError::InvalidPayload;
			}
			else
			{
				const auto ResolveStackLimit = [this](const FName DefinitionId)
				{
					const UCatEquipmentSettings* LocalSettings = GetDefault<UCatEquipmentSettings>();
					const UCatEquipmentDefinition* Definition =
						LocalSettings ? LocalSettings->FindRuntimeDefinition(DefinitionId) : nullptr;
					return Definition ? GetInventoryStackLimit(*Definition) : 1;
				};
				const CatRunInventorySlotOperations::FMoveSlotsResult MoveResult =
					CatRunInventorySlotOperations::MoveItemBetweenSlotArrays(
						SourceEquipment->Snapshot.InventorySlots, SourceEquipmentSlotIndex,
						Snapshot.InventorySlots, TargetCampSlotIndex, ResolveStackLimit);
				Result.bCommitted = MoveResult.bChanged;
				Result.Error = MoveResult.Error;
				if (MoveResult.bChanged && bEquipmentReceivesCampSlot && EquipmentReceivedDefinition)
				{
					SourceEquipment->AutoSelectGrantedInventoryItem(
						*EquipmentReceivedDefinition, EquipmentReceivedDefinitionId);
				}
			}
		}
	}
	if (Result.bCommitted)
	{
		++SourceEquipment->Snapshot.Revision;
		++Snapshot.Revision;
		SourceEquipment->PublishSnapshot();
		PublishSnapshot();
	}
	Result.Revision = Snapshot.Revision;
	TerminalCache.Add(Key, Result);
	TerminalPayloadByKey.Add(Key, PayloadSignature);
	return Result;
}

// 公共仓库拖入背包流程：
// 1. 用 RequestId 和双方版本/槽位做幂等签名；重放只返回首次终态，不重复扣公共仓库或发背包。
// 2. 首次提交同时验证公共仓库、玩家随身库存、阶段、Revision 和槽位，确保这次 Drop 可以同时改两份数据源。
// 3. 通过后把公共仓库源格移动、合并或交换到背包目标格；目标格不是空格时也按玩家拖放目标处理。
// 4. 成功后两边各自推进版本并广播完整快照，Model 只收到变化信号并让各 WBP 自己刷新。
FCatDomainCommandResult ACatCampInventoryActor::WithdrawToEquipmentSlotFromAuthority(const FGuid RequestId,
	const int64 ExpectedCampRevision, const int32 SourceCampSlotIndex, UCatEquipmentComponent* TargetEquipment,
	const int64 ExpectedEquipmentRevision, const int32 TargetEquipmentSlotIndex)
{
	FCatDomainCommandResult Result;
	Result.RequestId = RequestId;
	const FString Key = MakeTerminalKey(TEXT("WithdrawToEquipmentSlot"), RequestId);
	const FString PayloadSignature = FString::Printf(
		TEXT("ExpectedCamp=%lld|SourceCamp=%d|ExpectedEquipment=%lld|TargetEquipment=%d"),
		ExpectedCampRevision, SourceCampSlotIndex, ExpectedEquipmentRevision, TargetEquipmentSlotIndex);
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

	AActor* EquipmentOwner = TargetEquipment ? TargetEquipment->GetOwner() : nullptr;
	if (!HasAuthority() || !RequestId.IsValid() || !TargetEquipment || !EquipmentOwner
		|| !EquipmentOwner->HasAuthority() || SourceCampSlotIndex < 0 || TargetEquipmentSlotIndex < 0)
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (TargetEquipment->HasActiveFishingUse() || TargetEquipment->HasActiveRunConsumableUse())
	{
		Result.Error = ECatDomainCommandError::InvalidPhase;
	}
	else if (Snapshot.Revision != ExpectedCampRevision
		|| TargetEquipment->Snapshot.Revision != ExpectedEquipmentRevision)
	{
		Result.Error = ECatDomainCommandError::RevisionConflict;
	}
	else
	{
		EnsureInventorySlotArray();
		TargetEquipment->EnsureInventorySlotArray();
		if (!Snapshot.InventorySlots.IsValidIndex(SourceCampSlotIndex)
			|| !TargetEquipment->Snapshot.InventorySlots.IsValidIndex(TargetEquipmentSlotIndex))
		{
			Result.Error = ECatDomainCommandError::InvalidPayload;
		}
		else
		{
			const FName SourceDefinitionId = Snapshot.InventorySlots[SourceCampSlotIndex].DefinitionId;
			const FName EquipmentTargetDefinitionId =
				TargetEquipment->Snapshot.InventorySlots[TargetEquipmentSlotIndex].DefinitionId;
			const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
			const UCatEquipmentDefinition* SourceDefinition =
				Settings ? Settings->FindRuntimeDefinition(SourceDefinitionId) : nullptr;
			const UCatEquipmentDefinition* EquipmentTargetDefinition =
				Settings && !EquipmentTargetDefinitionId.IsNone()
					? Settings->FindRuntimeDefinition(EquipmentTargetDefinitionId) : nullptr;
			if (SourceDefinitionId.IsNone() || !SourceDefinition
				|| Snapshot.InventorySlots[SourceCampSlotIndex].Quantity <= 0)
			{
				Result.Error = ECatDomainCommandError::InvalidPayload;
			}
			else if (!EquipmentTargetDefinitionId.IsNone() && !EquipmentTargetDefinition)
			{
				Result.Error = ECatDomainCommandError::InvalidPayload;
			}
			else
			{
				const auto ResolveStackLimit = [this](const FName DefinitionId)
				{
					const UCatEquipmentSettings* LocalSettings = GetDefault<UCatEquipmentSettings>();
					const UCatEquipmentDefinition* Definition =
						LocalSettings ? LocalSettings->FindRuntimeDefinition(DefinitionId) : nullptr;
					return Definition ? GetInventoryStackLimit(*Definition) : 1;
				};
				const CatRunInventorySlotOperations::FMoveSlotsResult MoveResult =
					CatRunInventorySlotOperations::MoveItemBetweenSlotArrays(
						Snapshot.InventorySlots, SourceCampSlotIndex,
						TargetEquipment->Snapshot.InventorySlots, TargetEquipmentSlotIndex, ResolveStackLimit);
				Result.bCommitted = MoveResult.bChanged;
				Result.Error = MoveResult.Error;
				if (MoveResult.bChanged)
				{
					TargetEquipment->AutoSelectGrantedInventoryItem(*SourceDefinition, SourceDefinitionId);
				}
			}
		}
	}
	if (Result.bCommitted)
	{
		++TargetEquipment->Snapshot.Revision;
		++Snapshot.Revision;
		TargetEquipment->PublishSnapshot();
		PublishSnapshot();
	}
	Result.Revision = Snapshot.Revision;
	TerminalCache.Add(Key, Result);
	TerminalPayloadByKey.Add(Key, PayloadSignature);
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

// 堆叠上限流程：定义显式 MaxStackSize 优先；装备型永远一格一件，数量型未声明时沿用项目默认堆叠上限。
int32 ACatCampInventoryActor::GetInventoryStackLimit(const UCatEquipmentDefinition& Definition) const
{
	if (Definition.MaxStackSize > 0)
	{
		return FMath::Max(1, Definition.MaxStackSize);
	}
	if (!Definition.bRunConsumable)
	{
		return 1;
	}
	const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
	const int32 ConfiguredLimit = Settings ? Settings->InventoryQuantityStackCapacity : 0;
	return ConfiguredLimit > 0 ? ConfiguredLimit : MAX_int32;
}

// 容量预检流程：复制当前格子后交给通用写入模拟；模拟能完整放入才返回 true，正式 Snapshot 不会被 const 预检修改。
bool ACatCampInventoryActor::CanStoreItem(const UCatEquipmentDefinition& Definition,
	const FName DefinitionId, const int32 Quantity) const
{
	TArray<FCatRunInventorySlot> SimulatedSlots = Snapshot.InventorySlots;
	return AddItemQuantityToSlots(SimulatedSlots, Definition, DefinitionId, Quantity);
}

// 整批容量预检流程：按归一化后的物品顺序逐行模拟写入同一份临时格子，保证“每行单独可放”和“整车一起可放”不会出现两种答案。
bool ACatCampInventoryActor::CanStoreItems(const TArray<FCatCampInventoryAddItemRequest>& Items) const
{
	if (Items.IsEmpty())
	{
		return false;
	}
	TArray<FCatRunInventorySlot> SimulatedSlots = Snapshot.InventorySlots;
	const UCatEquipmentSettings* Settings = GetDefault<UCatEquipmentSettings>();
	for (const FCatCampInventoryAddItemRequest& Item : Items)
	{
		const UCatEquipmentDefinition* Definition =
			Settings ? Settings->FindRuntimeDefinition(Item.DefinitionId) : nullptr;
		if (!Definition || !AddItemQuantityToSlots(SimulatedSlots, *Definition, Item.DefinitionId, Item.Quantity))
		{
			return false;
		}
	}
	return true;
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

// 入库写入流程：单行提交先复用容量预检，再把正式 Snapshot 交给通用格子写入；Revision 和广播仍由提交入口统一处理。
bool ACatCampInventoryActor::AddItemQuantity(const UCatEquipmentDefinition& Definition,
	const FName DefinitionId, const int32 Quantity)
{
	if (!CanStoreItem(Definition, DefinitionId, Quantity))
	{
		return false;
	}
	return AddItemQuantityToSlots(Snapshot.InventorySlots, Definition, DefinitionId, Quantity);
}

// 格子写入流程：
// 1. 先按配置容量补齐传入数组，调用方传临时数组时就是模拟，传 Snapshot 时就是正式写入。
// 2. 同定义未满格优先吸收数量并补齐该堆栈实例身份，剩余数量再创建新的运行期实例落到空格。
// 3. 只有全部数量都放完才返回 true，调用方因此可以用它保证整批入库不产生半批结果。
bool ACatCampInventoryActor::AddItemQuantityToSlots(TArray<FCatRunInventorySlot>& InventorySlots,
	const UCatEquipmentDefinition& Definition, const FName DefinitionId, const int32 Quantity) const
{
	if (DefinitionId.IsNone() || Quantity <= 0)
	{
		return false;
	}
	const int32 SlotCapacity = GetConfiguredSlotCapacity();
	if (InventorySlots.Num() < SlotCapacity)
	{
		InventorySlots.AddDefaulted(SlotCapacity - InventorySlots.Num());
	}
	const int32 StackLimit = GetInventoryStackLimit(Definition);
	if (StackLimit <= 0)
	{
		return false;
	}
	int32 Remaining = Quantity;
	for (FCatRunInventorySlot& Slot : InventorySlots)
	{
		if (Slot.DefinitionId == DefinitionId && Slot.Quantity > 0 && Slot.Quantity < StackLimit)
		{
			CatRunInventorySlotOperations::NormalizeStoredItemSlot(Slot, Definition);
			const int32 Added = FMath::Min(Remaining, StackLimit - Slot.Quantity);
			Slot.Quantity += Added;
			Remaining -= Added;
			if (Remaining <= 0)
			{
				return true;
			}
		}
	}
	for (FCatRunInventorySlot& Slot : InventorySlots)
	{
		if (!CatRunInventorySlotOperations::IsInventorySlotOccupied(Slot))
		{
			const int32 Added = FMath::Min(Remaining, StackLimit);
			Slot = CatRunInventorySlotOperations::MakeInventoryItemSlot(Definition, DefinitionId, Added);
			Remaining -= Added;
			if (Remaining <= 0)
			{
				return true;
			}
		}
	}
	return false;
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
