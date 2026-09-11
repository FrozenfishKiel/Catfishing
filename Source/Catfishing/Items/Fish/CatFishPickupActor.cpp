#include "Items/Fish/CatFishPickupActor.h"

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
#include "Inventory/CatInventoryStatics.h"
#include "Inventory/CatInventorySettings.h"
#include "Logging/CatLog.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsEngine/BodySetup.h"
#include "TimerManager.h"

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
	if (PresentationState.State == ECatFishPickupState::Carried)
	{
		ApplyCarriedVisualTransform();
		ReconcileAttachmentFromPresentation(TEXT("BeginPlay"));
	}
	else
	{
		ApplyLandedVisualTransform();
	}
}

// 非物理态先按原鱼当前世界尺寸修正待应用的附着缩放，再让引擎处理附件并收敛表现。
// 不用量化后的缩放覆盖原鱼；物理态仍跳过旧附件，避免丢弃后被迟到消息拉回嘴部。
void ACatFishPickupActor::OnRep_AttachmentReplication()
{
	if (!GetReplicatedMovement().bRepPhysics)
	{
		if (RootComponent && AttachmentReplication.AttachParent)
		{
			USceneComponent* Parent = AttachmentReplication.AttachComponent ? AttachmentReplication.AttachComponent.Get()
				: AttachmentReplication.AttachParent->GetRootComponent();
			if (Parent)
			{
				AttachmentReplication.RelativeScale3D = RootComponent->IsUsingAbsoluteScale() ? GetActorScale3D()
					: GetActorTransform().GetRelativeTransform(Parent->GetSocketTransform(AttachmentReplication.AttachSocket)).GetScale3D();
			}
		}
		Super::OnRep_AttachmentReplication();
	}
	ReconcileAttachmentFromPresentation(TEXT("AttachmentReplication"));
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

// 先由引擎同步刚体和位置，再处理可能晚于表现到达的物理标志；统一收敛负责取消嘴部重试。
void ACatFishPickupActor::OnRep_ReplicatedMovement()
{
	Super::OnRep_ReplicatedMovement();
	// 正常地面物理更新只走引擎；旧 Carried 遇到首个物理包时以尚关闭的碰撞识别一次纠错，不每包重建几何。
	if (GetAttachParentActor() || (PresentationState.State == ECatFishPickupState::Carried
		&& (!GetReplicatedMovement().bRepPhysics || WorldCollision->GetCollisionEnabled() == ECollisionEnabled::NoCollision)))
	{
		ReconcileAttachmentFromPresentation(TEXT("ReplicatedMovement"));
	}
}

void ACatFishPickupActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, PresentationState);
}

// 世界鱼初始化流程：先校验冻结身份、重量和表现尺度，再写入复制状态与捕获上下文，最后刷新网格；已归档库存鱼没有新捕获地域，不伪造 RegionId。
bool ACatFishPickupActor::InitializeFromAuthority(const FGuid InFishingSessionId, const FGuid InFishInstanceId,
	UCatFishDefinition* InFishDefinition, const double InWeightKilograms, const double InVisualScale, const FName InRegionId,
	const TArray<FString>& InFishingParticipantStableNetIds, const FVector GroundNormal)
{
	if (!HasAuthority() || bIdentityInitialized || !InFishingSessionId.IsValid() || !InFishInstanceId.IsValid()
		|| InFishingSessionId == InFishInstanceId || !InFishDefinition || !InFishDefinition->IsRuntimeDefinitionReady()
		|| !FMath::IsFinite(InWeightKilograms) || InWeightKilograms <= 0.0
		|| !FMath::IsFinite(InVisualScale) || InVisualScale <= 0.0
		|| (!bCaptureRecorded && InRegionId.IsNone()) || GroundNormal.ContainsNaN())
	{
		return false;
	}
	PresentationState.FishingSessionId = InFishingSessionId;
	PresentationState.FishInstanceId = InFishInstanceId;
	PresentationState.FishDefinitionId = InFishDefinition->FishDefinitionId;
	PresentationState.WeightKilograms = InWeightKilograms;
	PresentationState.VisualScale = InVisualScale;
	PresentationState.GroundNormal = GroundNormal.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector::UpVector);
	PresentationState.State = ECatFishPickupState::Available;
	FishDefinition = InFishDefinition;
	FishPresentationDefinition = InFishDefinition->LoadRuntimePresentationDefinition();
	RegionId = InRegionId;
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

