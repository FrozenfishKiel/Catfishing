#include "Save/CatRunSaveGame.h"

#include "Logging/CatLog.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CatRunSaveGame)

// 版本查询流程：v9 明确区分职责拆分后的物品使用架构；物品编号保持不变，旧开发档不能冒充新格式。
int32 UCatRunSaveGame::GetLatestDataVersion() const
{
	return 9;
}

// 读盘流程：保留磁盘版本和原始载荷，让协调器拒绝不兼容旧档；本回调不迁移、不覆盖，也不替玩家删除旧文件。
void UCatRunSaveGame::HandlePostLoad()
{
	if (FormatVersion != GetLatestDataVersion())
		UE_LOG(LogCatRun, Warning, TEXT("Event=persistence_item_schema_rejected FileVersion=%d RequiredVersion=%d OriginalFilePreserved=1"), FormatVersion, GetLatestDataVersion());
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
