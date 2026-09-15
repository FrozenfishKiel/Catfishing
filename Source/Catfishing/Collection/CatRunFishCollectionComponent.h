#pragma once

#include "CoreMinimal.h"
#include "Collection/CatRunFishCollectionTypes.h"
#include "Components/ActorComponent.h"
#include "Framework/Core/CatRunContracts.h"
#include "CatRunFishCollectionComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCatRunFishCollectionChanged);

/** GameState 默认持有的局内公共图鉴；服务器记录，所有客户端只读，陈列资产缺失也照常积累。 */
UCLASS()
class CATFISHING_API UCatRunFishCollectionComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UCatRunFishCollectionComponent();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** GameState 接收权威 Run 快照时同步生命周期；跨天保留，自然局末清空，房主中断保留供世界存档。 */
	void SynchronizeRunFromAuthority(const FCatRunPublicState& Run);

	/** 只消费正式捕获事实；按实物鱼 ID 幂等，不接受客户端写口，也不依赖个人 Grant/ACK。 */
	bool RecordCaptureFromAuthority(FGuid FishInstanceId, FName FishDefinitionId, const FString& HookerStableNetId);

	UFUNCTION(BlueprintPure, Category = "Catfishing|Collection|Run")
	const FCatRunFishCollectionSnapshot& GetSnapshot() const { return Snapshot; }

	UPROPERTY(BlueprintAssignable, Category = "Catfishing|Collection|Run")
	FCatRunFishCollectionChanged OnCollectionChanged;

	/** 世界槽的导出/恢复边界；旧档的空数组合法，恢复只在本 World 首次开局前执行。 */
	const TArray<FCatRunFishCollectionCapture>& GetCapturesForWorldSave() const { return Captures; }
	bool RestoreCapturesFromAuthority(const TArray<FCatRunFishCollectionCapture>& SavedCaptures);
	static bool ValidateCaptures(const TArray<FCatRunFishCollectionCapture>& SavedCaptures);

private:
	void Publish(const TCHAR* Event);
	void RebuildPages();
	void LogRejected(const TCHAR* Reason, FGuid FishInstanceId) const;

	UFUNCTION()
	void OnRep_Snapshot();

	UPROPERTY(ReplicatedUsing = OnRep_Snapshot)
	FCatRunFishCollectionSnapshot Snapshot;

	/** 服务器唯一记录集合；页集由它归约，Save 只保存它，不保存复制快照。 */
	UPROPERTY()
	TArray<FCatRunFishCollectionCapture> Captures;

	bool bAcceptingCaptures = false;
	bool bRunClosed = false;
	bool bCanRestore = false;
};
