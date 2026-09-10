#include "FishContainers/CatFishGuardActor.h"

#include "Character/CatCharacter.h"
#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Condition/CatConditionComponent.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/CatInteractionSettings.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "Inventory/CatFishOnlyInventoryComponent.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatFishGuardInventoryItemInstance.h"
#include "Inventory/CatInventoryItemDefinition.h"
#include "FishContainers/CatFishPickupSettings.h"
#include "Net/UnrealNetwork.h"
#include "Logging/CatLog.h"
#include "UI/CatLocalPlayerUISubsystem.h"
#include "UI/Inventory/CatFishGuardInventoryWidget.h"

// 构造流程：创建独立箱子根、正式鱼库存和交互入口；开启 Actor 复制并关闭 Tick，鱼数组只由 InventoryComponent 复制。
ACatFishGuardActor::ACatFishGuardActor()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = false;
	WorldCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("WorldCollision"));
	SetRootComponent(WorldCollision);
	WorldCollision->SetBoxExtent(FVector(28.0, 17.0, 15.0));
	WorldCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	WorldCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	WorldCollision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	WorldCollision->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	GuardRoot = CreateDefaultSubobject<USceneComponent>(TEXT("GuardRoot"));
	GuardRoot->SetupAttachment(WorldCollision);
	InteractionCollision = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionCollision"));
	InteractionCollision->SetupAttachment(GuardRoot);
	SetReplicateMovement(true);
	InteractionCollision->SetSphereRadius(75.0f);
	InteractionCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionCollision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractionCollision->SetGenerateOverlapEvents(false);
	FishInventory = CreateDefaultSubobject<UCatFishOnlyInventoryComponent>(TEXT("FishInventory"));
	InventoryViewClass = TSoftClassPtr<UCatFishGuardInventoryWidget>(
		FSoftClassPath(TEXT("/Game/UI/Inventory/WBP_CatFishGuardInventory.WBP_CatFishGuardInventory_C")));
	InteractionPrompt = NSLOCTEXT("Catfishing", "FishGuardInteractionPrompt", "打开鱼护");
	GuardDefinition = TSoftObjectPtr<UCatInventoryItemDefinition>(
		FSoftObjectPath(TEXT("/Game/Catfishing/Data/Items/Item_FishGuard.Item_FishGuard")));
}

// 归属复制流程：只追加库存宿主；鱼内容仍由原 FishInventory FastArray 复制，背包不保管另一份鱼快照。
void ACatFishGuardActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, InventoryOwner);
}

// 库存生成流程：只接收一件鱼护实例并绑定自身载体；初始容量继续由原 BeginPlay 设置，不复制或覆盖任何内鱼。
bool ACatFishGuardActor::InitializeFromInventoryFromAuthority(UCatInventoryItemInstance* Item, const int32 Quantity)
{
	UCatFishGuardInventoryItemInstance* GuardInstance = Cast<UCatFishGuardInventoryItemInstance>(Item);
	if (!HasAuthority() || Quantity != 1 || !GuardInstance || !GuardInstance->GetItemDefinition()) return false;
	GuardItem = GuardInstance;
	GuardDefinition = GuardInstance->GetItemDefinition();
	GuardItem->SetGuardFromAuthority(this);
	GuardItem->SetRuntimeOwnerActor(this);
	return true;
}

// 地面资格读取流程：库存持有的载体不可作为世界容器访问；正在销毁的鱼护同样拒绝新的交易或开护。
bool ACatFishGuardActor::IsGrounded() const
{
	return InventoryOwner == nullptr && !IsActorBeingDestroyed();
}

// 嘴部鱼护查询流程：只扫描角色已有附件，隐藏的背包载体不占嘴；不另建角色侧携带列表。
ACatFishGuardActor* ACatFishGuardActor::FindCarriedGuard(const ACatCharacter* Character)
{
	if (!Character) return nullptr;
	TArray<AActor*> AttachedActors;
	Character->GetAttachedActors(AttachedActors);
	for (AActor* Actor : AttachedActors)
	{
		ACatFishGuardActor* Guard = Cast<ACatFishGuardActor>(Actor);
		if (Guard && Guard->InventoryOwner == Character && !Guard->IsHidden()) return Guard;
	}
	return nullptr;
}