void ACatFishPickupActor::RefreshFishPresentation()
{
	if (!FishMesh || PresentationState.FishDefinitionId.IsNone()
		|| AppliedPresentationFishDefinitionId == PresentationState.FishDefinitionId)
	{
		return;
	}
	if (GetNetMode() == NM_DedicatedServer)
	{
		AppliedPresentationFishDefinitionId = PresentationState.FishDefinitionId;
		return;
	}

	if (!FishPresentationDefinition)
	{
		const UCatFishCatalogSettings* Catalog = GetDefault<UCatFishCatalogSettings>();
		const UCatFishDefinition* Definition = Catalog
			? Catalog->FindRuntimeDefinition(PresentationState.FishDefinitionId) : nullptr;
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
			*PresentationState.FishDefinitionId.ToString(),
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
	AppliedPresentationFishDefinitionId = PresentationState.FishDefinitionId;
	UE_LOG(LogCatFishContainers, Log,
		TEXT("Event=fish_pickup_presentation_applied FishDefinition=%s FishInstanceId=%s Pickup=%s NetMode=%s Authority=%s Presentation=%s Mesh=%s Skeleton=%s LandedAnimation=%s VisualScale=%.3f State=%s"),
		*PresentationState.FishDefinitionId.ToString(),
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
	ApplyVisualScale();
}

// 视觉缩放流程：落地与嘴叼共用服务器冻结的鱼体比例；只缩放 FishMesh，不缩放交互根、附着关系或碰撞半径。
void ACatFishPickupActor::ApplyVisualScale()
{
	if (!FishMesh)
	{
		return;
	}
	const FVector BaseScale = PresentationState.State == ECatFishPickupState::Carried
		? CarriedMeshBaseTransform.GetScale3D() : LandedMeshBaseTransform.GetScale3D();
	const double Scale = FMath::IsFinite(PresentationState.VisualScale) && PresentationState.VisualScale > 0.0
		? PresentationState.VisualScale : 1.0;
	FishMesh->SetRelativeScale3D(BaseScale * Scale);
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
	if (!HasAuthority() || !Character || !PlayerState || PresentationState.State != ECatFishPickupState::Available
		|| !Character->GetMesh() || (!bAlreadyClaimed && !Character->TryClaimMouthCarriedActorFromAuthority(this)))
	{
		return false;
	}
	AuthorityCarrier = Character;
	Character->OnDestroyed.AddDynamic(this, &ThisClass::HandleAuthorityCarrierDestroyed);
	SetOwner(Character);
	SetInstigator(Character);
	PresentationState.State = ECatFishPickupState::Carried;
	PresentationState.CarriedByPlayerState = PlayerState;
	// 从物理丢弃态拾回时先停止刚体；否则 Chaos 会在附着后继续把根组件移出嘴部。
	WorldCollision->SetSimulatePhysics(false);
	WorldCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ApplyLocalFocus(false);
	if (!AttachCarriedRootToMouth(Character, TEXT("AuthorityCommit"), false))
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

// 嘴部附着流程：先检查角色、根和客户端骨架，再按保留世界尺寸的规则连接目标Socket；只应用配置位置与旋转。
// 复制纠正也只比较这两项，不把为抵消父级缩放而产生的局部缩放误判为错误；最后刷新嘴叼姿态并按需记录纠正结果。
bool ACatFishPickupActor::AttachCarriedRootToMouth(ACatCharacter* Character, const TCHAR* Source,
	const bool bLogCorrection)
{
	USkeletalMeshComponent* CharacterMesh = Character ? Character->GetMesh() : nullptr;
	USceneComponent* Root = GetRootComponent();
	if (!Character || !CharacterMesh || !Root)
	{
		return false;
	}
	const UCatFishPickupSettings* Settings = GetDefault<UCatFishPickupSettings>();
	const bool bCharacterMeshReady = CharacterMesh->GetSkeletalMeshAsset() != nullptr;
	if (!HasAuthority() && !bCharacterMeshReady)
	{
		return false;
	}
	const FName SocketName = Settings && bCharacterMeshReady ? Settings->MouthCarrySocketName : NAME_None;
	const FTransform RelativeTransform = Settings ? Settings->MouthCarryRelativeTransform : FTransform::Identity;
	USceneComponent* PreviousParent = Root->GetAttachParent();
	const FName PreviousSocket = Root->GetAttachSocketName();
	const FTransform PreviousRelative = Root->GetRelativeTransform();
	const bool bNeedsCorrection = PreviousParent != CharacterMesh || PreviousSocket != SocketName
		|| !PreviousRelative.EqualsNoScale(RelativeTransform, UE_KINDA_SMALL_NUMBER);
	if ((PreviousParent != CharacterMesh || PreviousSocket != SocketName)
		&& !AttachToComponent(CharacterMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName))
	{
		return false;
	}
	Root->SetRelativeLocationAndRotation(RelativeTransform.GetLocation(), RelativeTransform.GetRotation());
	ApplyCarriedVisualTransform();
	const bool bExact = Root->GetAttachParent() == CharacterMesh && Root->GetAttachSocketName() == SocketName
		&& Root->GetRelativeTransform().EqualsNoScale(RelativeTransform, UE_KINDA_SMALL_NUMBER);
	if (bLogCorrection && bNeedsCorrection)
	{
		if (bExact)
		{
			UE_LOG(LogCatFishContainers, Log,
				TEXT("Event=fish_pickup_attachment_reconciled Result=Corrected Source=%s SessionId=%s FishInstanceId=%s Pickup=%s Carrier=%s PreviousParent=%s PreviousSocket=%s DesiredParent=%s DesiredSocket=%s PreviousRelative=%s FinalRelative=%s NetMode=%d"),
				Source ? Source : TEXT("Unknown"),
				*PresentationState.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
				*PresentationState.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(this),
				*GetNameSafe(Character), *GetNameSafe(PreviousParent), *PreviousSocket.ToString(),
				*GetNameSafe(CharacterMesh), *SocketName.ToString(), *PreviousRelative.ToHumanReadableString(),
				*Root->GetRelativeTransform().ToHumanReadableString(), static_cast<int32>(GetNetMode()));
		}
		else
		{
			UE_LOG(LogCatFishContainers, Warning,
				TEXT("Event=fish_pickup_attachment_reconciled Result=Failed Source=%s SessionId=%s FishInstanceId=%s Pickup=%s Carrier=%s PreviousParent=%s PreviousSocket=%s DesiredParent=%s DesiredSocket=%s NetMode=%d"),
				Source ? Source : TEXT("Unknown"),
				*PresentationState.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
				*PresentationState.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(this),
				*GetNameSafe(Character), *GetNameSafe(PreviousParent), *PreviousSocket.ToString(),
				*GetNameSafe(CharacterMesh), *SocketName.ToString(), static_cast<int32>(GetNetMode()));
		}
	}
	return bExact;
}

// 客户端先排除权威执行；地面或已收到物理复制时取消嘴部重试并脱离附件、恢复落地外观。
// 只有非物理携带态才解析角色并精确附着；角色未就绪进入原有有限重试，成功则清空重试计数。
void ACatFishPickupActor::ReconcileAttachmentFromPresentation(const TCHAR* Source)
{
	if (HasAuthority())
	{
		return;
	}
	if (PresentationState.State != ECatFishPickupState::Carried || GetReplicatedMovement().bRepPhysics)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(AttachmentReconcileTimer);
		}
		AttachmentReconcileAttemptCount = 0;
		bAttachmentReconcileRetryExhausted = false;
		if (GetAttachParentActor())
		{
			DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		}
		ApplyLandedVisualTransform();
		WorldCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		// RepNotify 顺序不固定：表现回调曾关闭模拟时，不能等 bRepPhysics 再次变化才恢复。
		SyncReplicatedPhysicsSimulation();
		return;
	}

	// 重新叼起时 bRepPhysics=false 可能最后到达；在附着前关闭旧刚体并恢复嘴部碰撞约束。
	WorldCollision->SetSimulatePhysics(false);
	WorldCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ACatCharacter* Character = PresentationState.CarriedByPlayerState
		? Cast<ACatCharacter>(PresentationState.CarriedByPlayerState->GetPawn()) : nullptr;
	if (!Character)
	{
		Character = Cast<ACatCharacter>(GetAttachmentReplication().AttachParent.Get());
	}
	if (!Character)
	{
		Character = Cast<ACatCharacter>(GetAttachParentActor());
	}
	if (!Character || !AttachCarriedRootToMouth(Character, Source, true))
	{
		if (AttachmentReconcileAttemptCount == 0)
		{
			UE_LOG(LogCatFishContainers, Log,
				TEXT("Event=fish_pickup_attachment_reconcile_deferred Reason=CarrierOrMeshUnavailable Source=%s SessionId=%s FishInstanceId=%s Pickup=%s CarrierPlayerState=%s RepAttachParent=%s CurrentAttachParent=%s NetMode=%d"),
				Source ? Source : TEXT("Unknown"),
				*PresentationState.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
				*PresentationState.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(this),
				*GetNameSafe(PresentationState.CarriedByPlayerState),
				*GetNameSafe(GetAttachmentReplication().AttachParent.Get()), *GetNameSafe(GetAttachParentActor()),
				static_cast<int32>(GetNetMode()));
		}
		ScheduleAttachmentReconcileRetry();
		return;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AttachmentReconcileTimer);
	}
	AttachmentReconcileAttemptCount = 0;
	bAttachmentReconcileRetryExhausted = false;
}

