#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "CatFishingLineCurveComponent.generated.h"

/** 单一网格绘制的纯表现鱼线；无碰撞、无复制、无粒子惯性，由 Hook 发布本地平滑端点和线长。 */
UCLASS(ClassGroup=(Fishing), meta=(BlueprintSpawnableComponent))
class CATFISHING_API UCatFishingLineCurveComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()
public:
	UCatFishingLineCurveComponent(const FObjectInitializer& ObjectInitializer);
	bool UpdateCurve(const FVector& Start, const FVector& End, double PaidOutLengthCentimeters, int32 NumSegments, double WidthCentimeters);
	void ClearCurve();
	/** 已绘制的世界坐标中心线，仅供表现诊断；玩法不得消费。 */
	const TArray<FVector>& GetCurveWorldPoints() const { return CurveWorldPoints; }
	double GetCurveLengthCentimeters() const;

private:
	TArray<FVector> CurveWorldPoints;
	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FProcMeshTangent> Tangents;
	TArray<int32> Triangles;
	double LastPaidOutLength = -1.0;
	double LastWidth = -1.0;
};
