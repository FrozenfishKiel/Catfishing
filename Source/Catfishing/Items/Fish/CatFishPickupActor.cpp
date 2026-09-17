#include "AbilitySystem/Core/CatAbilitySystemComponent.h"
#include "AbilitySystem/Tags/CatStateTags.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "Inventory/CatWorldDropProtectionComponent.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Condition/CatFishThrowEffectActor.h"
#include "Fishing/Integration/CatFishingResolutionSubsystem.h"
#include "AbilitySystem/Effects/CatFishingScoopCooldownEffect.h"
#include "Fishing/Integration/CatFishingCommandComponent.h"

#include "Algo/Unique.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/Skeleton.h"
#include "Character/CatCharacter.h"
#include "Collection/CatRunImprintService.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Condition/CatConditionComponent.h"
#include "Data/CatFishDefinition.h"
#include "Data/CatFishCatalogSettings.h"
#include "Engine/World.h"
#include "Framework/Game/CatGameplayTypes.h"
#include "Framework/Game/CatfishingGameState.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Interaction/CatInteractionSettings.h"
#include "FishContainers/CatFishPickupSettings.h"
#include "FishContainers/CatFishGuardActor.h"
#include "Fishing/Presentation/CatFishPresentationDefinition.h"
#include "Fishing/Presentation/CatFishGrounding.h"
#include "FishContainers/World/CatWorldSurfaceResolver.h"
#include "Engine/SkeletalMesh.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Inventory/CatInventoryComponent.h"
#include "Inventory/CatInventoryAccessRules.h"
#include "Inventory/CatInventoryStatics.h"
#include "Inventory/CatInventorySettings.h"
#include "Logging/CatLog.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsEngine/BodySetup.h"

// 构造流程：创建支持场景碰撞的复制根并忽略Pawn，鱼体仅负责冻结姿态与缩放；不在CDO阶段读取定义或创建捕获事实。
ACatFishPickupActor::ACatFishPickupActor()
{
	bReplicates = true;
	SetReplicateMovement(true);
	bAlwaysRelevant = false;
	PrimaryActorTick.bCanEverTick = false;

	WorldCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("WorldCollision"));
	SetRootComponent(WorldCollision);
	WorldCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	WorldCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	WorldCollision->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	WorldCollision->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	WorldCollision->SetNotifyRigidBodyCollision(true);
	WorldCollision->OnComponentHit.AddDynamic(this, &ThisClass::HandleThrownFishHit);
	InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
	InteractionSphere->SetupAttachment(WorldCollision);
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionSphere->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractionSphere->SetGenerateOverlapEvents(false);

	FishMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FishMesh"));
	FishMesh->SetupAttachment(WorldCollision);
	// 鱼体使用冻结的世界比例；猫身体/嘴 Socket 的缩放不能把同一条鱼缩小或放大。
	FishMesh->SetAbsolute(false, false, true);
	FishMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FishMesh->SetRenderCustomDepth(false);
}

// 生成流程：设置查询球半径后加载冻结姿态；网格和盒形根按同一身份定尺寸，不在每次位置复制时重建刚体。
void ACatFishPickupActor::BeginPlay()
{
	Super::BeginPlay();
	const UCatFishPickupSettings* Settings = GetDefault<UCatFishPickupSettings>();
	if (Settings && InteractionSphere)
	{
		InteractionSphere->SetSphereRadius(static_cast<float>(Settings->PickupCollisionRadiusCentimeters));
		const UCatInteractionSettings* InteractionSettings = GetDefault<UCatInteractionSettings>();
		if (InteractionSettings)
		{
			InteractionSphere->SetCollisionResponseToChannel(InteractionSettings->TargetingTraceChannel, ECR_Block);
		}
	}
	RefreshFishPresentation();
	RefreshCarryPresentation(true);
}

// 销毁收口流程：若本鱼仍是角色嘴部的唯一引用，先按本 Actor 清除；随后才交给父类释放组件，避免售鱼或容器析构留下失效复制指针。
void ACatFishPickupActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		if (ACatCharacter* Character = AuthorityCarrier.Get()) Character->ReleaseMouthCarriedActorFromAuthority(this);
	}
	Super::EndPlay(EndPlayReason);
}

void ACatFishPickupActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, PresentationState);
}

// 世界鱼初始化流程：先校验冻结身份、重量和表现尺度，再写入复制状态与捕获上下文，最后刷新网格；已归档库存鱼没有新捕获条件，不伪造地域/时段/天气。
bool ACatFishPickupActor::InitializeFromAuthority(const FGuid InFishingSessionId, const FGuid InFishInstanceId,
	UCatFishDefinition* InFishDefinition, const double InWeightKilograms, const double InVisualScale,
	const FCatCaptureConditionSnapshot& InCaptureCondition, const FString& InHookerStableNetId,
	const TArray<FString>& InFishingParticipantStableNetIds, const FVector GroundNormal)
{
	if (!HasAuthority() || bIdentityInitialized || !InFishingSessionId.IsValid() || !InFishInstanceId.IsValid()
		|| InFishingSessionId == InFishInstanceId || !InFishDefinition || !InFishDefinition->IsRuntimeDefinitionReady()
		|| !FMath::IsFinite(InWeightKilograms) || InWeightKilograms <= 0.0
		|| !FMath::IsFinite(InVisualScale) || InVisualScale <= 0.0
		|| (!bCaptureRecorded && InCaptureCondition.RegionId.IsNone()) || GroundNormal.ContainsNaN())
	{
		return false;
	}
	PresentationState.FishingSessionId = InFishingSessionId;
	PresentationState.FishInstanceId = InFishInstanceId;
	PresentationState.ItemId = InFishDefinition->ItemId;
	PresentationState.WeightKilograms = InWeightKilograms;
	PresentationState.VisualScale = InVisualScale;
	PresentationState.GroundNormal = GroundNormal.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector::UpVector);
	PresentationState.State = ECatFishPickupState::Available;
	FishDefinition = InFishDefinition;
	FishPresentationDefinition = InFishDefinition->LoadRuntimePresentationDefinition();
	CaptureCondition = InCaptureCondition;
	HookerStableNetId = InHookerStableNetId;
	FishingParticipantStableNetIds = InFishingParticipantStableNetIds;
	FishingParticipantStableNetIds.RemoveAll([](const FString& Entry) { return Entry.IsEmpty(); });
	FishingParticipantStableNetIds.Sort();
	FishingParticipantStableNetIds.SetNum(Algo::Unique(FishingParticipantStableNetIds));
	bIdentityInitialized = true;
	RefreshFishPresentation();
	// 旧生成入口传入的是地面接触点和侧躺Actor旋转；侧躺现在归网格，根保持水平并换算为真实物理中心。
	const FVector ContactPoint = GetActorLocation();
	SetActorRotation(FRotator(0.0, GetActorRotation().Yaw, 0.0));
	ApplyLandedVisualTransform();
	const double Lift = CatFishGrounding::ComputeVerticalLift(WorldCollision->CalcBounds(FTransform::Identity).GetBox(),
		GetActorTransform(), ContactPoint, PresentationState.GroundNormal);
	AddActorWorldOffset(FVector(0.0, 0.0, Lift), false, nullptr, ETeleportType::TeleportPhysics);
	ForceNetUpdate();
	UE_LOG(LogCatFishContainers, Log, TEXT("Event=fish_pickup_grounded SessionId=%s FishInstanceId=%s Pickup=%s Contact=%s Normal=%s MeshLocation=%s MeshScale=%s VisualScale=%.3f World=%s NetMode=%d Authority=%d Role=%s"),
		*InFishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *InFishInstanceId.ToString(EGuidFormats::DigitsWithHyphens), *GetName(),
		*ContactPoint.ToCompactString(), *PresentationState.GroundNormal.ToCompactString(),
		*FishMesh->GetComponentLocation().ToCompactString(), *FishMesh->GetComponentScale().ToCompactString(), InVisualScale,
		*GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), *UEnum::GetValueAsString(GetLocalRole()));
	return true;
}


