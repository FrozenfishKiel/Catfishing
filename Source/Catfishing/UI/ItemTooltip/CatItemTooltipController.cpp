#include "UI/ItemTooltip/CatItemTooltipController.h"

#include "UI/InventorySlot/CatInventorySlotWidget.h"
#include "UI/ItemTooltip/CatItemTooltipModel.h"
#include "UI/ItemTooltip/CatItemTooltipWidget.h"
#include "Inventory/CatInventoryItemInstance.h"
#include "GameFramework/PlayerController.h"
#include "Logging/CatLog.h"

// 先释放上次绑定，再注入正式 View 并创建纯投影 Model；缺 View 时保持未绑定。
void UCatItemTooltipController::Bind(UCatItemTooltipWidget* InView)
{
	Unbind();
	View = InView;
	if (View) Model = NewObject<UCatItemTooltipModel>(this);
}

// 清理顺序是先停止来源和动画，再释放显示及投影对象，避免卸载期间 Tick 读到半绑定状态。
void UCatItemTooltipController::Unbind()
{
	ForceHideTooltip();
	View = nullptr;
	Model = nullptr;
}

// 悬停先验证非空格与物品定义；通过后让该格成为唯一来源，投影到 View 并在进入点启动淡入。
// 日志只记录显示边沿和稳定物品 ID，不输出捕获者身份，也不逐帧记录数值。
void UCatItemTooltipController::ShowTooltip(UCatInventorySlotWidget* Source, const FVector2D& ScreenPosition)
{
	FCatItemTooltipViewData Data;
	if (!IsValid(Source) || !View || !Model || Source->GetInventoryEntry().StackCount <= 0
		|| !Model->BuildViewData(Source->GetInventoryEntry().Instance, Data))
	{
		HideTooltip(Source);
		return;
	}
	ActiveSource = Source;
	View->RenderItem(Data);
	View->ShowAt(ScreenPosition);
	const APlayerController* Player = View->GetOwningPlayer();
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_item_tooltip_shown World=%s NetMode=%d Authority=%d LocalRole=%d Controller=%s Source=%s Item=%s"),
		*GetPathNameSafe(View->GetWorld()), Player ? static_cast<int32>(Player->GetNetMode()) : -1,
		Player && Player->HasAuthority(), Player ? static_cast<int32>(Player->GetLocalRole()) : -1,
		*GetNameSafe(Player), *GetPathNameSafe(Source),
		*Source->GetInventoryEntry().Instance->GetItemInstanceId().ToString());
}

// 来源身份匹配才清除并淡出；忽略旧来源的迟到 Leave，保护已经接管的新格子。
void UCatItemTooltipController::HideTooltip(const UCatInventorySlotWidget* Source)
{
	if (!Source || ActiveSource.Get() != Source) return;
	ActiveSource.Reset();
	if (View) View->HideTooltip();
	UE_LOG(LogCatUI, Log, TEXT("Event=ui_item_tooltip_hidden World=%s Source=%s Reason=SourceLeft"),
		*GetPathNameSafe(View ? View->GetWorld() : nullptr), *GetPathNameSafe(Source));
}

// 页面或玩家 UI 退出时不依赖鼠标 Leave；先清来源，再立即收起仍可能在淡出的 View。
void UCatItemTooltipController::ForceHideTooltip()
{
	ActiveSource.Reset();
	if (View) View->HideTooltip(true);
}

// 弱来源曾被设置时仍保留一次 Tick 处理失效对象；清理后立即停止，不扫描其他格子。
bool UCatItemTooltipController::IsTickable() const
{
	return !IsTemplate() && View && Model && !ActiveSource.IsExplicitlyNull();
}

// 本地 UI 的显示更新不受玩法暂停限制；返回 true 仅影响本控制器的 Tick。
bool UCatItemTooltipController::IsTickableWhenPaused() const
{
	return true;
}

// 读取正式 View 的 World；未绑定返回空，不另存可能在旅行后过期的 World。
UWorld* UCatItemTooltipController::GetTickableGameObjectWorld() const
{
	return View ? View->GetWorld() : nullptr;
}

// 统计身份读取流程：向 Tickable 系统返回本控制器的独立 stat 项；它只用于性能面板归类活动 Tooltip 投影成本，不读取或写入任何业务状态。
TStatId UCatItemTooltipController::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCatItemTooltipController, STATGROUP_Tickables);
}

// 先检查格子是否仍在悬停且有效，再重读同一个实例的复制字段；无物品则清理，否则只更新内容。
// 这里读取实例而非等待 FastArray 通知，因为耐久单独复制时库存列表可能没有变化。
void UCatItemTooltipController::Tick(float DeltaTime)
{
	UCatInventorySlotWidget* Source = ActiveSource.Get();
	FCatItemTooltipViewData Data;
	if (!IsValid(Source) || !Source->IsHovered() || !Source->IsVisible()
		|| Source->GetInventoryEntry().StackCount <= 0 || !Model->BuildViewData(Source->GetInventoryEntry().Instance, Data))
	{
		ForceHideTooltip();
		return;
	}
	View->RenderItem(Data);
}
