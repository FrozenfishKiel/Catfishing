#pragma once

#include "CoreMinimal.h"
#include "CatRunFishCollectionTypes.generated.h"

/** 本局登记者的公开爪印身份；稳定账号只留在服务器，显示名变化或离线不改变爪印。 */
USTRUCT(BlueprintType)
struct FCatRunFishCollectionPawprint
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FGuid RegistrantId;

	UPROPERTY(SaveGame, BlueprintReadOnly)
	FString DisplayName;
};

/** 公共板子的一页；同鱼种只占一页，各次捕获的上钩者各盖一次爪印。 */
USTRUCT(BlueprintType)
struct FCatRunFishCollectionPage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FName FishDefinitionId = NAME_None;

	UPROPERTY(BlueprintReadOnly)
	TArray<FCatRunFishCollectionPawprint> Pawprints;
};

/** GameState 组件复制的完整只读快照；迟加入玩家也取得全局页集，不依赖历史 RPC。 */
USTRUCT(BlueprintType)
struct FCatRunFishCollectionSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FGuid RunId;

	UPROPERTY(BlueprintReadOnly)
	int64 Revision = 0;

	UPROPERTY(BlueprintReadOnly)
	TArray<FCatRunFishCollectionPage> Pages;
};

/** 服务器已提交的捕获记录，也是世界断点的领域导出载荷；不复制、不写个人 Profile。 */
USTRUCT()
struct FCatRunFishCollectionCapture
{
	GENERATED_BODY()

	UPROPERTY(SaveGame)
	FGuid FishInstanceId;

	UPROPERTY(SaveGame)
	FName FishDefinitionId = NAME_None;

	/** 归属沿用捕获链的上钩者；断线重连仍复用同一爪印，不能用显示名或拾取者替代。 */
	UPROPERTY(SaveGame)
	FString HookerStableNetId;

	UPROPERTY(SaveGame)
	FCatRunFishCollectionPawprint Pawprint;
};
