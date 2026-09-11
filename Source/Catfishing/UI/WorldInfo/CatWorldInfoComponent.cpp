#include "UI/WorldInfo/CatWorldInfoComponent.h"
#include "UI/WorldInfo/CatWorldInfoRegistry.h"
#include "UI/WorldInfo/CatWorldInfoWidget.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

// 默认接线：关闭 Tick，设置锚点偏移和正式软类；是否可见由每名玩家自己的控制器求值。
UCatWorldInfoComponent::UCatWorldInfoComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetRelativeLocation(FVector(0, 0, 120));
	WidgetClass = TSoftClassPtr<UCatWorldInfoWidget>(FSoftClassPath(TEXT("/Game/UI/WorldInfo/WBP_CatWorldInfo.WBP_CatWorldInfo_C")));
}

// 注册流程：父类完成组件入场后登记本世界目录；专服不创建 UI，但允许领域派生组件生产摘要。
void UCatWorldInfoComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UCatWorldInfoRegistry* Registry = GetWorld()->GetSubsystem<UCatWorldInfoRegistry>()) Registry->RegisterSource(this);
}

// 退出流程：先从发现目录移除，再完成父类退出；视图持弱引用，不阻止组件销毁。
void UCatWorldInfoComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
		if (UCatWorldInfoRegistry* Registry = World->GetSubsystem<UCatWorldInfoRegistry>()) Registry->UnregisterSource(this);
	Super::EndPlay(EndPlayReason);
}

// 显隐求值：关闭、无观察者、距离配置无效或超距时隐藏；距离内 FocusOnly 要求命中，NearbyFull 或命中返回详情，其余返回摘要。
// 距离由控制器按身体到对象原点计算；层级结果只控制阅读，不放宽交互资格。
ECatWorldInfoDetail UCatWorldInfoComponent::EvaluateDisplay_Implementation(APlayerController* Viewer, const bool bFocused, const double DistanceCentimeters) const
{
	if (!bInfoEnabled || !Viewer || !FMath::IsFinite(DistanceCentimeters) || !FMath::IsFinite(DisplayDistanceCentimeters)
		|| DisplayDistanceCentimeters <= 0 || DistanceCentimeters > DisplayDistanceCentimeters) return ECatWorldInfoDetail::Hidden;
	if (DisplayPolicy == ECatWorldInfoPolicy::FocusOnly) return bFocused ? ECatWorldInfoDetail::Full : ECatWorldInfoDetail::Hidden;
	return DisplayPolicy == ECatWorldInfoPolicy::NearbyFull || bFocused ? ECatWorldInfoDetail::Full : ECatWorldInfoDetail::Summary;
}

// 默认数据入口：先复制静态内容，再检查观察者、非隐藏层级和非空标题；任一不满足即返回 false，调用方不得显示输出或沿用旧内容。
bool UCatWorldInfoComponent::BuildInfo_Implementation(APlayerController* Viewer, ECatWorldInfoDetail Detail, FCatWorldInfoViewData& OutData) const
{
	OutData = StaticInfo;
	return Viewer && Detail != ECatWorldInfoDetail::Hidden && !OutData.Title.IsEmpty();
}

// 内容通知：只递增本地显示序号；控制器下次处理可见且可投影的候选时重读该源，隐藏期间不立即重建内容。
void UCatWorldInfoComponent::NotifyInfoChanged()
{
	++InfoSerial;
}

// 通知读取：返回最近的数据变更序号，允许每名玩家独立追踪最后渲染到的位置。
uint32 UCatWorldInfoComponent::GetInfoSerial() const
{
	return InfoSerial;
}