namespace CatFishPickupPresentationPrivate
{
	static const TCHAR* NetModeValue(const ENetMode NetMode)
	{
		switch (NetMode)
		{
		case NM_Standalone: return TEXT("Standalone");
		case NM_DedicatedServer: return TEXT("DedicatedServer");
		case NM_ListenServer: return TEXT("ListenServer");
		case NM_Client: return TEXT("Client");
		default: return TEXT("Unknown");
		}
	}
}

// 鱼种资源刷新流程：
// 1. 没有网格组件、没有鱼种身份或已应用同一鱼种时直接返回，避免复制位置包反复加载资产。
// 2. Dedicated Server 只记录已应用的鱼种身份，不加载表现资产。
// 3. 客户端或 Listen Server 从鱼种表解析表现定义，校验 Mesh、落地动画和 Skeleton 兼容性；失败时清空表现并记录可排查日志。
// 4. 成功后保存落地/嘴叼两套基础相对姿态，冻结落地动画到最后一帧并刷新骨骼；后续只由 RefreshCarryPresentation 选择落地或嘴叼姿态。
void ACatFishPickupActor::RefreshFishPresentation()
{
	if (!FishMesh || (PresentationState.ItemId == 0)
		|| AppliedPresentationItemId == PresentationState.ItemId)
	{
		return;
	}
	if (GetNetMode() == NM_DedicatedServer)
	{
		AppliedPresentationItemId = PresentationState.ItemId;
		return;
	}

	if (!FishPresentationDefinition)
	{
		const UCatFishCatalogSettings* Catalog = GetDefault<UCatFishCatalogSettings>();
		const UCatFishDefinition* Definition = Catalog
			? Catalog->FindRuntimeDefinition(PresentationState.ItemId) : nullptr;
		FishPresentationDefinition = Definition ? Definition->LoadRuntimePresentationDefinition() : nullptr;
	}
	USkeletalMesh* Mesh = FishPresentationDefinition
		? FishPresentationDefinition->SkeletalMesh.LoadSynchronous() : nullptr;
	UAnimSequenceBase* LandedAnimation = FishPresentationDefinition
		? FishPresentationDefinition->LandedAnimation.LoadSynchronous() : nullptr;
	USkeleton* AnimationSkeleton = LandedAnimation ? LandedAnimation->GetSkeleton() : nullptr;
	const bool bCompatible = Mesh && LandedAnimation && AnimationSkeleton
		&& AnimationSkeleton->IsCompatibleMesh(Mesh);
	if (!FishPresentationDefinition || !bCompatible)
	{
		FishMesh->SetAnimation(nullptr);
		FishMesh->SetSkeletalMeshAsset(nullptr);
		UE_LOG(LogCatFishContainers, Warning,
			TEXT("Event=fish_pickup_presentation_rejected FishDefinition=%s FishInstanceId=%s Pickup=%s NetMode=%s Authority=%s Presentation=%s Mesh=%s LandedAnimation=%s AnimationSkeleton=%s Reason=%s"),
			*FString::FromInt(PresentationState.ItemId),
			*PresentationState.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(this),
			CatFishPickupPresentationPrivate::NetModeValue(GetNetMode()), HasAuthority() ? TEXT("true") : TEXT("false"),
			*GetNameSafe(FishPresentationDefinition), *GetNameSafe(Mesh), *GetNameSafe(LandedAnimation),
			*GetNameSafe(AnimationSkeleton), FishPresentationDefinition ? TEXT("AssetOrSkeletonMismatch") : TEXT("DefinitionChainMissing"));
		return;
	}

	LandedMeshBaseTransform = FishPresentationDefinition->LandedMeshRelativeTransform;
	CarriedMeshBaseTransform = FishPresentationDefinition->CarriedMeshRelativeTransform;
	FishMesh->SetSkeletalMeshAsset(Mesh);
	FishMesh->PlayAnimation(LandedAnimation, false);
	FishMesh->SetPosition(LandedAnimation->GetPlayLength(), false);
	FishMesh->SetPlayRate(0.0f);
	// 在第一帧显示前求出冻结姿态，避免用未初始化骨骼的包围盒计算贴地高度。
	FishMesh->TickAnimation(0.0f, false);
	FishMesh->RefreshBoneTransforms();
	AppliedPresentationItemId = PresentationState.ItemId;
	UE_LOG(LogCatFishContainers, Log,
		TEXT("Event=fish_pickup_presentation_applied FishDefinition=%s FishInstanceId=%s Pickup=%s NetMode=%s Authority=%s Presentation=%s Mesh=%s Skeleton=%s LandedAnimation=%s VisualScale=%.3f State=%s"),
		*FString::FromInt(PresentationState.ItemId),
		*PresentationState.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(this),
		CatFishPickupPresentationPrivate::NetModeValue(GetNetMode()), HasAuthority() ? TEXT("true") : TEXT("false"),
		*GetNameSafe(FishPresentationDefinition), *GetNameSafe(Mesh), *GetNameSafe(Mesh->GetSkeleton()),
		*GetNameSafe(LandedAnimation), PresentationState.VisualScale, *UEnum::GetValueAsString(PresentationState.State));
}

// 落地视觉流程：恢复定义局部姿态与侧躺角，再应用落地定义的冻结缩放；用鱼体包围盒设置物理半尺寸并将网格平移到根中心。
// 平移只改变表现相对根的位置，不再把根当作地面接触点，所以物理运动和位置复制无需反复补贴地偏移。
void ACatFishPickupActor::ApplyLandedVisualTransform()
{
	if (!FishMesh)
	{
		return;
	}
	FishMesh->SetRelativeTransform(LandedMeshBaseTransform);
	const double Roll = FishPresentationDefinition ? FishPresentationDefinition->LandedActorRollDegrees : 90.0;
	FishMesh->SetRelativeRotation(FQuat(FVector::ForwardVector, FMath::DegreesToRadians(Roll)) * LandedMeshBaseTransform.GetRotation());
	// 物理复制可能先于 Available 到达，此时也必须使用落地比例，不能从旧 Carried 状态选到嘴叼比例。
	const double Scale = FMath::IsFinite(PresentationState.VisualScale) && PresentationState.VisualScale > 0.0
		? PresentationState.VisualScale : 1.0;
	FishMesh->SetRelativeScale3D(LandedMeshBaseTransform.GetScale3D() * Scale);
	if (FishMesh->GetSkeletalMeshAsset())
	{
		const FBox MeshBounds = FishMesh->GetSkeletalMeshAsset()->GetBounds().GetBox()
			+ FishMesh->CalcBounds(FTransform::Identity).GetBox();
		const FTransform MeshToActor = FishMesh->GetComponentTransform().GetRelativeTransform(GetActorTransform());
		const FBox BodyBounds = MeshBounds.TransformBy(MeshToActor.ToMatrixWithScale());
		if (BodyBounds.IsValid && !BodyBounds.GetExtent().ContainsNaN())
		{
			WorldCollision->SetBoxExtent(BodyBounds.GetExtent().ComponentMax(FVector(1.0)), false);
			FishMesh->SetRelativeLocation(FishMesh->GetRelativeLocation() - BodyBounds.GetCenter());
		}
	}
}

// 嘴叼视觉流程：清掉仅为落地摆放准备的局部位置和旋转，让 FishMesh 原点直接跟随 MouthCarry 骨骼；重量冻结缩放保持不变。
void ACatFishPickupActor::ApplyCarriedVisualTransform()
{
	if (!FishMesh)
	{
		return;
	}
	FishMesh->SetRelativeTransform(CarriedMeshBaseTransform);
	const double Scale = FMath::IsFinite(PresentationState.VisualScale) && PresentationState.VisualScale > 0.0
		? PresentationState.VisualScale : 1.0;
	FishMesh->SetRelativeScale3D(CarriedMeshBaseTransform.GetScale3D() * Scale);
}

