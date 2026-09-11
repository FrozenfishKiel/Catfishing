#include "UI/WorldInfo/CatAltarWorldInfoComponent.h"
#include "UI/WorldInfo/CatFishTankWorldInfoComponent.h"
#include "Camp/CatAltarActor.h"
#include "Camp/CatCampHubActor.h"
#include "FishContainers/CatFishTankActor.h"
#include "Framework/Game/CatfishingGameState.h"
#include "Engine/World.h"

// 构造流程：沿用公共正式信息牌，将默认阅读条件设为本玩家准心命中且身体位于八米内，并设置排序权重为 10；
// 再启用间隔 0.2 秒的 Tick，供后续观察已关联对象的信息变化，不在构造时读取业务状态或判断献祭权限。
UCatAltarWorldInfoComponent::UCatAltarWorldInfoComponent()
{
	DisplayPolicy = ECatWorldInfoPolicy::FocusOnly;
	DisplayDistanceCentimeters = 800.0f;
	DisplayPriority = 10;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.2f;
}

// 依赖观察：先完成父类 Tick，再沿祭坛->营地->鱼缸解析摘要源；缺少 Run 或鱼缸时使用 -1/零作为观察哨兵。
// 修订、源序号或源身份任一变化时同时更新三份观察缓存并通知重读，因此迟到、替换和销毁不会继续沿用旧储备。
void UCatAltarWorldInfoComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	const ACatfishingGameState* State = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
	const ACatAltarActor* Altar = Cast<ACatAltarActor>(GetOwner());
	const ACatCampHubActor* Camp = Altar ? Altar->GetCampHub() : nullptr;
	const ACatFishTankActor* Tank = Camp ? Camp->ResolveSharedFishTank() : nullptr;
	UCatFishTankWorldInfoComponent* TankInfo = Tank ? Tank->FindComponentByClass<UCatFishTankWorldInfoComponent>() : nullptr;
	const int64 Revision = State ? State->GetRunPublicState().Revision : -1;
	const uint32 Serial = TankInfo ? TankInfo->GetInfoSerial() : 0;
	if (Revision != ObservedRunRevision || Serial != ObservedTankSerial || ObservedTank.Get() != TankInfo)
	{
		ObservedRunRevision = Revision;
		ObservedTankSerial = Serial;
		ObservedTank = TankInfo;
		NotifyInfoChanged();
	}
}

