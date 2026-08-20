#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Environment/CatWaterTypes.h"
#include "CatWaterRegion.generated.h"

/**
 * 关卡里一个显式配置的中性水域，回答「这个坐标算不算水」，并在服务器把站进来的猫置成淋湿。
 *
 * 它是纯几何：一个由关卡作者摆好的 axis-aligned 包围盒加一个稳定 ID，不依赖 Experimental Water 插件。
 * 它不持有窝料池——窝料挂在 UCatChumSpotSubsystem 的动态圆形窝点上，那些窝点由投料落点长出来、会衰减消散，
 * 与关卡静态水域几何是两套完全不同的生命周期，一个窝点也可能横跨或落在多片水域几何之间。
 * 落水置湿是它唯一的运行时副作用：飞书猫册写"雨天渐湿、落水全湿"，淋湿只是表现状态，这里不附带任何数值或移动惩罚。
 */
UCLASS(BlueprintType)
class CATFISHING_API ACatWaterRegion : public AActor
{
	GENERATED_BODY()

public:
	/** 关闭复制、按固定节拍开启 Tick；区域是关卡 authority 配置，客户端不把它当环境真相副本。 */
	ACatWaterRegion();

	/** 服务器按节拍把站在本水域 AABB 里的每只猫置成 Wet；客户端、未配置区域或没有猫在水里时什么都不做。 */
	virtual void Tick(float DeltaSeconds) override;

	/** 验证显式 gate、稳定 ID、正 Revision 和有限正包围盒；默认对象因此不可被查询命中。 */
	bool IsRuntimeConfigured() const;

	/** 落水检查的 Tick 间隔（秒）；淋湿是纯表现状态，不需要逐帧判定，按这个节拍扫一次足够让人看见"下水就湿"。 */
	static constexpr float WetCheckIntervalSeconds = 0.25f;

	/** 在显式 axis-aligned prototype 几何中判断世界点；配置无效时始终返回 false。 */
	bool ContainsWorldPoint(const FVector& WorldPoint) const;

	/** 构造只读查询快照；仅在 IsRuntimeConfigured 成立后调用。 */
	FCatWaterRegionSnapshot MakeSnapshot() const;

	/** 鱼表与环境 DTO 使用的稳定区域 ID；默认 None 表示未接线。 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Water")
	FName RegionId = NAME_None;

	/** 关卡作者对临时 AABB 查询的显式 gate；默认关闭，不把 Actor 位置误当最终岸线。 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Water|Prototype")
	bool bEnablePrototypeBounds = false;

	/** 相对 Actor 位置的 prototype 包围盒中心；仅在 gate 开启后读取。 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Water|Prototype")
	FVector LocalCenterOffset = FVector::ZeroVector;

	/** prototype 包围盒半尺寸；任一轴非正或非有限均视为 Unset。 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Water|Prototype")
	FVector HalfExtent = FVector::ZeroVector;

	/** 关卡几何版本；WaterQuery 把它写入快照，让后续命令能拒绝陈旧命中。
	 *  它只随关卡作者改动几何而变，运行时不再被任何写入推进——窝料写入推进的是窝点集合自己的 Revision。 */
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Water|Prototype", meta = (ClampMin = "1"))
	int64 RegionRevision = 0;

private:
	/** 水域在世界里的锚点，也是这个 Actor 唯一的可写 Transform 来源。
	 *  它本身不参与任何判定：包围盒是「本组件所在位置 + LocalCenterOffset ± HalfExtent」，
	 *  没有它关卡作者就拖不动这片水域，几何只能全靠偏移硬写。 */
	UPROPERTY(VisibleAnywhere, Category = "Water")
	TObjectPtr<USceneComponent> RegionRoot;
};
