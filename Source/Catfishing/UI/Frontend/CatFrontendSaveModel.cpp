#include "UI/Frontend/CatFrontendSaveModel.h"

#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Logging/CatLog.h"
#include "Save/CatSaveSubsystem.h"

namespace
{
	const TArray<FCatSaveSlotSummary> EmptySlotSummaries;
}

// SaveModel 绑定流程把前端存档页接到正式 SaveSubsystem，避免 UI 在本地拼出一套影子槽目录：
// 1. 先解除可能来自失效 LocalPlayer 的订阅，保证复用对象不会把失效 GameInstance 的异步结果投给新页面。
// 2. 再从指定 LocalPlayer 的 GameInstance 取得唯一 Save 子系统并订阅其变化。
// 3. 最后请求真实槽目录刷新；目录 I/O 期间 View 只读取正式 busy/结果，不构造 Profile 或内存替身。
void UCatFrontendSaveModel::Initialize(ULocalPlayer* InLocalPlayer)
{
	Shutdown();
	LocalPlayer = InLocalPlayer;
	UGameInstance* GameInstance = InLocalPlayer ? InLocalPlayer->GetGameInstance() : nullptr;
	UCatSaveSubsystem* Save = GameInstance ? GameInstance->GetSubsystem<UCatSaveSubsystem>() : nullptr;
	if (!Save)
	{
		UE_LOG(LogCatUI, Warning, TEXT("Event=frontend_save_source_unavailable Player=%s GameInstance=%s"), *GetNameSafe(InLocalPlayer), *GetNameSafe(GameInstance));
		OnChanged.Broadcast();
		return;
	}

	SaveSubsystem = Save;
	SaveChangedHandle = Save->OnChanged.AddUObject(this, &ThisClass::HandleSaveChanged);
	UE_LOG(LogCatUI, Log, TEXT("Event=frontend_save_source_bound Player=%s GameInstance=%s Save=%s"), *GetNameSafe(InLocalPlayer), *GetNameSafe(GameInstance), *GetNameSafe(Save));
	RefreshSlotSummaries();
}

// 销毁流程：
// 1. 若 Save 子系统仍有效，先按句柄解除 View 对其状态广播的订阅。
// 2. 再清空句柄、来源和 LocalPlayer，保证迟到的磁盘回调不会刷新已销毁的 Frontend。
// 3. 不取消 Save 已受理的 I/O；该请求仍由 GameInstance 级系统按自身生命周期收口。
void UCatFrontendSaveModel::Shutdown()
{
	if (UCatSaveSubsystem* Save = SaveSubsystem.Get())
	{
		Save->OnChanged.Remove(SaveChangedHandle);
		UE_LOG(LogCatUI, Log, TEXT("Event=frontend_save_source_unbound Player=%s Save=%s"), *GetNameSafe(LocalPlayer.Get()), *GetNameSafe(Save));
	}
	SaveChangedHandle.Reset();
	SaveSubsystem.Reset();
	LocalPlayer.Reset();
}

// 目录刷新流程：只把请求交给正式 Save 子系统；来源不存在时广播一次空状态，让 View 显示真实不可用而不是沿用失效列表。
void UCatFrontendSaveModel::RefreshSlotSummaries()
{
	if (UCatSaveSubsystem* Save = GetSaveSubsystem())
	{
		Save->RefreshSlotSummaries();
		return;
	}
	OnChanged.Broadcast();
}

// 新建槽流程：只向已绑定 Save 提交显示名；正式来源已在受理和异步终态广播，Model 不重复通知；来源缺失直接返回明确拒绝。
FCatSaveResult UCatFrontendSaveModel::RequestCreateSlot(const FString& DisplayName)
{
	if (UCatSaveSubsystem* Save = GetSaveSubsystem())
	{
		return Save->RequestCreateSlot(DisplayName);
	}
	return FCatSaveResult{FGuid::NewGuid(), false, FText::FromString(TEXT("存档服务当前不可用。"))};
}

