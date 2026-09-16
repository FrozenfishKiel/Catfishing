#pragma once
#include "CoreMinimal.h"
#include "Framework/Core/CatProfileContracts.h"
#include "GameFramework/SaveGame.h"
#include "CatCollectionSaveGame.generated.h"

/** 账号专用图鉴载荷；正式 Steam 账号与编辑器开发账号使用隔离槽，仅含图鉴和授予账本，不包含相册、装备或局内断点。 */
UCLASS()
class CATFISHING_API UCatCollectionSaveGame : public USaveGame
{
	GENERATED_BODY()
public:
	/** 专用数字图鉴格式版本；创建时初始化、Profile 加载和写盘前精确校验，未知版本拒绝覆盖。 */
	UPROPERTY(SaveGame) int32 SchemaVersion = 1;
	/** 文件所属账号键；Profile 创建时写入、加载时与当前身份核对，禁止跨账号认领，不输出到日志。 */
	UPROPERTY(SaveGame) FString AccountKey;
	/** 本账号捕获、知识和重量记录；Profile 的成功授予事务是唯一写入者。 */
	UPROPERTY(SaveGame) TArray<FCatFishCollectionRecord> FishCollection;
	/** 当前追踪的数字鱼种，0 表示未追踪；Profile 验证捕获后写入，图鉴与库存读取，名称和偏好不复制保存。 */
	UPROPERTY(SaveGame) int32 TrackedItemId = 0;
	/** 本账号尚待合并及已完成的图鉴授予；只含图鉴种类，不存相册授予。 */
	UPROPERTY(SaveGame) TArray<FCatPendingGrantJournalEntry> GrantJournal;
	/** 完整落盘的授予去重集合；重投递据此返回 ACK，不重复增加记录。 */
	UPROPERTY(SaveGame) TArray<FGuid> AppliedGrantIds;
};
