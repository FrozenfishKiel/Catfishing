#include "Save/CatRunSaveGame.h"

#include "Logging/CatLog.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CatRunSaveGame)

// 版本查询流程：返回使用分仓、公款和鱼缸档位断点的 v7；这是磁盘 schema，与库存复制版本无关。
int32 UCatRunSaveGame::GetLatestDataVersion() const
{
	return 7;
}

// v5/v6 只在内存升级版本，不覆盖原文件；未知版本交由协调器拒绝。
// v6 已在线存在两种形状：bHasInventoryCheckpoint 区分分仓断点与旧单仓，绝不能把旧档默认公款 0 当事实。
// 旧单仓保留载荷，由 RestoreWorldAfterHostsReady 在宿主就绪后迁移；玩家背包鱼由库存恢复按槽跳过。
void UCatRunSaveGame::HandlePostLoad()
{
	const int32 PreviousVersion = FormatVersion;
	const bool bLegacyV5 = GetSavedDataVersion() == 0 && FormatVersion == 5;
	const bool bLegacyV6 = (GetSavedDataVersion() == 0 || GetSavedDataVersion() == 6) && FormatVersion == 6;
	if (bLegacyV5 || bLegacyV6)
	{
		FormatVersion = GetLatestDataVersion();
		if (GetSavedDataVersion() == 6) SavedDataVersion = GetLatestDataVersion();
		UE_LOG(LogCatRun, Log, TEXT("Event=persistence_schema_migrated Slot=%s FromVersion=%d ToVersion=%d InventoryCheckpoint=%d Result=InMemoryOnly"),
			*SlotId.ToString(), PreviousVersion, FormatVersion, bHasInventoryCheckpoint);
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
