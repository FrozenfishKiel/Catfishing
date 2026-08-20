#pragma once

#include "CoreMinimal.h"
#include "Framework/Core/CatProfileContracts.h"
#include "GameFramework/SaveGame.h"
#include "CatProfileSaveGame.generated.h"

/** 本地玩家永久档案；只保存图鉴、相册索引、解锁与 Grant Journal，不保存局内实物鱼或服务器身份。 */
UCLASS()
class CATFISHING_API UCatProfileSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/** 代码当前理解的唯一档案结构版本；版本迁移尚未实现，任何不匹配档案都必须保持不可写。 */
	static constexpr int32 CurrentSchemaVersion = 1;

	/** 当前档案结构版本；加载方只接受与代码一致的版本，未知版本保持不可写，避免无迁移规则时覆盖玩家数据。 */
	UPROPERTY(SaveGame)
	int32 SchemaVersion = CurrentSchemaVersion;

	/** Grant 先落 Pending、再合并、最后落 Complete 的 durable Journal；其中永远不接收 CapturePlanId。 */
	UPROPERTY(SaveGame)
	TArray<FCatPendingGrantJournalEntry> GrantJournal;

	/** 已完整 durable 合并的 GrantId 集合；重复投递据此允许 ACK 而不重复修改内容。 */
	UPROPERTY(SaveGame)
	TArray<FGuid> AppliedGrantIds;

	/** 本地鱼图鉴事实；实物容器删除或一局结束不会回滚这些记录。 */
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

	/**
	 * 跨局保留的功能型装备槽位选择；服务器仍会用正式目录验证，不包含局内耐久或耗材数量。
	 * 当前没有任何代码读写它：原来的一对读写入口从未被接上，已删。字段本身保留是因为它已经在玩家存档里，
	 * 删掉 UPROPERTY(SaveGame) 会改变存档布局；装备选择真正接线时直接用这个字段，不要另建一个。
	 */
	UPROPERTY(SaveGame)
	TMap<FName, FName> EquipmentSelectionBySlot;
};
