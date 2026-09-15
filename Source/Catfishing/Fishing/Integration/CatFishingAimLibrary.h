#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Environment/CatWaterTypes.h"
#include "CatFishingAimLibrary.generated.h"

class ACatCharacter;
class AController;
class APlayerController;
class UCatEquipmentComponent;
class UCatEquipmentDefinition;

/** 抛竿/打窝瞄准的公共数学；服务器裁决和客户端预览调用同一组函数，保证预览线与真实落点一致。 */
UCLASS()
class CATFISHING_API UCatFishingAimLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 视角中心选鱼身份；资格仍由服务器按身体射线与碰撞中心复核。 */
	static AActor* ResolveFishingViewTarget(APlayerController* Controller, const FVector& Origin, const FVector& Direction);
	/** 显示鼠标时取鼠标射线，否则取当前镜头准星射线；仅在本地采集输入。 */
	static bool TryGetLocalCastViewRay(APlayerController* Controller, FVector& OutOrigin, FVector& OutDirection);
	/** 服务器限定输入射线的来源和朝向；落点仍须经过水域、竿尖射程及角色视线裁决。 */
	static bool IsCastViewRayValid(FVector Origin, FVector Direction, FVector PawnViewLocation, FVector ControlDirection);
	/** 找到离世界点最近的已烘焙水域 Handle；找不到返回无效 Handle。 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Fishing|Aim", meta = (WorldContext = "WorldContextObject"))
	static FCatWaterRegionHandle FindNearestWaterRegion(UObject* WorldContextObject, FVector WorldPoint);

	/**
	 * 抛竿瞄准（规格 3.1：点哪落哪，无蓄力）：视线射线与水面求交，再经水域校验/修正。
	 * 失败时调用方必须保持未瞄准状态，不能沿用上一帧水域或落点继续提交抛竿。
	 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Fishing|Aim", meta = (WorldContext = "WorldContextObject"))
	static bool ResolveCastAimPoint(UObject* WorldContextObject, FVector ViewLocation, FRotator ViewRotation,
		FCatWaterRegionHandle& OutWaterRegion, FVector& OutLandingWorldPoint);

	/** 按当前设置把蓄力 Alpha 换算成初速度向量与抛出点；纯数学，不查场景。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Fishing|Aim")
	static void ComputeChumLaunch(FVector CharacterLocation, FRotator ViewRotation, float ChargeAlpha,
		FVector& OutLaunchOrigin, FVector& OutLaunchVelocity);

	/**
	 * 打窝蓄力抛物线预测（规格 3.1 打窝：蓄力抛掷、抛物线预览）。
	 * 用引擎 PredictProjectilePath 做碰撞预测；OutPath 供预览画线，OutLandingWorldPoint 为命中点或最远点。
	 * bOutHitWater 表示落点经水域校验后位于水中。
	 */
	UFUNCTION(BlueprintCallable, Category = "Catfishing|Fishing|Aim", meta = (WorldContext = "WorldContextObject"))
	static bool PredictChumThrow(UObject* WorldContextObject, FVector CharacterLocation, FRotator ViewRotation,
		float ChargeAlpha, TArray<FVector>& OutPath, FVector& OutLandingWorldPoint,
		FCatWaterRegionHandle& OutWaterRegion, bool& bOutHitWater);

	/** 把按住时长换算为 0..1 蓄力 Alpha（按 ChumChargeMaxSeconds）。 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Fishing|Aim")
	static float ChargeAlphaFromHeldSeconds(float HeldSeconds);

	/**
	 * 解析抄网唯一有效长度：玩家必须选中完整 ScoopNet；有效长度只读全局 Fishing 射程。
	 * 服务器裁决与 debug 显示共用此入口，避免无装备时仍显示绿色范围。
	 */
	static bool TryResolveScoopReach(const UCatEquipmentComponent* Equipment, double& OutReachCentimeters);
	/** 用已验证的抄网定义计算同一条范围公式；统一 Use 不依赖 Equipment 快照选择另一件抄网。 */
	static bool TryResolveScoopReach(const UCatEquipmentDefinition* ScoopDefinition, double& OutReachCentimeters);

	/**
	 * 解析抄网唯一有效朝向：只使用 Character Actor 的水平前向，不读取 Controller/Camera 朝向。
	 * 服务器裁决与 debug 显示共用此入口，保证自由转动镜头不会改变实际抄网方向。
	 */
	static FVector ResolveScoopFacingHorizontal(const ACatCharacter* Character);

	/**
	 * 抄网判定（唯一口径）：抄手向正前方水平发射一条线段，与挂在鱼身上的圆相交即够得着。
	 *
	 *   俯视：线段起点 = 抄手水平位置，方向 = 面向的水平分量，长度 = ReachCentimeters；
	 *         圆 = 鱼的水平位置 + RadiusCentimeters。二者相交即通过。
	 *   侧视：另外要求 |抄手Z − 鱼Z| <= MaxVerticalDeltaCentimeters（传 <= 0 表示不限制）。
	 *
	 * 为什么不做 3D 射线：鱼在水下看不清，逼玩家瞄准深度会变成盲操作；而且现实里站高一点更好捞，
	 * 3D 判定却会让站得高的人够不着。水平判定 + 高度上限对应的心智模型是"对着鱼、走近点、别站太高"。
	 *
	 * 服务器裁决与 debug 绘制调用同一函数，保证画出来的范围就是判定范围。
	 */
	UFUNCTION(BlueprintPure, Category = "Catfishing|Fishing|Aim")
	static bool DoesScoopRayReachFish(FVector ScooperLocation, FVector FacingHorizontal, float ReachCentimeters,
		FVector FishLocation, float RadiusCentimeters, float MaxVerticalDeltaCentimeters);
};