// 嘴部鱼查询流程：只把角色唯一复制引用转换为鱼类型；附着层仅负责表现，不能再被当作占用事实扫描。
ACatFishPickupActor* ACatFishPickupActor::FindCarriedFish(const ACatCharacter* Character)
{
	return Character ? Cast<ACatFishPickupActor>(Character->GetMouthCarriedActor()) : nullptr;
}

// 叼起流程：先复核 authority 与单嘴资格，已由当前请求预认领的同一 Actor 可以继续附着；再绑定宿主退出通知并停止物理、写携带状态。
// 附着失败撤回通知和状态；bPublish 为 false 时调用方会在库存静默移格成功后统一发布，避免客户端看到半提交携带。
bool ACatFishPickupActor::BeginMouthCarryFromAuthority(ACatCharacter* Character, APlayerState* PlayerState, const bool bPublish)
{
	const bool bAlreadyClaimed = Character && Character->GetMouthCarriedActor() == this;
	if (!HasAuthority() || !IsValid(Character) || !IsValid(PlayerState)
		|| Character->GetWorld() != GetWorld() || PlayerState->GetWorld() != GetWorld()
		|| Character->GetPlayerState() != PlayerState || !FishDefinition || !bIdentityInitialized
		|| IsActorBeingDestroyed() || PresentationState.State != ECatFishPickupState::Available
		|| !Character->GetMesh() || (!bAlreadyClaimed && !Character->TryClaimMouthCarriedActorFromAuthority(this)))
	{
		return false;
	}
	AuthorityCarrier = Character;
	Character->OnDestroyed.AddDynamic(this, &ThisClass::HandleAuthorityCarrierDestroyed);
	SetOwner(Character);
	SetInstigator(Character);
	DisarmThrowEffect();
	PresentationState.State = ECatFishPickupState::Carried;
	PresentationState.CarriedByPlayerState = PlayerState;
	// 从物理丢弃态拾回时先停止刚体；否则 Chaos 会在附着后继续把根组件移出嘴部。
	WorldCollision->SetSimulatePhysics(false);
	WorldCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ApplyLocalFocus(false);
	if (!AttachCarriedRootToMouth(Character))
	{
		DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		Character->OnDestroyed.RemoveDynamic(this, &ThisClass::HandleAuthorityCarrierDestroyed);
		AuthorityCarrier.Reset();
		SetOwner(nullptr);
		SetInstigator(nullptr);
		PresentationState.State = ECatFishPickupState::Available;
		PresentationState.CarriedByPlayerState = nullptr;
		Character->ReleaseMouthCarriedActorFromAuthority(this);
		InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		WorldCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		return false;
	}
	if (bPublish)
	{
		ForceNetUpdate();
		Character->ForceNetUpdate();
	}
	UE_LOG(LogCatFishContainers, Log,
		TEXT("Event=fish_pickup_mouth_attach_committed SessionId=%s FishInstanceId=%s Pickup=%s Carrier=%s ParentComponent=%s Socket=%s ActorRelative=%s NetMode=%d"),
		*PresentationState.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
		*PresentationState.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(this),
		*GetNameSafe(Character), *GetNameSafe(GetRootComponent() ? GetRootComponent()->GetAttachParent() : nullptr),
		GetRootComponent() ? *GetRootComponent()->GetAttachSocketName().ToString() : TEXT("None"),
		GetRootComponent() ? *GetRootComponent()->GetRelativeTransform().ToHumanReadableString() : TEXT("Invalid"),
		static_cast<int32>(GetNetMode()));
	return Character->GetMouthCarriedActor() == this && GetAttachParentActor() == Character;
}

// 保管实例核对流程：只比较已初始化的稳定鱼 GUID 与当前保管的同一实例；用于 Carry 预检，绝不在这里恢复、隐藏或切换运行宿主。
bool ACatFishPickupActor::CanCarryInventoryItemFromAuthority(const UCatFishInventoryItemInstance* ExpectedItem) const
{
	return HasAuthority() && !IsActorBeingDestroyed() && !bConsumptionCommitted && !AuthorityCarrier.IsValid()
		&& PresentationState.State == ECatFishPickupState::Available && bIdentityInitialized
		&& ExpectedItem != nullptr && InventoryItem == ExpectedItem
		&& ExpectedItem->GetItemInstanceId() == PresentationState.FishInstanceId;
}

// 保管态回滚流程：
// 1. 只接受当前仍关联的同一库存实例，避免失败回调覆盖其它请求已经重新绑定的鱼。
// 2. 结束本鱼嘴部附着并按 expected actor 清嘴，再隐藏/关闭碰撞回到容器内不可交互表现。
// 3. 提交前没有修改实例的 WorldActor 或运行宿主，因此这里也不写回；附着回调中已完成的库存转移必须保留。
void ACatFishPickupActor::RestoreInventoryRetentionFromAuthority(UCatFishInventoryItemInstance* ExpectedItem,
	const FTransform& ExpectedWorldTransform)
{
	if (!HasAuthority() || IsActorBeingDestroyed() || !ExpectedItem || InventoryItem != ExpectedItem) return;
	EndMouthCarryFromAuthority();
	SetActorTransform(ExpectedWorldTransform, false, nullptr, ETeleportType::TeleportPhysics);
	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);
	bConsumptionCommitted = false;
	ForceNetUpdate();
}

// 服务器附着流程：检查目标 Mesh 和配置，停止物理由上游携带事务完成；保持实物世界尺寸，仅写鱼专用的位置与朝向。
// 附件失败由调用方回滚单嘴占用和归属，客户端不会调用此方法或从 PlayerState 推导第二个附着目标。
bool ACatFishPickupActor::AttachCarriedRootToMouth(ACatCharacter* Character)
{
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (!HasAuthority() || !Mesh || !GetRootComponent()) return false;
	const UCatFishPickupSettings* Settings = GetDefault<UCatFishPickupSettings>();
	const FName Socket = Settings && Mesh->GetSkeletalMeshAsset() ? Settings->MouthCarrySocketName : NAME_None;
	const FTransform Relative = Settings ? Settings->MouthCarryRelativeTransform : FTransform::Identity;
	if (!AttachToComponent(Mesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket)) return false;
	GetRootComponent()->SetRelativeLocationAndRotation(Relative.GetLocation(), Relative.GetRotation());
	ApplyCarriedVisualTransform();
	return GetRootComponent()->GetAttachParent() == Mesh && GetRootComponent()->GetAttachSocketName() == Socket
		&& GetRootComponent()->GetRelativeTransform().EqualsNoScale(Relative, UE_KINDA_SMALL_NUMBER);
}

// 鱼体表现流程：
// 1. 读取共同基类已经应用到根组件上的附件和服务器物理状态，推导当前是否处于嘴叼表现。
// 2. 鱼种资源刚更新时强制重算网格姿态；普通复制包若碰撞模式未变化则快速返回，避免每包重算鱼体包围盒。
// 3. 根据推导结果切换嘴叼或落地网格姿态，并同步物理根与准星碰撞；嘴叼时清掉本地描边焦点。
// 4. 此处不修改根附件或物理模拟，迟到的 Carried/Available 不能将共同入口已落地的鱼重新挂回猫嘴。
void ACatFishPickupActor::RefreshCarryPresentation(const bool bForceRefresh)
{
	const bool bCarried = GetAttachParentActor() != nullptr
		&& !(HasAuthority() ? WorldCollision->IsSimulatingPhysics() : GetReplicatedMovement().bRepPhysics);
	const ECollisionEnabled::Type Collision = bCarried ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics;
	if (!bForceRefresh && WorldCollision->GetCollisionEnabled() == Collision) return;
	if (bCarried) ApplyCarriedVisualTransform();
	else ApplyLandedVisualTransform();
	WorldCollision->SetCollisionEnabled(Collision);
	InteractionSphere->SetCollisionEnabled(bCarried ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);
	if (bCarried) ApplyLocalFocus(false);
}