// 读档流程：以稳定槽标识向已绑定 Save 提交读取，通知仅转发正式来源；Controller 在受理返回及后续变化后核对活动槽和许可，Model 不重复广播或创建房间。
FCatSaveResult UCatFrontendSaveModel::RequestLoadSlot(const FName SlotId)
{
	if (UCatSaveSubsystem* Save = GetSaveSubsystem())
	{
		return Save->RequestLoadSlot(SlotId);
	}
	return FCatSaveResult{FGuid::NewGuid(), false, FText::FromString(TEXT("存档服务当前不可用。"))};
}

// 删除流程：向已绑定 Save 提交稳定槽标识；活动槽、busy 和文件失败由 Save 裁决并广播，Model 不另改列表或重复通知。
FCatSaveResult UCatFrontendSaveModel::RequestDeleteSlot(const FName SlotId)
{
	if (UCatSaveSubsystem* Save = GetSaveSubsystem())
	{
		return Save->RequestDeleteSlot(SlotId);
	}
	return FCatSaveResult{FGuid::NewGuid(), false, FText::FromString(TEXT("存档服务当前不可用。"))};
}

// 未入房载荷释放流程：只向已绑定的正式 Save 来源提交释放，保留其 busy 拒绝；成功时 Save 自己同步广播 OnChanged，Model 不重复广播或写入第二份活动槽状态。
bool UCatFrontendSaveModel::ReleaseActiveRun()
{
	UCatSaveSubsystem* Save = GetSaveSubsystem();
	return Save && Save->ReleaseActiveRun();
}

// 摘要读取流程：返回 Save 持有的 const 列表；来源失效时返回稳定空数组，View 不会保留跨 LocalPlayer 的无效槽位。
const TArray<FCatSaveSlotSummary>& UCatFrontendSaveModel::GetSlotSummaries() const
{
	if (UCatSaveSubsystem* Save = GetSaveSubsystem())
	{
		return Save->GetSlotSummaries();
	}
	return EmptySlotSummaries;
}

// 旅行许可读取流程：只转发 Save 成功读入后的正式事实；来源失效时 fail-closed，Controller 不能据失效选择创建房间。
bool UCatFrontendSaveModel::HasLoadedRunForTravel() const
{
	return GetSaveSubsystem() && GetSaveSubsystem()->HasLoadedRunForTravel();
}

// 忙碌读取流程：只转发正式异步 I/O 标记；来源失效时返回 false，同时结果文本会明确服务不可用而不把它伪装成空闲成功。
bool UCatFrontendSaveModel::IsBusy() const
{
	return GetSaveSubsystem() && GetSaveSubsystem()->IsBusy();
}

// 结果读取流程：从正式 Save 来源读取用户可见说明；来源失效时返回明确错误，调用方不能由文本反推磁盘结果。
FText UCatFrontendSaveModel::GetLastResultText() const
{
	if (UCatSaveSubsystem* Save = GetSaveSubsystem())
	{
		return Save->GetLastResultText();
	}
	return FText::FromString(TEXT("存档服务当前不可用。"));
}

// 活动槽读取流程：只转发 Save 的稳定活动槽；来源失效时返回 None，避免迟到通知错误匹配新流程。
FName UCatFrontendSaveModel::GetActiveSlotId() const
{
	if (UCatSaveSubsystem* Save = GetSaveSubsystem())
	{
		return Save->GetActiveSlotId();
	}
	return NAME_None;
}

// 来源定位流程：先检查已订阅的 Save 弱引用，再确认 LocalPlayer 仍持有有效 GameInstance；任一层失效即返回空，不构造替代系统。
UCatSaveSubsystem* UCatFrontendSaveModel::GetSaveSubsystem() const
{
	UCatSaveSubsystem* Save = SaveSubsystem.Get();
	ULocalPlayer* Player = LocalPlayer.Get();
	return Save && Player && Player->GetGameInstance() ? Save : nullptr;
}

// 通知转发流程：Save 子系统已经更新自己的目录、活动槽、busy 或结果；Model 不复制任何字段，只广播让 View 和 Controller 重新读取同一来源。
void UCatFrontendSaveModel::HandleSaveChanged()
{
	OnChanged.Broadcast();
}
