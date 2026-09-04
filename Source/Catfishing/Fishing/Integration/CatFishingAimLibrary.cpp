#include "Fishing/Integration/CatFishingAimLibrary.h"

#include "Engine/World.h"
#include "Environment/CatChumFieldSettings.h"
#include "Environment/CatWaterQuerySubsystem.h"
#include "Character/CatCharacter.h"
#include "Equipment/CatEquipmentComponent.h"
#include "Equipment/CatEquipmentDefinition.h"
#include "Equipment/CatEquipmentSettings.h"
#include "Fishing/CatFishingSettings.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"

bool UCatFishingAimLibrary::TryGetLocalCastViewRay(APlayerController* Controller, FVector& OutOrigin, FVector& OutDirection)
{
	OutOrigin = OutDirection = FVector::ZeroVector;
	if (!Controller || !Controller->IsLocalController()) return false;
	if (Controller->ShouldShowMouseCursor())
	{
		return Controller->DeprojectMousePositionToWorld(OutOrigin, OutDirection);
	}
	FRotator Rotation;
	Controller->GetPlayerViewPoint(OutOrigin, Rotation);
	OutDirection = Rotation.Vector();
	return !OutOrigin.ContainsNaN() && !OutDirection.ContainsNaN();
}

bool UCatFishingAimLibrary::IsCastViewRayValid(const FVector Origin, const FVector Direction,
	const FVector PawnViewLocation, const FVector ControlDirection)
{
	// 容纳第三人称弹簧臂和少量传输期间的角色位移；禁止远处伪造相机或反向射线。
	return !Origin.ContainsNaN() && !Direction.ContainsNaN() && !PawnViewLocation.ContainsNaN()
		&& !ControlDirection.ContainsNaN() && Direction.IsNormalized()
		&& FVector::DistSquared(Origin, PawnViewLocation) <= FMath::Square(1500.0)
		&& FVector::DotProduct(Direction, ControlDirection.GetSafeNormal()) >= 0.5;
}

FCatWaterRegionHandle UCatFishingAimLibrary::FindNearestWaterRegion(UObject* WorldContextObject, const FVector WorldPoint)
{
	// 从任意 UObject 取所在 World，再取水域查询子系统；两者缺一即视为无水域可用
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	const UCatWaterQuerySubsystem* Water = World ? World->GetSubsystem<UCatWaterQuerySubsystem>() : nullptr;
	if (!Water) return FCatWaterRegionHandle();
	// 只找“预览用”的最近岸线水域，不做落点合法性校验，调用方还需要自己再校验/求交
	const FCatWaterSpatialResult Nearest = Water->QueryNearestShoreForPreview(WorldPoint);
	return Nearest.bSucceeded ? Nearest.WaterRegion : FCatWaterRegionHandle();
}

// 抛竿瞄准流程：先按视点找最近水域取 Handle，再把视线射线与该水域水面求交并做落点校验/修正；任一步失败返回 false。
bool UCatFishingAimLibrary::ResolveCastAimPoint(UObject* WorldContextObject, const FVector ViewLocation,
	const FRotator ViewRotation, FCatWaterRegionHandle& OutWaterRegion, FVector& OutLandingWorldPoint)
{
	// 先清空输出参数，任何一步提前 return false 时调用方都拿到确定的“空”结果，不会读到脏值
	OutWaterRegion = FCatWaterRegionHandle();
	OutLandingWorldPoint = FVector::ZeroVector;
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	const UCatWaterQuerySubsystem* Water = World ? World->GetSubsystem<UCatWaterQuerySubsystem>() : nullptr;
	if (!Water) return false;
	// 第一步：以视点位置找最近的候选水域，作为下面求交的搜索范围（水域可能不止一片）
	const FCatWaterRegionHandle Handle = FindNearestWaterRegion(WorldContextObject, ViewLocation);
	if (!Handle.IsValid()) return false;
	// 第二步：用视线方向（归一化）与该水域的水面几何求交，得到真正的瞄准落点
	const FCatWaterSpatialResult Hit = Water->ResolveRayToWater(ViewLocation, ViewRotation.Vector().GetSafeNormal(), Handle);
	// 命中失败，或命中点其实在水域范围之外（例如视线擦过水域外的延伸面），都算瞄准失败
	if (!Hit.bSucceeded || Hit.Containment == ECatWaterContainment::Outside) return false;
	OutWaterRegion = Hit.WaterRegion;
	OutLandingWorldPoint = Hit.WaterSurfaceWorldPoint;
	return true;
}

