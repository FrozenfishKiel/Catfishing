#include "Environment/CatWaterRegion.h"

#include "Character/CatCharacter.h"
#include "Condition/CatConditionComponent.h"
#include "EngineUtils.h"

// 构造流程：建立可摆放的根节点，关闭网络复制，Tick 只保留给落水置湿并按固定节拍而不是逐帧运行；
// 水域几何本身仍只由查询子系统按需读取，这里不建立第二套 Environment 模拟。
//
// 根节点是必需的：没有它 AActor 就没有可写的 Transform，`SetActorLocation` 会静默失效、
// `GetActorLocation()` 恒为原点，于是关卡作者在编辑器里既拖不动这片水域，几何也只能全靠
// `LocalCenterOffset` 表达——而 `ContainsWorldPoint`/`MakeSnapshot` 算的是「Actor 位置 + 偏移」，
// 两者本该配合使用。摆放是这个类唯一的使用方式，所以这里补上根节点。
ACatWaterRegion::ACatWaterRegion()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = WetCheckIntervalSeconds;
	bReplicates = false;
	RegionRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RegionRoot"));
	SetRootComponent(RegionRoot);
}

// 落水置湿流程：先在客户端、未配置区域直接返回；然后遍历本 World 的每只猫，把站在 AABB 里的那些交给 Condition 置 Wet。
// 客户端判断用 NetMode 而不是 HasAuthority：本 Actor 不复制，客户端从地图加载出来的实例也会自认 authority，只有 NetMode 能把它们挡掉。
// 这里只写 true、不写 false：离开水域不会自动变干，项目里清 Wet 的唯一入口是玩家主动抖水（ServerCompleteShakeDry）。
// Condition 的 SetWetFromAuthority 对相同值直接返回，所以人一直泡在水里时每个节拍重复写 true 不会推高 Snapshot Revision。
void ACatWaterRegion::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (GetNetMode() == NM_Client || !IsRuntimeConfigured())
	{
		return;
	}
	for (TActorIterator<ACatCharacter> It(GetWorld()); It; ++It)
	{
		UCatConditionComponent* Conditions = It->GetConditionComponent();
		if (Conditions && ContainsWorldPoint(It->GetActorLocation()))
		{
			Conditions->SetWetFromAuthority(true);
		}
	}
}

// 配置校验流程：按 gate、ID、Revision、有限中心与三个正半尺寸逐项检查；任何 Unset 都阻止水域进入查询候选。
bool ACatWaterRegion::IsRuntimeConfigured() const
{
	return bEnablePrototypeBounds && !RegionId.IsNone() && RegionRevision > 0
		&& !LocalCenterOffset.ContainsNaN() && !HalfExtent.ContainsNaN()
		&& HalfExtent.X > 0.0 && HalfExtent.Y > 0.0 && HalfExtent.Z > 0.0;
}

// 点包含流程：配置无效或坐标非有限时立即失败；否则只对显式世界 AABB 做包含测试，不猜测岸线、深度或 WaterBody 语义。
bool ACatWaterRegion::ContainsWorldPoint(const FVector& WorldPoint) const
{
	if (!IsRuntimeConfigured() || WorldPoint.ContainsNaN())
	{
		return false;
	}
	const FVector WorldCenter = GetActorLocation() + LocalCenterOffset;
	return FBox::BuildAABB(WorldCenter, HalfExtent).IsInsideOrOn(WorldPoint);
}

// 快照构造流程：复制稳定 ID、配置 Revision 与本次世界 AABB；不暴露 Actor 指针，调用方不能反向修改区域，也拿不到窝料事实。
FCatWaterRegionSnapshot ACatWaterRegion::MakeSnapshot() const
{
	FCatWaterRegionSnapshot Snapshot;
	Snapshot.RegionId = RegionId;
	Snapshot.RegionRevision = RegionRevision;
	Snapshot.WorldCenter = GetActorLocation() + LocalCenterOffset;
	Snapshot.HalfExtent = HalfExtent;
	return Snapshot;
}