// 拾取流程：依次验证地面、触达、身体、单嘴占用和配置，再让正式背包整件收货；各拒绝原因落盘，满包不变，成功由宿主同步附着原鱼护。
bool ACatFishGuardActor::PickUpFromAuthority(AController* RequestingController, const FGuid RequestId)
{
	ACatCharacter* Character = RequestingController ? Cast<ACatCharacter>(RequestingController->GetPawn()) : nullptr;
	const UCatFishPickupSettings* Settings = GetDefault<UCatFishPickupSettings>();
	const auto Finish = [&](const bool bAccepted, const TCHAR* Reason)
	{
		const FString Event = FString::Printf(
			TEXT("Event=fish_guard_pickup RequestId=%s Guard=%s Player=%s ItemInstanceId=%s Result=%s World=%s NetMode=%d Authority=%d LocalRole=%d"),
			*RequestId.ToString(), *GetName(), *GetNameSafe(Character),
			GuardItem ? *GuardItem->GetItemInstanceId().ToString() : TEXT("None"), Reason,
			*GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole());
		if (bAccepted) { UE_LOG(LogCatFishContainers, Log, TEXT("%s"), *Event); }
		else { UE_LOG(LogCatFishContainers, Warning, TEXT("%s"), *Event); }
		return bAccepted;
	};
	if (!HasAuthority() || !RequestId.IsValid()) return Finish(false, TEXT("InvalidRequest"));
	if (!IsGrounded() || !bInteractionEnabled) return Finish(false, TEXT("UnavailableGuard"));
	if (!Character || !Character->GetConditionComponent() || Character->GetConditionComponent()->GetSnapshot().bDowned)
		return Finish(false, TEXT("UnavailableCharacter"));
	if (!IsAuthorityRequestSpatiallyValid(RequestingController)) return Finish(false, TEXT("UnreachableGuard"));
	if (ACatFishPickupActor::FindCarriedFish(Character) || FindCarriedGuard(Character)) return Finish(false, TEXT("MouthOccupied"));
	if (!Character->GetMesh() || !Settings || !Character->GetMesh()->DoesSocketExist(Settings->MouthCarrySocketName))
		return Finish(false, TEXT("MouthSocketUnavailable"));
	if (!GuardItem)
	{
		UCatInventoryItemDefinition* Definition = GuardDefinition.LoadSynchronous();
		if (!Definition || !Definition->IsInventoryRuntimeDefinitionReady()
			|| Definition->GetMaxStackCount() != 1
			|| !Definition->GetPreferredInstanceType()
			|| !Definition->GetPreferredInstanceType()->IsChildOf(UCatFishGuardInventoryItemInstance::StaticClass()))
			return Finish(false, TEXT("GuardDefinitionUnavailable"));
		GuardItem = NewObject<UCatFishGuardInventoryItemInstance>(this, Definition->GetPreferredInstanceType());
		GuardItem->SetItemDefinition(Definition);
		GuardItem->SetGuardFromAuthority(this);
		GuardItem->SetRuntimeOwnerActor(this);
	}
	FCatInventoryReceiveBatch Batch;
	FCatInventoryInstanceEntry& Entry = Batch.InstanceEntries.AddDefaulted_GetRef();
	Entry.ItemInstance = GuardItem;
	Entry.Count = 1;
	const bool bPickedUp = Character->GetInventoryComponent() && Character->GetInventoryComponent()->TryAddInventoryBatch(Batch);
	return Finish(bPickedUp, bPickedUp ? TEXT("Success") : TEXT("InventoryRejected"));
}

// 宿主转换流程：解除旧宿主销毁回调，写入新归属并绑定新宿主，再更新表现和网络；原有鱼库存从未换所有者。
void ACatFishGuardActor::SetInventoryOwnerFromAuthority(AActor* NewInventoryOwner)
{
	if (!HasAuthority() || InventoryOwner == NewInventoryOwner) return;
	if (InventoryOwner) InventoryOwner->OnDestroyed.RemoveDynamic(this, &ThisClass::HandleInventoryOwnerDestroyed);
	InventoryOwner = NewInventoryOwner;
	SetOwner(NewInventoryOwner);
	if (InventoryOwner) InventoryOwner->OnDestroyed.AddDynamic(this, &ThisClass::HandleInventoryOwnerDestroyed);
	OnRep_InventoryOwner();
	ForceNetUpdate();
	UE_LOG(LogCatFishContainers, Log, TEXT("Event=fish_guard_owner Guard=%s InventoryOwner=%s Grounded=%d World=%s NetMode=%d Authority=%d LocalRole=%d"),
		*GetName(), *GetNameSafe(InventoryOwner), IsGrounded(), *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole());
}

