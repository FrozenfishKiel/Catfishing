#include "Inventory/CatWorldDropProtectionComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "GameFramework/Actor.h"
#include "Logging/CatLog.h"

UCatWorldDropProtectionComponent::UCatWorldDropProtectionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UCatWorldDropProtectionComponent::ArmFromAuthority(AActor* Actor)
{
	if (!IsValid(Actor) || !Actor->HasAuthority()) return;
	const UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(Actor->GetRootComponent());
	if (!Body) return;
	UCatWorldDropProtectionComponent* Protection = Actor->FindComponentByClass<UCatWorldDropProtectionComponent>();
	if (!Protection)
	{
		Protection = NewObject<UCatWorldDropProtectionComponent>(Actor);
		Actor->AddInstanceComponent(Protection);
		Protection->RegisterComponent();
	}
	Protection->LastSafeTransform = Actor->GetActorTransform();
	Protection->LastSafeCenter = Body->Bounds.Origin;
	Protection->SetComponentTickEnabled(true);
	UE_LOG(LogCatfishing, Log, TEXT("Event=world_drop_protection_armed Actor=%s World=%s NetMode=%d Authority=1 LocalRole=%d"),
		*GetNameSafe(Actor), *GetNameSafe(Actor->GetWorld()), Actor->GetNetMode(), Actor->GetLocalRole());
}

void UCatWorldDropProtectionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AActor* Actor = GetOwner();
	UPrimitiveComponent* Body = Actor ? Cast<UPrimitiveComponent>(Actor->GetRootComponent()) : nullptr;
	if (!Actor || !Actor->HasAuthority() || Actor->IsHidden() || Actor->GetAttachParentActor() || !Body)
	{
		SetComponentTickEnabled(false);
		return;
	}
	const FVector Center = Body->Bounds.Origin;
	if (Center.Equals(LastSafeCenter, UE_SMALL_NUMBER)) return;
	const UCatWaterQuerySubsystem* Water = GetWorld()->GetSubsystem<UCatWaterQuerySubsystem>();
	// 球半径保守包住旋转中的箱体；碰撞后反弹/外力改变方向也继续扫实际运动段。
	if (Water && Water->DoesWorldDropSweepTouchWater(LastSafeCenter, Center, Body->Bounds.SphereRadius))
	{
		Actor->SetActorTransform(LastSafeTransform, false, nullptr, ETeleportType::TeleportPhysics);
		Body->SetPhysicsLinearVelocity(FVector(0, 0, FMath::Min(0.0, Body->GetPhysicsLinearVelocity().Z)));
		Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		Actor->ForceNetUpdate();
		UE_LOG(LogCatfishing, Log, TEXT("Event=world_drop_water_blocked Actor=%s From=%s To=%s World=%s NetMode=%d Authority=1 LocalRole=%d"),
			*GetNameSafe(Actor), *LastSafeCenter.ToCompactString(), *Center.ToCompactString(), *GetNameSafe(GetWorld()), Actor->GetNetMode(), Actor->GetLocalRole());
		return;
	}
	LastSafeTransform = Actor->GetActorTransform();
	LastSafeCenter = Center;
}