void UCatFishingAimLibrary::ComputeChumLaunch(const FVector CharacterLocation, const FRotator ViewRotation,
	const float ChargeAlpha, FVector& OutLaunchOrigin, FVector& OutLaunchVelocity)
{
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	// 蓄力比例夹在 [0,1]，防止调用方传入越界值影响后续插值
	const double Alpha = FMath::Clamp(static_cast<double>(ChargeAlpha), 0.0, 1.0);
	// 蓄力越久初速度越大：在配置的最小/最大抛掷速度之间线性插值
	const double Speed = FMath::Lerp(Settings->ChumThrowMinSpeed, Settings->ChumThrowMaxSpeed, Alpha);
	// 抛出起点只按角色朝向的 Yaw 偏移（忽略 Pitch/Roll），避免低头/侧身导致起点跑到奇怪位置
	const FRotator YawOnly(0.0, ViewRotation.Yaw, 0.0);
	OutLaunchOrigin = CharacterLocation + YawOnly.RotateVector(Settings->ChumThrowOriginOffset);
	// 抛出方向在视线 Yaw 基础上叠加一个固定俯仰角，形成向前上方的抛物线初速度
	const FRotator Elevated(Settings->ChumThrowElevationDegrees, ViewRotation.Yaw, 0.0);
	OutLaunchVelocity = Elevated.Vector() * Speed;
}