void ACatFishPickupActor::ScheduleAttachmentReconcileRetry()
{
	UWorld* World = GetWorld();
	if (!World || HasAuthority() || PresentationState.State != ECatFishPickupState::Carried
		|| World->GetTimerManager().IsTimerActive(AttachmentReconcileTimer))
	{
		return;
	}
	constexpr int32 MaximumAttempts = 40;
	if (AttachmentReconcileAttemptCount >= MaximumAttempts)
	{
		if (!bAttachmentReconcileRetryExhausted)
		{
			bAttachmentReconcileRetryExhausted = true;
			UE_LOG(LogCatFishContainers, Warning,
				TEXT("Event=fish_pickup_attachment_reconcile_failed Reason=RetryExhausted Attempts=%d SessionId=%s FishInstanceId=%s Pickup=%s CarrierPlayerState=%s RepAttachParent=%s NetMode=%d"),
				AttachmentReconcileAttemptCount,
				*PresentationState.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
				*PresentationState.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(this),
				*GetNameSafe(PresentationState.CarriedByPlayerState),
				*GetNameSafe(GetAttachmentReplication().AttachParent.Get()), static_cast<int32>(GetNetMode()));
		}
		return;
	}
	World->GetTimerManager().SetTimer(AttachmentReconcileTimer, this,
		&ThisClass::RetryAttachmentReconcile, 0.05f, false);
}

