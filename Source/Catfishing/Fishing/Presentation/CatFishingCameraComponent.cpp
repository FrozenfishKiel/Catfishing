#include "Fishing/Presentation/CatFishingCameraComponent.h"

#include "Camera/CameraTypes.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "Fishing/Actors/CatFishingRodActor.h"
#include "Fishing/CatFishingService.h"
#include "Fishing/Presentation/CatFishingPresentationSettings.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Logging/CatLog.h"

UCatFishingCameraComponent::UCatFishingCameraComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

const ACatFishingRodActor* UCatFishingCameraComponent::FindHeldRodOperatedBy(const AController* Controller)
{
	UWorld* World = Controller ? Controller->GetWorld() : nullptr;
	APlayerState* Player = Controller ? Controller->PlayerState.Get() : nullptr;
	if (!World || !Player) return nullptr;
	const auto IsHeld = [Player](const ACatFishingRodActor* Rod)
	{
		return IsValid(Rod) && Rod->GetPresentationState().bDeployed
			&& !Rod->GetPresentationState().bBroken
			&& Rod->GetPresentationState().PoseMode == ECatFishingRodPoseMode::Held
			&& Rod->GetPresentationState().OperatorPlayerStates.Contains(Player);
	};
	if (UCatFishingService* Fishing = World->GetSubsystem<UCatFishingService>())
	{
		const ACatFishingRodActor* Rod = Fishing->FindRodOperatedBy(Player);
		return IsHeld(Rod) ? Rod : nullptr;
	}
	for (TActorIterator<ACatFishingRodActor> It(World); It; ++It)
	{
		if (IsHeld(*It)) return *It;
	}
	return nullptr;
}

const ACatFishingRodActor* UCatFishingCameraComponent::FindFightRodHeldBy(const AController* Controller)
{
	const ACatFishingRodActor* Rod = FindHeldRodOperatedBy(Controller);
	return Rod && Rod->IsPrimaryOperator(Controller->PlayerState)
		&& Rod->GetCarrierConstraintState().bFightActive ? Rod : nullptr;
}

FRotator UCatFishingCameraComponent::ResolveFacingRotation(const AController* Controller)
{
	if (const ACatFishingRodActor* Rod = FindFightRodHeldBy(Controller))
	{
		return Rod->GetGripWorldTransform().Rotator();
	}
	return Controller ? Controller->GetControlRotation() : FRotator::ZeroRotator;
}

const ACatFishingRodActor* UCatFishingCameraComponent::FindLocalViewRod() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const APlayerController* Controller = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	return Controller && Controller->IsLocalController() && Controller->GetPawn() == Character
		&& Controller->GetViewTarget() == Character ? FindFightRodHeldBy(Controller) : nullptr;
}

bool UCatFishingCameraComponent::TryGetCameraView(const float DeltaTime, FMinimalViewInfo& OutView)
{
	const ACatFishingRodActor* Rod = FindLocalViewRod();
	if (!Rod)
	{
		RestoreView();
		bReportedInvalidView = false;
		return false;
	}
	const UCatFishingPresentationSettings* Settings = GetDefault<UCatFishingPresentationSettings>();
	const FTransform Grip = Rod->GetGripWorldTransform();
	// 配置非法时沿用原镜头；只报告一次，避免每帧重复刷屏。
	if (Grip.ContainsNaN() || Settings->FightCameraGripOffsetCentimeters.ContainsNaN()
		|| !FMath::IsFinite(Settings->FightCameraFieldOfView)
		|| !FMath::IsFinite(Settings->FightCameraFollowResponseSeconds)
		|| Settings->FightCameraFollowResponseSeconds <= 0.0
		|| Settings->FightCameraFieldOfView < 30.0f || Settings->FightCameraFieldOfView > 140.0f)
	{
		RestoreView();
		if (!bReportedInvalidView)
		{
			UE_LOG(LogCatFishing, Warning,
				TEXT("Event=fishing_fight_camera_rejected Reason=InvalidViewConfiguration RodActorId=%s Holder=%s World=%s NetMode=%d Authority=%s LocalRole=%d"),
				*Rod->GetPresentationState().RodActorId.ToString(EGuidFormats::DigitsWithHyphens), *GetNameSafe(GetOwner()), *GetNameSafe(GetWorld()),
				static_cast<int32>(GetWorld()->GetNetMode()), GetOwner()->HasAuthority() ? TEXT("true") : TEXT("false"),
				static_cast<int32>(GetOwner()->GetLocalRole()));
			bReportedInvalidView = true;
		}
		return false;
	}
	bReportedInvalidView = false;
	const bool bNewRod = ViewedRodId != Rod->GetPresentationState().RodActorId;
	if (bNewRod)
	{
		RestoreView();
		ACharacter* Character = CastChecked<ACharacter>(GetOwner());
		ViewingController = Cast<APlayerController>(Character->GetController());
		HiddenMesh = Character->GetMesh();
		if (USkeletalMeshComponent* Mesh = HiddenMesh.Get())
		{
			bSavedOwnerNoSee = Mesh->bOwnerNoSee;
			Mesh->SetOwnerNoSee(true);
		}
		ViewedRodId = Rod->GetPresentationState().RodActorId;
		SmoothedGrip = Grip;
		LastTargetRotation = Grip.GetRotation();
		SetComponentTickEnabled(true);
	}
	else
	{
		// 即使本帧没有收到新姿态也继续追随。指数响应跨帧率一致，四元数走最短弧，
		// 避免僵持慢转、20Hz 复制及 +/-180 度接缝直接变成镜头台阶。
		const double FollowSeconds = FMath::IsFinite(DeltaTime) ? FMath::Clamp<double>(DeltaTime, 0.0, 0.1) : 0.0;
		const double Alpha = 1.0 - FMath::Exp(-FollowSeconds / Settings->FightCameraFollowResponseSeconds);
		const FQuat PreviousRotation = SmoothedGrip.GetRotation();
		SmoothedGrip.SetRotation(FQuat::Slerp(PreviousRotation, Grip.GetRotation(), Alpha).GetNormalized());
		SmoothedGrip.SetLocation(FMath::Lerp(SmoothedGrip.GetLocation(), Grip.GetLocation(), Alpha));
		MaximumTargetStepDegrees = FMath::Max(MaximumTargetStepDegrees,
			FMath::RadiansToDegrees(LastTargetRotation.AngularDistance(Grip.GetRotation())));
		MaximumViewStepDegrees = FMath::Max(MaximumViewStepDegrees,
			FMath::RadiansToDegrees(PreviousRotation.AngularDistance(SmoothedGrip.GetRotation())));
		LastTargetRotation = Grip.GetRotation();
	}
	OutView.Location = SmoothedGrip.TransformPositionNoScale(Settings->FightCameraGripOffsetCentimeters);
	OutView.Rotation = SmoothedGrip.Rotator();
	OutView.FOV = Settings->FightCameraFieldOfView;
	LastViewRotation = OutView.Rotation;
	const double Now = GetWorld()->GetTimeSeconds();
	if (bNewRod || Now >= NextDiagnosticSeconds)
	{
		LogView(bNewRod ? TEXT("FirstPerson") : TEXT("FollowingRod"));
		NextDiagnosticSeconds = Now + 1.0;
		MaximumTargetStepDegrees = MaximumViewStepDegrees = 0.0;
	}
	return true;
}

