#if WITH_DEV_AUTOMATION_TESTS

#include "UI/HUD/CatHUDModel.h"
#include "UI/HUD/CatHUDWidget.h"
#include "Input/HittestGrid.h"
#include "Misc/AutomationTest.h"
#include "Rendering/DrawElements.h"
#include "Styling/WidgetStyle.h"
#include "Types/PaintArgs.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCatHUDCrosshairVisibilityTest,
	"Catfishing.Unit.UI.HUD.CrosshairDefaultsOnAndPaintsAtCenter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 走真实 Model 投影和 NativePaint，不只检查默认布尔值；不把 Slate 绘制提交冒充正式画面验收。
bool FCatHUDCrosshairVisibilityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UCatHUDModel* Model = NewObject<UCatHUDModel>();
	UCatHUDWidget* Widget = NewObject<UCatHUDWidget>();
	TestTrue(TEXT("准星默认显示"), FCatHUDViewState().bShowCrosshair);
	Model->Refresh();
	Widget->RenderHUD(Model->GetViewState());
	TestTrue(TEXT("Model 刷新后仍向 HUD 投影准星"), Widget->GetLastHUDViewState().bShowCrosshair);
	TestFalse(TEXT("恢复准星不启用猫调试摘要"), Widget->GetLastHUDViewState().bShowCatStatusDebugText);
	TestFalse(TEXT("恢复准星不启用钓鱼调试摘要"), Widget->GetLastHUDViewState().bShowFishingFeedbackDebugText);

	FHittestGrid HitTestGrid;
	const FPaintArgs PaintArgs(nullptr, HitTestGrid, FVector2D::ZeroVector, 0.0, 0.0f);
	auto CheckPaint = [&](const FVector2D& Size, const bool bExpectedVisible)
	{
		FSlateWindowElementList Elements(nullptr);
		const FGeometry Geometry = FGeometry::MakeRoot(Size, FSlateLayoutTransform());
		const int32 Layer = Widget->NativePaint(PaintArgs, Geometry,
			FSlateRect(0.0f, 0.0f, Size.X, Size.Y), Elements, 7, FWidgetStyle(), true);
		const auto& Lines = Elements.GetUncachedDrawElements().Get<static_cast<uint8>(EElementType::ET_Line)>();
		TestEqual(TEXT("准星绘制层位于 HUD 之上，隐藏时不增加层"), Layer, bExpectedVisible ? 8 : 7);
		TestEqual(TEXT("可见时绘制四臂，隐藏或零尺寸时不提交线条"), Lines.Num(), bExpectedVisible ? 4 : 0);
		if (bExpectedVisible && Lines.Num() == 4)
		{
			const FVector2f Center(Size * 0.5);
			const FVector2f ExpectedStarts[] = {
				Center + FVector2f(-11.0f, 0.0f), Center + FVector2f(3.0f, 0.0f),
				Center + FVector2f(0.0f, -11.0f), Center + FVector2f(0.0f, 3.0f)};
			const FVector2f ExpectedEnds[] = {
				Center + FVector2f(-3.0f, 0.0f), Center + FVector2f(11.0f, 0.0f),
				Center + FVector2f(0.0f, -3.0f), Center + FVector2f(0.0f, 11.0f)};
			for (int32 Index = 0; Index < Lines.Num(); ++Index)
			{
				const TArray<FVector2f>& Points = Lines[Index].GetPoints();
				TestEqual(TEXT("每臂有两个端点"), Points.Num(), 2);
				if (Points.Num() == 2)
				{
					TestTrue(TEXT("起点相对视口中心定位"), Points[0].Equals(ExpectedStarts[Index]));
					TestTrue(TEXT("终点相对视口中心定位"), Points[1].Equals(ExpectedEnds[Index]));
				}
			}
		}
	};

	CheckPaint(FVector2D(1920.0, 1080.0), true);
	CheckPaint(FVector2D(1280.0, 720.0), true);
	CheckPaint(FVector2D::ZeroVector, false);
	FCatHUDViewState HiddenState = Model->GetViewState();
	HiddenState.bShowCrosshair = false;
	Widget->RenderHUD(HiddenState);
	CheckPaint(FVector2D(1920.0, 1080.0), false);
	Model->Unbind();
	Model->Refresh();
	Widget->RenderHUD(Model->GetViewState());
	CheckPaint(FVector2D(1920.0, 1080.0), true);
	return !HasAnyErrors();
}

#endif