void ACatFishPickupActor::RetryAttachmentReconcile()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AttachmentReconcileTimer);
	}
	++AttachmentReconcileAttemptCount;
	ReconcileAttachmentFromPresentation(TEXT("DeferredRetry"));
}

// 结束携带流程：移除配对宿主回调，清空服务器携带者，再保留世界变换解绑并清除所有者和表现归属。
// 不选择落点、不启用物理，调用方在完成自己的预检后决定固定落地或轻抛。
void ACatFishPickupActor::EndMouthCarryFromAuthority()
{
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

// 主动丢弃流程：确认服务器及当前携带者，预检身体、物理依赖和前方空间；拒绝时保持原鱼与嘴部不变。
// 成功才解除携带、恢复落地姿态并给原刚体施加配置初速度，最后记录结果；不保存请求或重放状态。
bool ACatFishPickupActor::DropFromAuthority(AController* RequestingController)
{
	bool bDropped = false;
	ECatDomainCommandError Error = ECatDomainCommandError::InvalidIdentity;
	const TCHAR* Reason = TEXT("InvalidRequester");
	APlayerState* PlayerState = IsValid(RequestingController) ? RequestingController->PlayerState : nullptr;
	ACatCharacter* Character = IsValid(RequestingController) ? Cast<ACatCharacter>(RequestingController->GetPawn()) : nullptr;
	const bool bValidRequester = HasAuthority() && GetWorld() && IsValid(RequestingController)
		&& !RequestingController->IsActorBeingDestroyed() && RequestingController->GetWorld() == GetWorld() && IsValid(PlayerState)
		&& PlayerState->GetWorld() == GetWorld()
		&& IsValid(Character) && Character->GetWorld() == GetWorld() && !Character->IsActorBeingDestroyed()
		&& Character->GetController() == RequestingController && Character->GetPlayerState() == PlayerState;
	if (bValidRequester)
	{
	const UCatConditionComponent* Condition = Character->GetConditionComponent();
	const UCatInventorySettings* Settings = GetDefault<UCatInventorySettings>();
	const UCatFishPickupSettings* PickupSettings = GetDefault<UCatFishPickupSettings>();
	FTransform ReleaseTransform;
	if (!bIdentityInitialized || !PresentationState.FishInstanceId.IsValid() || !FishDefinition
		|| !PresentationState.FishingSessionId.IsValid() || PresentationState.FishDefinitionId.IsNone()
		|| !FMath::IsFinite(PresentationState.VisualScale) || PresentationState.VisualScale <= 0.0
		|| !FMath::IsFinite(PresentationState.WeightKilograms) || PresentationState.WeightKilograms <= 0.0)
	{
		Error = ECatDomainCommandError::InvalidPayload;
		Reason = TEXT("InvalidFishIdentity");
	}
	else if (IsActorBeingDestroyed() || bConsumptionCommitted)
	{
		Error = ECatDomainCommandError::AlreadyResolved;
		Reason = TEXT("ConsumingOrDestroying");
	}
	else if (!Condition || Condition->GetSnapshot().bDowned || AuthorityCarrier.Get() != Character
		|| PresentationState.State != ECatFishPickupState::Carried || PresentationState.CarriedByPlayerState != PlayerState
		|| FindCarriedFish(Character) != this
		|| !Character->GetMesh() || !PickupSettings || !GetRootComponent()
		|| GetRootComponent()->GetAttachParent() != Character->GetMesh()
		|| GetRootComponent()->GetAttachSocketName() != (Character->GetMesh()->GetSkeletalMeshAsset()
			? PickupSettings->MouthCarrySocketName : NAME_None))
	{
		Error = ECatDomainCommandError::PermissionDenied;
		Reason = TEXT("NotCurrentMouthFishOrDowned");
	}
	else if (!Settings || !WorldCollision || GetRootComponent() != WorldCollision || !WorldCollision->IsRegistered()
		|| WorldCollision->Mobility != EComponentMobility::Movable || !GetWorld()->GetPhysicsScene()
		|| !WorldCollision->GetBodySetup() || WorldCollision->GetBodySetup()->AggGeom.BoxElems.IsEmpty()
		|| WorldCollision->GetBodySetup()->GetCollisionTraceFlag() == CTF_UseComplexAsSimple
		|| !GetActorEnableCollision() || !InteractionSphere || !FishMesh || !FishMesh->GetSkeletalMeshAsset()
		|| !FMath::IsFinite(Settings->DropForwardSpeed) || Settings->DropForwardSpeed < 0.0
		|| !FMath::IsFinite(Settings->DropUpwardSpeed) || Settings->DropUpwardSpeed < 0.0
		|| GetActorScale3D().ContainsNaN() || GetActorScale3D().GetMin() <= UE_SMALL_NUMBER)
	{
		Error = ECatDomainCommandError::DependencyUnavailable;
		Reason = TEXT("PhysicsOrSettingsUnavailable");
	}
	else
	{
		// Mesh 使用绝对缩放：把落地世界尺寸换算回当前根的局部尺寸，独立预检真正落地姿态。
		// 全程不切姿态或改碰撞；保留共享求解器的原盒检测，再用目标盒补检，覆盖两种姿态尺寸不同的情况。
		FTransform LandedTransform = LandedMeshBaseTransform;
		const double Roll = FishPresentationDefinition ? FishPresentationDefinition->LandedActorRollDegrees : 90.0;
		LandedTransform.SetRotation(FQuat(FVector::ForwardVector, FMath::DegreesToRadians(Roll)) * LandedMeshBaseTransform.GetRotation());
		LandedTransform.SetScale3D(LandedMeshBaseTransform.GetScale3D() * PresentationState.VisualScale / GetActorScale3D());
		const FBox MeshBounds = FishMesh->GetSkeletalMeshAsset()->GetBounds().GetBox()
			+ FishMesh->CalcBounds(FTransform::Identity).GetBox();
		const FBox LandedBounds = MeshBounds.TransformBy(LandedTransform.ToMatrixWithScale());
		if (!LandedBounds.IsValid || LandedBounds.GetExtent().ContainsNaN())
		{
			Error = ECatDomainCommandError::DependencyUnavailable;
			Reason = TEXT("InvalidLandedBounds");
		}
		else if (!UCatInventoryStatics::FindWorldReleaseTransform(Character, this, ECatInventoryWorldAction::Drop, *Settings, ReleaseTransform))
		{
			Error = ECatDomainCommandError::PermissionDenied;
			Reason = TEXT("ReleaseSpaceBlocked");
		}
		else
		{
			// 目标盒各轴向外扩 0.1 厘米（1 毫米），保守覆盖浮点姿态换算误差，不以严格相等拒绝有效丢弃。
			const FVector Extent = LandedBounds.GetExtent().ComponentMax(FVector(1.0))
				* ReleaseTransform.GetScale3D().GetAbs() + FVector(0.1);
			FCollisionQueryParams Query(SCENE_QUERY_STAT(CatFishDropLandedBounds), false, Character);
			Query.AddIgnoredActor(this);
			FHitResult Hit;
			const FCollisionShape Shape = FCollisionShape::MakeBox(Extent);
			if (GetWorld()->SweepSingleByChannel(Hit, Character->GetPawnViewLocation(), ReleaseTransform.GetLocation(),
				ReleaseTransform.GetRotation(), ECC_WorldDynamic, Shape, Query)
				|| GetWorld()->OverlapBlockingTestByChannel(ReleaseTransform.GetLocation(), ReleaseTransform.GetRotation(),
					ECC_WorldDynamic, Shape, Query))
			{
				Error = ECatDomainCommandError::PermissionDenied;
				Reason = TEXT("LandedReleaseSpaceBlocked");
			}
			else
			{
				EndMouthCarryFromAuthority();
				SetActorTransform(ReleaseTransform, false, nullptr, ETeleportType::TeleportPhysics);
				ApplyLandedVisualTransform();
				WorldCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
				WorldCollision->SetSimulatePhysics(true);
				WorldCollision->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
				WorldCollision->SetPhysicsLinearVelocity(Character->GetActorForwardVector().GetSafeNormal2D()
					* Settings->DropForwardSpeed + FVector(0.0, 0.0, Settings->DropUpwardSpeed));
				ForceNetUpdate();
				Character->ForceNetUpdate();
				bDropped = true;
				Error = ECatDomainCommandError::None;
				Reason = TEXT("Dropped");
			}
		}
	}
	}
	UE_CLOG(!bDropped, LogCatFishContainers, Warning,
		TEXT("Event=fish_drop_rejected World=%s NetMode=%d Authority=%d LocalRole=%d Player=%s FishInstanceId=%s Reason=%s Error=%s"),
		*GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole(), *GetNameSafe(PlayerState),
		*PresentationState.FishInstanceId.ToString(), Reason, *UEnum::GetValueAsString(Error));
	UE_CLOG(bDropped, LogCatFishContainers, Log,
		TEXT("Event=fish_drop_result World=%s NetMode=%d Authority=%d LocalRole=%d Player=%s FishInstanceId=%s"),
		*GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole(), *GetNameSafe(PlayerState),
		*PresentationState.FishInstanceId.ToString());
	return bDropped;
}

// 退出携带流程：解绑旧角色并清空嘴部状态，查询地面；失败时以 DropLocation 和向上法线替代。
// 根保持现有世界缩放与水平朝向，使倒地、失去占有和销毁释放与主动丢弃维持同一鱼体和碰撞尺寸；网格恢复侧躺后按法线抬升物理中心并发布固定地面状态。
void ACatFishPickupActor::ReleaseMouthCarryFromAuthority(const FVector& DropLocation)
{
	if (!HasAuthority() || PresentationState.State != ECatFishPickupState::Carried)
	{
		return;
	}
	const ACatCharacter* PreviousCarrier = AuthorityCarrier.Get();
	EndMouthCarryFromAuthority();
	const UCatFishPickupSettings* Settings = GetDefault<UCatFishPickupSettings>();
	const FCatWorldSurfaceResult Surface = FCatWorldSurfaceResolver::ResolveHighestBlockingSurface(
		GetWorld(), DropLocation, Settings->LandingGroundTraceChannel, {this, PreviousCarrier});
	UE_CLOG(!Surface.bSucceeded, LogCatFishContainers, Warning,
		TEXT("Event=fish_pickup_ground_query_failed SessionId=%s FishInstanceId=%s Pickup=%s Result=KeepDropLocation Contact=%s World=%s NetMode=%d Authority=%d Role=%s"),
		*PresentationState.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *PresentationState.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*GetName(), *DropLocation.ToCompactString(), *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), *UEnum::GetValueAsString(GetLocalRole()));
	FRotator Rotation(0.0, GetActorRotation().Yaw, 0.0);
	SetActorTransform(FTransform(Rotation, Surface.bSucceeded ? Surface.WorldPosition : DropLocation, GetActorScale3D()),
		false, nullptr, ETeleportType::TeleportPhysics);
	PresentationState.GroundNormal = Surface.bSucceeded ? Surface.SurfaceNormal : FVector::UpVector;
	ApplyLandedVisualTransform();
	const double Lift = CatFishGrounding::ComputeVerticalLift(WorldCollision->CalcBounds(FTransform::Identity).GetBox(),
		GetActorTransform(), Surface.bSucceeded ? Surface.WorldPosition : DropLocation, PresentationState.GroundNormal);
	AddActorWorldOffset(FVector(0.0, 0.0, Lift), false, nullptr, ETeleportType::TeleportPhysics);
	WorldCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	if (InteractionSphere)
	{
		InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}
	ForceNetUpdate();
	UE_LOG(LogCatFishContainers, Log, TEXT("Event=fish_pickup_dropped SessionId=%s FishInstanceId=%s Pickup=%s SurfaceFound=%d Contact=%s Normal=%s MeshScale=%s World=%s NetMode=%d Authority=%d"),
		*PresentationState.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *PresentationState.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*GetName(), Surface.bSucceeded, *GetActorLocation().ToCompactString(), *PresentationState.GroundNormal.ToCompactString(),
		*FishMesh->GetComponentScale().ToCompactString(), *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority());
}

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
	if (!HasAuthority() || bConsumptionCommitted || !RequestId.IsValid() || TargetInventoryHost == nullptr
		|| TargetInventoryHost->GetWorld() != GetWorld() || StableNetId.IsEmpty()
		|| !Character || PresentationState.State != ECatFishPickupState::Carried
		|| GetAttachParentActor() != Character || FindCarriedFish(Character) != this)
	{
		return Result;
	}
	if (!FishDefinition || (!bCaptureRecorded && (!Imprint || !Imprint->CanRecordCommittedCapture())))
	{
		Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
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
	TArray<UCatInventoryComponent*> TargetInventories;
	UCatInventoryStatics::AppendInventoryComponentsFromActor(TargetInventoryHost, TargetInventories);
	UCatInventoryComponent* TargetInventory = nullptr;
	for (UCatInventoryComponent* CandidateInventory : TargetInventories)
	{
		if (CandidateInventory != nullptr && CandidateInventory->CanFullyAcceptInventoryBatch(ReceiveBatch))
		{
			TargetInventory = CandidateInventory;
			break;
		}
	}
	if (FishItemInstance == nullptr || TargetInventory == nullptr)
	{
		Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	// 收货会在最后才广播；先锁住本鱼，保留同一实例和 Actor，避免广播重入时将半提交状态当成可售库存。
	bConsumptionCommitted = true;
	if (!TargetInventory->TryAddInventoryBatchInternal(ReceiveBatch, false))
	{
		bConsumptionCommitted = false;
		Result.Command.Error = ECatDomainCommandError::CapacityExceeded;
		return Result;
	}

	Result.Command.bCommitted = true;
	Result.Command.Error = ECatDomainCommandError::None;
	Result.Committed.CaptureRequestId = RequestId;
	Result.Committed.FishingSessionId = PresentationState.FishingSessionId;
	Result.Committed.FishInstance.FishInstanceId = PresentationState.FishInstanceId;
	Result.Committed.FishInstance.FishDefinitionId = PresentationState.FishDefinitionId;
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
	// 入护提交后 Actor 是静态保管载体而非消费终态；允许同一实例后续从库存重新叼起。
	bConsumptionCommitted = false;
	TargetInventory->BroadcastInventoryChange();
	ForceNetUpdate();
	return Result;
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
		return FishItem->GetItemInstanceId() == PresentationState.FishInstanceId;
	}
	bCaptureRecorded = true;
	if (!InitializeFromAuthority(FishItem->GetSourceFishingSessionId(), FishItem->GetItemInstanceId(), Definition,
		FishItem->GetFishWeightKilograms(), Presentation->ComputeUniformVisualScale(FishItem->GetFishWeightKilograms()),
		NAME_None, {}))
	{
		bCaptureRecorded = false;
		return false;
	}
	InventoryItem = FishItem;
	InventoryItem->SetWorldActor(this);
	InventoryItem->SetRuntimeOwnerActor(this);
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
	if (!InitializeFromAuthority(Item->GetSourceFishingSessionId(), Item->GetItemInstanceId(), Item->GetFishDefinition(),
		Item->GetFishWeightKilograms(), Presentation->ComputeUniformVisualScale(Item->GetFishWeightKilograms()), NAME_None, {}))
	{
		bCaptureRecorded = false;
		return false;
	}
	InventoryItem = Item;
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
		&& FishDefinition && PresentationState.FishInstanceId.IsValid() && !PresentationState.FishDefinitionId.IsNone()
		&& FMath::IsFinite(PresentationState.WeightKilograms) && PresentationState.WeightKilograms > 0.0
		&& RequestingController && RequestingController->GetWorld() == GetWorld()
		&& PlayerState && PlayerState->GetUniqueId().IsValid()
		&& (bAvailableWorldFish
			|| (PresentationState.State == ECatFishPickupState::Carried
				&& RequestingController->GetPawn() == AuthorityCarrier.Get()))
		&& (bCaptureRecorded || (Imprint && Imprint->CanRecordCommittedCapture()));
}