// 结束携带流程：移除配对宿主回调，清空服务器携带者，再保留世界变换解绑并清除所有者和表现归属。
// 不选择落点、不启用物理，调用方在完成自己的预检后决定固定落地或轻抛。
void ACatFishPickupActor::EndMouthCarryFromAuthority()
{
	DisarmThrowEffect();
	if (ACatCharacter* Character = AuthorityCarrier.Get())
	{
		Character->OnDestroyed.RemoveDynamic(this, &ThisClass::HandleAuthorityCarrierDestroyed);
		Character->ReleaseMouthCarriedActorFromAuthority(this);
	}
	AuthorityCarrier.Reset();
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	SetOwner(nullptr);
	SetInstigator(nullptr);
	PresentationState.State = ECatFishPickupState::Available;
	PresentationState.CarriedByPlayerState = nullptr;
}

// 鱼只提供自己的实例身份，公共释放据此处理实际库存归属，不创建替代鱼。
UCatInventoryItemInstance* ACatFishPickupActor::GetCarriedInventoryItem() const { return InventoryItem; }

// 鱼的预检流程：先确认这条鱼仍处于同一角色嘴叼且未被消费，再按表现资源计算侧躺后的真实盒形。
// 主动 Q 缺身份、Mesh 或碰撞配置时拒绝，并追加目标姿态扫掠与水面保护；强制释放可保留公共层位置或地面查询结果，均不修改实物。
bool ACatFishPickupActor::PrepareCarryRelease(ACatCharacter* Character, FTransform& Transform, bool bThrow) const
{
	if (bConsumptionCommitted || AuthorityCarrier.Get() != Character
		|| PresentationState.State != ECatFishPickupState::Carried) return false;
	// 强制退出不能被失效表现资源卡住；没有可计算的鱼体时保留公共层给出的原位置并继续清嘴，主动 Q 仍严格拒绝。
	if (!bIdentityInitialized || !FishDefinition || !FishMesh || !FishMesh->GetSkeletalMeshAsset() || !WorldCollision) return !bThrow;
	FTransform LandedTransform = LandedMeshBaseTransform;
	const double Roll = FishPresentationDefinition ? FishPresentationDefinition->LandedActorRollDegrees : 90.0;
	LandedTransform.SetRotation(FQuat(FVector::ForwardVector, FMath::DegreesToRadians(Roll)) * LandedMeshBaseTransform.GetRotation());
	LandedTransform.SetScale3D(LandedMeshBaseTransform.GetScale3D() * PresentationState.VisualScale / GetActorScale3D());
	const FBox MeshBounds = FishMesh->GetSkeletalMeshAsset()->GetBounds().GetBox() + FishMesh->CalcBounds(FTransform::Identity).GetBox();
	const FBox LandedBounds = MeshBounds.TransformBy(LandedTransform.ToMatrixWithScale());
	if (!LandedBounds.IsValid || LandedBounds.GetExtent().ContainsNaN()) return false;
	if (!bThrow)
	{
		const UCatFishPickupSettings* Settings = GetDefault<UCatFishPickupSettings>();
		const FCatWorldSurfaceResult Surface = FCatWorldSurfaceResolver::ResolveHighestBlockingSurface(
			GetWorld(), Transform.GetLocation(), Settings->LandingGroundTraceChannel, {this, Character});
		const FVector Contact = Surface.bSucceeded ? Surface.WorldPosition : Transform.GetLocation();
		Transform.SetRotation(FRotator(0, GetActorRotation().Yaw, 0).Quaternion());
		Transform.SetLocation(Contact);
		const double Lift = CatFishGrounding::ComputeVerticalLift(LandedBounds, Transform, Contact,
			Surface.bSucceeded ? Surface.SurfaceNormal : FVector::UpVector);
		Transform.AddToTranslation(FVector(0, 0, Lift));
		return true;
	}
	// 目标盒额外扩一毫米，保持旧丢弃对姿态换算误差的保护，不改投掷距离或速度。
	const FVector Extent = LandedBounds.GetExtent().ComponentMax(FVector(1.0)) * Transform.GetScale3D().GetAbs() + FVector(0.1);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(CatFishDropLandedBounds), false, Character);
	Query.AddIgnoredActor(this);
	FHitResult Hit;
	const UCatWaterQuerySubsystem* Water = GetWorld()->GetSubsystem<UCatWaterQuerySubsystem>();
	return !(Water && Water->DoesWorldDropSweepTouchWater(Transform.GetLocation(), Transform.GetLocation(), Extent.Size()))
		&& !GetWorld()->SweepSingleByChannel(Hit, Character->GetPawnViewLocation(), Transform.GetLocation(),
			Transform.GetRotation(), ECC_WorldDynamic, FCollisionShape::MakeBox(Extent), Query)
		&& !GetWorld()->OverlapBlockingTestByChannel(Transform.GetLocation(), Transform.GetRotation(),
			ECC_WorldDynamic, FCollisionShape::MakeBox(Extent), Query);
}

// 公共层完成归属和解绑后，鱼只清理专属宿主监听、恢复侧躺与交互，并按定义启用已有投掷效果。
// 根物理、初速度和网络发布由共同提交统一完成，避免鱼另走一套丢弃。
void ACatFishPickupActor::OnCarryReleased(ACatCharacter* Character, bool bThrow)
{
	DisarmThrowEffect();
	Character->OnDestroyed.RemoveDynamic(this, &ThisClass::HandleAuthorityCarrierDestroyed);
	AuthorityCarrier.Reset();
	PresentationState.State = ECatFishPickupState::Available;
	PresentationState.CarriedByPlayerState = nullptr;
	ApplyLandedVisualTransform();
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	bThrowEffectArmed = bThrow && FishDefinition->ThrowEffect.Kind != ECatFishThrowEffectKind::None;
	ThrowingCharacter = bThrowEffectArmed ? Character : nullptr;
	if (bThrowEffectArmed)
	{
		WorldCollision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
		WorldCollision->IgnoreActorWhenMoving(Character, true);
	}
}

// 宿主销毁后仍通过公共释放收口，保留该宿主最后位置作为强制落地候选。
void ACatFishPickupActor::HandleAuthorityCarrierDestroyed(AActor* DestroyedActor)
{
	const FVector DropLocation = DestroyedActor ? DestroyedActor->GetActorLocation() : GetActorLocation();
	ReleaseMouthCarryFromAuthority(DropLocation);
}

