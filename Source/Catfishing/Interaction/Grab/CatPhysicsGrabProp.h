#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CatPhysicsGrabProp.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** The prototype's only prop configuration. Dimensions are full local sizes in centimeters. */
USTRUCT()
struct FCatPhysicsGrabPropConfiguration
{
	GENERATED_BODY()
	UPROPERTY() FVector DimensionsCentimeters = FVector(20.0);
	UPROPERTY() FLinearColor Color = FLinearColor(0.36f, 0.52f, 0.64f);
	UPROPERTY() float MassKilograms = 1.0f;
	UPROPERTY() bool bDynamic = false;
	UPROPERTY() bool bRod = false;
};

/** Isolated trial geometry. Authority simulates dynamic props; clients interpolate observations. */
UCLASS()
class CATFISHING_API ACatPhysicsGrabProp : public AActor
{
	GENERATED_BODY()
public:
	ACatPhysicsGrabProp();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	bool ConfigureFromAuthority(const FVector& DimensionsCentimeters, bool bDynamic, float MassKilograms,
		const FLinearColor& Color, bool bRod = false);
	void ResetFromAuthority();
	UStaticMeshComponent* GetPhysicsMesh() const { return PhysicsMesh; }
	bool IsDynamicProp() const { return Configuration.bDynamic; }

protected:
	virtual void BeginPlay() override;

private:
	void ApplyConfiguration();
	UFUNCTION() void OnRep_Configuration();
	UFUNCTION() void OnRep_BodyTransform();
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> PhysicsMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> BlockMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> RodMesh;
	UPROPERTY() TObjectPtr<UMaterialInterface> BaseMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ColorMaterial;
	UPROPERTY(ReplicatedUsing=OnRep_Configuration) FCatPhysicsGrabPropConfiguration Configuration;
	UPROPERTY(ReplicatedUsing=OnRep_BodyTransform) FTransform BodyTransform;
	FTransform ResetTransform;
	float SnapshotElapsedSeconds = 0.0f;
	bool bHasReceivedBodyTransform = false;
	bool bMaterialWarningReported = false;
};
