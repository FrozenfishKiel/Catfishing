#include "CatfishingEditor.h"

#include "CoreGlobals.h"
#include "Framework/Commands/InputBindingManager.h"
#include "Framework/Commands/UICommandInfo.h"
#include "InputCoreTypes.h"
#include "Misc/CoreDelegates.h"

IMPLEMENT_MODULE(FCatfishingEditorModule, CatfishingEditor)

DEFINE_LOG_CATEGORY_STATIC(LogCatfishingEditor, Log, All);

namespace UE::CatfishingEditor::Private
{
	static constexpr int32 PlayWorldStopShortcutMaxRetryFrames = 120;
	static const FName PlayWorldBindingContextName(TEXT("PlayWorld"));
	static const FName StopPlaySessionCommandName(TEXT("StopPlaySession"));

	// 目标快捷键只在编辑器模块内生成；运行时模块继续只认识普通 Escape 的菜单 Action，不反向依赖编辑器命令系统。
	static FInputChord MakeDesiredPlayWorldStopChord()
	{
		return FInputChord(EModifierKey::Shift, EKeys::Escape);
	}

	// 日志里使用 Slate 的显示文本；无效快捷键写成 None，方便区分“清空备用键”和“没有读到命令”两种情况。
	static FString DescribeChord(const FInputChord& Chord)
	{
		return Chord.IsValidChord() ? Chord.GetInputText().ToString() : FString(TEXT("None"));
	}
}

// 启动流程：
// 1. 先设置有限重试预算，避免命令注册顺序异常时无限等待。
// 2. 立即尝试修复已注册的 PlayWorld Stop 命令。
// 3. 如果 UnrealEd 此时还没注册命令，就挂到下一帧继续等，而不是把裸 Escape 留到玩家进入 PIE 时才暴露。
void FCatfishingEditorModule::StartupModule()
{
	PlayWorldStopShortcutAttemptsRemaining = UE::CatfishingEditor::Private::PlayWorldStopShortcutMaxRetryFrames;
	if (!ApplyPlayWorldStopShortcut())
	{
		ArmPlayWorldStopShortcutRetry();
	}
}

// 关闭流程：模块卸载前解除可能残留的逐帧重试委托；快捷键本身已经由 Slate 保存到用户 EditorKeyBindings，不在这里回滚。
void FCatfishingEditorModule::ShutdownModule()
{
	if (PlayWorldStopShortcutRetryHandle.IsValid())
	{
		FCoreDelegates::OnBeginFrame.Remove(PlayWorldStopShortcutRetryHandle);
		PlayWorldStopShortcutRetryHandle.Reset();
	}
	PlayWorldStopShortcutAttemptsRemaining = 0;
}

// 快捷键修复流程：
// 1. 从 Slate 的真实命令表查找 PlayWorld.StopPlaySession，命令缺失时返回 false 交给启动重试。
// 2. 读取当前主键和备用键；如果已经是 Shift+Escape 且没有备用键，直接视为完成。
// 3. 写入 Shift+Escape 主键并清空备用键，防止原有的裸 Escape 仍从第二快捷键停止 PIE。
// 4. 调用 SaveInputBindings 写入 UE 实际读取的 EditorKeyBindings.ini，并记录变更前值与目标文件路径。
bool FCatfishingEditorModule::ApplyPlayWorldStopShortcut()
{
	using namespace UE::CatfishingEditor::Private;

	const TSharedPtr<FUICommandInfo> StopCommand =
		FInputBindingManager::Get().FindCommandInContext(PlayWorldBindingContextName, StopPlaySessionCommandName);
	if (!StopCommand.IsValid())
	{
		return false;
	}

	const FInputChord DesiredChord = MakeDesiredPlayWorldStopChord();
	const FInputChord CurrentPrimary = *StopCommand->GetActiveChord(EMultipleKeyBindingIndex::Primary);
	const FInputChord CurrentSecondary = *StopCommand->GetActiveChord(EMultipleKeyBindingIndex::Secondary);
	if (CurrentPrimary == DesiredChord && !CurrentSecondary.IsValidChord())
	{
		UE_LOG(LogCatfishingEditor, Log,
			TEXT("Event=editor_play_stop_shortcut_ready Command=PlayWorld.StopPlaySession Chord=%s Config=%s"),
			*DescribeChord(CurrentPrimary),
			*GEditorKeyBindingsIni);
		return true;
	}

	StopCommand->SetActiveChord(DesiredChord, EMultipleKeyBindingIndex::Primary);
	StopCommand->SetActiveChord(FInputChord(), EMultipleKeyBindingIndex::Secondary);
	FInputBindingManager::Get().SaveInputBindings();

	UE_LOG(LogCatfishingEditor, Log,
		TEXT("Event=editor_play_stop_shortcut_repaired Command=PlayWorld.StopPlaySession PreviousPrimary=%s PreviousSecondary=%s NewPrimary=%s Config=%s"),
		*DescribeChord(CurrentPrimary),
		*DescribeChord(CurrentSecondary),
		*DescribeChord(DesiredChord),
		*GEditorKeyBindingsIni);
	return true;
}

// 重试安装流程：同一模块只保留一个 OnBeginFrame 句柄；命令系统稍后完成注册时，重试入口会负责解除。
void FCatfishingEditorModule::ArmPlayWorldStopShortcutRetry()
{
	if (PlayWorldStopShortcutRetryHandle.IsValid())
	{
		return;
	}
	PlayWorldStopShortcutRetryHandle =
		FCoreDelegates::OnBeginFrame.AddRaw(this, &FCatfishingEditorModule::HandlePlayWorldStopShortcutRetry);
}

// 重试推进流程：每帧先尝试修复，成功就解绑；未成功则消耗一次预算，耗尽时记录警告并停止等待。
void FCatfishingEditorModule::HandlePlayWorldStopShortcutRetry()
{
	if (ApplyPlayWorldStopShortcut())
	{
		FCoreDelegates::OnBeginFrame.Remove(PlayWorldStopShortcutRetryHandle);
		PlayWorldStopShortcutRetryHandle.Reset();
		return;
	}

	--PlayWorldStopShortcutAttemptsRemaining;
	if (PlayWorldStopShortcutAttemptsRemaining <= 0)
	{
		UE_LOG(LogCatfishingEditor, Warning,
			TEXT("Event=editor_play_stop_shortcut_unavailable Command=PlayWorld.StopPlaySession Reason=CommandNotRegistered"));
		FCoreDelegates::OnBeginFrame.Remove(PlayWorldStopShortcutRetryHandle);
		PlayWorldStopShortcutRetryHandle.Reset();
	}
}
