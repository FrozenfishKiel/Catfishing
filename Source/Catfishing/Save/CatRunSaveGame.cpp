#include "Save/CatRunSaveGame.h"

#include "Logging/CatLog.h"
#include "Inventory/CatInventorySettings.h"
#include "Kismet/GameplayStatics.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CatRunSaveGame)

// 版本查询流程：返回使用数字物品身份的 v8，保留分仓、公款和鱼缸档位断点；这是磁盘 schema，与库存复制版本无关。
int32 UCatRunSaveGame::GetLatestDataVersion() const
{
	return 8;
}

// 旧版加载流程：只接受已知 v5/v6/v7 形状，先备份原始字节，再完整转换嵌套物品身份，成功后升级版本。
// 未知身份或备份失败保持旧版本，由协调器拒绝恢复与后续写入；旧单仓的宿主恢复行为保持不变。
void UCatRunSaveGame::HandlePostLoad()
{
	const int32 PreviousVersion = FormatVersion;
	const bool bKnownLegacy = (FormatVersion >= 5 && FormatVersion <= 7)
		&& (GetSavedDataVersion() == 0 || GetSavedDataVersion() == FormatVersion);
	if (bKnownLegacy)
	{
		TArray<uint8> OriginalBytes;
		const FString BackupSlot = GetSaveSlotName() + TEXT("_BeforeNumericIds");
		FString Error;
		const bool bReadOriginal = UGameplayStatics::LoadDataFromSlot(OriginalBytes, GetSaveSlotName(), GetPlatformUserIndex());
		TArray<uint8> ExistingBackup;
		// 旧构建可能在曾经迁移后又写入新进度；已有备份必须逐字节等于当前原文件，不一致或读失败即拒绝迁移，不覆盖备份。
		const bool bBackupReady = bReadOriginal && (UGameplayStatics::DoesSaveGameExist(BackupSlot, GetPlatformUserIndex())
			? UGameplayStatics::LoadDataFromSlot(ExistingBackup, BackupSlot, GetPlatformUserIndex()) && ExistingBackup == OriginalBytes
			: UGameplayStatics::SaveDataToSlot(OriginalBytes, BackupSlot, GetPlatformUserIndex()));
		if (!bBackupReady) Error = TEXT("OriginalUnavailableOrBackupConflict");
		if (bBackupReady
			&& UCatInventorySettings::MigrateLegacyItemReferences(this, Error))
		{
			FormatVersion = GetLatestDataVersion();
			SavedDataVersion = GetLatestDataVersion();
			UE_LOG(LogCatRun, Log, TEXT("Event=persistence_item_schema_migrated FromVersion=%d ToVersion=%d OriginalFilePreserved=1"), PreviousVersion, FormatVersion);
		}
		else
		{
			UE_LOG(LogCatRun, Error, TEXT("Event=persistence_item_migration_rejected FromVersion=%d Reason=%s OriginalFilePreserved=1"), PreviousVersion, *Error);
		}
	}
	Super::HandlePostLoad();
}

// 写盘回调流程：先让引擎发布自身完成事件，再移出并清空一次性接收者，最后把真实结果交给协调器。
// 委托使用局部副本，接收者即使释放候选强引用或发起下一次保存，也不会改变正在执行的委托。
void UCatRunSaveGame::HandlePostSave(const bool bSuccess)
{
	Super::HandlePostSave(bSuccess);
	FCatRunSaveFinished Completion = MoveTemp(OnSaveFinished);
	OnSaveFinished.Unbind();
	Completion.ExecuteIfBound(this, bSuccess);
}
