#include "Framework/Game/PhysicsPrototype/CatPhysicsPrototypeHUD.h"

#include "Character/Physics/CatPhysicsPrototypePawn.h"
#include "Engine/Canvas.h"
#include "Interaction/Grab/CatPhysicsGrabComponent.h"
#include "Interaction/Grab/CatPhysicsGrabProp.h"

void ACatPhysicsPrototypeHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas) return;
	const float Scale = FMath::Clamp(Canvas->SizeX / 1280.0f, 0.72f, 1.25f);
	DrawRect(FLinearColor(0.025f, 0.045f, 0.065f, 0.85f), 16, 16, 660 * Scale, 178 * Scale);
	const auto Line = [this, Scale](const TCHAR* Text, const float Row, const FLinearColor Color = FLinearColor::White)
	{
		DrawText(Text, Color, 30, 25 + Row * 25 * Scale, nullptr, Scale);
	};
	Line(TEXT("物理抓握试验场"), 0, FLinearColor(0.93f, 0.77f, 0.44f));
	Line(TEXT("WASD 移动  |  鼠标转向  |  Space 跳跃  |  R 复位"), 1);
	Line(TEXT("按住左 / 右键：伸出对应爪子并抓握；松开：放手"), 2);
	Line(TEXT("转向身旁的猫并靠近再伸手；低头可抓地面或竿"), 3);
	Line(GetNetMode() == NM_Standalone ? TEXT("Tab 切换猫咪；未控制的猫也能被推动和拉动")
		: TEXT("联机试验：每位玩家控制一只猫"), 4, FLinearColor(0.71f, 0.85f, 0.90f));
	Line(TEXT("F1 显示 / 隐藏抓握辅助线：青色伸手，绿色抓住"), 5, FLinearColor(0.71f, 0.85f, 0.90f));
	const ACatPhysicsPrototypePawn* Pawn = PlayerOwner ? Cast<ACatPhysicsPrototypePawn>(PlayerOwner->GetPawn()) : nullptr;
	const UCatPhysicsGrabComponent* Grab = Pawn ? Pawn->GetGrabComponent() : nullptr;
	const auto HandLabel = [Grab](const bool bLeft)
	{
		if (!Grab || !Grab->IsReaching(bLeft)) return FString(TEXT("放松"));
		if (!Grab->IsGripping(bLeft)) return FString(TEXT("伸手中"));
		AActor* Target = Grab->GetGripTarget(bLeft);
		if (Cast<ACatPhysicsPrototypePawn>(Target)) return FString(TEXT("抓住猫咪"));
		if (const ACatPhysicsGrabProp* Prop = Cast<ACatPhysicsGrabProp>(Target))
			return FString(Prop->IsDynamicProp() ? TEXT("抓住物体") : TEXT("抓住固定支点"));
		return FString(TEXT("已抓住"));
	};
	const FString Hands = FString::Printf(TEXT("左爪：%s        右爪：%s"), *HandLabel(true), *HandLabel(false));
	DrawRect(FLinearColor(0.025f, 0.045f, 0.065f, 0.85f), 16, Canvas->SizeY - 56 * Scale, 470 * Scale, 40 * Scale);
	DrawText(Hands, FLinearColor::White, 30, Canvas->SizeY - 48 * Scale, nullptr, Scale);
	const float X = Canvas->SizeX * 0.5f, Y = Canvas->SizeY * 0.5f;
	DrawLine(X - 5, Y, X + 5, Y, FLinearColor::White, 1.4f);
	DrawLine(X, Y - 5, X, Y + 5, FLinearColor::White, 1.4f);
}
