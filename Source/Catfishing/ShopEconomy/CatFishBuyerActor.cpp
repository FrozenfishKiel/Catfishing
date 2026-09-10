#include "ShopEconomy/CatFishBuyerActor.h"

#include "AbilitySystem/Executions/CatShopEconomyTransactionExecutionCalculation.h"
#include "Character/CatCharacter.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FishContainers/CatFishGuardActor.h"
#include "Framework/Game/CatfishingPlayerController.h"
#include "Inventory/CatFishInventoryItemInstance.h"
#include "Items/Fish/CatFishPickupActor.h"
#include "ShopEconomy/CatShopEconomySettings.h"
#include "ShopEconomy/Trading/CatShopTradeController.h"

// 构造流程：创建可查询根与外观组件，开启 Actor 复制；不创建第二套商店库存或交易服务。
ACatFishBuyerActor::ACatFishBuyerActor()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = false;
	InteractionCollision = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionCollision"));
	SetRootComponent(InteractionCollision);
	InteractionCollision->SetSphereRadius(40.0f);
	InteractionCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionCollision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	BuyerMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BuyerMesh"));
	BuyerMesh->SetupAttachment(InteractionCollision);
	BuyerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

// 买家查询流程：只遍历当前 World 的收购 Actor，返回第一个满足双方范围与视线的对象；无匹配时 UI 不显示出售。
ACatFishBuyerActor* ACatFishBuyerActor::FindAvailableBuyer(AController* Player, AActor* Source)
{
	if (!Player || !Player->GetWorld()) return nullptr;
	for (TActorIterator<ACatFishBuyerActor> It(Player->GetWorld()); It; ++It)
	{
		if (It->CanServeSource(Player, Source)) return *It;
	}
	return nullptr;
}

// 服务资格流程：先限定来源为本玩家或地面鱼护，复核同世界与双方距离，再从猫眼与鱼护分别检查买家视线；不读鱼护所有者。
bool ACatFishBuyerActor::CanServeSource(AController* Player, AActor* Source) const
{
	APawn* Pawn = Player ? Player->GetPawn() : nullptr;
	const ACatFishGuardActor* Guard = Cast<ACatFishGuardActor>(Source);
	if (!Pawn || !Source || !GetWorld() || IsActorBeingDestroyed() || !FMath::IsFinite(ServiceRadiusCentimeters)
		|| ServiceRadiusCentimeters <= 0.0 || Pawn->GetWorld() != GetWorld() || Source->GetWorld() != GetWorld()
		|| (Source != Pawn && (!Guard || !Guard->IsGrounded()))
		|| FVector::DistSquared(Pawn->GetActorLocation(), GetActorLocation()) > FMath::Square(ServiceRadiusCentimeters)
		|| FVector::DistSquared(Source->GetActorLocation(), GetActorLocation()) > FMath::Square(ServiceRadiusCentimeters)) return false;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(CatFishBuyerReach), false, Pawn);
	Query.AddIgnoredActor(Source);
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, Pawn->GetPawnViewLocation(), GetActorLocation(), ECC_Visibility, Query)
		&& Hit.GetActor() != this) return false;
	return !Guard || !GetWorld()->LineTraceSingleByChannel(Hit, Guard->GetActorLocation(), GetActorLocation(), ECC_Visibility, Query)
		|| Hit.GetActor() == this;
}

// 单鱼预览流程：读取正式表与鱼实例的冻结重量，用 GAS 计算器共用函数估价；不读取或预测团队余额。
bool ACatFishBuyerActor::TryAppraiseFish(UCatFishInventoryItemInstance* Fish, int32& OutPrice) const
{
	OutPrice = 0;
	if (!Fish) return false;
	FCatShopFishSaleLine Line;
	Line.FishInstanceId = Fish->GetItemInstanceId();
	Line.FishDefinitionId = Fish->GetItemDefinitionId();
	Line.WeightKilograms = Fish->GetFishWeightKilograms();
	return UCatShopEconomyTransactionExecutionCalculation::TryCalculateFishSale(
		GetDefault<UCatShopEconomySettings>()->DefaultFishSalePriceTable.LoadSynchronous(), {Line}, OutPrice);
}

// 交互可用性读取流程：仅当嘴叼鱼存在且买家可服务当前 Pawn 才显示动作，库存鱼护由其自身页面出售。
bool ACatFishBuyerActor::CanInteract_Implementation(AController* RequestingController) const
{
	const ACatCharacter* Character = RequestingController ? Cast<ACatCharacter>(RequestingController->GetPawn()) : nullptr;
	return Character && ACatFishPickupActor::FindCarriedFish(Character) && CanServeSource(RequestingController, RequestingController->GetPawn());
}

// 文案读取流程：返回收鱼动作名，范围和携带资格由交互检查决定，不在此提交出售。
FText ACatFishBuyerActor::GetInteractionPrompt_Implementation() const
{
	return NSLOCTEXT("Catfishing", "SellMouthFish", "出售嘴里的鱼");
}

// 半径读取流程：暴露同一服务距离给交互扫描，非法配置返回零以禁止远程交互。
double ACatFishBuyerActor::GetInteractionRadius_Implementation() const
{
	return FMath::IsFinite(ServiceRadiusCentimeters) ? FMath::Max(0.0, ServiceRadiusCentimeters) : 0.0;
}

// 世界交互流程：客户端只转发请求；服务器由同一交易协调器解析嘴叼鱼并提交，之后用既有领域回执通道通知玩家。
bool ACatFishBuyerActor::Interact_Implementation(AController* RequestingController, const FGuid RequestId)
{
	ACatfishingPlayerController* Controller = Cast<ACatfishingPlayerController>(RequestingController);
	if (!Controller || !RequestId.IsValid()) return false;
	if (!HasAuthority())
	{
		Controller->ServerRequestInteraction(this, RequestId);
		return true;
	}
	UCatShopTradeController* Trading = GetWorld()->GetSubsystem<UCatShopTradeController>();
	if (!Trading) return false;
	const FCatShopOrderResult Result = Trading->SubmitFishSaleFromPlayer(Controller, this, nullptr, {}, RequestId);
	Controller->ClientReceiveCampCommandResult(Result.Delivery);
	return CatIsAcceptedDomainCommandResult(Result.Delivery);
}