// 打窝预测流程：用引擎弹道预测取路径与命中点，再把命中点交给水域校验；未命中任何东西时取轨迹末点。
bool UCatFishingAimLibrary::PredictChumThrow(UObject* WorldContextObject, const FVector CharacterLocation,
	const FRotator ViewRotation, const float ChargeAlpha, TArray<FVector>& OutPath, FVector& OutLandingWorldPoint,
	FCatWaterRegionHandle& OutWaterRegion, bool& bOutHitWater)
{
	// 输出参数先清空，任何提前 return 都不会留下上一次调用的残留数据
	OutPath.Reset();
	OutLandingWorldPoint = FVector::ZeroVector;
	OutWaterRegion = FCatWaterRegionHandle();
	bOutHitWater = false;
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World) return false;
	const UCatFishingSettings* Settings = GetDefault<UCatFishingSettings>();
	const UCatChumFieldSettings* ChumSettings = GetDefault<UCatChumFieldSettings>();

	// 复用与真实抛掷完全相同的起点/初速度计算，保证“预览轨迹”和“服务器最终落点”用同一套物理输入
	FVector Origin, Velocity;
	ComputeChumLaunch(CharacterLocation, ViewRotation, ChargeAlpha, Origin, Velocity);

	// 引擎内建弹道预测：8 个采样点子步、4 个碰撞检测半径、按窝料放置的视线通道做阻挡检测
	FPredictProjectilePathParams Params(8.0f, Origin, Velocity, 4.0f,
		ChumSettings ? ChumSettings->PlacementLineOfSightChannel.GetValue() : ECC_Visibility);
	Params.bTraceWithCollision = true; // 需要真的做碰撞检测，否则轨迹会穿过地形/障碍物
	Params.bTraceComplex = false; // 用简单碰撞体即可，复杂碰撞对预测轨迹没必要且更贵
	Params.SimFrequency = 20.0f; // 每秒 20 次采样，兼顾轨迹平滑度与性能
	// 用项目自定义的重力缩放而不是世界默认重力，让抛物线弧度可以单独调参
	Params.OverrideGravityZ = static_cast<float>(World->GetGravityZ() * Settings->ChumThrowGravityScale);
	Params.DrawDebugType = EDrawDebugTrace::None; // 不画调试线，可视化由蓝图/专门的调试子系统负责
	FPredictProjectilePathResult Result;
	const bool bHit = UGameplayStatics::PredictProjectilePath(WorldContextObject, Params, Result);

	// 把预测轨迹的采样点原样拷给调用方，用于客户端画抛物线预览线
	OutPath.Reserve(Result.PathData.Num());
	for (const FPredictProjectilePathPointData& Point : Result.PathData) OutPath.Add(Point.Location);
	// 命中了东西就用碰撞点作落点，否则退化为取轨迹追踪到的最后一段终点（例如飞出最大模拟距离）
	OutLandingWorldPoint = bHit && Result.HitResult.bBlockingHit
		? Result.HitResult.ImpactPoint : Result.LastTraceDestination.Location;

	// 落点水域校验：允许修正到水里（MaxLandingCorrection），修正后以水面点为准。
	if (const UCatWaterQuerySubsystem* Water = World->GetSubsystem<UCatWaterQuerySubsystem>())
	{
		const FCatWaterRegionHandle Handle = FindNearestWaterRegion(WorldContextObject, OutLandingWorldPoint);
		if (Handle.IsValid())
		{
			// 候选落点可能落在岸上/水域边缘，ResolveCandidatePointToWater 会把它吸附/修正到合法水面点
			const FCatWaterSpatialResult Resolved = Water->ResolveCandidatePointToWater(OutLandingWorldPoint, Handle);
			if (Resolved.bSucceeded && Resolved.Containment != ECatWaterContainment::Outside)
			{
				OutWaterRegion = Resolved.WaterRegion;
				OutLandingWorldPoint = Resolved.WaterSurfaceWorldPoint; // 用修正后的水面点覆盖原始碰撞点
				bOutHitWater = true;
			}
		}
	}
	// 只要预测出了至少一段轨迹就算“有结果”返回 true；是否落水由 bOutHitWater 单独告知调用方
	return OutPath.Num() > 0;
}

float UCatFishingAimLibrary::ChargeAlphaFromHeldSeconds(const float HeldSeconds)
{
	// 分母不能为 0，至少给 0.05 秒的下限防止除零/瞬间满蓄力
	const double MaxSeconds = FMath::Max(0.05, GetDefault<UCatFishingSettings>()->ChumChargeMaxSeconds);
	// 按住时长线性映射到 [0,1]，超过上限的按住时间视为满蓄力
	return static_cast<float>(FMath::Clamp(static_cast<double>(HeldSeconds) / MaxSeconds, 0.0, 1.0));
}

// 抄网有效长度解析流程：先读取全局上限，再从服务器装备快照读取当前抄网定义；两者必须同时完整。
// debug 与服务器共用这条入口，避免无装备时仍使用全局值画绿色范围、权威裁决却把距离算成 0。
bool UCatFishingAimLibrary::TryResolveScoopReach(const UCatEquipmentComponent* Equipment,
	double& OutReachCentimeters)
{
	OutReachCentimeters = 0.0;
	const UCatFishingSettings* FishingSettings = GetDefault<UCatFishingSettings>();
	if (!FishingSettings || !FishingSettings->TryGetScoopReach(OutReachCentimeters))
	{
		return false;
	}

	const FName SelectedScoopDefinitionId = Equipment ? Equipment->GetSnapshot().ScoopNetDefinitionId : NAME_None;
	const UCatEquipmentDefinition* ScoopDefinition = SelectedScoopDefinitionId.IsNone() ? nullptr
		: GetDefault<UCatEquipmentSettings>()->FindRuntimeDefinition(SelectedScoopDefinitionId);
	if (!ScoopDefinition || ScoopDefinition->Kind != ECatEquipmentKind::ScoopNet
		|| !ScoopDefinition->IsRuntimeDefinitionReady() || ScoopDefinition->ScoopReachCentimeters <= 0.0)
	{
		OutReachCentimeters = 0.0;
		return false;
	}
	OutReachCentimeters = FMath::Min(OutReachCentimeters, ScoopDefinition->ScoopReachCentimeters);
	return FMath::IsFinite(OutReachCentimeters) && OutReachCentimeters > 0.0;
}