void UCatFishingCameraComponent::RestoreView()
{
	if (!ViewedRodId.IsValid()) return;
	if (USkeletalMeshComponent* Mesh = HiddenMesh.Get()) Mesh->SetOwnerNoSee(bSavedOwnerNoSee);
	if (APlayerController* Controller = ViewingController.Get(); Controller && Controller->GetPawn() == GetOwner())
	{
		// 离开时从最后看见的方向继续，丢弃受阻期间看不见的超前目标。
		Controller->SetControlRotation(LastViewRotation);
	}
	LogView(TEXT("Restored"));
	HiddenMesh.Reset();
	ViewingController.Reset();
	ViewedRodId.Invalidate();
	SmoothedGrip = FTransform::Identity;
	LastTargetRotation = FQuat::Identity;
	MaximumTargetStepDegrees = MaximumViewStepDegrees = 0.0;
	SetComponentTickEnabled(false);
}

void UCatFishingCameraComponent::TickComponent(const float DeltaTime, const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// 切换观战目标后 CalcCamera 不再被调用，仍必须归还隐藏状态。
	if (!FindLocalViewRod()) RestoreView();
}

void UCatFishingCameraComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RestoreView();
	Super::EndPlay(EndPlayReason);
}

void UCatFishingCameraComponent::LogView(const TCHAR* Mode) const
{
	const APlayerController* Controller = ViewingController.Get();
	UE_LOG(LogCatFishing, Display,
		TEXT("Event=fishing_fight_camera Mode=%s RodActorId=%s HolderPlayerId=%d Holder=%s "
			"ViewRotation=%s RequestedRotation=%s TargetRotation=%s FollowResponseSeconds=%.3f "
			"FollowErrorDegrees=%.3f MaxTargetStepDegrees=%.3f MaxViewStepDegrees=%.3f "
			"Source=%s World=%s NetMode=%d Authority=%s LocalRole=%d"),
		Mode, *ViewedRodId.ToString(EGuidFormats::DigitsWithHyphens), Controller && Controller->PlayerState ? Controller->PlayerState->GetPlayerId() : INDEX_NONE,
		*GetNameSafe(GetOwner()), *LastViewRotation.ToCompactString(),
		Controller ? *Controller->GetControlRotation().ToCompactString() : TEXT("None"),
		*LastTargetRotation.Rotator().ToCompactString(), GetDefault<UCatFishingPresentationSettings>()->FightCameraFollowResponseSeconds,
		FMath::RadiansToDegrees(SmoothedGrip.GetRotation().AngularDistance(LastTargetRotation)),
		MaximumTargetStepDegrees, MaximumViewStepDegrees,
		!GetOwner()->HasAuthority() ? TEXT("ReplicatedRod") : TEXT("AuthorityRod"),
		*GetNameSafe(GetWorld()), static_cast<int32>(GetWorld()->GetNetMode()),
		GetOwner()->HasAuthority() ? TEXT("true") : TEXT("false"), static_cast<int32>(GetOwner()->GetLocalRole()));
}