// 单鱼消费流程：复核同一预检条件后先关闭重入，再按鱼身份补捕获记录，最后解除宿主回调、附着和交互并销毁。
// 捕获记录以鱼 GUID 为幂等键，批量出售共享交易 RequestId 也不会把多条鱼误认为同一次捕获。
bool ACatFishPickupActor::ConsumeFromAuthority(AController* RequestingController, const FGuid RequestId,
	TFunction<bool()> CommitBeforeConsumption)
{
	if (!RequestId.IsValid() || !CanConsumeFromAuthority(RequestingController)) return false;
	bConsumptionCommitted = true;
	// 售鱼入账在实物占用期间提交：失败即释放占用，尚未归档或销毁鱼；普通消费不提供回调即可沿原流程进行。
	if (CommitBeforeConsumption && !CommitBeforeConsumption())
	{
		bConsumptionCommitted = false;
		return false;
	}
	const FString StableNetId = RequestingController->PlayerState->GetUniqueId()->ToString();
	if (!bCaptureRecorded)
	{
		FCatCaptureCommittedResult Capture;
		Capture.CaptureRequestId = PresentationState.FishInstanceId;
		Capture.FishingSessionId = PresentationState.FishingSessionId;
		Capture.FishInstance.FishInstanceId = PresentationState.FishInstanceId;
		Capture.FishInstance.FishDefinitionId = PresentationState.FishDefinitionId;
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
		*RequestId.ToString(), *PresentationState.FishInstanceId.ToString(), *PresentationState.FishDefinitionId.ToString(),
		PresentationState.WeightKilograms, bCaptureRecorded, *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), GetLocalRole());
	if (!Destroy())
	{
		UE_LOG(LogCatFishContainers, Error, TEXT("Event=world_fish_destroy_failed Request=%s FishInstanceId=%s Actor=%s"),
			*RequestId.ToString(), *PresentationState.FishInstanceId.ToString(), *GetName());
	}
	return true;
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
		FText::FromName(PresentationState.FishDefinitionId),
		FText::AsNumber(PresentationState.WeightKilograms));
}

