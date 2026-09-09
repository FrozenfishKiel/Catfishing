#include "Items/World/CatFishPickupActor.h"

#include "Algo/Unique.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/Skeleton.h"
#include "Character/CatCharacter.h"
#include "Collection/CatRunImprintService.h"
#include "Components/SkeletalMeshComponent.h"
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
#include "Items/CatItemsService.h"
#include "Items/CatWorldItemSettings.h"
#include "Fishing/Presentation/CatFishPresentationDefinition.h"
#include "Fishing/Presentation/CatFishGrounding.h"
#include "Items/World/CatWorldSurfaceResolver.h"
#include "Engine/SkeletalMesh.h"
#include "Logging/CatLog.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

ACatFishPickupActor::ACatFishPickupActor()
{
	bReplicates = true;
	SetReplicateMovement(true);
	bAlwaysRelevant = false;
	PrimaryActorTick.bCanEverTick = false;

	InteractionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionSphere"));
	SetRootComponent(InteractionSphere);
	InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionSphere->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractionSphere->SetGenerateOverlapEvents(false);

	FishMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FishMesh"));
	FishMesh->SetupAttachment(InteractionSphere);
	// 鱼体使用冻结的世界比例；猫身体/嘴 Socket 的缩放不能把同一条鱼缩小或放大。
	FishMesh->SetAbsolute(false, false, true);
	FishMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FishMesh->SetRenderCustomDepth(false);
}

void ACatFishPickupActor::BeginPlay()
{
	Super::BeginPlay();
	const UCatWorldItemSettings* Settings = GetDefault<UCatWorldItemSettings>();
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

void ACatFishPickupActor::OnRep_AttachmentReplication()
{
	Super::OnRep_AttachmentReplication();
	ReconcileAttachmentFromPresentation(TEXT("AttachmentReplication"));
}

void ACatFishPickupActor::OnRep_ReplicatedMovement()
{
	Super::OnRep_ReplicatedMovement();
	if (PresentationState.State == ECatFishPickupState::Available) ApplyLandedVisualTransform();
}

void ACatFishPickupActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, PresentationState);
}

