#pragma once

#include "CoreMinimal.h"
#include "Collection/CatRunFishCollectionTypes.h"
#include "GameFramework/Actor.h"
#include "CatCampCollectionBoardActor.generated.h"

class AGameStateBase;
class UCatRunFishCollectionComponent;
class UStaticMeshComponent;

/** 营地公共图鉴的被动陈列宿主；摆放后自动订阅全队页集，外观、爪印与翻页由蓝图实现。 */
UCLASS(BlueprintType, Blueprintable)
class CATFISHING_API ACatCampCollectionBoardActor : public AActor
{
	GENERATED_BODY()
public:
	ACatCampCollectionBoardActor();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 随时可重读，包括板子比首次捕获更晚生成的情况；没有 GameState 时给空快照。 */
	UFUNCTION(BlueprintPure, Category = "Camp|Collection")
	FCatRunFishCollectionSnapshot GetCollectionSnapshot() const;

protected:
	/** 网格可直接在子蓝图配置；无需另配容器、玩家身份或 Profile 引用。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camp|Collection")
	TObjectPtr<UStaticMeshComponent> BoardMesh;

	/** 本机初次绑定、页集复制或局末清空时刷新全部陈列；此事件没有玩法写权。 */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = "Camp|Collection")
	void BP_RefreshCollectionPresentation(const FCatRunFishCollectionSnapshot& Collection);

private:
	void BindGameState(AGameStateBase* GameState);
	UFUNCTION()
	void RefreshPresentation();
	TWeakObjectPtr<UCatRunFishCollectionComponent> BoundCollection;
	FDelegateHandle GameStateSetHandle;
};
