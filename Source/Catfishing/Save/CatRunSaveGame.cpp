#include "Save/CatRunSaveGame.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CatRunSaveGame)

// 版本查询流程：返回包含普通库存鱼载荷的 v6；这是磁盘 schema，与库存复制版本无关。
int32 UCatRunSaveGame::GetLatestDataVersion() const
{
	return 6;
}

// 读盘迁移流程：仅识别未使用 LocalPlayerSaveGame 的旧 v5；旧格式没有库存鱼，新增鱼字段自然保持空值。
// 迁移只改已加载对象，原文件不动；未知版本保持原值，由协调器拒绝，绝不默认创建新世界覆盖它。
void UCatRunSaveGame::HandlePostLoad()
{
	if (GetSavedDataVersion() == 0 && FormatVersion == 5)
	{
		FormatVersion = GetLatestDataVersion();
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