// 入护流程：校验操作者、原嘴叼鱼与目标库存，恢复或创建同身份鱼实例后静默整件收货。
// 成功按顺序归档新捕获、结束嘴部携带、隐藏原 Actor 为库存保管载体并发布库存；箱满或权限失败时继续叼着，绝不销毁原 Actor。
FCatCaptureCommitResult ACatFishPickupActor::StoreInFishGuardFromAuthority(AController* RequestingController,
	const FGuid RequestId, AActor* TargetInventoryHost)
{
	FCatCaptureCommitResult Result;
	Result.Command.RequestId = RequestId;
	Result.Command.Error = ECatDomainCommandError::InvalidPayload;
	APlayerState* PlayerState = RequestingController ? RequestingController->PlayerState : nullptr;
	ACatCharacter* Character = RequestingController ? Cast<ACatCharacter>(RequestingController->GetPawn()) : nullptr;
	const FString StableNetId = PlayerState && PlayerState->GetUniqueId().IsValid()
		? PlayerState->GetUniqueId()->ToString() : FString();
	UCatRunImprintService* Imprint = GetWorld() ? GetWorld()->GetSubsystem<UCatRunImprintService>() : nullptr;
	const auto Finish = [&]()
	{
		const FString Event = FString::Printf(TEXT("Event=fish_store_result RequestId=%s FishInstanceId=%s Actor=%s Target=%s Player=%s Committed=%d Error=%s World=%s NetMode=%d Authority=%d LocalRole=%d"),
			*RequestId.ToString(), *PresentationState.FishInstanceId.ToString(), *GetName(), *GetNameSafe(TargetInventoryHost), *GetNameSafe(Character),
			Result.Command.bCommitted, *UEnum::GetValueAsString(Result.Command.Error), *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole());
		if (Result.Command.bCommitted) { UE_LOG(LogCatFishContainers, Log, TEXT("%s"), *Event); }
		else { UE_LOG(LogCatFishContainers, Warning, TEXT("%s"), *Event); }
		return Result;
	};
	if (!HasAuthority() || bConsumptionCommitted || !RequestId.IsValid() || TargetInventoryHost == nullptr
		|| TargetInventoryHost->GetWorld() != GetWorld() || StableNetId.IsEmpty()
		|| !Character || PresentationState.State != ECatFishPickupState::Carried
		|| GetAttachParentActor() != Character || FindCarriedFish(Character) != this)
	{
		return Finish();
	}
	if (!Character->GetConditionComponent() || Character->GetCatAbilitySystemComponent()->HasMatchingGameplayTag(CatStateTags::Downed)
		|| !CatInventoryAccessRules::ResolveReachableFishContainer(TargetInventoryHost, Character))
	{
		Result.Command.Error = ECatDomainCommandError::PermissionDenied;
		UE_LOG(LogCatFishContainers, Warning, TEXT("Event=fish_store_rejected RequestId=%s FishInstanceId=%s Target=%s Player=%s Reason=InvalidGroundContainerOrReach World=%s NetMode=%d Authority=1 LocalRole=%d"),
			*RequestId.ToString(), *PresentationState.FishInstanceId.ToString(), *GetNameSafe(TargetInventoryHost), *GetNameSafe(Character), *GetNameSafe(GetWorld()), GetNetMode(), GetLocalRole());
		return Finish();
	}
	if (!FishDefinition || (!bCaptureRecorded && (!Imprint || !Imprint->CanRecordCommittedCapture())))
	{
		Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		return Finish();
	}

	UCatFishInventoryItemInstance* FishItemInstance = InventoryItem
		? InventoryItem.Get() : NewObject<UCatFishInventoryItemInstance>(TargetInventoryHost);
	if (FishItemInstance != nullptr && !InventoryItem)
	{
		FishItemInstance->SetItemDefinition(FishDefinition);
		FishItemInstance->InitializeFishFromAuthority(PresentationState.FishingSessionId,
			PresentationState.FishInstanceId, StableNetId, PresentationState.WeightKilograms);
	}

	FCatInventoryReceiveBatch ReceiveBatch;
	FCatInventoryInstanceEntry& FishEntry = ReceiveBatch.InstanceEntries.AddDefaulted_GetRef();
	FishEntry.ItemInstance = FishItemInstance;
	FishEntry.Count = 1;
	UCatInventoryComponent* TargetInventory = CatInventoryAccessRules::ResolveReachableFishContainer(TargetInventoryHost, Character);
	if (FishItemInstance == nullptr || TargetInventory == nullptr)
	{
		Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		return Finish();
	}
	// 收货会在最后才广播；先锁住本鱼，保留同一实例和 Actor，避免广播重入时将半提交状态当成可售库存。
	bConsumptionCommitted = true;
	bool bReceived = false;
	{
		TGuardValue<TWeakObjectPtr<UCatInventoryComponent>> StoreScope(InventoryStoreTarget, TargetInventory);
		bReceived = TargetInventory->TryAddInventoryBatch(ReceiveBatch, false);
	}
	if (!bReceived)
	{
		bConsumptionCommitted = false;
		Result.Command.Error = ECatDomainCommandError::CapacityExceeded;
		return Finish();
	}

	Result.Command.bCommitted = true;
	Result.Command.Error = ECatDomainCommandError::None;
	Result.Committed.CaptureRequestId = RequestId;
	Result.Committed.FishingSessionId = PresentationState.FishingSessionId;
	Result.Committed.FishInstance.FishInstanceId = PresentationState.FishInstanceId;
	Result.Committed.FishInstance.ItemId = PresentationState.ItemId;
	Result.Committed.FishInstance.OwnerStableNetId = StableNetId;
	Result.Committed.FishInstance.SourceFishingSessionId = PresentationState.FishingSessionId;
	Result.Committed.FishInstance.WeightKilograms = PresentationState.WeightKilograms;
	ArchiveCommittedCapture(Result.Committed, StableNetId);
	EndMouthCarryFromAuthority();
	PresentationState.CarriedByPlayerState = nullptr;
	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);
	InventoryItem = FishItemInstance;
	FishItemInstance->SetWorldActor(this);
	// 入护提交这一刻这条鱼才有正式归属；按实例自己记下的捕获者发布色环身份，不用本次提交者覆盖既有实例的原主。
	PublishOwnerPresentationFromAuthority(FishItemInstance->GetFishOwnerStableNetId());
	// 入护提交后 Actor 是静态保管载体而非消费终态；允许同一实例后续从库存重新叼起。
	bConsumptionCommitted = false;
	TargetInventory->BroadcastInventoryChange();
	ForceNetUpdate();
	return Finish();
}

// 库存落地流程：读取实物鱼实例与表现定义，标记为已捕获后复用世界鱼初始化；失败不保留实例，来源库存尚未扣除。
bool ACatFishPickupActor::InitializeFromInventoryFromAuthority(UCatInventoryItemInstance* Item, const int32 Quantity)
{
	UCatFishInventoryItemInstance* FishItem = Cast<UCatFishInventoryItemInstance>(Item);
	UCatFishDefinition* Definition = FishItem ? FishItem->GetFishDefinition() : nullptr;
	UCatFishPresentationDefinition* Presentation = Definition ? Definition->LoadRuntimePresentationDefinition() : nullptr;
	if (!HasAuthority() || Quantity != 1 || !FishItem || !Presentation) return false;
	// 已保管的同一 Actor 回到嘴部或地面时只恢复实例归属，不允许再次初始化而拒绝历史鱼。
	if (bIdentityInitialized)
	{
		if (InventoryItem && InventoryItem != FishItem) return false;
		InventoryItem = FishItem;
		InventoryItem->SetWorldActor(this);
		InventoryItem->SetRuntimeOwnerActor(this);
		PublishOwnerPresentationFromAuthority(FishItem->GetFishOwnerStableNetId());
		return FishItem->GetItemInstanceId() == PresentationState.FishInstanceId;
	}
	bCaptureRecorded = true;
	// 库存落地的鱼早已归档：没有新的捕获条件，也不再需要上钩者身份，两者都留空而不是猜一份。
	if (!InitializeFromAuthority(FishItem->GetSourceFishingSessionId(), FishItem->GetItemInstanceId(), Definition,
		FishItem->GetFishWeightKilograms(), Presentation->ComputeUniformVisualScale(FishItem->GetFishWeightKilograms()),
		FCatCaptureConditionSnapshot{}, FString(), {}))
	{
		bCaptureRecorded = false;
		return false;
	}
	InventoryItem = FishItem;
	InventoryItem->SetWorldActor(this);
	InventoryItem->SetRuntimeOwnerActor(this);
	PublishOwnerPresentationFromAuthority(FishItem->GetFishOwnerStableNetId());
	return true;
}

// Carry 初始化流程：只把同一鱼实例的冻结身份读入新载体；来源库存仍持有该实例，因此直到静默扣格成功前不写 WorldActor 或运行宿主。
bool ACatFishPickupActor::InitializeFromInventoryForCarryFromAuthority(UCatFishInventoryItemInstance* Item,
	const int32 Quantity)
{
	UCatFishPresentationDefinition* Presentation = Item && Item->GetFishDefinition()
		? Item->GetFishDefinition()->LoadRuntimePresentationDefinition() : nullptr;
	if (!HasAuthority() || Quantity != 1 || !Item || !Presentation || bIdentityInitialized) return false;
	bCaptureRecorded = true;
	// 同上：Carry 载体承的是同一条已归档的库存鱼，不带新捕获条件与上钩者。
	if (!InitializeFromAuthority(Item->GetSourceFishingSessionId(), Item->GetItemInstanceId(), Item->GetFishDefinition(),
		Item->GetFishWeightKilograms(), Presentation->ComputeUniformVisualScale(Item->GetFishWeightKilograms()),
		FCatCaptureConditionSnapshot{}, FString(), {}))
	{
		bCaptureRecorded = false;
		return false;
	}
	InventoryItem = Item;
	PublishOwnerPresentationFromAuthority(Item->GetFishOwnerStableNetId());
	return true;
}