double ACatFishPickupActor::GetInteractionRadius_Implementation() const
{
	const UCatInteractionSettings* Settings = GetDefault<UCatInteractionSettings>();
	return Settings ? Settings->MaximumServerInteractionDistanceCentimeters : 0.0;
}

bool ACatFishPickupActor::IsAuthorityRequestSpatiallyValid(const AController* RequestingController) const
{
	const APawn* Pawn = RequestingController ? RequestingController->GetPawn() : nullptr;
	const UCatInteractionSettings* Settings = GetDefault<UCatInteractionSettings>();
	UWorld* World = GetWorld();
	if (!HasAuthority() || !Pawn || !Settings || !World
		|| FVector::Dist(Pawn->GetPawnViewLocation(), GetActorLocation())
			> Settings->MaximumServerInteractionDistanceCentimeters)
	{
		return false;
	}
	if (!Settings->bRequireServerLineOfSight)
	{
		return true;
	}
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CatFishPickupLineOfSight), true, Pawn);
	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, Pawn->GetPawnViewLocation(), GetActorLocation(),
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

	APlayerState* PlayerState = RequestingController ? RequestingController->PlayerState : nullptr;
	const FString StableNetId = PlayerState && PlayerState->GetUniqueId().IsValid()
		? PlayerState->GetUniqueId()->ToString() : FString();
	const FString CacheKey = FString::Printf(TEXT("%s|%s"), *StableNetId,
		*RequestId.ToString(EGuidFormats::DigitsWithHyphens));
	if (PickupTerminalByRequester.Contains(CacheKey))
	{
		return true;
	}
	FCatDomainCommandResult Terminal;
	Terminal.RequestId = RequestId;
	ACatCharacter* Character = RequestingController ? Cast<ACatCharacter>(RequestingController->GetPawn()) : nullptr;
	UCatConditionComponent* Condition = Character ? Character->GetConditionComponent() : nullptr;
	if (!HasAuthority() || !bIdentityInitialized || !RequestId.IsValid() || StableNetId.IsEmpty())
	{
		Terminal.Error = ECatDomainCommandError::InvalidPayload;
	}
	else if (PresentationState.State != ECatFishPickupState::Available || IsHidden()
		|| (InventoryItem && InventoryItem->GetRuntimeOwnerActor() != this))
	{
		Terminal.Error = ECatDomainCommandError::AlreadyResolved;
	}
	else if (!Character || !Condition || Condition->GetSnapshot().bDowned || !IsAuthorityRequestSpatiallyValid(RequestingController))
	{
		Terminal.Error = ECatDomainCommandError::PermissionDenied;
	}
	else if (!FishDefinition)
	{
		Terminal.Error = ECatDomainCommandError::DependencyUnavailable;
	}
	else if (FindCarriedFish(Character) || ACatFishGuardActor::FindCarriedGuard(Character))
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
	return Terminal.bCommitted;
}