bool ACatFishPickupActor::InitializeFromAuthority(const FGuid InFishingSessionId, const FGuid InFishInstanceId,
	UCatFishDefinition* InFishDefinition, const double InWeightKilograms, const double InVisualScale, const FName InRegionId,
	const TArray<FString>& InFishingParticipantStableNetIds, const FVector GroundNormal)
{
	if (!HasAuthority() || bIdentityInitialized || !InFishingSessionId.IsValid() || !InFishInstanceId.IsValid()
		|| InFishingSessionId == InFishInstanceId || !InFishDefinition || !InFishDefinition->IsRuntimeDefinitionReady()
		|| !FMath::IsFinite(InWeightKilograms) || InWeightKilograms <= 0.0
		|| !FMath::IsFinite(InVisualScale) || InVisualScale <= 0.0 || InRegionId.IsNone() || GroundNormal.ContainsNaN())
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
	ApplyLandedVisualTransform();
	ForceNetUpdate();
	UE_LOG(LogCatItems, Log, TEXT("Event=fish_pickup_grounded SessionId=%s FishInstanceId=%s Pickup=%s Contact=%s Normal=%s MeshLocation=%s MeshScale=%s VisualScale=%.3f World=%s NetMode=%d Authority=%d Role=%s"),
		*InFishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *InFishInstanceId.ToString(EGuidFormats::DigitsWithHyphens), *GetName(),
		*GetActorLocation().ToCompactString(), *PresentationState.GroundNormal.ToCompactString(),
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
		UE_LOG(LogCatItems, Warning,
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
	UE_LOG(LogCatItems, Log,
		TEXT("Event=fish_pickup_presentation_applied FishDefinition=%s FishInstanceId=%s Pickup=%s NetMode=%s Authority=%s Presentation=%s Mesh=%s Skeleton=%s LandedAnimation=%s VisualScale=%.3f State=%s"),
		*PresentationState.FishDefinitionId.ToString(),
		*PresentationState.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(this),
		CatFishPickupPresentationPrivate::NetModeValue(GetNetMode()), HasAuthority() ? TEXT("true") : TEXT("false"),
		*GetNameSafe(FishPresentationDefinition), *GetNameSafe(Mesh), *GetNameSafe(Mesh->GetSkeleton()),
		*GetNameSafe(LandedAnimation), PresentationState.VisualScale, *UEnum::GetValueAsString(PresentationState.State));
}

// 落地视觉流程：从鱼种表现定义恢复地面鱼专用局部变换；统一视觉缩放随后单独应用。
void ACatFishPickupActor::ApplyLandedVisualTransform()
{
	if (!FishMesh)
	{
		return;
	}
	FishMesh->SetRelativeTransform(LandedMeshBaseTransform);
	ApplyVisualScale();
	if (FishMesh->GetSkeletalMeshAsset())
	{
		const FBox Bounds = FishMesh->GetSkeletalMeshAsset()->GetBounds().GetBox()
			+ FishMesh->CalcBounds(FTransform::Identity).GetBox();
		const double Lift = CatFishGrounding::ComputeVerticalLift(Bounds, FishMesh->GetComponentTransform(),
			GetActorLocation(), PresentationState.GroundNormal);
		FishMesh->AddWorldOffset(FVector(0.0, 0.0, Lift));
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

ACatFishPickupActor* ACatFishPickupActor::FindCarriedFish(const ACatCharacter* Character)
{
	if (!Character)
	{
		return nullptr;
	}
	TArray<AActor*> AttachedActors;
	Character->GetAttachedActors(AttachedActors, false, true);
	for (AActor* AttachedActor : AttachedActors)
	{
		ACatFishPickupActor* Fish = Cast<ACatFishPickupActor>(AttachedActor);
		if (Fish && Fish->PresentationState.State == ECatFishPickupState::Carried
			&& Fish->GetAttachParentActor() == Character)
		{
			return Fish;
		}
	}
	return nullptr;
}

bool ACatFishPickupActor::BeginMouthCarryFromAuthority(ACatCharacter* Character, APlayerState* PlayerState)
{
	if (!HasAuthority() || !Character || !PlayerState || PresentationState.State != ECatFishPickupState::Available
		|| FindCarriedFish(Character) || !Character->GetMesh())
	{
		return false;
	}
	AuthorityCarrier = Character;
	Character->OnDestroyed.AddDynamic(this, &ThisClass::HandleAuthorityCarrierDestroyed);
	SetOwner(Character);
	SetInstigator(Character);
	PresentationState.State = ECatFishPickupState::Carried;
	PresentationState.CarriedByPlayerState = PlayerState;
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
		InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		return false;
	}
	ForceNetUpdate();
	Character->ForceNetUpdate();
	UE_LOG(LogCatItems, Log,
		TEXT("Event=fish_pickup_mouth_attach_committed SessionId=%s FishInstanceId=%s Pickup=%s Carrier=%s ParentComponent=%s Socket=%s ActorRelative=%s NetMode=%d"),
		*PresentationState.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens),
		*PresentationState.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(this),
		*GetNameSafe(Character), *GetNameSafe(GetRootComponent() ? GetRootComponent()->GetAttachParent() : nullptr),
		GetRootComponent() ? *GetRootComponent()->GetAttachSocketName().ToString() : TEXT("None"),
		GetRootComponent() ? *GetRootComponent()->GetRelativeTransform().ToHumanReadableString() : TEXT("Invalid"),
		static_cast<int32>(GetNetMode()));
	return GetAttachParentActor() == Character;
}

bool ACatFishPickupActor::AttachCarriedRootToMouth(ACatCharacter* Character, const TCHAR* Source,
	const bool bLogCorrection)
{
	USkeletalMeshComponent* CharacterMesh = Character ? Character->GetMesh() : nullptr;
	USceneComponent* Root = GetRootComponent();
	if (!Character || !CharacterMesh || !Root)
	{
		return false;
	}
	const UCatWorldItemSettings* Settings = GetDefault<UCatWorldItemSettings>();
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
		|| !PreviousRelative.Equals(RelativeTransform, UE_KINDA_SMALL_NUMBER);
	if ((PreviousParent != CharacterMesh || PreviousSocket != SocketName)
		&& !AttachToComponent(CharacterMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName))
	{
		return false;
	}
	SetActorRelativeTransform(RelativeTransform);
	ApplyCarriedVisualTransform();
	const bool bExact = Root->GetAttachParent() == CharacterMesh && Root->GetAttachSocketName() == SocketName
		&& Root->GetRelativeTransform().Equals(RelativeTransform, UE_KINDA_SMALL_NUMBER);
	if (bLogCorrection && bNeedsCorrection)
	{
		if (bExact)
		{
			UE_LOG(LogCatItems, Log,
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
			UE_LOG(LogCatItems, Warning,
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

void ACatFishPickupActor::ReconcileAttachmentFromPresentation(const TCHAR* Source)
{
	if (HasAuthority())
	{
		return;
	}
	if (PresentationState.State != ECatFishPickupState::Carried)
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
		return;
	}

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
			UE_LOG(LogCatItems, Log,
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
			UE_LOG(LogCatItems, Warning,
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

void ACatFishPickupActor::ReleaseMouthCarryFromAuthority(const FVector& DropLocation)
{
	if (!HasAuthority() || PresentationState.State != ECatFishPickupState::Carried)
	{
		return;
	}
	if (ACatCharacter* Character = AuthorityCarrier.Get())
	{
		Character->OnDestroyed.RemoveDynamic(this, &ThisClass::HandleAuthorityCarrierDestroyed);
	}
	const ACatCharacter* PreviousCarrier = AuthorityCarrier.Get();
	AuthorityCarrier.Reset();
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	SetOwner(nullptr);
	SetInstigator(nullptr);
	const UCatWorldItemSettings* Settings = GetDefault<UCatWorldItemSettings>();
	const FCatWorldSurfaceResult Surface = FCatWorldSurfaceResolver::ResolveHighestBlockingSurface(
		GetWorld(), DropLocation, Settings->LandingGroundTraceChannel, {this, PreviousCarrier});
	UE_CLOG(!Surface.bSucceeded, LogCatItems, Warning,
		TEXT("Event=fish_pickup_ground_query_failed SessionId=%s FishInstanceId=%s Pickup=%s Result=KeepDropLocation Contact=%s World=%s NetMode=%d Authority=%d Role=%s"),
		*PresentationState.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *PresentationState.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*GetName(), *DropLocation.ToCompactString(), *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority(), *UEnum::GetValueAsString(GetLocalRole()));
	FRotator Rotation(0.0, GetActorRotation().Yaw,
		FishPresentationDefinition ? FishPresentationDefinition->LandedActorRollDegrees : 90.0);
	SetActorTransform(FTransform(Rotation, Surface.bSucceeded ? Surface.WorldPosition : DropLocation),
		false, nullptr, ETeleportType::TeleportPhysics);
	PresentationState.GroundNormal = Surface.bSucceeded ? Surface.SurfaceNormal : FVector::UpVector;
	PresentationState.State = ECatFishPickupState::Available;
	PresentationState.CarriedByPlayerState = nullptr;
	ApplyLandedVisualTransform();
	if (InteractionSphere)
	{
		InteractionSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}
	ForceNetUpdate();
	UE_LOG(LogCatItems, Log, TEXT("Event=fish_pickup_dropped SessionId=%s FishInstanceId=%s Pickup=%s SurfaceFound=%d Contact=%s Normal=%s MeshScale=%s World=%s NetMode=%d Authority=%d"),
		*PresentationState.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *PresentationState.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
		*GetName(), Surface.bSucceeded, *GetActorLocation().ToCompactString(), *PresentationState.GroundNormal.ToCompactString(),
		*FishMesh->GetComponentScale().ToCompactString(), *GetNameSafe(GetWorld()), GetNetMode(), HasAuthority());
}

void ACatFishPickupActor::HandleAuthorityCarrierDestroyed(AActor* DestroyedActor)
{
	const FVector DropLocation = DestroyedActor ? DestroyedActor->GetActorLocation() : GetActorLocation();
	ReleaseMouthCarryFromAuthority(DropLocation);
}

FCatCaptureCommitResult ACatFishPickupActor::StoreInFishGuardFromAuthority(AController* RequestingController,
	const FGuid RequestId, const FGuid TargetContainerId, const int64 ExpectedTargetRevision)
{
	FCatCaptureCommitResult Result;
	Result.Command.RequestId = RequestId;
	Result.Command.Error = ECatDomainCommandError::InvalidPayload;
	APlayerState* PlayerState = RequestingController ? RequestingController->PlayerState : nullptr;
	ACatCharacter* Character = RequestingController ? Cast<ACatCharacter>(RequestingController->GetPawn()) : nullptr;
	const FString StableNetId = PlayerState && PlayerState->GetUniqueId().IsValid()
		? PlayerState->GetUniqueId()->ToString() : FString();
	UCatItemsService* Items = GetWorld() ? GetWorld()->GetSubsystem<UCatItemsService>() : nullptr;
	UCatRunImprintService* Imprint = GetWorld() ? GetWorld()->GetSubsystem<UCatRunImprintService>() : nullptr;
	if (!HasAuthority() || !RequestId.IsValid() || !TargetContainerId.IsValid() || StableNetId.IsEmpty()
		|| !Character || PresentationState.State != ECatFishPickupState::Carried
		|| GetAttachParentActor() != Character || FindCarriedFish(Character) != this)
	{
		return Result;
	}
	if (!Items || !Imprint || !FishDefinition || !Imprint->CanRecordCommittedCapture())
	{
		Result.Command.Error = ECatDomainCommandError::DependencyUnavailable;
		return Result;
	}
	FCatCaptureCommitCommand Command;
	Command.Context.RequestId = RequestId;
	Command.Context.StableNetId = StableNetId;
	Command.Context.ExpectedRevision = ExpectedTargetRevision;
	Command.FishingSessionId = PresentationState.FishingSessionId;
	Command.FishInstanceId = PresentationState.FishInstanceId;
	Command.FishDefinitionId = PresentationState.FishDefinitionId;
	Command.TargetContainerId = TargetContainerId;
	Command.WeightKilograms = PresentationState.WeightKilograms;
	Command.SacrificeContribution = FishDefinition->SacrificeContribution;
	Result = Items->CommitCapture(Command);
	const bool bCommitted = Result.Command.bCommitted
		|| (Result.Command.Error == ECatDomainCommandError::AlreadyResolved
			&& Result.Committed.FishInstance.FishInstanceId == PresentationState.FishInstanceId
			&& Result.Committed.ContainerId == TargetContainerId);
	if (!bCommitted)
	{
		return Result;
	}
	ArchiveCommittedCapture(Result.Committed, StableNetId);
	if (ACatCharacter* Carrier = AuthorityCarrier.Get())
	{
		Carrier->OnDestroyed.RemoveDynamic(this, &ThisClass::HandleAuthorityCarrierDestroyed);
	}
	AuthorityCarrier.Reset();
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	SetOwner(nullptr);
	SetInstigator(nullptr);
	PresentationState.CarriedByPlayerState = nullptr;
	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);
	Destroy();
	return Result;
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

bool ACatFishPickupActor::CanInteract_Implementation(AController* RequestingController) const
{
	return RequestingController
		&& PresentationState.State == ECatFishPickupState::Available
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
	if (PresentationState.State != ECatFishPickupState::Available)
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
	else if (PresentationState.State != ECatFishPickupState::Available)
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
	else if (FindCarriedFish(Character))
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

void ACatFishPickupActor::ArchiveCommittedCapture(const FCatCaptureCommittedResult& Committed,
	const FString& PickerStableNetId)
{
	UCatRunImprintService* Imprint = GetWorld() ? GetWorld()->GetSubsystem<UCatRunImprintService>() : nullptr;
	if (!Imprint || !FishDefinition)
	{
		return;
	}
	FCatCaptureConditionSnapshot Condition;
	Condition.RegionId = RegionId;
	const FGuid FishRecordedGrantId = Imprint->RecordCommittedCapture(Committed, PickerStableNetId, Condition);
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

void ACatFishPickupActor::OnRep_PresentationState(const FCatFishPickupPresentationState& Previous)
{
	RefreshFishPresentation();
	if (PresentationState.State == ECatFishPickupState::Carried)
	{
		if (InteractionSphere)
		{
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
		}
	}
	BP_OnPickupPresentationChanged(Previous, PresentationState);
	if (Previous.FishInstanceId != PresentationState.FishInstanceId || Previous.State != PresentationState.State)
	{
		UE_LOG(LogCatItems, Log, TEXT("Event=fish_pickup_ground_received SessionId=%s FishInstanceId=%s Pickup=%s State=%s Contact=%s Normal=%s VisualScale=%.3f MeshScale=%s World=%s NetMode=%d Role=%s"),
			*PresentationState.FishingSessionId.ToString(EGuidFormats::DigitsWithHyphens), *PresentationState.FishInstanceId.ToString(EGuidFormats::DigitsWithHyphens),
			*GetName(), *UEnum::GetValueAsString(PresentationState.State), *GetActorLocation().ToCompactString(),
			*PresentationState.GroundNormal.ToCompactString(), PresentationState.VisualScale, *FishMesh->GetComponentScale().ToCompactString(),
			*GetNameSafe(GetWorld()), GetNetMode(), *UEnum::GetValueAsString(GetLocalRole()));
	}
}
