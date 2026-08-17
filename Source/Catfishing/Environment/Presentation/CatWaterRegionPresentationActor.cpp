#include "Environment/Presentation/CatWaterRegionPresentationActor.h"

#include "Components/SceneComponent.h"

ACatWaterRegionPresentationActor::ACatWaterRegionPresentationActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	SetRootComponent(VisualRoot);
	VisualRoot->SetCanEverAffectNavigation(false);
}

void ACatWaterRegionPresentationActor::ApplyWaterGeometryPresentation(const FCatWaterGeometryCache& Cache)
{
	FCatWaterPresentationSnapshot Snapshot;
	Snapshot.WaterRegion = Cache.Handle;
	Snapshot.WaterSurfaceZ = Cache.WaterSurfaceZ;
	Snapshot.BoundsMin = Cache.Bounds2D.Min;
	Snapshot.BoundsMax = Cache.Bounds2D.Max;
	auto AddLoops = [&Snapshot, &Cache](const TArray<FCatWaterBakedPolygon>& Polygons, ECatWaterBoundaryOperation Operation)
	{
		for (const FCatWaterBakedPolygon& Polygon : Polygons)
		{
			FCatWaterPresentationLoop& Loop = Snapshot.Loops.AddDefaulted_GetRef();
			Loop.BoundaryId = Polygon.BoundaryId;
			Loop.Operation = Operation;
			for (const FVector2D& Point : Polygon.Vertices)
			{
				Loop.WorldPoints.Add(Cache.PlaneToWorld.TransformPosition(FVector(Point.X, Point.Y, 0.0)));
			}
		}
	};
	AddLoops(Cache.IncludePolygons, ECatWaterBoundaryOperation::Include);
	AddLoops(Cache.ExcludePolygons, ECatWaterBoundaryOperation::Exclude);
	PresentationSnapshot = MoveTemp(Snapshot);
	BP_ApplyWaterGeometryPresentation(PresentationSnapshot);
}

void ACatWaterRegionPresentationActor::SetWaterPreviewVisible(const bool bVisible)
{
	bPreviewVisible = bVisible;
	BP_SetWaterPreviewVisible(bVisible);
}