// 捕获归档流程：已入库的鱼直接跳过；新捕获先写 FishRecorded，再按现有配置提交可选印记候选与成像计划。
void ACatFishPickupActor::ArchiveCommittedCapture(const FCatCaptureCommittedResult& Committed,
	const FString& PickerStableNetId)
{
	UCatRunImprintService* Imprint = GetWorld() ? GetWorld()->GetSubsystem<UCatRunImprintService>() : nullptr;
	if (bCaptureRecorded || !Imprint || !FishDefinition)
	{
		return;
	}
	FCatCaptureConditionSnapshot Condition;
	Condition.RegionId = RegionId;
	const FGuid FishRecordedGrantId = Imprint->RecordCommittedCapture(Committed, PickerStableNetId, Condition);
	bCaptureRecorded = FishRecordedGrantId.IsValid();
	if (FishDefinition->CaptureImprintEventId.IsNone())
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
	Candidate.FishDefinitionId = Committed.FishInstance.FishDefinitionId;
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

// 表现复制流程：先解析正式鱼种网格；仅非物理携带态停止刚体并收敛嘴部附着，地面或物理态恢复碰撞。
// 最后通知蓝图并仅对身份或状态变化落盘；物理复制已到达时忽略旧 Carried 的附着要求，防止把丢弃鱼再次挂嘴。
void ACatFishPickupActor::OnRep_PresentationState(const FCatFishPickupPresentationState& Previous)
{
	RefreshFishPresentation();
	if (PresentationState.State == ECatFishPickupState::Carried && !GetReplicatedMovement().bRepPhysics)
	{
		if (InteractionSphere)
		{
			WorldCollision->SetSimulatePhysics(false);
			WorldCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			InteractionSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		ApplyLocalFocus(false);
		ReconcileAttachmentFromPresentation(TEXT("PresentationState"));
	}
	else
	{
		ReconcileAttachmentFromPresentation(TEXT("PresentationState"));
		if (InteractionSphere)
		{
			InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			WorldCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		}
	}
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
