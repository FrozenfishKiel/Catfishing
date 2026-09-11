#include "Inventory/CatFishInventoryItemInstance.h"

#include "Character/CatCharacter.h"
#include "Condition/CatConditionComponent.h"
#include "Data/CatFishDefinition.h"
#include "Inventory/CatInventoryComponent.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "Net/UnrealNetwork.h"

// 鱼实例构造流程：基础实例已分配 ItemInstanceId；鱼专属字段等捕获提交时再由服务器写入。
UCatFishInventoryItemInstance::UCatFishInventoryItemInstance(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

// 复制声明流程：来源会话和重量是公开鱼物品状态，捕获者 StableNetId 不注册复制以避免身份泄漏到客户端。
void UCatFishInventoryItemInstance::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, SourceFishingSessionId);
	DOREPLIFETIME(ThisClass, WeightKilograms);
}

// 鱼状态初始化流程：
// 1. 只允许服务器或尚未绑定运行宿主的构造期写入，客户端不能伪造鱼身份。
// 2. 逐项验证来源会话、鱼实例、捕获者身份和重量，任一缺失都保持实例未初始化。
// 3. 成功后复用基础实例的 authority 身份写口，再写鱼专属字段。
bool UCatFishInventoryItemInstance::InitializeFishFromAuthority(const FGuid InFishingSessionId,
	const FGuid InFishInstanceId, const FString& InOwnerStableNetId, const double InWeightKilograms)
{
	AActor* RuntimeOwner = GetRuntimeOwnerActor();
	if ((RuntimeOwner != nullptr && !RuntimeOwner->HasAuthority())
		|| !InFishingSessionId.IsValid()
		|| !InFishInstanceId.IsValid()
		|| InFishingSessionId == InFishInstanceId
		|| InOwnerStableNetId.IsEmpty()
		|| !FMath::IsFinite(InWeightKilograms)
		|| InWeightKilograms <= 0.0)
	{
		return false;
	}

	SetItemInstanceIdFromAuthority(InFishInstanceId);
	SourceFishingSessionId = InFishingSessionId;
	OwnerStableNetId = InOwnerStableNetId;
	WeightKilograms = InWeightKilograms;
	return GetItemInstanceId() == InFishInstanceId;
}

// 鱼定义读取流程：运行实例只承认 UCatFishDefinition；普通库存定义不能伪装成可吃鱼。
UCatFishDefinition* UCatFishInventoryItemInstance::GetFishDefinition() const
{
	return Cast<UCatFishDefinition>(GetItemDefinition());
}

// 来源会话读取流程：返回捕获时冻结的 FishingSession ID，不尝试查找已经结束的会话对象。
FGuid UCatFishInventoryItemInstance::GetSourceFishingSessionId() const
{
	return SourceFishingSessionId;
}

// 捕获者身份读取流程：只返回服务器内存字段；客户端收到的实例自然为空字符串。
const FString& UCatFishInventoryItemInstance::GetFishOwnerStableNetId() const
{
	return OwnerStableNetId;
}

// 重量读取流程：返回单条鱼冻结重量；调用方必须自行处理无效或未初始化实例。
double UCatFishInventoryItemInstance::GetFishWeightKilograms() const
{
	return WeightKilograms;
}

// 鱼 Use 裁决流程：复用通用扣量语义后，再确认鱼专属字段和当前实例完全一致。
ECatDomainCommandError UCatFishInventoryItemInstance::Use(const FCatInventoryEntry& Item,
	const int32 Quantity) const
{
	const ECatDomainCommandError BaseError = Super::Use(Item, Quantity);
	if (BaseError != ECatDomainCommandError::None)
	{
		return BaseError;
	}

	const UCatFishDefinition* Definition = GetFishDefinition();
	if (Definition == nullptr
		|| Item.Instance != this || Item.StackCount != 1
		|| Quantity != 1
		|| !SourceFishingSessionId.IsValid() || OwnerStableNetId.IsEmpty()
		|| !FMath::IsFinite(WeightKilograms) || WeightKilograms <= 0.0)
	{
		return ECatDomainCommandError::InvalidPayload;
	}
	return ECatDomainCommandError::None;
}

// 鱼扣量策略读取流程：吃鱼会真实移除这一条库存鱼，不借出、不创建世界 Actor。
bool UCatFishInventoryItemInstance::ConsumesInventoryQuantityOnUse() const
{
	return true;
}

