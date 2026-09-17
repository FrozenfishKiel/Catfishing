#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatProfileContracts.h"
#include "GameFramework/SaveGame.h"
#include "CatProfileSaveGame.generated.h"

/** 本机玩家的相册、解锁、装备选择及对应授予账本；旧图鉴仅归档保留，当前账号图鉴由独立 Collection 文件读写。 */
UCLASS()
class CATFISHING_API UCatProfileSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/** 当前数字物品身份档案版本；旧版只在完整转换后升级，未知版本保持不可写。 */
	static constexpr int32 CurrentSchemaVersion = 3;

	/** 当前档案结构版本；加载方只接受与代码一致的版本，未知版本保持不可写。 */
	UPROPERTY(SaveGame)
	int32 SchemaVersion = CurrentSchemaVersion;

	/** 本机非图鉴授予的持久化账本；旧图鉴授予只保留、不重放到当前账号，其中不接收 CapturePlanId。 */
	UPROPERTY(SaveGame)
	TArray<FCatPendingGrantJournalEntry> GrantJournal;

	/** 已完整 durable 合并的 GrantId 集合；重复投递据此允许 ACK 而不重复修改内容。 */
	UPROPERTY(SaveGame)
	TArray<FGuid> AppliedGrantIds;

	/** 旧本地鱼图鉴归档；无可靠账号归属，保留供人工迁移，不再作为运行图鉴读写。 */
	UPROPERTY(SaveGame)
	TArray<FCatFishCollectionRecord> FishCollection;

	/** 本地相册稳定索引；图片路径和字节由外部成像存储拥有，不进入本 SaveGame 合同。 */
	UPROPERTY(SaveGame)
	TArray<FCatLocalImprintRecord> Imprints;

	/** 每个一局相册到封面印记的映射；只有 bRunAlbumCover Grant 可以写入。 */
	UPROPERTY(SaveGame)
	TMap<FGuid, FGuid> RunAlbumCovers;

	/** 已正式授予的跨局解锁 ID；具体收益仍由内容定义，不由 Profile 推导。 */
	UPROPERTY(SaveGame)
	TArray<FName> UnlockIds;

	/** 跨局保留的功能型装备槽位选择；服务器仍会用正式目录验证，不包含局内耐久或耗材数量。 */
	UPROPERTY(SaveGame)
	TMap<FName, int32> EquipmentItemBySlot;

	/** v2 的英文物品选择；只供版本迁移读取，转换成功后清空。 */
	UPROPERTY()
	TMap<FName, FName> EquipmentSelectionBySlot;
};