// 鱼定义读取流程：直接返回初始化时确认的唯一资产；不按 UI 名称或客户端报价反推定义。
UCatFishDefinition* ACatFishPickupActor::GetFishDefinition() const
{
	return FishDefinition;
}

// 消费预检流程：确认服务器、有效实物和操作者，再检查世界可用或嘴叼归属；尚未归档的鱼还要求捕获命令门开放。
// 隐藏且仍由库存宿主持有的 Actor 只是容器保管载体，不能被陈旧引用直接消费；此处仍不检查游戏命令门或距离。
bool ACatFishPickupActor::CanConsumeFromAuthority(AController* RequestingController) const
{
	const APlayerState* PlayerState = RequestingController ? RequestingController->PlayerState : nullptr;
	const UCatRunImprintService* Imprint = GetWorld() ? GetWorld()->GetSubsystem<UCatRunImprintService>() : nullptr;
	const bool bAvailableWorldFish = PresentationState.State == ECatFishPickupState::Available
		&& !IsHidden() && (!InventoryItem || InventoryItem->GetRuntimeOwnerActor() == this);
	return HasAuthority() && !IsActorBeingDestroyed() && !bConsumptionCommitted && bIdentityInitialized
		&& FishDefinition && PresentationState.FishInstanceId.IsValid() && !(PresentationState.ItemId == 0)
		&& FMath::IsFinite(PresentationState.WeightKilograms) && PresentationState.WeightKilograms > 0.0
		&& RequestingController && RequestingController->GetWorld() == GetWorld()
		&& PlayerState && PlayerState->GetUniqueId().IsValid()
		&& (bAvailableWorldFish
			|| (PresentationState.State == ECatFishPickupState::Carried
				&& RequestingController->GetPawn() == AuthorityCarrier.Get()))
		&& (bCaptureRecorded || (Imprint && Imprint->CanRecordCommittedCapture()));
}

// 实物消费准备流程：先复核身份、归属、请求者和捕获归档依赖，再写入同请求预留并关闭交互/二次消费入口；准备阶段不触发捕获归档或世界表现变化。
bool ACatFishPickupActor::PrepareConsumptionFromAuthority(AController* Controller, FGuid RequestId)
{
	if (!RequestId.IsValid() || !CanConsumeFromAuthority(Controller)) return false;
	PreparedConsumptionRequest = RequestId;
	bConsumptionCommitted = true;
	return true;
}

// 实物消费完成流程：
// 1. 只接受准备阶段记录的同一 RequestId，拒绝其它事务误释放本鱼。
// 2. 取消时只打开消费门并保留实物；接受时不重新做可能失败的资格检查。
// 3. 接受后按原规则补捕获归档、结束嘴部携带、隐藏并关闭碰撞，最后销毁原 Actor。
// 4. 外层保证同一同步调用栈内完成；批量献祭先准备全部实物，再调用本入口逐个收口。
void ACatFishPickupActor::FinishConsumptionFromAuthority(AController* RequestingController, FGuid RequestId, bool bCommit)
{
	if (!RequestId.IsValid() || PreparedConsumptionRequest != RequestId) return;
	PreparedConsumptionRequest.Invalidate();
	if (!bCommit) { bConsumptionCommitted = false; return; }
	const FString StableNetId = RequestingController->PlayerState->GetUniqueId()->ToString();
	if (!bCaptureRecorded)
	{
		FCatCaptureCommittedResult Capture;
		Capture.CaptureRequestId = PresentationState.FishInstanceId;
		Capture.FishingSessionId = PresentationState.FishingSessionId;
		Capture.FishInstance.FishInstanceId = PresentationState.FishInstanceId;
		Capture.FishInstance.ItemId = PresentationState.ItemId;
		Capture.FishInstance.SourceFishingSessionId = PresentationState.FishingSessionId;
		Capture.FishInstance.OwnerStableNetId = StableNetId;
		Capture.FishInstance.WeightKilograms = PresentationState.WeightKilograms;
		ArchiveCommittedCapture(Capture, StableNetId);
	}
	EndMouthCarryFromAuthority();
	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);
	UE_LOG(LogCatFishContainers, Log,
		TEXT("Event=world_fish_consumed Request=%s FishInstanceId=%s Definition=%s WeightKg=%.6f Recorded=%d World=%s NetMode=%d Authority=%d LocalRole=%d"),
		*RequestId.ToString(), *PresentationState.FishInstanceId.ToString(), *FString::FromInt(PresentationState.ItemId),
		PresentationState.WeightKilograms, bCaptureRecorded, *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole());
	if (!Destroy())
	{
		UE_LOG(LogCatFishContainers, Error, TEXT("Event=world_fish_destroy_failed Request=%s FishInstanceId=%s Actor=%s"),
			*RequestId.ToString(), *PresentationState.FishInstanceId.ToString(), *GetName());
	}
}

// 单鱼出售/消费流程：先走同一准备门独占实物，再执行可选提交回调；回调拒绝时释放预留并保留实物，接受时复用完成入口归档和销毁。
bool ACatFishPickupActor::ConsumeFromAuthority(AController* RequestingController, FGuid RequestId, TFunction<bool()> CommitBeforeConsumption)
{
	if (!PrepareConsumptionFromAuthority(RequestingController, RequestId)) return false;
	const bool bAccepted = !CommitBeforeConsumption || CommitBeforeConsumption();
	FinishConsumptionFromAuthority(RequestingController, RequestId, bAccepted);
	return bAccepted;
}

void ACatFishPickupActor::ApplyLocalFocus(const bool bFocused)
{
	bLocallyFocused = bFocused && PresentationState.State == ECatFishPickupState::Available;
	if (FishMesh)
	{
		const UCatInteractionSettings* Settings = GetDefault<UCatInteractionSettings>();
		FishMesh->SetCustomDepthStencilValue(Settings ? Settings->FocusStencilValue : 1);
		FishMesh->SetRenderCustomDepth(bLocallyFocused);
	}
}

// 可交互查询流程：排除提交期间、已消费实物和库存隐藏保管 Actor，再检查请求者存在、地面状态与冻结身份；空间和身体校验留给交互提交。
bool ACatFishPickupActor::CanInteract_Implementation(AController* RequestingController) const
{
	return !bConsumptionCommitted && RequestingController
		&& PresentationState.State == ECatFishPickupState::Available
		&& !IsHidden() && (!InventoryItem || InventoryItem->GetRuntimeOwnerActor() == this)
		&& PresentationState.FishingSessionId.IsValid()
		&& PresentationState.FishInstanceId.IsValid();
}

void ACatFishPickupActor::BeginLocalFocus_Implementation()
{
	ApplyLocalFocus(true);
}

void ACatFishPickupActor::EndLocalFocus_Implementation()
{
	ApplyLocalFocus(false);
}

FText ACatFishPickupActor::GetInteractionPrompt_Implementation() const
{
	if (PresentationState.State != ECatFishPickupState::Available || IsHidden()
		|| (InventoryItem && InventoryItem->GetRuntimeOwnerActor() != this))
	{
		return FText::GetEmpty();
	}
	return FText::Format(NSLOCTEXT("Catfishing", "FishPickupPrompt", "叼起 {0}  {1} kg"),
		FText::AsNumber(PresentationState.ItemId),
		FText::AsNumber(PresentationState.WeightKilograms));
}