// 归属收敛流程：两端都按库存归属设置碰撞并停止携带刚体；嘴部资格、附着与隐藏只由服务器决定。
// 客户端让引擎消费服务器的 AttachmentReplication/bHidden，避免旧鱼销毁晚到时重新裁决并覆盖正确附着。
void ACatFishGuardActor::OnRep_InventoryOwner()
{
	SetActorEnableCollision(InventoryOwner == nullptr);
	if (InventoryOwner) WorldCollision->SetSimulatePhysics(false);
	if (!HasAuthority()) return;
	if (InventoryOwner)
	{
		ACatCharacter* Character = Cast<ACatCharacter>(InventoryOwner);
		const UCatFishPickupSettings* Settings = GetDefault<UCatFishPickupSettings>();
		ACatFishGuardActor* OtherGuard = FindCarriedGuard(Character);
		if (Character && Character->GetMesh() && Settings
			&& Character->GetMesh()->DoesSocketExist(Settings->MouthCarrySocketName)
			&& !ACatFishPickupActor::FindCarriedFish(Character) && (!OtherGuard || OtherGuard == this))
		{
			AttachToComponent(Character->GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, Settings->MouthCarrySocketName);
			SetActorRelativeTransform(MouthCarryTransform);
			SetActorHiddenInGame(false);
		}
		else
		{
			DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			SetActorHiddenInGame(true);
		}
	}
	else
	{
		DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		SetActorHiddenInGame(false);
		SetActorEnableCollision(true);
	}
}

// 宿主退出流程：仍由该宿主保管时把实例交回鱼护自身并开启重力；世界 Actor 和内部鱼保留，销毁回调在归属转换时解除。
void ACatFishGuardActor::HandleInventoryOwnerDestroyed(AActor* DestroyedActor)
{
	if (HasAuthority() && InventoryOwner == DestroyedActor && GuardItem)
	{
		GuardItem->SetRuntimeOwnerActor(this);
		WorldCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		WorldCollision->SetSimulatePhysics(true);
	}
}

// BeginPlay 流程：先按归属恢复碰撞和携带表现，再配置准星查询通道；服务器设置鱼库存容量，客户端通过库存复制接收格子。
void ACatFishGuardActor::BeginPlay()
{
	Super::BeginPlay();
	OnRep_InventoryOwner();
	if (const UCatInteractionSettings* Settings = GetDefault<UCatInteractionSettings>(); Settings && InteractionCollision)
	{
		InteractionCollision->SetCollisionResponseToChannel(Settings->TargetingTraceChannel, ECR_Block);
	}
	if (!HasAuthority())
	{
		return;
	}
	if (FishInventory != nullptr)
	{
		FishInventory->SetInventorySlotCountFromAuthority(FishInventorySlotCapacity);
	}
}

// 鱼库存读取流程：返回本 Actor 持有的正式鱼库存组件；调用者继续通过 InventoryComponent 命令写入。
UCatFishOnlyInventoryComponent* ACatFishGuardActor::GetFishInventoryComponent() const
{
	return FishInventory;
}

// 页面类解析流程：同步加载鱼护自身配置的库存 View，并确认它就是鱼护页面类型；配置错成普通背包页时返回空，交给交互打开链路记录拒绝。
TSubclassOf<UCatFishGuardInventoryWidget> ACatFishGuardActor::LoadInventoryViewClass() const
{
	UClass* LoadedClass = InventoryViewClass.LoadSynchronous();
	if (!LoadedClass || !LoadedClass->IsChildOf(UCatFishGuardInventoryWidget::StaticClass()))
	{
		return nullptr;
	}
	return LoadedClass;
}

// 可交互判断流程：读取交互开关、请求 Controller、容器 ID 和复制组件；任一条件缺失都拒绝，让提示和执行入口保持同一套可用性边界。
bool ACatFishGuardActor::CanInteract_Implementation(AController* RequestingController) const
{
	const APlayerController* PlayerController = Cast<APlayerController>(RequestingController);
	return bInteractionEnabled && IsGrounded() && PlayerController && FishInventory;
}

// 提示文本流程：复用交互可用性的核心边界；禁用或库存组件缺失时返回空文本，避免玩家看到当前不可执行的鱼护提示。
FText ACatFishGuardActor::GetInteractionPrompt_Implementation() const
{
	return bInteractionEnabled && IsGrounded() && FishInventory ? InteractionPrompt : FText::GetEmpty();
}