// 鱼使用预检流程：只读地确认库存格持有本实例，并让目标角色 Condition 判断这条鱼现在能否食用。
bool UCatFishInventoryItemInstance::CanUseFromInventory(
	const FCatInventoryEntry& InventoryEntry, APawn* UserPawn) const
{
	const UCatFishDefinition* Definition = GetFishDefinition();
	const ACatCharacter* Character = Cast<ACatCharacter>(UserPawn);
	const UCatConditionComponent* Condition = Character ? Character->GetConditionComponent() : nullptr;
	return InventoryEntry.Instance == this
		&& InventoryEntry.StackCount == 1
		&& GetItemInstanceId().IsValid()
		&& SourceFishingSessionId.IsValid()
		&& !OwnerStableNetId.IsEmpty()
		&& FMath::IsFinite(WeightKilograms)
		&& WeightKilograms > 0.0
		&& Definition != nullptr
		&& Definition->IsRuntimeDefinitionReady()
		&& Condition != nullptr
		&& Condition->ValidateFishConsumption(Definition) == ECatDomainCommandError::None;
}

// 鱼库存 Use 提交流程：
// 1. 复核来源库存、使用者和鱼定义，失败时不进入库存扣量。
// 2. 让正式库存按当前槽位和实例 ID 扣除这一条鱼。
// 3. 库存扣除成功后才提交 Character Condition 的吃鱼效果；身体失败时库存入口会回滚刚才的扣量。
// 4. 返回公共领域结果给 UI，结果只描述本次吃鱼命令的提交或失败。
FCatDomainCommandResult UCatFishInventoryItemInstance::UseFromInventorySlotFromAuthority(
	const FCatInventoryEntry& InventoryEntry, const FCatInventoryItemUseContext& UseContext)
{
	FCatDomainCommandResult Result;
	Result.RequestId = UseContext.RequestId;


	ACatCharacter* Character = Cast<ACatCharacter>(UseContext.UserPawn);
	if (Character == nullptr && UseContext.RequestingController != nullptr)
	{
		Character = Cast<ACatCharacter>(UseContext.RequestingController->GetPawn());
	}
	UCatConditionComponent* Condition = Character ? Character->GetConditionComponent() : nullptr;
	UCatFishDefinition* Definition = GetFishDefinition();
	if (InventoryEntry.Instance != this || InventoryEntry.StackCount != 1
		|| UseContext.SourceInventory == nullptr || Character == nullptr || Condition == nullptr
		|| Definition == nullptr || !UseContext.RequestId.IsValid())
	{
		Result.Error = ECatDomainCommandError::InvalidPayload;
		return Result;
	}

	const FString PayloadContext = FString::Printf(TEXT("Fish=%s|Definition=%s|SourceSession=%s"),
		*GetItemInstanceId().ToString(EGuidFormats::DigitsWithHyphens),
		*Definition->FishDefinitionId.ToString(),
		*SourceFishingSessionId.ToString(EGuidFormats::DigitsWithHyphens));
	const FCatInventoryItemUseResult InventoryResult =
		UseContext.SourceInventory->UseItemInstanceFromAuthority(UseContext.RequestId,
			GetItemInstanceId(), 1, PayloadContext,
			[&](FCatInventoryItemUseResult& MutableResult)
			{
				(void)MutableResult;
				return CanUseFromInventory(InventoryEntry, Character)
					? ECatDomainCommandError::None : ECatDomainCommandError::DependencyUnavailable;
			},
			[&](FCatInventoryItemUseResult& MutableResult)
			{
				(void)MutableResult;
				const FCatDomainCommandResult BodyResult =
					Condition->ConsumeCommittedFish(UseContext.RequestId, Definition);
				if (!CatIsAcceptedDomainCommandResult(BodyResult))
				{
					return false;
				}
				// 身体效果已经接受，库存不会再回滚这条鱼；此时才释放容器保管的隐藏 Actor，避免失败回滚留下失配的库存条目。
				if (ACatFishPickupActor* RetainedFishActor = Cast<ACatFishPickupActor>(GetWorldActor()))
				{
					RetainedFishActor->Destroy();
				}
				return true;
			});

	Result.RequestId = InventoryResult.RequestId;

	Result.bCommitted = InventoryResult.bCommitted;
	Result.bTerminalReplay = InventoryResult.bTerminalReplay;
	Result.bReplayedTerminalCommitted = InventoryResult.bReplayedTerminalCommitted;
	Result.Error = InventoryResult.Error;
	Result.ReplayedTerminalError = InventoryResult.ReplayedTerminalError;
	return Result;
}