// 可拾距离：落岸鱼直接读全游戏统一交互半径 1.5 米（钓鱼规则 §5.5:273「落岸鱼的可拾距离是 1.5 米，
// 全游戏统一交互半径，与抄网射程是两个参数」）。本 Actor 不再维护第二个距离，也不复用抄网射程。
double ACatFishPickupActor::GetInteractionRadius_Implementation() const
{
	const UCatInteractionSettings* Settings = GetDefault<UCatInteractionSettings>();
	return Settings ? Settings->GetInteractionRadiusCentimeters() : 0.0;
}

bool ACatFishPickupActor::IsAuthorityRequestSpatiallyValid(const AController* RequestingController) const
{
	const APawn* Pawn = RequestingController ? RequestingController->GetPawn() : nullptr;
	const UCatInteractionSettings* Settings = GetDefault<UCatInteractionSettings>();
	UWorld* World = GetWorld();
	// 墓碑（2026-09-14，T15 验收回退）：§5.5 的 150cm 是玩法半径，不授权抹掉网络余量。
	// 依 CatInteractionSettings 2026-09-12 统一半径裁决，服务器仍加技术容差，避免准星亮却按不动；身体→碰撞中心测量保留。
	const double ServerDistance = Settings ? Settings->GetServerInteractionDistanceCentimeters() : 0.0;
	if (!HasAuthority() || !Pawn || !Settings || !World || Pawn->GetWorld() != World
		|| RequestingController->GetWorld() != World || ServerDistance <= 0.0
		|| FVector::Dist(Pawn->GetActorLocation(), GetFishingCollisionCenter()) > ServerDistance)
	{
		return false;
	}
	if (!Settings->bRequireServerLineOfSight)
	{
		return true;
	}
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CatFishPickupLineOfSight), true, Pawn);
	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, Pawn->GetPawnViewLocation(), GetFishingCollisionCenter(),
		Settings->TargetingTraceChannel, QueryParams);
	return !bHit || Hit.GetActor() == this;
}

// 嘴叼拾取流程：客户端只把同一个 Actor 接口请求转发到服务器；服务器复核身份、身体状态和空间后，
// 把这条世界鱼附着到角色嘴部。鱼仍由本 Actor 承载，不进入 Equipment 背包，也不会在拾取时猜测目标鱼护。
bool ACatFishPickupActor::Interact_Implementation(AController* RequestingController,
	const FGuid RequestId)
{
	if (!HasAuthority())
	{
		ACatfishingPlayerController* PlayerController = Cast<ACatfishingPlayerController>(RequestingController);
		if (!PlayerController || !PlayerController->IsLocalController() || !RequestId.IsValid())
		{
			return false;
		}
		PlayerController->ServerRequestInteraction(this, RequestId);
		return true;
	}

	if (UCatFishingResolutionSubsystem* Queue = GetWorld()->GetSubsystem<UCatFishingResolutionSubsystem>())
	{
		Queue->Enqueue(ECatFishingResolution::Catch, RequestingController, RequestId,
			[WeakThis = TWeakObjectPtr<ThisClass>(this), Requester = TWeakObjectPtr<AController>(RequestingController), RequestId]()
			{
				if (WeakThis.IsValid() && Requester.IsValid()) WeakThis->ResolveFishingPickupFromAuthority(Requester.Get(), RequestId);
			});
		return true; // 仅受理；最终结果由仲裁回执给出。
	}
	return false;
}

