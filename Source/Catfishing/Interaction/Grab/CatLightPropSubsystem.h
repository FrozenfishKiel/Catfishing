#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Components/PrimitiveComponent.h"
#include "CatLightPropSubsystem.generated.h"

class FCatLightPropContactCallback;
struct FCollisionQueryParams;

/** World-local collision policy; never owns movement, grips or fishing membership. */
UCLASS()
class CATFISHING_API UCatLightPropSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	void RegisterCatPart(UPrimitiveComponent* Component);
	void RegisterLightProp(UPrimitiveComponent* Component);
	void UnregisterBody(UPrimitiveComponent* Component);
	void AppendSupportQueryIgnores(FCollisionQueryParams& Params) const;
	uint64 GetModifiedContactCount() const;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;
private:
	void RegisterBody(UPrimitiveComponent* Component, bool bLightProp);
	void PublishParticleRoles();
	UFUNCTION() void HandlePhysicsStateChanged(UPrimitiveComponent* Component, EComponentPhysicsStateChange Change);
	TMap<TWeakObjectPtr<UPrimitiveComponent>, bool> RegisteredBodies;
	TSet<TWeakObjectPtr<UPrimitiveComponent>> UnavailableBodies;
	TMap<int32, bool> PublishedRoles;
	uint64 PublishRevision = 0;
	FCatLightPropContactCallback* ContactCallback = nullptr;
	uint64 LastLoggedContactCount = 0;
	double NextContactLogSeconds = 0;
	bool bShuttingDown = false;
};
