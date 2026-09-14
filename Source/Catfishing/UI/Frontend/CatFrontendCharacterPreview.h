#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CatFrontendCharacterPreview.generated.h"

class ACharacter;
class UAnimSequence;
class USkeletalMeshComponent;
class USceneCaptureComponent2D;
class UPointLightComponent;
class UTextureRenderTarget2D;

/** 仅本地 UI 展示的角色模型；不生成游戏 Character，不参与复制、输入或碰撞。 */
UCLASS(Transient, NotBlueprintable)
class CATFISHING_API ACatFrontendCharacterPreview : public AActor
{
	GENERATED_BODY()
public:
	ACatFrontendCharacterPreview();
	bool InitializePreview(TSubclassOf<ACharacter> CharacterClass, UAnimSequence* Animation);
	UTextureRenderTarget2D* GetTexture() const { return Texture; }
private:
	UPROPERTY() TObjectPtr<USkeletalMeshComponent> Mesh;
	UPROPERTY() TObjectPtr<USceneCaptureComponent2D> Capture;
	UPROPERTY() TObjectPtr<UPointLightComponent> KeyLight;
	UPROPERTY() TObjectPtr<UPointLightComponent> FillLight;
	UPROPERTY() TObjectPtr<UTextureRenderTarget2D> Texture;
};
