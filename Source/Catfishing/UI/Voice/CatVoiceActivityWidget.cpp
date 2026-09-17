#include "UI/Voice/CatVoiceActivityWidget.h"
#include "Online/Voice/CatProximityVoiceComponent.h"
#include "Online/Voice/CatVoiceSettings.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Rendering/DrawElementTypes.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"
#include "Widgets/Layout/SSpacer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCatVoiceUI, Log, All);

UCatVoiceActivityWidget::UCatVoiceActivityWidget(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	SetIsFocusable(false);
	// 硬引用现有正式中文字体，随原生 Widget CDO 收集 Cook 依赖。
	static ConstructorHelpers::FObjectFinder<UObject> Font(TEXT("/Game/UI/Shop/F_CatShopChinese.F_CatShopChinese"));
	SpeakerFont = FSlateFontInfo(Font.Object, 16, TEXT("Regular"));
}

TSharedRef<SWidget> UCatVoiceActivityWidget::RebuildWidget() { return SNew(SSpacer); }

void UCatVoiceActivityWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::HitTestInvisible);
	ForceVolatile(true); // 振幅和波纹时间来自只读投影，必须每帧重绘，不复用静态绘制缓存。
	UE_LOG(LogCatVoiceUI, Log, TEXT("Event=voice_overlay_attached World=%s NetMode=%d LocalUser=%d"),
		*GetNameSafe(GetWorld()), GetWorld() ? int32(GetWorld()->GetNetMode()) : -1, GetOwningLocalPlayer() ? GetOwningLocalPlayer()->GetControllerId() : -1);
}

void UCatVoiceActivityWidget::NativeDestruct()
{
	for (const auto& Row : Speakers) { if (Row.Envelope.bActive) { LogActivity(Row, false); } }
	Speakers.Reset();
	AnimationTime = 0;
	Super::NativeDestruct();
}

void UCatVoiceActivityWidget::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	AnimationTime = FMath::Fmod(AnimationTime + InDeltaTime, 100.0f);
	RefreshSpeakers(InDeltaTime);
}

void UCatVoiceActivityWidget::RefreshSpeakers(const float DeltaSeconds)
{
	UWorld* World = GetWorld();
	const AGameStateBase* Game = World ? World->GetGameState() : nullptr;
	const APlayerController* LocalController = GetOwningPlayer();
	ULocalPlayer* Local = GetOwningLocalPlayer();
	const auto* Transmit = Local ? Local->GetSubsystem<UCatVoiceTransmitSubsystem>() : nullptr;
	TSet<APlayerState*> Present;
	if (Game && LocalController && LocalController->GetPawn() && !World->bIsTearingDown)
	{
		for (APlayerState* Player : Game->PlayerArray)
		{
			if (!IsValid(Player) || Player->IsActorBeingDestroyed()) { continue; }
			Present.Add(Player);
			auto* Row = Speakers.FindByPredicate([Player](const auto& R) { return R.Player == Player; });
			if (!Row) { Row = &Speakers.AddDefaulted_GetRef(); Row->Player = Player; }
			Row->bLocal = Player == LocalController->PlayerState;
			FString Name = Player->GetPlayerName().Replace(TEXT("\n"), TEXT(" ")).Replace(TEXT("\r"), TEXT(" ")).Replace(TEXT("\t"), TEXT(" "));
			Name = Name.IsEmpty() ? TEXT("玩家") : Name.Left(128);
			Row->Name = Row->bLocal ? Name + TEXT("（你）") : Name;
			auto* Remote = Player->FindComponentByClass<UCatProximityVoiceComponent>();
			const bool bEligible = Row->bLocal ? Transmit && Transmit->IsSending() && Transmit->IsInputContextAllowed()
				: Remote && Remote->HasAudibleStream();
			const float Level = Row->bLocal ? (Transmit ? Transmit->GetOutgoingVoiceLevel() : 0) : (Remote ? Remote->GetAudibleVoiceLevel() : 0);
			const bool bWasActive = Row->Envelope.bActive;
			Row->Envelope.Update(Level, bEligible, DeltaSeconds, *GetDefault<UCatVoiceSettings>());
			if (bWasActive != Row->Envelope.bActive) { LogActivity(*Row, Row->Envelope.bActive); }
		}
	}
	Speakers.RemoveAll([&](const auto& Row)
	{
		if (Present.Contains(Row.Player.Get())) { return false; }
		if (Row.Envelope.bActive) { LogActivity(Row, false); }
		return true;
	});
}