// 抄网是角色身体动作，不是镜头瞄准动作：只取 Character Actor 的水平前向。
// 服务器裁决和本地 debug 都调用这里，避免任一入口重新引入 Controller/Camera 朝向。
FVector UCatFishingAimLibrary::ResolveScoopFacingHorizontal(const ACatCharacter* Character)
{
	if (!Character)
	{
		return FVector::ZeroVector;
	}

	const FVector CharacterForward = Character->GetActorForwardVector();
	return FVector(CharacterForward.X, CharacterForward.Y, 0.0).GetSafeNormal();
}

// 抄网判定流程：俯视投影下的「线段 ∩ 圆」+ 独立的垂直高度差上限。
// 数学上等价于"圆心到线段的最短距离 <= 半径"，用参数化投影求最近点，全程平方比较不开方。
bool UCatFishingAimLibrary::DoesScoopRayReachFish(const FVector ScooperLocation, const FVector FacingHorizontal,
	const float ReachCentimeters, const FVector FishLocation, const float RadiusCentimeters,
	const float MaxVerticalDeltaCentimeters)
{
	// 只取水平面朝向：抬头低头不影响能不能捞到，玩家不需要"瞄准鱼的深度"。
	const FVector Facing = FVector(FacingHorizontal.X, FacingHorizontal.Y, 0.0).GetSafeNormal();
	// 朝向退化（正上/正下看）、长度或半径非法、坐标含 NaN，任一条件命中都判定够不着。
	if (Facing.IsNearlyZero() || ScooperLocation.ContainsNaN() || FishLocation.ContainsNaN()
		|| !FMath::IsFinite(ReachCentimeters) || ReachCentimeters <= 0.0f
		|| !FMath::IsFinite(RadiusCentimeters) || RadiusCentimeters <= 0.0f
		|| !FMath::IsFinite(MaxVerticalDeltaCentimeters))
	{
		return false;
	}
	// 唯一的垂直约束：挡住"水平方向够得着、但站在悬崖/高台上实际捞不到"。<=0 表示不限制高度差。
	if (MaxVerticalDeltaCentimeters > 0.0f
		&& FMath::Abs(FishLocation.Z - ScooperLocation.Z) > static_cast<double>(MaxVerticalDeltaCentimeters))
	{
		return false;
	}
	// 以下全在水平面内计算（Z 一律置零），线段 = [抄手, 抄手 + Facing * Reach]。
	const FVector2D ToFish(FishLocation.X - ScooperLocation.X, FishLocation.Y - ScooperLocation.Y);
	const FVector2D Direction(Facing.X, Facing.Y);
	const double Reach = static_cast<double>(ReachCentimeters);
	// 鱼心在射线方向上的投影长度；夹到 [0, Reach] 就得到线段上离鱼最近的那个点的参数。
	// 夹到 0 = 鱼在身后（最近点是抄手自己）；夹到 Reach = 鱼在射线延长线外（最近点是线段末端）。
	const double Projection = FMath::Clamp(FVector2D::DotProduct(ToFish, Direction), 0.0, Reach);
	const FVector2D ClosestPointOnSegment = Direction * Projection;
	// 最近点到鱼心的水平距离 <= 半径 → 线段与圆相交（含线段整段在圆内的情形）。
	return FVector2D::DistSquared(ClosestPointOnSegment, ToFish)
		<= FMath::Square(static_cast<double>(RadiusCentimeters));
}
