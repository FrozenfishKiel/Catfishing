#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Voice/CatVoicePresentation.h"
#include "CatVoiceActivityWidget.generated.h"
class APlayerState;

struct FCatVoiceSpeakerRow
{
	TWeakObjectPtr<APlayerState> Player;
	FString Name;
	bool bLocal = false;
	FCatVoiceActivityEnvelope Envelope;
};

/** 只读局内语音提示；不接收输入、不创建采集器，生命周期归 LocalPlayer UI。 */
UCLASS()
class CATFISHING_API UCatVoiceActivityWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UCatVoiceActivityWidget(const FObjectInitializer& ObjectInitializer);
	const TArray<FCatVoiceSpeakerRow>& GetSpeakerRows() const { return Speakers; }
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
private:
	friend class FCatVoiceActivityWidgetTest;
	void RefreshSpeakers(float DeltaSeconds);
	void LogActivity(const FCatVoiceSpeakerRow& Row, bool bActive) const;
	UPROPERTY() FSlateFontInfo SpeakerFont;
	TArray<FCatVoiceSpeakerRow> Speakers;
	float AnimationTime = 0;
};
