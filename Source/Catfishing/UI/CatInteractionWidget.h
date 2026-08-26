#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CatInteractionWidget.generated.h"

class UBorder;
class UTextBlock;

/** LocalPlayer 专属交互 View；只渲染准星和目标接口提供的只读提示，不发玩法命令。 */
UCLASS()
class CATFISHING_API UCatInteractionWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void RenderTarget(AActor* Target);

protected:
	virtual void NativeOnInitialized() override;

private:
	UPROPERTY(Transient) TObjectPtr<UBorder> CrosshairDot;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> PromptText;
};