bool ACatFishPickupActor::ResolveFishingPickupFromAuthority(AController* RequestingController, const FGuid RequestId)
{
	APlayerState* PlayerState = RequestingController ? RequestingController->PlayerState : nullptr;
	const FString StableNetId = PlayerState && PlayerState->GetUniqueId().IsValid()
		? PlayerState->GetUniqueId()->ToString() : FString();
	const FString CacheKey = FString::Printf(TEXT("%s|%s"), *StableNetId,
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	if (PickupTerminalByRequester.Contains(CacheKey))
	{
		return PickupTerminalByRequester[CacheKey].bCommitted;
	}
	FCatDomainCommandResult Terminal;
	Terminal.RequestId = RequestId;
	ACatCharacter* Character = RequestingController ? Cast<ACatCharacter>(RequestingController->GetPawn()) : nullptr;
	UCatAbilitySystemComponent* Condition = Character ? Character->GetCatAbilitySystemComponent() : nullptr;
	if (!HasAuthority() || !bIdentityInitialized || !RequestId.IsValid() || StableNetId.IsEmpty())
	{
		Terminal.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (PresentationState.State != ECatFishPickupState::Available || IsHidden()
		|| (InventoryItem && InventoryItem->GetRuntimeOwnerActor() != this))
	{
		Terminal.Error = ECatDomainCommandError::AlreadyResolved;
	}
	else if (!Character || UCatGE_FishingScoopCooldown::IsOperationBlocked(Character) || !Condition || Condition->HasMatchingGameplayTag(CatStateTags::Downed) || !IsAuthorityRequestSpatiallyValid(RequestingController))
	{
		Terminal.Error = ECatDomainCommandError::PermissionDenied;
	}
	else if (!FishDefinition)
	{
		Terminal.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else if (Character->GetMouthCarriedActor() != nullptr)
	{
		Terminal.Error = ECatDomainCommandError::InvalidPhase;
	}
	else
	{
		Terminal.bCommitted = BeginMouthCarryFromAuthority(Character, PlayerState);
		Terminal.Error = Terminal.bCommitted ? ECatDomainCommandError::None
			: ECatDomainCommandError::DependencyUnavailable;
	}
	PickupTerminalByRequester.Add(CacheKey, Terminal);
	if (!Terminal.bCommitted) UCatGE_FishingScoopCooldown::ApplyMissFromAuthority(RequestingController);
	if (ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(RequestingController))
	{
		FCatFishingCommandResult Result;
		Result.CommandType = ECatFishingCommandType::RequestScoop;
		Result.RequestId = RequestId;
		Result.bCommitted = Terminal.bCommitted;
		Result.Error = Terminal.bCommitted ? ECatFishingCommandError::None : ECatFishingCommandError::StaleScoopTarget;
		if (UCatFishingCommandComponent* Commands = Controller->FindComponentByClass<UCatFishingCommandComponent>()) Commands->DeliverResultFromAuthority(Result);
	}
	UE_LOG(LogCatFishContainers, Log, TEXT("Event=fish_pickup_resolved RequestId=%s FishInstanceId=%s Player=%s Committed=%d World=%s NetMode=%d Authority=1 LocalRole=%d"),
		*RequestId.ToString(), *PresentationState.FishInstanceId.ToString(), *GetNameSafe(RequestingController), Terminal.bCommitted,
		*GetNameSafe(GetWorld()), int32(GetNetMode()), int32(GetLocalRole()));
	return Terminal.bCommitted;
}

// 归属发布流程：只在服务器把实物鱼实例记的捕获者 StableNetId 现场解析成同 World 的 PlayerState，再写进复制表现供表现层画主人色环。
// 身份字符串仍留在服务器；尚未归档、捕获者已离场或重连换了 PlayerState 时发布空归属，等下一次入护/落地/取回重新解析，不在这里缓存身份字符串。
// 只写表现字段，不新建也不改写实例归属；空归属与有归属都按同一条复制路径通知蓝图。
void ACatFishPickupActor::PublishOwnerPresentationFromAuthority(const FString& InOwnerStableNetId)
{
	const UWorld* World = GetWorld();
	if (!HasAuthority() || !World) return;
	APlayerState* ResolvedOwner = nullptr;
	if (!InOwnerStableNetId.IsEmpty())
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Controller = It->Get();
			APlayerState* CandidatePlayerState = Controller ? Controller->PlayerState : nullptr;
			if (CandidatePlayerState && CandidatePlayerState->GetUniqueId().IsValid()
				&& CandidatePlayerState->GetUniqueId()->ToString() == InOwnerStableNetId)
			{
				ResolvedOwner = CandidatePlayerState;
				break;
			}
		}
	}
	if (PresentationState.OwnerPlayerState == ResolvedOwner) return;
	PresentationState.OwnerPlayerState = ResolvedOwner;
	ForceNetUpdate();
	UE_LOG(LogCatFishContainers, Log,
		TEXT("Event=fish_pickup_owner_published FishInstanceId=%s Pickup=%s Owner=%s Resolved=%d World=%s NetMode=%d Authority=%d"),
		*PresentationState.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(this),
		*GetNameSafe(ResolvedOwner), ResolvedOwner != nullptr, *GetNameSafe(World), GetNetMode(), HasAuthority());
}

// 捕获归档流程：已入库的鱼直接跳过；新捕获先写 FishRecorded，再按现有配置提交可选印记候选与成像计划。
// 两条归属分开：图鉴收集层写给上钩者（钓鱼规则 §5.6:285、图鉴 §3.1.4:111「收集层归上钩者」），
// 演出贡献名单与实物归属仍认这次把鱼收进来的人。上钩者缺失（库存鱼、旧存档）时才退回收件人＝拾取者。
void ACatFishPickupActor::ArchiveCommittedCapture(const FCatCaptureCommittedResult& Committed,
	const FString& PickerStableNetId)
{
	UCatRunImprintService* Imprint = GetWorld() ? GetWorld()->GetSubsystem<UCatRunImprintService>() : nullptr;
	if (bCaptureRecorded || !Imprint || !FishDefinition)
	{
		return;
	}
	const FString CollectionRecipientStableNetId = HookerStableNetId.IsEmpty()
		? PickerStableNetId : HookerStableNetId;
	// 首钓判定必须在写 FishRecorded 之前问：这一份 Grant 自己就会把该鱼种记进本局事实。
	const bool bFirstRecordOfThisSpecies = Imprint->IsFirstFishRecordForRecipient(
		CollectionRecipientStableNetId, Committed.FishInstance.ItemId);
	const FGuid FishRecordedGrantId = Imprint->RecordCommittedCapture(Committed,
		CollectionRecipientStableNetId, CaptureCondition);
	bCaptureRecorded = FishRecordedGrantId.IsValid();
	UE_LOG(LogCatFishContainers, Log,
		TEXT("Event=fish_capture_archived FishInstanceId=%s Definition=%s RecipientResolved=%s RecipientIsHooker=%s "
			"FirstRecord=%s Region=%s TimeOfDay=%s Weather=%s Granted=%s"),
		*Committed.FishInstance.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*FString::FromInt(Committed.FishInstance.ItemId),
		CollectionRecipientStableNetId.IsEmpty() ? TEXT("false") : TEXT("true"),
		HookerStableNetId.IsEmpty() ? TEXT("false") : TEXT("true"),
		bFirstRecordOfThisSpecies ? TEXT("true") : TEXT("false"),
		*CaptureCondition.RegionId.ToString(), *CaptureCondition.TimeOfDayId.ToString(),
		*CaptureCondition.WeatherId.ToString(), FishRecordedGrantId.IsValid() ? TEXT("true") : TEXT("false"));
	// 印记只给首钓新鱼种（图鉴 §3.1.8:149）；同一种鱼的第二条起不再成像，印记册「宁缺毋滥」。
	if (FishDefinition->CaptureImprintEventId.IsNone() || !bFirstRecordOfThisSpecies)
	{
		return;
	}
	FCatImprintCandidate Candidate;
	Candidate.CandidateId = Committed.FishInstance.FishInstanceId;
	if (const ACatfishingGameState* GameState = GetWorld()->GetGameState<ACatfishingGameState>())
	{
		Candidate.RunId = GameState->GetRunPublicState().Phase.RunId;
	}
	Candidate.EventType = FishDefinition->CaptureImprintEventId;
	Candidate.SubjectId = Committed.FishInstance.FishInstanceId;
	Candidate.ItemId = Committed.FishInstance.ItemId;
	Candidate.ParticipantStableNetIds = FishingParticipantStableNetIds;
	Candidate.ParticipantStableNetIds.AddUnique(PickerStableNetId);
	Candidate.ParticipantStableNetIds.Sort();
	Candidate.ParticipantCount = Candidate.ParticipantStableNetIds.Num();
	TArray<FCatCapturePlan> Plans;
	if (FishRecordedGrantId.IsValid() && Imprint->CanAcceptImprintCandidate(Candidate)
		&& Imprint->SubmitImprintCandidate(Candidate))
	{
		Imprint->CreateCapturePlansForParticipants(Candidate.CandidateId,
			Candidate.ParticipantStableNetIds, false, Plans);
	}
}

// 鱼身份复制流程：先解析鱼种网格，再按共同基类当前已经应用的根附件刷新鱼体姿态与碰撞。
// 之后通知蓝图并记录身份或携带状态变化；本回调不操作根组件物理和附着，也不会用表现状态重新推导携带者。
void ACatFishPickupActor::OnRep_PresentationState(const FCatFishPickupPresentationState& Previous)
{
	RefreshFishPresentation();
	RefreshCarryPresentation(true);
	BP_OnPickupPresentationChanged(Previous, PresentationState);
	if (Previous.FishInstanceId != PresentationState.FishInstanceId || Previous.State != PresentationState.State)
	{
		UE_LOG(LogCatFishContainers, Log, TEXT("Event=fish_pickup_ground_received SessionId=%s FishInstanceId=%s Pickup=%s State=%s Contact=%s Normal=%s VisualScale=%.3f MeshScale=%s World=%s NetMode=%d Role=%s"),
			*PresentationState.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *PresentationState.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
			*GetName(), *UEnum::GetValueAsString(PresentationState.State), *GetActorLocation().ToCompactString(),
			*PresentationState.GroundNormal.ToCompactString(), PresentationState.VisualScale, *FishMesh->GetComponentScale().ToCompactString(),
			*GetNameSafe(GetWorld()), GetNetMode(), *UEnum::GetValueAsString(GetLocalRole()));
	}
}

FVector ACatFishPickupActor::GetFishingCollisionCenter() const
{
	return WorldCollision->Bounds.Origin;
}

void ACatFishPickupActor::DisarmThrowEffect()
{
	bThrowEffectArmed = false;
	if (WorldCollision)
	{
		WorldCollision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		if (ThrowingCharacter.IsValid()) WorldCollision->IgnoreActorWhenMoving(ThrowingCharacter.Get(), false);
	}
	ThrowingCharacter.Reset();
}

void ACatFishPickupActor::HandleThrownFishHit(UPrimitiveComponent*, AActor* Other,
	UPrimitiveComponent*, FVector, const FHitResult& Hit)
{
	if (!HasAuthority() || !bThrowEffectArmed || Other == ThrowingCharacter.Get()) return;
	DisarmThrowEffect(); // 第一次非投掷者命中消费机会；弹跳不能重复施加。
	if (!FishDefinition) return;
	const FCatFishThrowEffect& Effect = FishDefinition->ThrowEffect;
	ACatCharacter* Target = Cast<ACatCharacter>(Other);
	if (Effect.Kind == ECatFishThrowEffectKind::KnockbackStartle && !Target) return;
	auto* Receiver = GetWorld()->SpawnActor<ACatFishThrowEffectActor>(Hit.ImpactPoint, FRotator::ZeroRotator);
	if (!Receiver || !Receiver->InitializeFromAuthority(Effect, Target, PresentationState.FishInstanceId))
	{
		if (Receiver) Receiver->Destroy();
		UE_LOG(LogCatFishContainers, Warning, TEXT("Event=fish_throw_rejected FishInstanceId=%s World=%s NetMode=%d Authority=1 LocalRole=%d Reason=EffectConfigurationUnavailable"),
			*PresentationState.FishInstanceId.ToString(), *GetNameSafe(GetWorld()), GetNetMode(), GetLocalRole());
	}
}