void UCatVoiceActivityWidget::LogActivity(const FCatVoiceSpeakerRow& Row, const bool bActive) const
{
	const auto* Player = Row.Player.Get();
	UE_LOG(LogCatVoiceUI, Log, TEXT("Event=voice_activity_changed World=%s NetMode=%d Authority=%d LocalRole=%d LocalUser=%d PlayerId=%d Local=%d Active=%d"),
		*GetNameSafe(GetWorld()), GetWorld() ? int32(GetWorld()->GetNetMode()) : -1,
		Player && Player->HasAuthority(), Player ? int32(Player->GetLocalRole()) : -1,
		GetOwningLocalPlayer() ? GetOwningLocalPlayer()->GetControllerId() : -1, Player ? Player->GetPlayerId() : -1, Row.bLocal, bActive);
}

int32 UCatVoiceActivityWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& Culling,
	FSlateWindowElementList& Draw, const int32 LayerId, const FWidgetStyle& Style, const bool bParentEnabled) const
{
	const int32 Layer = Super::NativePaint(Args, Geometry, Culling, Draw, LayerId, Style, bParentEnabled);
	const FVector2D Size = Geometry.GetLocalSize();
	const float Width = FMath::Min(310.0f, Size.X - 48.0f);
	if (Width <= 0) { return Layer; }
	int32 Index = 0;
	for (const auto& Row : Speakers)
	{
		if (!Row.Envelope.bActive) { continue; }
		const float Opacity = 1.0f - FMath::Clamp(Row.Envelope.QuietSeconds / FMath::Max(.001f, GetDefault<UCatVoiceSettings>()->ActivityReleaseSeconds), 0.0f, 1.0f);
		const FVector2D Origin(FMath::Max(24.0f, Size.X - Width - 28.0f), 128.0f + Index * 42.0f);
		if (Origin.Y + 36 > Size.Y - 24) { break; }
		FSlateDrawElement::MakeBox(Draw, Layer + 1, Geometry.ToPaintGeometry(FVector2D(Width, 36), FSlateLayoutTransform(Origin)),
			FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, FLinearColor(.012f, .045f, .04f, .8f * Opacity));
		const FVector2D Icon = Origin + FVector2D(13, 18);
		const FLinearColor Mint(.48f, .96f, .76f, Opacity);
		TArray<FVector2D> Body{ Icon + FVector2D(0,-4), Icon + FVector2D(5,-4), Icon + FVector2D(11,-9),
			Icon + FVector2D(11,9), Icon + FVector2D(5,4), Icon + FVector2D(0,4), Icon + FVector2D(0,-4) };
		FSlateDrawElement::MakeLines(Draw, Layer + 2, Geometry.ToPaintGeometry(), Body, ESlateDrawEffect::None, Mint, true, 1.8f);
		for (int32 Wave = 0; Wave < 3; ++Wave)
		{
			TArray<FVector2D> Arc;
			for (int32 Step = 0; Step <= 12; ++Step)
			{
				const float Angle = FMath::Lerp(-.85f, .85f, Step / 12.0f);
				Arc.Add(Icon + FVector2D(10,0) + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * (7.0f + 4.0f * Wave));
			}
			FLinearColor Tint = Mint;
			Tint.A *= .2f + .8f * FMath::Clamp(.5f + .5f * FMath::Sin(AnimationTime * 9 - Wave * 1.5f) + Row.Envelope.Level, 0.0f, 1.0f);
			FSlateDrawElement::MakeLines(Draw, Layer + 2, Geometry.ToPaintGeometry(), Arc, ESlateDrawEffect::None, Tint, true, 1.5f);
		}
		const auto Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		FString DisplayName = Row.Name;
		const float TextWidth = Width - 60;
		if (Measure->Measure(DisplayName, SpeakerFont).X > TextWidth)
		{
			const float SuffixWidth = Measure->Measure(TEXT("…"), SpeakerFont).X;
			const int32 End = Measure->FindLastWholeCharacterIndexBeforeOffset(DisplayName, SpeakerFont, FMath::Max(0.0f, TextWidth - SuffixWidth));
			DisplayName = DisplayName.Left(End + 1) + TEXT("…");
		}
		Draw.PushClip(FSlateClippingZone(Geometry.ToPaintGeometry(FVector2D(Width - 56,36), FSlateLayoutTransform(Origin + FVector2D(48,0)))));
		FSlateDrawElement::MakeText(Draw, Layer + 2, Geometry.ToPaintGeometry(FVector2D(Width - 56, 32), FSlateLayoutTransform(Origin + FVector2D(48,6))),
			DisplayName, SpeakerFont, ESlateDrawEffect::None, FLinearColor(.9f,.96f,.91f,Opacity));
		Draw.PopClip();
		++Index;
	}
	return Layer + 2;
}
