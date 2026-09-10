#include "Camp/CatCampInventoryActor.h"

#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/CatInteractionSettings.h"
#include "Inventory/CatInventoryComponent.h"
#include "Logging/CatLog.h"
#include "UI/CatLocalPlayerUISubsystem.h"
#include "UI/Inventory/CatCampInventoryWidget.h"

// 构造流程：公共仓库是关卡里的服务器权威 Actor；创建根节点、交互碰撞、默认独立 WBP 软路径、开启复制并关闭 Tick，库存变化只由正式 InventoryComponent 提交。
ACatCampInventoryActor::ACatCampInventoryActor()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	InteractionCollision = CreateDefaultSubobject<USphereComponent>(TEXT("InteractionCollision"));
	InteractionCollision->SetupAttachment(SceneRoot);
	InteractionCollision->SetSphereRadius(100.0f);
	InteractionCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionCollision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractionCollision->SetGenerateOverlapEvents(false);
	InventoryComponent = CreateDefaultSubobject<UCatInventoryComponent>(TEXT("InventoryComponent"));
	InventoryViewClass = TSoftClassPtr<UCatCampInventoryWidget>(
		FSoftClassPath(TEXT("/Game/UI/Inventory/WBP_CatCampInventory.WBP_CatCampInventory_C")));
	InteractionPrompt = NSLOCTEXT("Catfishing", "CampInventoryInteractionPrompt", "打开营地库存");
}

// BeginPlay 流程：读取项目交互设置并把公共仓库命中球对齐到同一 Trace 通道；服务器只在正式库存组件上补齐仓库容量。
void ACatCampInventoryActor::BeginPlay()
{
	Super::BeginPlay();
	if (const UCatInteractionSettings* Settings = GetDefault<UCatInteractionSettings>(); Settings && InteractionCollision)
	{
		InteractionCollision->SetCollisionResponseToChannel(Settings->TargetingTraceChannel, ECR_Block);
	}
	if (HasAuthority() && InventoryComponent)
	{
		InventoryComponent->SetInventorySlotCountFromAuthority(InventorySlotCapacity);
	}
}

// 可交互判断流程：只要求交互开关开启且请求来自玩家 Controller；具体距离由准星扫描和服务器库存命令再复核。
bool ACatCampInventoryActor::CanInteract_Implementation(AController* RequestingController) const
{
	return bInteractionEnabled && Cast<APlayerController>(RequestingController) != nullptr;
}

// 提示文本流程：交互关闭时返回空文本；打开时使用编辑器配置文本，让提示和实际入口保持同一个开关。
FText ACatCampInventoryActor::GetInteractionPrompt_Implementation() const
{
	return bInteractionEnabled ? InteractionPrompt : FText::GetEmpty();
}

// 距离读取流程：把编辑器配置的厘米值裁成非负有限数；异常值按 0 处理，让服务器距离复核保守失败。
double ACatCampInventoryActor::GetInteractionRadius_Implementation() const
{
	return FMath::IsFinite(InteractionRadiusCentimeters)
		? FMath::Max(0.0, InteractionRadiusCentimeters) : 0.0;
}