// 信息构建：
// 1. 清空输出并核对挂载者；非祭坛返回 false。本实现依赖控制器先判定观察者和层级，不再检查 Viewer 或 Detail。
// 2. 读取本机 Run 公开快照，依次选择未就绪、结算中、拒绝原因、白天、开放夜晚或已关闭的状态说明。
// 3. 写入进度和目标，再沿显式营地关系取得鱼缸摘要；缸内储备与地面预览分别判定有效性，缺失显示不可用，确认分母仍取祭坛人数。
// 4. 有有效的历史提交凭据才追加旧天结果及进度变化；客户端只消费服务器复制结果，不分类地面鱼或预测结算。
bool UCatAltarWorldInfoComponent::BuildInfo_Implementation(APlayerController* Viewer, ECatWorldInfoDetail Detail, FCatWorldInfoViewData& OutData) const
{
	OutData = FCatWorldInfoViewData();
	const ACatAltarActor* Altar = Cast<ACatAltarActor>(GetOwner());
	if (!Altar) return false;
	OutData.Title = NSLOCTEXT("CatWorldInfo", "AltarTitle", "圣猫祭坛");
	const FText Unavailable = NSLOCTEXT("CatWorldInfo", "Unavailable", "暂不可用");
	const ACatfishingGameState* State = GetWorld() ? GetWorld()->GetGameState<ACatfishingGameState>() : nullptr;
	const FCatRunPublicState* Run = State && State->GetRunPublicState().Phase.RunId.IsValid() ? &State->GetRunPublicState() : nullptr;
	const bool bReady = Run && Run->Phase.Phase != ECatRunPhase::NotStarted;
	OutData.Status = !bReady ? Unavailable
		: Run->DayTransition.bActive ? NSLOCTEXT("CatWorldInfo", "AltarTransition", "献祭结算中")
		: !Altar->GetOfferingError().IsEmpty() ? Altar->GetOfferingError()
		: Run->Phase.Phase == ECatRunPhase::DayActive ? NSLOCTEXT("CatWorldInfo", "AltarDaytime", "仅夜晚可献祭")
		: Run->Phase.Phase == ECatRunPhase::NormalNight && Run->Phase.bOfferingOpen ? NSLOCTEXT("CatWorldInfo", "AltarWaiting", "夜晚 · 等待全员确认")
		: NSLOCTEXT("CatWorldInfo", "AltarClosed", "献祭已关闭");
	// 行只包含可显示数据，键名供自定义 WBP 识别稳定概念；不把排列或字体塞入业务提供者。
	auto AddRow = [&OutData](FName Id, const FText& Label, const FText& Value, float Progress = -1.0f)
	{
		FCatWorldInfoRow& Row = OutData.Rows.AddDefaulted_GetRef();
		Row.Id = Id;
		Row.Label = Label;
		Row.Value = Value;
		Row.Progress = Progress;
	};
	AddRow(TEXT("WorldProgress"), NSLOCTEXT("CatWorldInfo", "WorldProgress", "世界进度"), bReady
		? FText::Format(NSLOCTEXT("CatWorldInfo", "Percent", "{0}%"), FText::AsNumber(Run->WorldProgress)) : Unavailable,
		bReady ? Run->WorldProgress / 100.0f : -1.0f);
	AddRow(TEXT("DailyTarget"), NSLOCTEXT("CatWorldInfo", "DailyTarget", "今日任务"), bReady && Run->DailyOfferingTarget > 0
		? FText::Format(NSLOCTEXT("CatWorldInfo", "Points", "{0} 点"), FText::AsNumber(Run->DailyOfferingTarget)) : Unavailable);
	const ACatCampHubActor* Camp = Altar->GetCampHub();
	const ACatFishTankActor* Tank = Camp ? Camp->ResolveSharedFishTank() : nullptr;
	const UCatFishTankWorldInfoComponent* TankInfo = Tank ? Tank->FindComponentByClass<UCatFishTankWorldInfoComponent>() : nullptr;
	int32 StockPoints = 0, Count = 0, Capacity = 0, GroundPoints = 0;
	const bool bStockReady = TankInfo && TankInfo->TryGetOfferingSummary(StockPoints, Count, Capacity);
	AddRow(TEXT("TankReserve"), NSLOCTEXT("CatWorldInfo", "TankReserve", "缸内储备"), bStockReady
		? FText::Format(NSLOCTEXT("CatWorldInfo", "Points", "{0} 点"), FText::AsNumber(StockPoints)) : Unavailable);
	AddRow(TEXT("GroundOffering"), NSLOCTEXT("CatWorldInfo", "GroundOffering", "本次地面待献"), Altar->TryGetGroundOfferingPoints(GroundPoints)
		? FText::Format(NSLOCTEXT("CatWorldInfo", "Points", "{0} 点"), FText::AsNumber(GroundPoints)) : Unavailable);
	AddRow(TEXT("Confirmed"), NSLOCTEXT("CatWorldInfo", "Confirmed", "已确认"), bReady
		? FText::Format(NSLOCTEXT("CatWorldInfo", "Fraction", "{0} / {1}"), FText::AsNumber(Altar->GetConfirmedCount()), FText::AsNumber(Altar->GetEligibleCount())) : Unavailable);
	if (Run && Run->DayTransition.LastCommittedOffering.RequestId.IsValid())
	{
		const FCatOfferingResultSnapshot& Result = Run->DayTransition.LastCommittedOffering;
		AddRow(TEXT("LastOffering"), FText::Format(NSLOCTEXT("CatWorldInfo", "LastOfferingDay", "第 {0} 天献祭"), FText::AsNumber(Result.SettlementDay)),
			FText::Format(NSLOCTEXT("CatWorldInfo", "OfferingResult", "{0}/{1} 点 · {2}"), FText::AsNumber(Result.OfferedPoints), FText::AsNumber(Result.TargetPoints),
				Result.bMetTarget ? NSLOCTEXT("CatWorldInfo", "TargetMet", "达标") : NSLOCTEXT("CatWorldInfo", "TargetMissed", "未达标")));
		AddRow(TEXT("LastWorldProgress"), NSLOCTEXT("CatWorldInfo", "LastProgress", "上次进度变化"),
			FText::Format(NSLOCTEXT("CatWorldInfo", "ProgressChange", "{0}% → {1}%"), FText::AsNumber(Result.WorldProgressBefore), FText::AsNumber(Result.WorldProgressAfter)));
	}
	return true;
}