// 距离读取流程：把编辑器配置的厘米值裁成非负有限数；异常值按 0 处理，让服务器距离复核保守失败。
double ACatFishGuardActor::GetInteractionRadius_Implementation() const
{
	return FMath::IsFinite(InteractionRadiusCentimeters)
		? FMath::Max(0.0, InteractionRadiusCentimeters) : 0.0;
}

bool ACatFishGuardActor::IsAuthorityRequestSpatiallyValid(const AController* RequestingController) const
{
	const APawn* Pawn = RequestingController ? RequestingController->GetPawn() : nullptr;
	const UCatInteractionSettings* Settings = GetDefault<UCatInteractionSettings>();
	UWorld* World = GetWorld();
	if (!HasAuthority() || !Pawn || !Settings || !World
		|| FVector::Dist(Pawn->GetPawnViewLocation(), GetActorLocation()) > GetInteractionRadius_Implementation())
	{
		return false;
	}
	if (!Settings->bRequireServerLineOfSight)
	{
		return true;
	}
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CatFishGuardLineOfSight), true, Pawn);
	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, Pawn->GetPawnViewLocation(), GetActorLocation(),
		Settings->TargetingTraceChannel, QueryParams);
	return !bHit || Hit.GetActor() == this;
}

// 鱼护交互流程：本地用鱼护对象自己的库存页打开本次命中的容器，并把同一接口请求转发给服务器；
// authority 若发现角色嘴上叼着鱼，则只向本 Actor 的正式库存提交。失败时鱼仍留在嘴上，绝不搜索其他鱼护。
bool ACatFishGuardActor::Interact_Implementation(AController* RequestingController, const FGuid RequestId)
{
	ACatfishingPlayerController* PlayerController = Cast<ACatfishingPlayerController>(RequestingController);
	if (!RequestId.IsValid() || !CanInteract_Implementation(RequestingController) || !PlayerController)
	{
		UE_LOG(LogCatFishContainers, Warning,
			TEXT("Event=fish_guard_interaction_rejected Reason=DependencyUnavailable Controller=%s Guard=%s"),
			*GetNameSafe(PlayerController), *GetNameSafe(this));
		return false;
	}

	bool bHandled = false;
	if (PlayerController->IsLocalController())
	{
		ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
		UCatLocalPlayerUISubsystem* UISubsystem = LocalPlayer
			? LocalPlayer->GetSubsystem<UCatLocalPlayerUISubsystem>() : nullptr;
		if (UISubsystem)
		{
			bHandled = UISubsystem->OpenInventory(FishInventory, LoadInventoryViewClass());
		}
	}

	if (!HasAuthority())
	{
		PlayerController->ServerRequestInteraction(this, RequestId);
		return true;
	}
	if (!IsAuthorityRequestSpatiallyValid(RequestingController))
	{
		UE_LOG(LogCatFishContainers, Warning,
			TEXT("Event=fish_guard_interaction_rejected Reason=PermissionDenied Controller=%s Guard=%s"),
			*GetNameSafe(PlayerController), *GetNameSafe(this));
		return bHandled;
	}

	ACatCharacter* Character = Cast<ACatCharacter>(PlayerController->GetPawn());
	if (ACatFishPickupActor* CarriedFish = ACatFishPickupActor::FindCarriedFish(Character))
	{
		const FCatCaptureCommitResult StoreResult = CarriedFish->StoreInFishGuardFromAuthority(
			PlayerController, RequestId, this);
		const bool bStored = StoreResult.Command.bCommitted
			|| StoreResult.Command.Error == ECatDomainCommandError::AlreadyResolved;
		UE_LOG(LogCatFishContainers, Log,
			TEXT("Event=fish_guard_store_carried_fish Guard=%s Committed=%s Error=%s Revision=%lld"),
			*GetNameSafe(this),
			bStored ? TEXT("true") : TEXT("false"),
			*UEnum::GetValueAsString(StoreResult.Command.Error), StoreResult.Command.Revision);
		bHandled = bHandled || bStored;
	}
	UE_LOG(LogCatFishContainers, Log,
		TEXT("Event=fish_guard_interaction_inventory_opened Guard=%s"), *GetNameSafe(this));
	return bHandled || Character != nullptr;
}