// 交互流程：
// 1. 先校验请求 ID、玩家 Controller 和交互开关；缺任一项都返回失败并记录日志。
// 2. 只有本地 Controller 会打开库存 UI；远端或服务器代理不会创建本地页面。
// 3. 打开时把本 Actor 的库存组件和独立 WBP 类交给同一 UI 入口；面板绑定该库存的 Model，后续移动仍由服务器重读正式库存。
bool ACatCampInventoryActor::Interact_Implementation(AController* RequestingController, const FGuid RequestId)
{
	APlayerController* PlayerController = Cast<APlayerController>(RequestingController);
	if (!RequestId.IsValid() || !CanInteract_Implementation(RequestingController) || !PlayerController)
	{
		UE_LOG(LogCatUI, Warning,
			TEXT("Event=camp_inventory_interaction_rejected Reason=DependencyUnavailable Controller=%s Inventory=%s"),
			*GetNameSafe(PlayerController), *GetNameSafe(this));
		return false;
	}
	if (!PlayerController->IsLocalController())
	{
		return false;
	}
	ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer();
	UCatLocalPlayerUISubsystem* UISubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<UCatLocalPlayerUISubsystem>() : nullptr;
	const bool bOpened = UISubsystem && UISubsystem->OpenInventory(InventoryComponent, LoadInventoryViewClass());
	UE_LOG(LogCatUI, Log, TEXT("Event=camp_inventory_interaction_opened Inventory=%s Opened=%s"),
		*GetNameSafe(this), bOpened ? TEXT("true") : TEXT("false"));
	return bOpened;
}

// Inventory 读取流程：直接返回构造期正式库存组件；公共仓库命令、UI 展示和保存导出都以它作为唯一库存事实源。
UCatInventoryComponent* ACatCampInventoryActor::GetInventoryComponent() const
{
	return InventoryComponent;
}

// 公共仓库恢复入口流程：
// 1. Actor 只确认自己处在服务器上下文并持有正式库存组件。
// 2. 具体定义、容量、实例身份和专属状态恢复全部交给 InventoryComponent，避免公共仓库保留第二套反序列化规则。
bool ACatCampInventoryActor::RestoreInventorySlotsFromAuthority(const TArray<FCatInventoryEntry>& RestoredSlots,
	FText& OutFailure)
{
	OutFailure = FText::GetEmpty();
	if (!HasAuthority() || !InventoryComponent)
	{
		OutFailure = FText::FromString(TEXT("营地仓库恢复不是服务器上下文或正式库存组件不可用。"));
		return false;
	}
	return InventoryComponent->RestoreInventorySlotsFromAuthority(RestoredSlots, GetConfiguredSlotCapacity(), OutFailure);
}

// 公共仓库导出流程：
// 1. Actor 只确认自己处在服务器上下文并持有正式库存组件。
// 2. 具体可序列化格位由 InventoryComponent 导出，公共仓库不重复解释物品实例、耐久或 held entry。
bool ACatCampInventoryActor::ExportInventorySlotsFromAuthority(
	TArray<FCatInventoryEntry>& OutSlots, FText& OutFailure) const
{
	OutSlots.Reset();
	OutFailure = FText::GetEmpty();
	if (!HasAuthority() || !InventoryComponent)
	{
		OutFailure = FText::FromString(TEXT("营地正式库存只能在服务器导出。"));
		return false;
	}
	return InventoryComponent->ExportInventorySlotsFromAuthority(OutSlots, GetConfiguredSlotCapacity(), OutFailure);
}

// 容量读取流程：返回公共仓库当前配置容量的安全值；UI 用它展示空格，提交逻辑仍由服务器重新检查容量和版本。
int32 ACatCampInventoryActor::GetInventorySlotCapacityForView() const
{
	return GetConfiguredSlotCapacity();
}

// 容量读取流程：公共仓库容量来自 Actor 配置，负值运行时夹到 0；0 表示仓库未配置，所有入库都会拒绝。
int32 ACatCampInventoryActor::GetConfiguredSlotCapacity() const
{
	return FMath::Max(0, InventorySlotCapacity);
}

// 页面类解析流程：同步加载营地仓库自身配置的库存 View，并确认它就是营地仓库页面类型；错配普通背包页时返回空，交互打开链路会记录拒绝。
TSubclassOf<UCatCampInventoryWidget> ACatCampInventoryActor::LoadInventoryViewClass() const
{
	UClass* LoadedClass = InventoryViewClass.LoadSynchronous();
	if (!LoadedClass || !LoadedClass->IsChildOf(UCatCampInventoryWidget::StaticClass()))
	{
		return nullptr;
	}
	return LoadedClass;
}
