#pragma once

#include "CoreMinimal.h"
#include "Environment/CatChumFieldReplicationComponent.h"
#include "GameFramework/Actor.h"

#include "CatChumFieldPresentationActor.generated.h"

class USceneComponent;
class UInstancedStaticMeshComponent;

UCLASS(Blueprintable, meta=(ChildCannotTick))
class CATFISHING_API ACatChumFieldPresentationActor : public AActor
{
	GENERATED_BODY()

public:
	ACatChumFieldPresentationActor();
	void ApplyPublicState(const FCatChumFieldPublicItem& NewState, bool bAdded);
	void NotifyFieldRemoved();
	const FCatChumFieldPublicItem& GetPublicState() const { return PublicState; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<USceneComponent> VisualRoot;

	/** 无正式美术时的程序化范围环；实例仅存在于表现 Actor，不参与窝点采样与服务器判定。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Chum Field|Placeholder")
	TObjectPtr<UInstancedStaticMeshComponent> PlaceholderRing;

	/** 无正式美术时的程序化窝料碎屑；由 FieldId 确定性生成，各客户端布局一致。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Chum Field|Placeholder")
	TObjectPtr<UInstancedStaticMeshComponent> PlaceholderSpecks;

	UFUNCTION(BlueprintImplementableEvent, meta=(DisplayName="Chum Field Added"))
	void BP_OnFieldAdded(const FCatChumFieldPublicItem& State);

	UFUNCTION(BlueprintImplementableEvent, meta=(DisplayName="Chum Field Changed"))
	void BP_OnFieldChanged(const FCatChumFieldPublicItem& State);

	UFUNCTION(BlueprintImplementableEvent, meta=(DisplayName="Chum Field Removed"))
	void BP_OnFieldRemoved(const FCatChumFieldPublicItem& State);

private:
	/** 按公开半径重建本地实例；只写表现组件，不回写 PublicState。 */
	void RebuildPlaceholderVisual();
	UPROPERTY(Transient)
	FCatChumFieldPublicItem PublicState;
};
