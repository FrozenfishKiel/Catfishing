#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "CatRodBendComponent.generated.h"

class ACatFishingHookActor;
class UStaticMeshComponent;
class UStaticMesh;

/** Reuses the tagged rod mesh/materials, deforms only its flexible span, and moves the visual line marker. */
UCLASS(ClassGroup=(Fishing), meta=(BlueprintSpawnableComponent))
class CATFISHING_API UCatRodBendComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()
public:
	UCatRodBendComponent(const FObjectInitializer& ObjectInitializer);
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	bool InitializeVisual();
	void BindHook(ACatFishingHookActor* Hook);
	/** Called before the line samples the marker. World time prevents duplicate integration in the same frame. */
	void RefreshVisual();
	bool IsVisualReady() const { return !RestSections.IsEmpty(); }
	FVector GetBendRadians() const { return SmoothedBend; }
	FVector GetRestTipLocal() const { return RestTip; }
	FVector GetRestTipWorld() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void RestoreSource();
	void LogVisualEvent(const TCHAR* Event, const TCHAR* Result, double TensionNewtons) const;
	TWeakObjectPtr<UStaticMeshComponent> SourceMesh;
	TWeakObjectPtr<USceneComponent> TipMarker;
	TWeakObjectPtr<UStaticMesh> BuiltMesh;
	TWeakObjectPtr<ACatFishingHookActor> LoadHook;
	TArray<FProcMeshSection> RestSections;
	TArray<int32> SourceMaterialIndices;
	/** Diagnostic correlation only, retained after the Hook has been destroyed. */
	FGuid DiagnosticSessionId;
	FVector RestTip = FVector::ZeroVector;
	FVector BendBase = FVector::ZeroVector;
	FVector BendAxis = FVector::UpVector;
	FVector SmoothedBend = FVector::ZeroVector;
	double LastUpdateSeconds = -1.0;
	double NextDiagnosticSeconds = 0.0;
	bool bWasLoaded = false;
	bool bFailureLogged = false;
	bool bSourceWasVisible = true;
	bool bSourceWasHidden = false;
};
