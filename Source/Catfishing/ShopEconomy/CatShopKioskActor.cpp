#include "ShopEconomy/CatShopKioskActor.h"

#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/CatInteractionSettings.h"
#include "UI/Shop/CatShopInteractionComponent.h"

// 构造流程：摊位不参与逐帧逻辑，先关闭 Tick，再建立空间根和页面打开组件；它自身不读取或修改商店经济后端。
ACatShopKioskActor::ACatShopKioskActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	InteractionCollision = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionCollision"));
	InteractionCollision->SetupAttachment(SceneRoot);
	InteractionCollision->SetSphereRadius(75.0f);
	InteractionCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionCollision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractionCollision->SetGenerateOverlapEvents(false);
	ShopInteraction = CreateDefaultSubobject<UCatShopInteractionComponent>(TEXT("ShopInteraction"));
	InteractionPrompt = NSLOCTEXT("Catfishing", "ShopKioskInteractionPrompt", "打开商店");
}

void ACatShopKioskActor::BeginPlay()
{
	Super::BeginPlay();
	if (const UCatInteractionSettings* Settings = GetDefault<UCatInteractionSettings>(); Settings && InteractionCollision)
	{
		InteractionCollision->SetCollisionResponseToChannel(Settings->TargetingTraceChannel, ECR_Block);
	}
}

bool ACatShopKioskActor::CanInteract_Implementation(AController* RequestingController) const
{
	const APlayerController* PlayerController = Cast<APlayerController>(RequestingController);
	return bInteractionEnabled && PlayerController && PlayerController->IsLocalController()
		&& ShopInteraction && !ShopInteraction->IsShopOpen();
}

FText ACatShopKioskActor::GetInteractionPrompt_Implementation() const
{
	return bInteractionEnabled && ShopInteraction && !ShopInteraction->IsShopOpen()
		? InteractionPrompt : FText::GetEmpty();
}

double ACatShopKioskActor::GetInteractionRadius_Implementation() const
{
	return FMath::IsFinite(InteractionRadiusCentimeters)
		? FMath::Max(0.0, InteractionRadiusCentimeters) : 0.0;
}

bool ACatShopKioskActor::Interact_Implementation(AController* RequestingController, const FGuid RequestId)
{
	APlayerController* PlayerController = Cast<APlayerController>(RequestingController);
	return RequestId.IsValid() && CanInteract_Implementation(RequestingController)
		&& ShopInteraction->OpenShopForPlayer(PlayerController);
}

// 组件读取流程：返回当前 Actor 自带的交互组件；调用方不能通过 Actor 绕过组件的交互 gate。
UCatShopInteractionComponent* ACatShopKioskActor::GetShopInteraction() const
{
	return ShopInteraction;
}
