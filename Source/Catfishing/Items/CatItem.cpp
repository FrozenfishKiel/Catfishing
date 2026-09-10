#include "Items/CatItem.h"

#include "Character/CatCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "GameFramework/Controller.h"
#include "Logging/CatLog.h"

// 构造流程：创建统一目标扫描碰撞和可选静态/骨骼表现；碰撞只响应可见性查询，不让掉落物改变角色物理。
ACatItem::ACatItem()
{
	bReplicates = true;
	SetReplicateMovement(true);
	PrimaryActorTick.bCanEverTick = false;
	PickupCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("PickupCollision"));
	SetRootComponent(PickupCollision);
	PickupCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PickupCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	PickupCollision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	StaticMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMesh"));
	StaticMesh->SetupAttachment(PickupCollision);
	StaticMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SkeletalMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("SkeletalMesh"));
	SkeletalMesh->SetupAttachment(PickupCollision);
	SkeletalMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

// 批次读取流程：返回蓝图配置的静态载荷副本；调用方随后仍由统一库存入口做 authority、定义和容量校验。
FCatInventoryReceiveBatch ACatItem::GetPickupInventory() const { return StaticPickupInventory; }

// 生成配置流程：基础世界物只提供稳定组件与批次契约；没有额外配置时不写碰撞或库存状态。
void ACatItem::InitializeActorSpawnConfig() {}

// 开始运行流程：先完成引擎生命周期，再按已经加载的蓝图默认值调用物品配置扩展点；不在构造期读取尚未反序列化的资产字段。
void ACatItem::BeginPlay()
{
	Super::BeginPlay();
	InitializeActorSpawnConfig();
}

// 交互资格流程：先确认 Controller 与本 Actor 同世界，再取其当前 CatCharacter 并按平方距离检查配置半径；任一事实缺失都拒绝。
bool ACatItem::CanInteract_Implementation(AController* RequestingController) const
{
	const ACatCharacter* Character = RequestingController ? Cast<ACatCharacter>(RequestingController->GetPawn()) : nullptr;
	return Character && RequestingController->GetWorld() == GetWorld() && Character->GetWorld() == GetWorld()
		&& !bPickupClaimed && !IsActorBeingDestroyed()
		&& FMath::IsFinite(InteractionRadiusCentimeters) && InteractionRadiusCentimeters > 0.0
		&& FVector::DistSquared(Character->GetActorLocation(), GetActorLocation()) <= FMath::Square(InteractionRadiusCentimeters);
}

// 提示读取流程：返回基础拾取文案；不读取库存容量，因为容量必须由服务器收货预检裁决。
FText ACatItem::GetInteractionPrompt_Implementation() const { return FText::FromString(TEXT("拾取")); }

// 半径读取流程：返回配置厘米值，让目标扫描与 authority 复核遵守同一边界。
double ACatItem::GetInteractionRadius_Implementation() const { return InteractionRadiusCentimeters; }

// 交互提交流程：
// 1. 客户端只通过现有 PlayerController RPC 重放这次目标交互，不直接修改世界物或库存。
// 2. authority 重新检查请求者距离和 RequestId，再读取完整批次并调用唯一 Actor 收货入口。
// 3. 入库前占用本世界物，阻止库存变化回调重入再次发货；失败释放占用，成功后销毁。
bool ACatItem::Interact_Implementation(AController* RequestingController, const FGuid RequestId)
{
	ACatfishingPlayerController* PlayerController = Cast<ACatfishingPlayerController>(RequestingController);
	if (!PlayerController || !RequestId.IsValid() || !CanInteract_Implementation(RequestingController))
	{
		UE_LOG(LogCatfishing, Warning, TEXT("Event=item_pickup_rejected Item=%s Request=%s Reason=InvalidRequesterOrReach World=%s NetMode=%d Authority=%d LocalRole=%d"),
			*GetNameSafe(this), *RequestId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), HasAuthority(), static_cast<int32>(GetLocalRole()));
		return false;
	}
	if (!HasAuthority())
	{
		UE_LOG(LogCatfishing, Log, TEXT("Event=item_pickup_requested Item=%s Request=%s World=%s NetMode=%d Authority=0 LocalRole=%d"),
			*GetNameSafe(this), *RequestId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()));
		PlayerController->ServerRequestInteraction(this, RequestId);
		return true;
	}
	const FCatInventoryReceiveBatch PickupBatch = GetPickupInventory();
	bPickupClaimed = true;
	const bool bCommitted = !PickupBatch.IsEmpty()
		&& UCatInventoryStatics::TryAddInventoryBatchToActor(PlayerController->GetPawn(), PickupBatch);
	if (!bCommitted)
	{
		bPickupClaimed = false;
		UE_LOG(LogCatfishing, Warning, TEXT("Event=item_pickup_rejected Item=%s Request=%s Reason=EmptyBatchOrInventoryRejected World=%s NetMode=%d Authority=1 LocalRole=%d"),
			*GetNameSafe(this), *RequestId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()));
		return false;
	}
	UE_LOG(LogCatfishing, Log,
		TEXT("Event=item_pickup_committed Item=%s Request=%s Player=%s Definitions=%d Instances=%d World=%s NetMode=%d Authority=1 LocalRole=%d"),
		*GetNameSafe(this),
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(PlayerController->GetPawn()),
		PickupBatch.DefinitionEntries.Num(), PickupBatch.InstanceEntries.Num(), *GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()));
	if (!Destroy())
	{
		// 库存已经提交，销毁失败也不能再次发货；关闭交互并记录需要定位的世界物，避免回滚出第二份物品。
		SetActorEnableCollision(false);
		SetActorHiddenInGame(true);
		UE_LOG(LogCatfishing, Warning, TEXT("Event=item_pickup_cleanup_failed Item=%s Request=%s World=%s NetMode=%d Authority=1 LocalRole=%d"), *GetNameSafe(this), *RequestId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(GetWorld()), static_cast<int32>(GetNetMode()), static_cast<int32>(GetLocalRole()));
	}
	return bCommitted;
}
